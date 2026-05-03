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
	StationByType.Add(Station->StationType, Station);
	UE_LOG(LogAssemblyLine, Log, TEXT("Registered station: %s"), *Station->DisplayName);
}

bool UAssemblyLineDirector::BuildLineDAG(const TArray<FStationNode>& Nodes)
{
	return DAG.BuildFromDAG(Nodes);
}

void UAssemblyLineDirector::RegisterRobot(AWorkerRobot* Robot)
{
	if (!Robot || !Robot->AssignedStation) return;
	RobotByStation.Add(Robot->AssignedStation->StationType, Robot);
	UE_LOG(LogAssemblyLine, Log, TEXT("Registered robot for: %s"), *Robot->AssignedStation->DisplayName);

	// Re-broadcast worker phase events with station type so observers (the cinematic camera)
	// can react without binding to every individual worker. We listen to Working entry/exit
	// (not PickUp/Place) so the closeup is reserved for the actual processing moment.
	Robot->OnStartedWorking.AddLambda([this](EStationType St) { OnStationActive.Broadcast(St); });
	Robot->OnFinishedWorking.AddLambda([this](EStationType St) { OnStationIdle.Broadcast(St); });
}

AStation* UAssemblyLineDirector::GetStation(EStationType Type) const
{
	if (const TObjectPtr<AStation>* S = StationByType.Find(Type)) return S->Get();
	return nullptr;
}

AWorkerRobot* UAssemblyLineDirector::GetRobot(EStationType Type) const
{
	if (const TObjectPtr<AWorkerRobot>* R = RobotByStation.Find(Type)) return R->Get();
	return nullptr;
}

AStation* UAssemblyLineDirector::GetStationOfType(EStationType Type) const
{
	return GetStation(Type);
}

AWorkerRobot* UAssemblyLineDirector::GetRobotForStation(EStationType Type) const
{
	return GetRobot(Type);
}

