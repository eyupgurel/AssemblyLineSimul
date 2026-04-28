#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "StationSubclasses.h"

DEFINE_SPEC(FStationSubclassesSpec,
	"AssemblyLineSimul.StationSubclasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FStationSubclassesSpec::Define()
{
	Describe("AFilterStation::FindKeptIndices (Story 25)", [this]()
	{
		It("returns the input-bucket indices that match each kept value, in order", [this]()
		{
			const TArray<int32> Indices = AFilterStation::FindKeptIndices({ 1, 2, 3 }, { 2, 3 });
			TestEqual(TEXT("count"), Indices.Num(), 2);
			TestEqual(TEXT("[0]"), Indices[0], 1);
			TestEqual(TEXT("[1]"), Indices[1], 2);
		});

		It("handles duplicates by claiming the first unmatched occurrence per kept value", [this]()
		{
			// input has two 5s; kept asks for one 5 and one 7 — expect indices 1 (first 5) and 2 (only 7).
			const TArray<int32> Indices = AFilterStation::FindKeptIndices({ 3, 5, 7, 5 }, { 5, 7 });
			TestEqual(TEXT("count"), Indices.Num(), 2);
			TestEqual(TEXT("first 5 is at index 1"), Indices[0], 1);
			TestEqual(TEXT("7 is at index 2"), Indices[1], 2);
		});

		It("returns empty when input is empty", [this]()
		{
			const TArray<int32> Indices = AFilterStation::FindKeptIndices({}, { 1 });
			TestEqual(TEXT("count"), Indices.Num(), 0);
		});

		It("returns empty when kept is empty", [this]()
		{
			const TArray<int32> Indices = AFilterStation::FindKeptIndices({ 1, 2, 3 }, {});
			TestEqual(TEXT("count"), Indices.Num(), 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
