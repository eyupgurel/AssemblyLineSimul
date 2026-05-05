#include "Payload.h"

FString UIntegerArrayPayload::ToPromptString() const
{
	if (Items.Num() == 0)
	{
		return TEXT("[]");
	}

	FString Out = TEXT("[");
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		Out += FString::FromInt(Items[i]);
		if (i < Items.Num() - 1)
		{
			Out += TEXT(", ");
		}
	}
	Out += TEXT("]");
	return Out;
}

UPayload* UIntegerArrayPayload::Clone(UObject* Outer) const
{
	UIntegerArrayPayload* Copy = NewObject<UIntegerArrayPayload>(Outer);
	if (Copy)
	{
		Copy->Items = Items;
	}
	return Copy;
}
