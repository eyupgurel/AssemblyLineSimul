#include "StationTalkWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"

void UStationTalkWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Skip if a Blueprint subclass already provided a widget tree (RootWidget set).
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	BodyText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("BodyText"));
	if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Root->AddChild(BodyText)))
	{
		Slot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		Slot->SetOffsets(FMargin(0.f));
	}

	BodyText->SetText(BodyContent);
	BodyText->SetColorAndOpacity(FLinearColor(0.47f, 0.86f, 1.f, 1.f));
	BodyText->SetJustification(ETextJustify::Center);
	FSlateFontInfo Font = BodyText->GetFont();
	Font.Size = 36;
	BodyText->SetFont(Font);
}

void UStationTalkWidget::SetBody(const FText& InText)
{
	BodyContent = InText;
	if (BodyText)
	{
		BodyText->SetText(InText);
	}
}
