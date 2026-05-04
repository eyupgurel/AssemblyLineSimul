#include "AssemblyLineDirector.h"
#include "Station.h"
#include "WorkerRobot.h"
#include "Bucket.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAssemblyLine, Log, All);

void UAssemblyLineDirector::RegisterStation(AStation* Station)
{
	if (!Station) return;

	// Story 35 — auto-assign FNodeRef using a per-Kind monotonic counter.
	// First Sorter → Sorter/0, second Sorter → Sorter/1, etc. The assigned
	// NodeRef is back-written into Station->NodeRef so dispatch (which now
	// keys on FNodeRef end-to-end) can identify which instance just finished.
	int32& NextInstance = NextInstanceByKind.FindOrAdd(Station->StationType, 0);
	const FNodeRef Ref{Station->StationType, NextInstance};
	NextInstance++;

	Station->NodeRef = Ref;
	StationByNodeRef.Add(Ref, Station);
	UE_LOG(LogAssemblyLine, Log,
		TEXT("Registered station: %s as (Kind=%d, Inst=%d)"),
		*Station->DisplayName, (int32)Ref.Kind, Ref.Instance);
}

bool UAssemblyLineDirector::BuildLineDAG(const TArray<FStationNode>& Nodes)
{
	return DAG.BuildFromDAG(Nodes);
}

void UAssemblyLineDirector::RegisterRobot(AWorkerRobot* Robot)
{
	if (!Robot || !Robot->AssignedStation) return;
	// Story 35 — key on the assigned station's FNodeRef so multi-instance
	// specs get one worker per instance (not one worker per Kind).
	const FNodeRef Ref = Robot->AssignedStation->NodeRef;
	RobotByNodeRef.Add(Ref, Robot);
	UE_LOG(LogAssemblyLine, Log,
		TEXT("Registered robot for: %s (Kind=%d, Inst=%d)"),
		*Robot->AssignedStation->DisplayName, (int32)Ref.Kind, Ref.Instance);

	// Re-broadcast worker phase events with station type so observers (the cinematic camera)
	// can react without binding to every individual worker. We listen to Working entry/exit
	// (not PickUp/Place) so the closeup is reserved for the actual processing moment.
	// Story 36 — forward FNodeRef so multi-instance is preserved end-to-end
	// (the camera distinguishes Filter/0 from Filter/1).
	Robot->OnStartedWorking.AddLambda([this](const FNodeRef& Ref) { OnStationActive.Broadcast(Ref); });
	Robot->OnFinishedWorking.AddLambda([this](const FNodeRef& Ref) { OnStationIdle.Broadcast(Ref); });
}

AStation* UAssemblyLineDirector::GetStation(const FNodeRef& Ref) const
{
	if (const TObjectPtr<AStation>* S = StationByNodeRef.Find(Ref)) return S->Get();
	return nullptr;
}

AWorkerRobot* UAssemblyLineDirector::GetRobot(const FNodeRef& Ref) const
{
	if (const TObjectPtr<AWorkerRobot>* R = RobotByNodeRef.Find(Ref)) return R->Get();
	return nullptr;
}

AStation* UAssemblyLineDirector::GetStationByNodeRef(const FNodeRef& Ref) const
{
	return GetStation(Ref);
}

AWorkerRobot* UAssemblyLineDirector::GetRobotByNodeRef(const FNodeRef& Ref) const
{
	return GetRobot(Ref);
}

AStation* UAssemblyLineDirector::GetStationOfType(EStationType Type) const
{
	// Story 35 — backward-compat shim: Instance 0 by convention.
	return GetStation(FNodeRef{Type, 0});
}

AWorkerRobot* UAssemblyLineDirector::GetRobotForStation(EStationType Type) const
{
	return GetRobot(FNodeRef{Type, 0});
}

