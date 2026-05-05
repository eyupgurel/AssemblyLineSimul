#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Payload.h"

DEFINE_SPEC(FIntegerArrayPayloadSpec,
	"AssemblyLineSimul.IntegerArrayPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FIntegerArrayPayloadSpec::Define()
{
	Describe("ItemCount + IsEmpty", [this]()
	{
		It("a fresh payload reports ItemCount == 0 and IsEmpty == true", [this]()
		{
			UIntegerArrayPayload* P = NewObject<UIntegerArrayPayload>();
			TestEqual(TEXT("ItemCount"), P->ItemCount(), 0);
			TestTrue(TEXT("IsEmpty"), P->IsEmpty());
		});

		It("after writing Items, ItemCount matches array length and IsEmpty == false", [this]()
		{
			UIntegerArrayPayload* P = NewObject<UIntegerArrayPayload>();
			P->Items = {1, 2, 3};
			TestEqual(TEXT("ItemCount"), P->ItemCount(), 3);
			TestFalse(TEXT("IsEmpty"), P->IsEmpty());
		});
	});

	Describe("ToPromptString", [this]()
	{
		It("empty payload renders as '[]'", [this]()
		{
			UIntegerArrayPayload* P = NewObject<UIntegerArrayPayload>();
			TestEqual(TEXT("empty render"), P->ToPromptString(), FString(TEXT("[]")));
		});

		It("populated payload renders as comma-space separated bracketed list", [this]()
		{
			UIntegerArrayPayload* P = NewObject<UIntegerArrayPayload>();
			P->Items = {1, 2, 3};
			TestEqual(TEXT("[1, 2, 3]"), P->ToPromptString(), FString(TEXT("[1, 2, 3]")));
		});
	});

	Describe("Clone", [this]()
	{
		It("produces an independent payload with copied Items (mutating clone does not affect source)", [this]()
		{
			UIntegerArrayPayload* P = NewObject<UIntegerArrayPayload>();
			P->Items = {7, 11, 13};

			UPayload* CloneBase = P->Clone(GetTransientPackage());
			UIntegerArrayPayload* Clone = Cast<UIntegerArrayPayload>(CloneBase);
			TestNotNull(TEXT("Clone returns a UIntegerArrayPayload"), Clone);
			if (!Clone) return;

			TestEqual(TEXT("Clone has same Items"), Clone->Items, P->Items);

			Clone->Items.Add(99);
			TestEqual(TEXT("source unchanged after clone mutation"),
				P->Items.Num(), 3);
		});
	});

	Describe("OnChanged broadcast", [this]()
	{
		It("SetItems mutates Items and broadcasts OnChanged once", [this]()
		{
			UIntegerArrayPayload* P = NewObject<UIntegerArrayPayload>();
			int32 Calls = 0;
			P->OnChanged.AddLambda([&]() { ++Calls; });

			P->SetItems({4, 5});

			TestEqual(TEXT("OnChanged fired once"), Calls, 1);
			TestEqual(TEXT("Items written"), P->Items.Num(), 2);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
