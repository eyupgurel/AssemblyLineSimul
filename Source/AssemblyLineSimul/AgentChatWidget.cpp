#include "AgentChatWidget.h"
#include "AgentChatSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableText.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"

namespace
{
	UButton* MakeAgentButton(UWidgetTree* Tree, UHorizontalBox* Row, const FString& Label,
		FName Name, UObject* Outer)
	{
		UButton* Btn = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* Lbl = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Lbl->SetText(FText::FromString(Label));
		Lbl->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		FSlateFontInfo Font = Lbl->GetFont();
		Font.Size = 18;
		Lbl->SetFont(Font);
		Btn->AddChild(Lbl);
		if (UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(Row->AddChild(Btn)))
		{
			Slot->SetPadding(FMargin(6.f, 0.f));
			FSlateChildSize Sz; Sz.Value = 1.f; Sz.SizeRule = ESlateSizeRule::Fill;
			Slot->SetSize(Sz);
		}
		return Btn;
	}
}

void UAgentChatWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Skip if a Blueprint subclass already provided a widget tree (RootWidget set).
	if (!WidgetTree || WidgetTree->RootWidget) return;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("Background"));
	Background->SetBrushColor(FLinearColor(0.05f, 0.08f, 0.12f, 0.85f));
	Background->SetPadding(FMargin(16.f));
	if (UCanvasPanelSlot* BgSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Background)))
	{
		BgSlot->SetAnchors(FAnchors(0.f, 1.f, 1.f, 1.f));   // bottom strip, full width
		BgSlot->SetAlignment(FVector2D(0.f, 1.f));          // anchor on bottom
		BgSlot->SetOffsets(FMargin(0.f, -200.f, 0.f, 0.f)); // 200px tall
		BgSlot->SetSize(FVector2D(0.f, 200.f));
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("Column"));
	Background->AddChild(Column);

	SelectionLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("SelectionLabel"));
	SelectionLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.47f, 0.86f, 1.f, 1.f)));
	{
		FSlateFontInfo F = SelectionLabel->GetFont(); F.Size = 18; SelectionLabel->SetFont(F);
	}
	Column->AddChild(SelectionLabel);

	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("ButtonRow"));
	Column->AddChild(Buttons);

	GeneratorButton = MakeAgentButton(WidgetTree, Buttons, TEXT("Generator"), TEXT("BtnGen"), this);
	FilterButton    = MakeAgentButton(WidgetTree, Buttons, TEXT("Filter"),    TEXT("BtnFil"), this);
	SorterButton    = MakeAgentButton(WidgetTree, Buttons, TEXT("Sorter"),    TEXT("BtnSor"), this);
	CheckerButton   = MakeAgentButton(WidgetTree, Buttons, TEXT("Checker"),   TEXT("BtnChk"), this);

	GeneratorButton->OnClicked.AddDynamic(this, &UAgentChatWidget::HandleGeneratorClicked);
	FilterButton   ->OnClicked.AddDynamic(this, &UAgentChatWidget::HandleFilterClicked);
	SorterButton   ->OnClicked.AddDynamic(this, &UAgentChatWidget::HandleSorterClicked);
	CheckerButton  ->OnClicked.AddDynamic(this, &UAgentChatWidget::HandleCheckerClicked);

	UHorizontalBox* InputRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("InputRow"));
	Column->AddChild(InputRow);

	InputBox = WidgetTree->ConstructWidget<UEditableText>(
		UEditableText::StaticClass(), TEXT("InputBox"));
	InputBox->SetHintText(FText::FromString(TEXT("Type your message and press Enter…")));
	InputBox->OnTextCommitted.AddDynamic(this, &UAgentChatWidget::HandleInputCommitted);
	{
		FSlateFontInfo F = InputBox->GetFont(); F.Size = 20; InputBox->SetFont(F);
	}
	if (UHorizontalBoxSlot* IbSlot = Cast<UHorizontalBoxSlot>(InputRow->AddChild(InputBox)))
	{
		IbSlot->SetPadding(FMargin(6.f, 4.f));
		FSlateChildSize Sz; Sz.Value = 1.f; Sz.SizeRule = ESlateSizeRule::Fill;
		IbSlot->SetSize(Sz);
	}

	UButton* SendBtn = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("SendBtn"));
	UTextBlock* SendLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	SendLbl->SetText(FText::FromString(TEXT("Send")));
	SendLbl->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	{
		FSlateFontInfo F = SendLbl->GetFont(); F.Size = 18; SendLbl->SetFont(F);
	}
	SendBtn->AddChild(SendLbl);
	SendBtn->OnClicked.AddDynamic(this, &UAgentChatWidget::HandleSendClicked);
	if (UHorizontalBoxSlot* SbSlot = Cast<UHorizontalBoxSlot>(InputRow->AddChild(SendBtn)))
	{
		SbSlot->SetPadding(FMargin(6.f, 4.f));
	}

	RefreshSelectionLabel();
}

void UAgentChatWidget::SetSelectedAgent(EStationType StationType)
{
	SelectedAgent = StationType;
	RefreshSelectionLabel();
}

void UAgentChatWidget::HandleGeneratorClicked() { SetSelectedAgent(EStationType::Generator); }
void UAgentChatWidget::HandleFilterClicked()    { SetSelectedAgent(EStationType::Filter);    }
void UAgentChatWidget::HandleSorterClicked()    { SetSelectedAgent(EStationType::Sorter);    }
void UAgentChatWidget::HandleCheckerClicked()   { SetSelectedAgent(EStationType::Checker);   }

void UAgentChatWidget::HandleSendClicked()
{
	SubmitCurrentInput();
}

void UAgentChatWidget::HandleInputCommitted(const FText& /*Text*/, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		SubmitCurrentInput();
	}
}

void UAgentChatWidget::SubmitCurrentInput()
{
	if (!InputBox) return;
	const FString Msg = InputBox->GetText().ToString().TrimStartAndEnd();
	if (Msg.IsEmpty()) return;

	UAgentChatSubsystem* Chat = nullptr;
	if (UGameInstance* GI = GetGameInstance())
	{
		Chat = GI->GetSubsystem<UAgentChatSubsystem>();
	}
	if (Chat)
	{
		Chat->SendMessage(SelectedAgent, Msg);
	}
	InputBox->SetText(FText::GetEmpty());
}

void UAgentChatWidget::RefreshSelectionLabel()
{
	if (!SelectionLabel) return;
	const TCHAR* Name = TEXT("Generator");
	switch (SelectedAgent)
	{
	case EStationType::Generator: Name = TEXT("Generator"); break;
	case EStationType::Filter:    Name = TEXT("Filter");    break;
	case EStationType::Sorter:    Name = TEXT("Sorter");    break;
	case EStationType::Checker:   Name = TEXT("Checker");   break;
	}
	SelectionLabel->SetText(FText::FromString(FString::Printf(TEXT("Talking to: %s"), Name)));
}

UButton* UAgentChatWidget::GetButtonFor(EStationType St) const
{
	switch (St)
	{
	case EStationType::Generator: return GeneratorButton;
	case EStationType::Filter:    return FilterButton;
	case EStationType::Sorter:    return SorterButton;
	case EStationType::Checker:   return CheckerButton;
	default: return nullptr;
	}
}
