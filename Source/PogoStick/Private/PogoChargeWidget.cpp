#include "PogoChargeWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"

namespace
{
const FLinearColor FullColor = FLinearColor::FromSRGBColor(FColor(120, 230, 90));
const FLinearColor OutOfRangeMarkerColor = FLinearColor::FromSRGBColor(FColor(230, 70, 60));

const FLinearColor CooldownColor = FLinearColor::FromSRGBColor(FColor(245, 155, 45));
const FLinearColor ChargedNoSurfaceColor = FLinearColor::FromSRGBColor(FColor(30, 115, 255));

constexpr float MarkerSize = 10.0f;

// Like the Grappling Hook's in-reach indicator: two corners of a hexagon a little larger than
// the game's hexagonal crosshair (pointy end up), one top-left and its mirror bottom-right.
// Each runs part way along the slanted top edge, round the corner and down the side.
constexpr float BowHexRadius = 21.0f; // centre to corner, at the middle of the stroke
constexpr float BowWidth = 3.0f;
constexpr float BowSlantedFraction = 0.6f; // of the slanted edge, measured from the corner
constexpr float BowSideFraction = 0.7f; // of the vertical side, measured from the corner
// Each bow is filled with thin strokes along concentric hexagons rather than one thick
// polyline, so the ends come out square instead of pointed.
constexpr int32 BowStrokes = 4;

FLinearColor GetCrosshairColor(EPogoCrosshairState State)
{
	switch (State)
	{
	case EPogoCrosshairState::Ready:
		return FullColor;
	case EPogoCrosshairState::ChargedNoSurface:
		return ChargedNoSurfaceColor;
	default:
		return CooldownColor;
	}
}
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
	SetPushPoint(false, FVector2D::ZeroVector, 0.0f, false);
}

void UPogoChargeWidget::SetCrosshairState(EPogoCrosshairState State)
{
	if (State == ShownCrosshairState)
	{
		return;
	}
	ShownCrosshairState = State;
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 UPogoChargeWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (ShownCrosshairState == EPogoCrosshairState::None)
	{
		return LayerId;
	}

	const FVector2f center = FVector2f(AllottedGeometry.GetLocalSize()) * 0.5f;
	const FLinearColor color = GetCrosshairColor(ShownCrosshairState);

	// The top-left bow on a unit hexagon (screen space, y down): top corner, upper-left corner,
	// lower-left corner.
	const float halfWidth = FMath::Cos(FMath::DegreesToRadians(30.0f));
	const FVector2f top(0.0f, -1.0f);
	const FVector2f upperLeft(-halfWidth, -0.5f);
	const FVector2f lowerLeft(-halfWidth, 0.5f);
	const FVector2f bow[] = {
		upperLeft + (top - upperLeft) * BowSlantedFraction,
		upperLeft,
		upperLeft + (lowerLeft - upperLeft) * BowSideFraction,
	};

	const float strokeSpacing = BowWidth / BowStrokes;
	for (const float side : { 1.0f, -1.0f }) // -1 turns it half round into the bottom-right bow
	{
		for (int32 stroke = 0; stroke < BowStrokes; ++stroke)
		{
			const float radius = BowHexRadius - BowWidth * 0.5f + strokeSpacing * (stroke + 0.5f);
			TArray<FVector2f> points;
			for (const FVector2f& point : bow)
			{
				points.Add(center + point * side * radius);
			}
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), MoveTemp(points),
				ESlateDrawEffect::None, color, true, strokeSpacing + 0.5f);
		}
	}
	return LayerId + 1;
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