void UAssemblyLineDirector::ClearLineState()
{
	// Preserve the Orchestrator entry — it's the chat-only meta agent that
	// survives across re-missioning so the operator's conversation
	// continues unbroken. Story 35 — preserve via FNodeRef key.
	const FNodeRef OrchRef{EStationType::Orchestrator, 0};
	TObjectPtr<AStation> OrchEntry;
	if (TObjectPtr<AStation>* Found = StationByNodeRef.Find(OrchRef))
	{
		OrchEntry = *Found;
	}
	StationByNodeRef.Reset();
	if (OrchEntry)
	{
		StationByNodeRef.Add(OrchRef, OrchEntry);
	}

	RobotByNodeRef.Reset();
	WaitingFor.Reset();
	InboundBuckets.Reset();
	DAG = FAssemblyLineDAG{};

	// Story 35 — reset the per-Kind instance counter so the next mission
	// starts fresh (Filter/0 again, not Filter/2). Preserve the Orchestrator
	// counter at 1 since we kept that entry.
	NextInstanceByKind.Reset();
	if (OrchEntry)
	{
		NextInstanceByKind.Add(EStationType::Orchestrator, 1);
	}

	// Cancel any timer the Director scheduled (recycle, auto-loop). Works
	// because Story 34 also moved those timers from CreateLambda to
	// CreateWeakLambda(this, ...) so the engine tracks ownership.
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearAllTimersForObject(this);
	}
}

void UAssemblyLineDirector::StartAllSourceCycles()
{
	// Story 32b — generalizes StartCycle for arbitrary spawned topologies.
	// Iterate every source node in the DAG; spawn an empty bucket at each
	// source station's input slot and dispatch. In a single-source DAG this
	// collapses to one StartCycle call; in fan-in it kicks both branches so
	// the merge gate fires once both arrive.
	UWorld* World = GetWorld();
	if (!World) return;

	const TArray<FNodeRef> Sources = DAG.GetSourceNodes();
	if (Sources.Num() == 0)
	{
		UE_LOG(LogAssemblyLine, Warning,
			TEXT("StartAllSourceCycles: DAG has no source nodes."));
		return;
	}

	UClass* Cls = BucketClass ? BucketClass.Get() : ABucket::StaticClass();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (const FNodeRef& Src : Sources)
	{
		AStation* SrcStation = GetStation(Src);
		if (!SrcStation)
		{
			UE_LOG(LogAssemblyLine, Warning,
				TEXT("StartAllSourceCycles: source (Kind=%d, Inst=%d) has no registered station."),
				(int32)Src.Kind, Src.Instance);
			continue;
		}

		const FVector SpawnLoc = SrcStation->InputSlot
			? SrcStation->InputSlot->GetComponentLocation()
			: SrcStation->GetActorLocation();
		ABucket* Bucket = World->SpawnActor<ABucket>(Cls, SpawnLoc, FRotator::ZeroRotator, Params);
		if (!Bucket)
		{
			UE_LOG(LogAssemblyLine, Error,
				TEXT("StartAllSourceCycles: failed to spawn bucket for (Kind=%d, Inst=%d)."),
				(int32)Src.Kind, Src.Instance);
			continue;
		}

		DispatchToStation(Src, Bucket, /*Source=*/nullptr);
	}
}

void UAssemblyLineDirector::CompleteCycle(ABucket* Bucket)
{
	OnCycleCompleted.Broadcast(Bucket);
	if (!bAutoLoop) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// Story 34 — CreateWeakLambda binds to `this` so
	// ClearAllTimersForObject(Director) cancels it on re-missioning.
	FTimerHandle Th;
	World->GetTimerManager().SetTimer(Th,
		FTimerDelegate::CreateWeakLambda(this, [this, Bucket]()
		{
			if (Bucket && IsValid(Bucket)) Bucket->Destroy();
			StartCycle();
		}),
		DelayBetweenCycles, false);
}

