#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Station.h"
#include "StationSubclasses.h"
#include "StationTalkWidget.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace AssemblyLineStationTests
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld(const TCHAR* Name)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			FURL URL;
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				World->BeginTearingDown();
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static AGeneratorStation* SpawnStation(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AGeneratorStation>(
			AGeneratorStation::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
}

DEFINE_SPEC(FStationSpec,
	"AssemblyLineSimul.Station",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FStationSpec::Define()
{
	using namespace AssemblyLineStationTests;

	Describe("TalkWidget", [this]()
	{
		It("hosts a UStationTalkWidget instance via TalkWidgetComponent", [this]()
		{
			FScopedTestWorld TW(TEXT("StationSpec_TalkWidget_Exists"));
			AStation* Station = SpawnStation(TW.World);

			TestNotNull(TEXT("TalkWidgetComponent exists"), Station->TalkWidgetComponent.Get());
			TestNotNull(TEXT("TalkWidget instance present"), Station->GetTalkWidget());
		});

		It("Speak updates the widget's body text", [this]()
		{
			FScopedTestWorld TW(TEXT("StationSpec_TalkWidget_Speak"));
			AStation* Station = SpawnStation(TW.World);

			Station->Speak(TEXT("hello"));

			UStationTalkWidget* W = Station->GetTalkWidget();
			TestNotNull(TEXT("widget present after Speak"), W);
			if (W)
			{
				TestEqual(TEXT("body text equals Speak argument"), W->GetBody().ToString(), FString(TEXT("hello")));
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
