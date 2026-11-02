#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PogoChargeWidget.generated.h"

class UBorder;
class UTextBlock;

/// What the ring around the crosshair shows.
enum class EPogoCrosshairState : uint8
{
	/// Nothing in reach: no ring.
	None,
	/// A surface is in reach, so charging now would launch.
	Ready,
	/// Fully charged but nothing in reach, so releasing now would cancel.
	ChargedNoSurface,
	/// Cooling down after a launch.
	Cooldown,
};

/// The Pogo Stick's HUD, built in C++ so no widget Blueprint is needed: two bows around
/// the crosshair and a marker on the surface the stick would push off with its distance. There is no
/// charge bar; the squashing spring shows the charge.
UCLASS()
class POGOSTICK_API UPogoChargeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/// Places the marker at ScreenPosition (widget space). bHit false hides it: the view
	/// trace didn't reach any surface.
	void SetPushPoint(bool bHit, const FVector2D& ScreenPosition, float DistanceMeters, bool bInRange, bool bShowDistance = true);

	void SetCrosshairState(EPogoCrosshairState State);

protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UBorder> Marker;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DistanceLabel;

	/// The state the bows around the crosshair are painted in.
	EPogoCrosshairState ShownCrosshairState = EPogoCrosshairState::None;
};