void UAssemblyLineDirector::StartCycle()
{
	const FNodeRef GenRef{EStationType::Generator, 0};
	AStation* Generator = GetStation(GenRef);
	if (!Generator)
	{
		UE_LOG(LogAssemblyLine, Warning, TEXT("StartCycle: no Generator/0 registered."));
		return;
	}

	// Spawn an empty bucket at the Generator's input slot.
	const FVector SpawnLoc = Generator->InputSlot
		? Generator->InputSlot->GetComponentLocation()
		: Generator->GetActorLocation();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UClass* Cls = BucketClass ? BucketClass.Get() : ABucket::StaticClass();
	ABucket* Bucket = GetWorld()->SpawnActor<ABucket>(Cls, SpawnLoc, FRotator::ZeroRotator, Params);
	if (!Bucket)
	{
		UE_LOG(LogAssemblyLine, Error, TEXT("StartCycle: failed to spawn bucket."));
		return;
	}

	UE_LOG(LogAssemblyLine, Log, TEXT("StartCycle: dispatching to Generator/0."));
	DispatchToStation(GenRef, Bucket, /*Source=*/nullptr);
}

void UAssemblyLineDirector::DispatchToStation(const FNodeRef& Target, ABucket* Bucket, AStation* SourceStation)
{
	AStation* TargetStation = GetStation(Target);
	AWorkerRobot* Robot = GetRobot(Target);
	if (!TargetStation || !Robot)
	{
		UE_LOG(LogAssemblyLine, Warning,
			TEXT("DispatchToStation: missing station or robot for (Kind=%d, Inst=%d)"),
			(int32)Target.Kind, Target.Instance);
		return;
	}

	if (Target.Kind == EStationType::Checker)
	{
		OnCheckerStarted.Broadcast();
	}

	// FromSlot: if there's a source station, that station's OutputSlot — otherwise the target's InputSlot
	// (which is also where the Director just spawned the empty bucket for the Generator).
	USceneComponent* FromSlot = SourceStation
		? SourceStation->OutputSlot
		: TargetStation->InputSlot;
	USceneComponent* ToSlot = TargetStation->OutputSlot;

	// Story 35 — capture FNodeRef so the completion callback knows exactly
	// which (Kind, Instance) finished. Critical for multi-instance specs:
	// Filter/0 finishing must consult Filter/0's successors, not Filter/1's.
	const FNodeRef CapturedRef = Target;
	Robot->BeginTask(Bucket, FromSlot, ToSlot,
		FWorkerTaskComplete::CreateLambda([this, CapturedRef](ABucket* DoneBucket)
		{
			OnRobotDoneAt(CapturedRef, DoneBucket);
		}));
}

void UAssemblyLineDirector::OnRobotDoneAt(EStationType Type, ABucket* Bucket)
{
	// Story 35 — backward-compat shim. Existing tests + production paths
	// that lost track of the Instance default to 0 (which is the conventional
	// "default" entry that GetStationOfType also resolves to).
	OnRobotDoneAt(FNodeRef{Type, 0}, Bucket);
}

