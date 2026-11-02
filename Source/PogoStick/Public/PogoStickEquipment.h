#pragma once

#include "CoreMinimal.h"
#include "Equipment/FGEquipment.h"
#include "PogoStickEquipment.generated.h"

class UAkAudioEvent;
class UAnimMontage;
class UAnimSequence;
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

	/// Whether the client's landing would bounce right now (bounce on, not cancelled by crouch or
	/// Primary Fire, aimed at a surface in range), and its minimum bounce speed. Fall damage is
	/// decided on the server when the player lands, before any bounce RPC could arrive, so the
	/// server must already know whether this landing is going to bounce.
	UFUNCTION(Server, Reliable)
	void Server_SetBounceSettings(bool bEnabled, float MinSpeed);

	UFUNCTION()
	void OnOwnerLanded(const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere, Category = "Pogo")
	TObjectPtr<USceneComponent> Root;

	/// Offset and rotation from the hand socket, from the Hold Position settings. In first person
	/// it's reattached to the arms' ArmBase bone instead (see UpdateHold).
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
	/// Builds the first-person (panini) material from the Rebar Gun's and hands it to the game.
	void RegisterFirstPersonMaterials();
	/// Places the stick: in first person in the middle of the view with both hands on the grips
	/// (the hold animation), otherwise in the hand from the Hold settings. Owning client.
	void UpdateHold(const FPogoStickSettings& Settings, float DeltaSeconds);
	/// Back to the third-person hold: the stick on the hand socket, no hold animation.
	void ReleaseFirstPersonHold();
	/// First person: how far the charge has pushed the stick in, 0 to 1, eased.
	float ChargePush = 0.0f;
	static constexpr float HoldBlendTime = 0.2f;
	void UpdateArmAnimation(ECameraMode CameraMode);
	/// The holder's third-person body is drawn, which photo mode does in first person too.
	bool IsThirdPersonBodyShown() const;
	/// Owning client: while charging, tilts the stick around its handlebar so its foot points at
	/// the push-off point. Runs after UpdateHold, which resets the hold every tick.
	void UpdateChargeAim(const FPogoStickSettings& Settings, float DeltaSeconds);
	float AimBlend = 0.0f;
	static constexpr float AimBlendTime = 0.15f;
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
	/// Owning client: plays the charge click the charge has just reached, if any.
	void PlayDueChargeClick();
	int32 NextChargeClick = 0;
	static constexpr float ChargeSoundFadeTime = 0.15f;
	void PlaySound2D(USoundBase* Sound) const;
	/// Owning client: the launch sounds, for both charged launches and landing bounces.
	void PlayLaunchSound() const;

	/// Server side. Applies or removes the no-fall-damage curve and client-trusted movement
	/// for the time between a launch and the next landing.
	void SetAirborneFromLaunch(bool bAirborne, bool bNoFallDamage);
	/// Server: applies the no-fall-damage curve while a launch or a coming bounce calls for it.
	void UpdateFallProtection();
	void SetNoFallDamage(bool bEnabled);
	bool IsCrouchHeld() const;
	/// Server: the current flight came from a charged launch with No Fall Damage on.
	bool bLaunchNoFallDamage = false;
	/// Server: falling fast enough to bounce, and the client says the landing would bounce.
	bool bBounceProtected = false;
	bool bNoFallDamageApplied = false;
	/// Server: who the protection applies to. GetInstigatorCharacter() returns null once the
	/// stick is unequipped, but a launch's protection can outlast the equip and must still be
	/// removed from the right player, or it sticks for good.
	AFGCharacterPlayer* GetProtectedCharacter() const;
	TWeakObjectPtr<AFGCharacterPlayer> LastEquippedCharacter;

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
	/// Server: ends the launch's flight, and its protection, once the owner is back on the ground.
	void TickAwaitingLanding(float DeltaSeconds);

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

	/// One per PogoStickAssets::ChargeClickPositions entry; null where an asset is missing.
	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> ChargeClicks;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> LaunchSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CancelSound;

	UPROPERTY(Transient)
	TObjectPtr<UAkAudioEvent> JumpPadLaunchSound;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ChargeAnimation;

	/// First person: the dynamic montage holding ChargeAnimation at the charge's frame.
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ChargeMontage;
};