void UAssemblyLineDirector::StartCycle()
{
	AStation* Generator = GetStation(EStationType::Generator);
	if (!Generator)
	{
		UE_LOG(LogAssemblyLine, Warning, TEXT("StartCycle: no Generator registered."));
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

	UE_LOG(LogAssemblyLine, Log, TEXT("StartCycle: dispatching to Generator."));
	DispatchToStation(EStationType::Generator, Bucket, /*Source=*/nullptr);
}

void UAssemblyLineDirector::DispatchToStation(EStationType Type, ABucket* Bucket, AStation* SourceStation)
{
	AStation* Target = GetStation(Type);
	AWorkerRobot* Robot = GetRobot(Type);
	if (!Target || !Robot)
	{
		UE_LOG(LogAssemblyLine, Warning, TEXT("DispatchToStation: missing station or robot for type %d"), (int32)Type);
		return;
	}

	if (Type == EStationType::Checker)
	{
		OnCheckerStarted.Broadcast();
	}

	// FromSlot: if there's a source station, that station's OutputSlot — otherwise the target's InputSlot
	// (which is also where the Director just spawned the empty bucket for the Generator).
	USceneComponent* FromSlot = SourceStation
		? SourceStation->OutputSlot
		: Target->InputSlot;
	USceneComponent* ToSlot = Target->OutputSlot;

	Robot->BeginTask(Bucket, FromSlot, ToSlot,
		FWorkerTaskComplete::CreateLambda([this, Type](ABucket* DoneBucket)
		{
			OnRobotDoneAt(Type, DoneBucket);
		}));
}

void UAssemblyLineDirector::OnRobotDoneAt(EStationType Type, ABucket* Bucket)
{
	if (!Bucket) return;

	// Story 17 AC17.7: any non-Generator station that ends up with an empty
	// bucket (Filter eliminated everything, etc.) triggers a recycle — destroy
	// the bucket and start a fresh Generator cycle. Generator emptying its own
	// output is a separate (LLM-misbehavior) concern — let it fall through to
	// Filter, which will then recycle on its empty output.
	if (Type != EStationType::Generator && Bucket->Contents.Num() == 0)
	{
		UE_LOG(LogAssemblyLine, Display,
			TEXT("RECYCLE: bucket empty after %d — destroying and starting fresh cycle."),
			(int32)Type);
		if (AStation* Source = GetStation(Type))
		{
			Source->SpeakAloud(TEXT("Bucket empty after rework — recycling. Starting a fresh cycle."));
		}
		OnCycleRecycled.Broadcast(Bucket);

		UWorld* World = GetWorld();
		if (World)
		{
			FTimerHandle Th;
			World->GetTimerManager().SetTimer(Th,
				FTimerDelegate::CreateLambda([this, Bucket]()
				{
					if (Bucket && IsValid(Bucket)) Bucket->Destroy();
					StartCycle();
				}),
				DelayBetweenCycles, false);
		}
		return;
	}

	// Story 31a — Checker has special handling (accept = end cycle, reject =
	// dispatch to send-back-to). Every other station's "next station" is just
	// the DAG's single successor (Story 31c will generalize for fan-out).
	if (Type == EStationType::Checker)
	{
		AWorkerRobot* CheckerBot = GetRobot(EStationType::Checker);
		const FStationProcessResult R = CheckerBot ? CheckerBot->LastResult : FStationProcessResult{};
		if (R.bAccepted)
		{
			UE_LOG(LogAssemblyLine, Log, TEXT("BUCKET ACCEPTED: %s"), *Bucket->GetContentsString());
			OnCycleCompleted.Broadcast(Bucket);
			if (bAutoLoop)
			{
				FTimerHandle Th;
				GetWorld()->GetTimerManager().SetTimer(Th,
					FTimerDelegate::CreateLambda([this, Bucket]()
					{
						if (Bucket && IsValid(Bucket)) Bucket->Destroy();
						StartCycle();
					}),
					DelayBetweenCycles, false);
			}
		}
		else
		{
			UE_LOG(LogAssemblyLine, Log, TEXT("BUCKET REJECTED (%s) — sending back to %s"),
				*R.Reason,
				R.SendBackTo == EStationType::Filter ? TEXT("Filter") : TEXT("Sorter"));
			OnCycleRejected.Broadcast(Bucket);
			DispatchToStation(R.SendBackTo, Bucket, GetStation(EStationType::Checker));
		}
		return;
	}

	const TArray<FNodeRef> Successors = DAG.GetSuccessors(FNodeRef{Type, 0});
	if (Successors.Num() == 0)
	{
		// No successor in the DAG and not the Checker — line is misconfigured
		// or a future fan-out story is exercising a terminal that isn't Checker.
		// For Story 31a this is a misconfiguration; surface it.
		UE_LOG(LogAssemblyLine, Warning,
			TEXT("OnRobotDoneAt: station type %d has no DAG successor"), (int32)Type);
		return;
	}

	AStation* SourceStation = GetStation(Type);
	if (Successors.Num() == 1)
	{
		// Linear case — original bucket forwarded as-is unless the destination
		// is a fan-in node, in which case we queue. Story 31a/b regression net
		// depends on the no-queue, no-clone, no-destroy path when neither
		// fan-out nor fan-in apply.
		if (!QueueForFanInOrDispatch(Successors[0], Bucket, Type))
		{
			DispatchToStation(Successors[0].Kind, Bucket, SourceStation);
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
		if (!QueueForFanInOrDispatch(Successor, Clone, Type))
		{
			DispatchToStation(Successor.Kind, Clone, SourceStation);
		}
	}
	Bucket->Destroy();
}

bool UAssemblyLineDirector::QueueForFanInOrDispatch(const FNodeRef& Child, ABucket* Bucket, EStationType ParentType)
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

	Waits.Remove(FNodeRef{ParentType, 0});
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

	AStation* ChildStation = GetStation(Child.Kind);
	if (!ChildStation || Inputs.Num() == 0)
	{
		UE_LOG(LogAssemblyLine, Warning,
			TEXT("FireFanInMerge: missing station or empty inputs for Kind=%d"),
			(int32)Child.Kind);
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
				OnRobotDoneAt(ChildCopy.Kind, Inputs[0]);
			}
		}));
}