void UAssemblyLineDirector::OnRobotDoneAt(const FNodeRef& Ref, ABucket* Bucket)
{
	if (!Bucket) return;

	// Story 17 AC17.7: any non-Generator station that ends up with an empty
	// bucket (Filter eliminated everything, etc.) triggers a recycle — destroy
	// the bucket and start a fresh Generator cycle. Generator emptying its own
	// output is a separate (LLM-misbehavior) concern — let it fall through to
	// Filter, which will then recycle on its empty output.
	if (Ref.Kind != EStationType::Generator && Bucket->Contents.Num() == 0)
	{
		UE_LOG(LogAssemblyLine, Display,
			TEXT("RECYCLE: bucket empty after (Kind=%d, Inst=%d) — destroying and starting fresh cycle."),
			(int32)Ref.Kind, Ref.Instance);
		if (AStation* Source = GetStation(Ref))
		{
			Source->SpeakAloud(TEXT("Bucket empty after rework — recycling. Starting a fresh cycle."));
		}
		OnCycleRecycled.Broadcast(Bucket);

		UWorld* World = GetWorld();
		if (World)
		{
			FTimerHandle Th;
			// Story 34 — CreateWeakLambda binds to `this` so
			// ClearAllTimersForObject(Director) cancels it on re-missioning.
			World->GetTimerManager().SetTimer(Th,
				FTimerDelegate::CreateWeakLambda(this, [this, Bucket]()
				{
					if (Bucket && IsValid(Bucket)) Bucket->Destroy();
					StartCycle();
				}),
				DelayBetweenCycles, false);
		}
		return;
	}

	// Story 31a/35 — Checker has special handling when TERMINAL: accept ends
	// the cycle, reject dispatches back. With Story 35 multi-instance support,
	// a Checker can also be MID-CHAIN (have DAG successors). In that case it
	// behaves like any other station on PASS (forward to successor) but still
	// routes REJECT via SendBackTo. This is what enables 5-stage missions
	// where the Checker verifies an intermediate result and then hands off
	// to a downstream stage (the operator's "...check, then take only the
	// best 2" mission shape).
	const TArray<FNodeRef> Successors = DAG.GetSuccessors(Ref);
	const bool bIsCheckerTerminal =
		(Ref.Kind == EStationType::Checker) && (Successors.Num() == 0);

	if (bIsCheckerTerminal)
	{
		AWorkerRobot* CheckerBot = GetRobot(Ref);
		const FStationProcessResult R = CheckerBot ? CheckerBot->LastResult : FStationProcessResult{};
		if (R.bAccepted)
		{
			UE_LOG(LogAssemblyLine, Log, TEXT("BUCKET ACCEPTED: %s"), *Bucket->GetContentsString());
			CompleteCycle(Bucket);
		}
		else
		{
			UE_LOG(LogAssemblyLine, Log, TEXT("BUCKET REJECTED (%s) — sending back to %s"),
				*R.Reason,
				R.SendBackTo == EStationType::Filter ? TEXT("Filter") : TEXT("Sorter"));
			OnCycleRejected.Broadcast(Bucket);
			// Send back resolves to Instance 0 of the named Kind (operator
			// targeting individual instances by name is a future story).
			DispatchToStation(FNodeRef{R.SendBackTo, 0}, Bucket, GetStation(Ref));
		}
		return;
	}

	// Story 35 — Checker mid-chain REJECT still routes via SendBackTo so the
	// rework path keeps working even when the Checker isn't terminal.
	if (Ref.Kind == EStationType::Checker)
	{
		AWorkerRobot* CheckerBot = GetRobot(Ref);
		const FStationProcessResult R = CheckerBot ? CheckerBot->LastResult : FStationProcessResult{};
		if (!R.bAccepted)
		{
			UE_LOG(LogAssemblyLine, Log,
				TEXT("BUCKET REJECTED MID-CHAIN (%s) — sending back to %s"),
				*R.Reason,
				R.SendBackTo == EStationType::Filter ? TEXT("Filter") : TEXT("Sorter"));
			OnCycleRejected.Broadcast(Bucket);
			DispatchToStation(FNodeRef{R.SendBackTo, 0}, Bucket, GetStation(Ref));
			return;
		}
		// PASS mid-chain falls through to the standard dispatch below.
		UE_LOG(LogAssemblyLine, Log,
			TEXT("BUCKET PASSED MID-CHAIN — forwarding to successor"));
	}

	if (Successors.Num() == 0)
	{
		// Story 37 — distinguish a valid registered terminal (cycle is
		// done; broadcast OnCycleCompleted + auto-loop) from a true
		// misconfiguration (Ref isn't even in the DAG; warn).
		if (DAG.FindNode(Ref) != nullptr)
		{
			UE_LOG(LogAssemblyLine, Log,
				TEXT("BUCKET REACHED TERMINAL (Kind=%d, Inst=%d): %s"),
				(int32)Ref.Kind, Ref.Instance, *Bucket->GetContentsString());
			CompleteCycle(Bucket);
		}
		else
		{
			UE_LOG(LogAssemblyLine, Warning,
				TEXT("OnRobotDoneAt: (Kind=%d, Inst=%d) has no DAG successor"),
				(int32)Ref.Kind, Ref.Instance);
		}
		return;
	}

	AStation* SourceStation = GetStation(Ref);
	if (Successors.Num() == 1)
	{
		// Linear case — original bucket forwarded as-is unless the destination
		// is a fan-in node, in which case we queue.
		if (!QueueForFanInOrDispatch(Successors[0], Bucket, Ref))
		{
			DispatchToStation(Successors[0], Bucket, SourceStation);
		}
		return;
	}

	// Story 31c — fan-out: K > 1 successors. Clone the bucket K times (one per
	// branch); each clone is also subject to the queue-for-fan-in check on its
	// destination (Story 31d).
	const FVector CloneSpawnLocation = (SourceStation && SourceStation->OutputSlot)
		? SourceStation->OutputSlot->GetComponentLocation()
		: (SourceStation ? SourceStation->GetActorLocation() : FVector::ZeroVector);
	for (const FNodeRef& Successor : Successors)
	{
		ABucket* Clone = Bucket->CloneIntoWorld(GetWorld(), CloneSpawnLocation);
		if (!Clone) continue;
		if (!QueueForFanInOrDispatch(Successor, Clone, Ref))
		{
			DispatchToStation(Successor, Clone, SourceStation);
		}
	}
	Bucket->Destroy();
}

