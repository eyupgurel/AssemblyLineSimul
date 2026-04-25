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

	switch (Type)
	{
	case EStationType::Generator:
		DispatchToStation(EStationType::Filter, Bucket, GetStation(EStationType::Generator));
		break;
	case EStationType::Filter:
		DispatchToStation(EStationType::Sorter, Bucket, GetStation(EStationType::Filter));
		break;
	case EStationType::Sorter:
		DispatchToStation(EStationType::Checker, Bucket, GetStation(EStationType::Sorter));
		break;
	case EStationType::Checker:
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
		break;
	}
	default:
		break;
	}
}
