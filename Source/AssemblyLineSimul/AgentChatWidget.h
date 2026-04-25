#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AssemblyLineTypes.h"
#include "AgentChatWidget.generated.h"

class UButton;
class UEditableText;
class UTextBlock;

UCLASS()
class ASSEMBLYLINESIMUL_API UAgentChatWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetSelectedAgent(EStationType StationType);
	EStationType GetSelectedAgent() const { return SelectedAgent; }

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION() void HandleGeneratorClicked();
	UFUNCTION() void HandleFilterClicked();
	UFUNCTION() void HandleSorterClicked();
	UFUNCTION() void HandleCheckerClicked();
	UFUNCTION() void HandleSendClicked();
	UFUNCTION() void HandleInputCommitted(const FText& Text, ETextCommit::Type CommitMethod);

private:
	EStationType SelectedAgent = EStationType::Generator;

	UPROPERTY() TObjectPtr<UButton>       GeneratorButton;
	UPROPERTY() TObjectPtr<UButton>       FilterButton;
	UPROPERTY() TObjectPtr<UButton>       SorterButton;
	UPROPERTY() TObjectPtr<UButton>       CheckerButton;
	UPROPERTY() TObjectPtr<UEditableText> InputBox;
	UPROPERTY() TObjectPtr<UTextBlock>    SelectionLabel;

	void SubmitCurrentInput();
	void RefreshSelectionLabel();
	UButton* GetButtonFor(EStationType St) const;
};
