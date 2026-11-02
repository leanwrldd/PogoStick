#pragma once

#include "CoreMinimal.h"
#include "Equipment/FGEquipment.h"
#include "PogoStickEquipment.generated.h"

class UAkAudioEvent;
class UAudioComponent;
class UCurveFloat;
class UMaterialInterface;
class UPogoChargeWidget;
class USoundBase;
class UStaticMeshComponent;
struct FPogoStickSettings;

/// The Pogo Stick. Hold Primary Fire to charge; once charged it pushes off the surface the
/// holder is looking at (if it's within range), launching them the opposite way. While equipped, a hard landing bounces the
/// player back up (unless they're holding Primary Fire).
///
/// Charging and the launch run on the owning client so they feel instant, then the client
/// tells the server the launch velocity it used. The server applies the same launch and,
/// until the player lands, accepts the client's movement so the two don't fight.
UCLASS()
class POGOSTICK_API APogoStickEquipment : public AFGEquipment
{
	GENERATED_BODY()

public:
	APogoStickEquipment();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	virtual void Equip(AFGCharacterPlayer* Character) override;
	virtual void UnEquip() override;

protected:
	virtual void OnCameraModeChanged_Implementation(ECameraMode newCameraMode) override;
	virtual void HandleDefaultEquipmentActionEvent(EDefaultEquipmentAction Action, EDefaultEquipmentActionEvent ActionEvent) override;

	UFUNCTION(Server, Reliable)
	void Server_Launch(FVector_NetQuantize100 LaunchVelocity, bool bOverrideVelocity, bool bNoFallDamage);

	/// The client's bounce settings. Fall damage is decided on the server when the player
	/// lands, before any bounce RPC could arrive, so the server must already know whether this
	/// landing is going to bounce.
	UFUNCTION(Server, Reliable)
	void Server_SetBounceSettings(bool bEnabled, float MinSpeed);

	UFUNCTION()
	void OnOwnerLanded(const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere, Category = "Pogo")
	TObjectPtr<USceneComponent> Root;

	/// Offset and rotation from the hand socket, from the Hold Position settings.
	UPROPERTY(VisibleAnywhere, Category = "Pogo")
	TObjectPtr<USceneComponent> HoldAdjust;

	/// Handlebars and upper shaft. Its origin is the grip.
	UPROPERTY(VisibleAnywhere, Category = "Pogo")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/// Sits at the top of the spring and is squashed on Z while charging. The spring mesh
	/// hangs from it, so a placeholder mesh can be rotated and scaled to fit underneath.
	UPROPERTY(VisibleAnywhere, Category = "Pogo")
	TObjectPtr<USceneComponent> SpringPivot;

	UPROPERTY(VisibleAnywhere, Category = "Pogo")
	TObjectPtr<UStaticMeshComponent> SpringMesh;

	/// Sits at the bottom of the spring, moving up as it squashes.
	UPROPERTY(VisibleAnywhere, Category = "Pogo")
	TObjectPtr<USceneComponent> FootPivot;

	UPROPERTY(VisibleAnywhere, Category = "Pogo")
	TObjectPtr<UStaticMeshComponent> FootMesh;

private:
	void LoadArt();
	void UpdateHoldAdjust(const FPogoStickSettings& Settings);
	void UpdateArmAnimation(ECameraMode CameraMode);
	bool bArtLoaded = false;
	void LayOutParts(bool bPlaceholders);
	void UpdateSpringVisual(float ChargeAlpha);

	void StartCharging();
	void CancelCharging();
	void Launch(const FVector& Direction);

	/// The stick pushes off whatever you're looking at: true if a surface is within range along
	/// the view, with the launch direction (straight back along the view) in OutLaunchDirection.
	/// OutHit receives the trace result; TraceRangeMultiplier lets the HUD marker look further
	/// than the push-off range so it can show surfaces that are out of reach.
	bool FindPushSurface(const FPogoStickSettings& Settings, FVector& OutLaunchDirection,
		FHitResult* OutHit = nullptr, float TraceRangeMultiplier = 1.0f) const;

	/// Owning client: moves the HUD marker onto the point the push-off check hits.
	void UpdatePushPointMarker();

	void ShowChargeWidget(bool bShow);
	void StopChargeSound();
	void PlaySound2D(USoundBase* Sound) const;
	/// Owning client: the launch sounds, for both charged launches and landing bounces.
	void PlayLaunchSound() const;

	/// Server side. Applies or removes the no-fall-damage curve and client-trusted movement
	/// for the time between a launch and the next landing.
	void SetAirborneFromLaunch(bool bAirborne, bool bNoFallDamage);

	void SetLandedBound(bool bBound);
	/// Owning client: tracks the fall velocity and keeps the server's bounce settings current.
	void TickBounceLocal(const FPogoStickSettings& Settings);
	/// Server: protects a fall that is going to bounce from fall damage.
	void TickBounceServer();

	bool bCharging = false;
	float ChargeElapsed = 0.0f;
	float CooldownRemaining = 0.0f;
	/// Settings captured when charging starts, so a mid-charge config edit can't skew it.
	float ActiveChargeTime = 2.0f;
	float ActiveSpringCompression = 0.5f;

	/// Where the spring's top sits under the body, and its unsquashed height. Measured from
	/// the meshes' bounds once they load.
	float SpringTopZ = 0.0f;
	float SpringHeight = 0.0f;

	/// Shifts every part so the right grip, not the handlebar centre, sits on the hand socket.
	FVector PartsOffset = FVector::ZeroVector;

	bool bAwaitingLanding = false;
	float AirborneTime = 0.0f;

	/// Velocity on the last tick the owner was falling. Landing zeroes the vertical speed
	/// before OnOwnerLanded runs, so the bounce reads it from here.
	FVector LastFallVelocity = FVector::ZeroVector;
	bool bLandedBound = false;

	bool bSentBounceEnabled = false;
	float SentMinBounceSpeed = -1.0f;
	bool bServerBounceEnabled = false;
	float ServerMinBounceSpeed = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UCurveFloat> PreviousFallDamageOverride;

	UPROPERTY(Transient)
	TObjectPtr<UCurveFloat> NoFallDamageCurve;

	UPROPERTY(Transient)
	TObjectPtr<UPogoChargeWidget> ChargeWidget;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ChargeAudio;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ChargeSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> LaunchSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CancelSound;

	UPROPERTY(Transient)
	TObjectPtr<UAkAudioEvent> JumpPadLaunchSound;
};
