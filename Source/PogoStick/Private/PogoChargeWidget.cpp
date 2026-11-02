#include "PogoChargeWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

namespace
{
const FLinearColor ChargingColor = FLinearColor::FromSRGBColor(FColor(245, 155, 45));
const FLinearColor FullColor = FLinearColor::FromSRGBColor(FColor(120, 230, 90));
const FLinearColor NoSurfaceColor = FLinearColor::FromSRGBColor(FColor(110, 110, 110));
const FLinearColor OutOfRangeMarkerColor = FLinearColor::FromSRGBColor(FColor(230, 70, 60));

constexpr float MarkerSize = 10.0f;
}

void UPogoChargeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (WidgetTree == nullptr || WidgetTree->RootWidget != nullptr)
	{
		return;
	}

	UCanvasPanel* canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
	WidgetTree->RootWidget = canvas;

	Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ChargeBar"));
	Bar->SetFillColorAndOpacity(ChargingColor);
	Bar->SetPercent(0.0f);

	// Centred, a little below the crosshair.
	UCanvasPanelSlot* barSlot = canvas->AddChildToCanvas(Bar);
	barSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	barSlot->SetAlignment(FVector2D(0.5f, 0.0f));
	barSlot->SetPosition(FVector2D(0.0f, 48.0f));
	barSlot->SetSize(FVector2D(180.0f, 8.0f));

	// The push-off point. Positioned in absolute widget space each tick, so anchored top-left.
	Marker = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PushPointMarker"));
	Marker->SetBrushColor(FullColor);
	UCanvasPanelSlot* markerSlot = canvas->AddChildToCanvas(Marker);
	markerSlot->SetAnchors(FAnchors(0.0f, 0.0f));
	markerSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	markerSlot->SetSize(FVector2D(MarkerSize, MarkerSize));

	DistanceLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PushPointDistance"));
	FSlateFontInfo font = DistanceLabel->GetFont();
	font.Size = 12;
	DistanceLabel->SetFont(font);
	DistanceLabel->SetShadowOffset(FVector2D(1.0f, 1.0f));
	DistanceLabel->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
	UCanvasPanelSlot* labelSlot = canvas->AddChildToCanvas(DistanceLabel);
	labelSlot->SetAnchors(FAnchors(0.0f, 0.0f));
	labelSlot->SetAlignment(FVector2D(0.0f, 0.5f));
	labelSlot->SetAutoSize(true);

	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetChargeVisible(false);
	SetPushPoint(false, FVector2D::ZeroVector, 0.0f, false);
}

void UPogoChargeWidget::SetCharge(float Alpha, bool bSurfaceInRange)
{
	if (Bar == nullptr)
	{
		return;
	}
	Bar->SetPercent(Alpha);
	Bar->SetFillColorAndOpacity(!bSurfaceInRange ? NoSurfaceColor : Alpha >= 1.0f ? FullColor : ChargingColor);
}

void UPogoChargeWidget::SetChargeVisible(bool bVisible)
{
	if (Bar != nullptr)
	{
		Bar->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UPogoChargeWidget::SetPushPoint(bool bHit, const FVector2D& ScreenPosition, float DistanceMeters, bool bInRange, bool bShowDistance)
{
	if (Marker == nullptr || DistanceLabel == nullptr)
	{
		return;
	}

	const ESlateVisibility visibility = bHit ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	Marker->SetVisibility(visibility);
	DistanceLabel->SetVisibility(bShowDistance ? visibility : ESlateVisibility::Collapsed);
	if (!bHit)
	{
		return;
	}

	const FLinearColor color = bInRange ? FullColor : OutOfRangeMarkerColor;
	Marker->SetBrushColor(color);
	DistanceLabel->SetColorAndOpacity(FSlateColor(color));
	DistanceLabel->SetText(FText::FromString(FString::Printf(TEXT("%.1f m"), DistanceMeters)));

	if (UCanvasPanelSlot* markerSlot = Cast<UCanvasPanelSlot>(Marker->Slot))
	{
		markerSlot->SetPosition(ScreenPosition);
	}
	if (UCanvasPanelSlot* labelSlot = Cast<UCanvasPanelSlot>(DistanceLabel->Slot))
	{
		labelSlot->SetPosition(ScreenPosition + FVector2D(MarkerSize, 0.0f));
	}
}
