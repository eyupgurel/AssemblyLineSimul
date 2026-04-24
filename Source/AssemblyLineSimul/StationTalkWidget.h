#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StationTalkWidget.generated.h"

class UTextBlock;

UCLASS()
class ASSEMBLYLINESIMUL_API UStationTalkWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetBody(const FText& InText);
	FText GetBody() const { return BodyContent; }

	// Bound to a TextBlock named "BodyText" in any BP subclass; otherwise constructed in-code.
	UPROPERTY(meta = (BindWidget, OptionalWidget = true))
	TObjectPtr<UTextBlock> BodyText;

protected:
	virtual void NativeOnInitialized() override;

private:
	FText BodyContent;
};
