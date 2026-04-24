#include "Station.h"
#include "Bucket.h"
#include "StationTalkWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AStation::AStation()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;  // 10 Hz is plenty for billboard rotation

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		MeshComponent->SetStaticMesh(CubeFinder.Object);
		MeshComponent->SetWorldScale3D(FVector(2.0f, 3.0f, 1.0f));
	}

	InputSlot = CreateDefaultSubobject<USceneComponent>(TEXT("InputSlot"));
	InputSlot->SetupAttachment(RootComponent);
	InputSlot->SetRelativeLocation(FVector(0.f, -200.f, 100.f));

	OutputSlot = CreateDefaultSubobject<USceneComponent>(TEXT("OutputSlot"));
	OutputSlot->SetupAttachment(RootComponent);
	OutputSlot->SetRelativeLocation(FVector(0.f, 200.f, 100.f));

	WorkerStandPoint = CreateDefaultSubobject<USceneComponent>(TEXT("WorkerStandPoint"));
	WorkerStandPoint->SetupAttachment(RootComponent);
	WorkerStandPoint->SetRelativeLocation(FVector(-250.f, 0.f, 0.f));

	NameLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NameLabel"));
	NameLabel->SetupAttachment(RootComponent);
	NameLabel->SetRelativeLocation(FVector(0.f, 0.f, 220.f));
	NameLabel->SetHorizontalAlignment(EHTA_Center);
	NameLabel->SetVerticalAlignment(EVRTA_TextCenter);
	NameLabel->SetWorldSize(80.f);
	NameLabel->SetTextRenderColor(FColor::Cyan);

	TalkWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("TalkWidgetComponent"));
	TalkWidgetComponent->SetupAttachment(RootComponent);
	TalkWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 380.f));
	TalkWidgetComponent->SetWidgetSpace(EWidgetSpace::World);
	TalkWidgetComponent->SetDrawSize(FVector2D(800.f, 200.f));
	TalkWidgetComponent->SetPivot(FVector2D(0.5f, 0.5f));
	TalkWidgetComponent->SetTwoSided(true);
	TalkWidgetComponent->SetWidgetClass(UStationTalkWidget::StaticClass());
	TalkWidgetClass = UStationTalkWidget::StaticClass();
}

void AStation::BeginPlay()
{
	Super::BeginPlay();
	if (NameLabel)
	{
		NameLabel->SetText(FText::FromString(DisplayName));
	}
}

void AStation::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	BillboardLabel(NameLabel);
	BillboardLabel(TalkWidgetComponent);
}

void AStation::BillboardLabel(USceneComponent* Comp)
{
	if (!Comp) return;
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!Cam) return;
	const FVector CamLoc = Cam->GetCameraLocation();
	FRotator LookAt = (CamLoc - Comp->GetComponentLocation()).Rotation();
	LookAt.Pitch = 0.f;
	LookAt.Roll = 0.f;
	// TextRender / world-space widget readable face points along local +X — flip 180° to face the camera.
	LookAt.Yaw += 180.f;
	Comp->SetWorldRotation(LookAt);
}

UStationTalkWidget* AStation::GetTalkWidget()
{
	if (!TalkWidgetComponent) return nullptr;
	if (UStationTalkWidget* Existing = Cast<UStationTalkWidget>(TalkWidgetComponent->GetUserWidgetObject()))
	{
		return Existing;
	}
	UClass* WidgetClass = TalkWidgetClass ? TalkWidgetClass.Get() : UStationTalkWidget::StaticClass();
	UStationTalkWidget* New = NewObject<UStationTalkWidget>(this, WidgetClass);
	TalkWidgetComponent->SetWidget(New);
	return New;
}

void AStation::ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete)
{
	FStationProcessResult Result;
	Result.bAccepted = true;
	OnComplete.ExecuteIfBound(Result);
}

void AStation::WriteTalkText(const FString& Text)
{
	if (UStationTalkWidget* W = GetTalkWidget())
	{
		W->SetBody(FText::FromString(Text));
	}
}

void AStation::Speak(const FString& Text)
{
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(StreamTimer);
	}
	StreamFullText.Empty();
	StreamCharIndex = 0;
	WriteTalkText(Text);
}

void AStation::SpeakStreaming(const FString& Text, float CharsPerSecond)
{
	StreamFullText = Text;
	StreamCharIndex = 0;
	WriteTalkText(FString());
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(StreamTimer);
		const float Interval = (CharsPerSecond > 0.f) ? 1.f / CharsPerSecond : 0.03f;
		W->GetTimerManager().SetTimer(StreamTimer, this, &AStation::TickStream, Interval, true);
	}
}

void AStation::ClearTalk()
{
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(StreamTimer);
	}
	StreamFullText.Empty();
	StreamCharIndex = 0;
	WriteTalkText(FString());
}

void AStation::TickStream()
{
	if (StreamCharIndex >= StreamFullText.Len())
	{
		if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(StreamTimer);
		return;
	}
	StreamCharIndex = FMath::Min(StreamCharIndex + 1, StreamFullText.Len());
	WriteTalkText(StreamFullText.Left(StreamCharIndex));
}
