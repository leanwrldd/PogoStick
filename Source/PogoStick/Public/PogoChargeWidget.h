#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PogoChargeWidget.generated.h"

class UBorder;
class UProgressBar;
class UTextBlock;

/// The Pogo Stick's HUD, built in C++ so no widget Blueprint is needed: a charge bar under the
/// crosshair, plus a marker on the surface the stick would push off with its distance.
UCLASS()
class POGOSTICK_API UPogoChargeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/// bSurfaceInRange false greys the bar out: nothing close enough to push off.
	void SetCharge(float Alpha, bool bSurfaceInRange = true);
	void SetChargeVisible(bool bVisible);

	/// Places the marker at ScreenPosition (widget space). bHit false hides it: the view
	/// trace didn't reach any surface.
	void SetPushPoint(bool bHit, const FVector2D& ScreenPosition, float DistanceMeters, bool bInRange, bool bShowDistance = true);

protected:
	virtual void NativeOnInitialized() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> Bar;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Marker;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DistanceLabel;
};