bool UAssemblyLineDirector::QueueForFanInOrDispatch(const FNodeRef& Child, ABucket* Bucket, const FNodeRef& ParentRef)
{
	const TArray<FNodeRef> Parents = DAG.GetParents(Child);
	if (Parents.Num() <= 1)
	{
		return false;  // not a fan-in node — caller dispatches normally
	}

	TSet<FNodeRef>& Waits = WaitingFor.FindOrAdd(Child);
	if (Waits.IsEmpty())
	{
		// First arrival of this cycle — initialize the wait set from the DAG.
		// Subsequent cycles re-enter via this branch after FireFanInMerge
		// cleared the entry, so the gate works for every cycle, not just
		// the first.
		for (const FNodeRef& P : Parents) Waits.Add(P);
	}

	// Story 35 — match by full FNodeRef so multi-instance fan-in works
	// (e.g. Filter/0 and Filter/1 both feeding into a Sorter — the wait
	// set has both entries, removing only the one that arrived).
	Waits.Remove(ParentRef);
	InboundBuckets.FindOrAdd(Child).Add(Bucket);

	if (Waits.IsEmpty())
	{
		FireFanInMerge(Child);
	}
	return true;
}

void UAssemblyLineDirector::FireFanInMerge(const FNodeRef& Child)
{
	TArray<TWeakObjectPtr<ABucket>> Weak;
	if (TArray<TWeakObjectPtr<ABucket>>* Found = InboundBuckets.Find(Child))
	{
		Weak = *Found;
	}
	InboundBuckets.Remove(Child);
	WaitingFor.Remove(Child);  // resets for the next cycle's arrivals

	TArray<ABucket*> Inputs;
	Inputs.Reserve(Weak.Num());
	for (const TWeakObjectPtr<ABucket>& W : Weak)
	{
		if (ABucket* B = W.Get()) Inputs.Add(B);
	}

	AStation* ChildStation = GetStation(Child);
	if (!ChildStation || Inputs.Num() == 0)
	{
		UE_LOG(LogAssemblyLine, Warning,
			TEXT("FireFanInMerge: missing station or empty inputs for (Kind=%d, Inst=%d)"),
			(int32)Child.Kind, Child.Instance);
		return;
	}

	// Bypass the worker's BeginTask for the merge step (Story 31d out-of-scope:
	// physical multi-bucket carry by the worker). Direct call into ProcessBucket;
	// continue the dispatch chain from Inputs[0] in OnComplete.
	const FNodeRef ChildCopy = Child;
	ChildStation->ProcessBucket(Inputs,
		FStationProcessComplete::CreateLambda([this, ChildCopy, Inputs](FStationProcessResult /*Result*/)
		{
			for (int32 i = 1; i < Inputs.Num(); ++i)
			{
				if (IsValid(Inputs[i])) Inputs[i]->Destroy();
			}
			if (Inputs.Num() > 0 && IsValid(Inputs[0]))
			{
				// Story 35 — pass the full FNodeRef so dispatch from a
				// multi-instance merge target consults its own successors.
				OnRobotDoneAt(ChildCopy, Inputs[0]);
			}
		}));
}
