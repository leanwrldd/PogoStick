#include "PogoStickEquipment.h"

#include "AkAudioEvent.h"
#include "AkGameplayStatics.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/StaticMesh.h"
#include "FGCharacterMovementComponent.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "PogoChargeWidget.h"
#include "PogoStick.h"
#include "PogoStickAssets.h"
#include "PogoStickConfiguration.h"
#include "Sound/SoundBase.h"
#include "UObject/UnrealType.h"

namespace
{
// The anim instance re-reads mArmAnimation every frame, so the pose can follow the camera mode.
// First person: the Rebar Gun pose keeps the stick in view (the hold offset was tuned with it).
// Third person: AE_RebarGun (and AE_Generic1Hand/2Hand) make the 3P anim graph scale every
// upper-body bone 2x for a native equipment class, ballooning the character; AE_OneHandEquipment
// is clean there but puts the first-person hand, and the stick, out of view.
constexpr EArmEquipment FirstPersonArmAnimation = EArmEquipment::AE_RebarGun;
constexpr EArmEquipment ThirdPersonArmAnimation = EArmEquipment::AE_OneHandEquipment;

/// Upper bound the server accepts from a client, in cm/s, so a modified client can't fling
/// itself across the map. Matches the config's 200 m/s maximum.
constexpr float MaxAcceptedLaunchSpeed = 200.0f * 100.0f;

/// The charge sound is authored for the default charge time and pitched to match others.
constexpr float AuthoredChargeTime = 2.0f;

/// Grace period before the server starts checking for a landing. The launch tick itself can
/// still report the player as on the ground.
constexpr float MinAirborneTime = 0.2f;

FObjectProperty* GetFallDamageOverrideProperty()
{
	// Protected on AFGCharacterBase; there's a setter but no getter.
	static FObjectProperty* property = FindFProperty<FObjectProperty>(AFGCharacterBase::StaticClass(), TEXT("mFallDamageCurveOverride"));
	return property;
}

template <typename T>
T* LoadOptional(const TCHAR* path)
{
	T* loaded = LoadObject<T>(nullptr, path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (loaded == nullptr)
	{
		UE_LOG(LogPogoStick, Warning, TEXT("Missing asset %s; continuing without it."), path);
	}
	return loaded;
}
} // namespace

APogoStickEquipment::APogoStickEquipment()
{
	PrimaryActorTick.bCanEverTick = true;

	mEquipmentSlot = EEquipmentSlot::ES_ARMS;
	// Switched per camera mode by UpdateArmAnimation; this is the third-person pose.
	mArmAnimation = ThirdPersonArmAnimation;
	mAttachSocket = TEXT("hand_rSocket");
	mDefaultEquipmentActions = static_cast<uint8>(EDefaultEquipmentAction::PrimaryFire);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	auto makePart = [this](const TCHAR* name, USceneComponent* parent)
	{
		UStaticMeshComponent* part = CreateDefaultSubobject<UStaticMeshComponent>(name);
		part->SetupAttachment(parent);
		part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		part->SetGenerateOverlapEvents(false);
		part->SetCastShadow(true);
		return part;
	};
	// Everything visible hangs off HoldAdjust, which the hold-position settings move and rotate
	// relative to the hand socket.
	HoldAdjust = CreateDefaultSubobject<USceneComponent>(TEXT("HoldAdjust"));
	HoldAdjust->SetupAttachment(Root);

	BodyMesh = makePart(TEXT("BodyMesh"), HoldAdjust);

	SpringPivot = CreateDefaultSubobject<USceneComponent>(TEXT("SpringPivot"));
	SpringPivot->SetupAttachment(HoldAdjust);
	SpringMesh = makePart(TEXT("SpringMesh"), SpringPivot);

	FootPivot = CreateDefaultSubobject<USceneComponent>(TEXT("FootPivot"));
	FootPivot->SetupAttachment(HoldAdjust);
	FootMesh = makePart(TEXT("FootMesh"), FootPivot);
}

void APogoStickEquipment::BeginPlay()
{
	Super::BeginPlay();
	LoadArt();
}

void APogoStickEquipment::OnCameraModeChanged_Implementation(ECameraMode newCameraMode)
{
	// Before the mesh visibility swap, so the third-person mesh never gets a frame of the
	// first-person pose.
	UpdateArmAnimation(newCameraMode);
	Super::OnCameraModeChanged_Implementation(newCameraMode);
}

void APogoStickEquipment::UpdateArmAnimation(ECameraMode CameraMode)
{
	// Other players only ever see the third-person mesh.
	mArmAnimation = IsLocalInstigator() && CameraMode == ECameraMode::ECM_FirstPerson
		? FirstPersonArmAnimation : ThirdPersonArmAnimation;
}

void APogoStickEquipment::UpdateHoldAdjust(const FPogoStickSettings& Settings)
{
	// The parts are laid out with the grip at HoldAdjust's origin, so scaling here shrinks the
	// stick around the hand.
	const FTransform hold(Settings.HoldRotation, Settings.HoldOffset, FVector(Settings.HoldScale));
	if (!HoldAdjust->GetRelativeTransform().Equals(hold))
	{
		HoldAdjust->SetRelativeTransform(hold);
	}
}

void APogoStickEquipment::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopChargeSound();
	ShowChargeWidget(false);
	SetLandedBound(false);
	SetAirborneFromLaunch(false, false);
	Super::EndPlay(EndPlayReason);
}

void APogoStickEquipment::LoadArt()
{
	// The game can call Equip() before BeginPlay(); whichever comes first loads the art, so
	// the first-person materials are registered before the equip applies them.
	if (bArtLoaded)
	{
		return;
	}
	bArtLoaded = true;

	UStaticMesh* body = LoadOptional<UStaticMesh>(PogoStickAssets::BodyMesh);
	UStaticMesh* spring = LoadOptional<UStaticMesh>(PogoStickAssets::SpringMesh);
	UStaticMesh* foot = LoadOptional<UStaticMesh>(PogoStickAssets::FootMesh);

	// All three come from one FBX, so treat them as a set: if any is missing, use stand-ins
	// for all of them rather than mixing real parts with placeholders.
	const bool bPlaceholders = body == nullptr || spring == nullptr || foot == nullptr;
	if (bPlaceholders)
	{
		UE_LOG(LogPogoStick, Warning, TEXT("Pogo Stick meshes not imported yet; using base-game placeholder meshes."));
		UStaticMesh* rod = LoadObject<UStaticMesh>(nullptr, PogoStickAssets::PlaceholderRodMesh, nullptr, LOAD_NoWarn | LOAD_Quiet);
		UStaticMesh* pipe = LoadObject<UStaticMesh>(nullptr, PogoStickAssets::PlaceholderSpringMesh, nullptr, LOAD_NoWarn | LOAD_Quiet);
		body = rod;
		spring = pipe;
		foot = rod;
	}

	BodyMesh->SetStaticMesh(body);
	SpringMesh->SetStaticMesh(spring);
	FootMesh->SetStaticMesh(foot);

	// No first-person (panini) materials: built against the SML project's placeholder
	// MPC_Panini they cook with a shader map ID the game never finds, and render as the default
	// checker material. See Tools/import_art.py.

	ChargeSound = LoadOptional<USoundBase>(PogoStickAssets::ChargeSound);
	LaunchSound = LoadOptional<USoundBase>(PogoStickAssets::LaunchSound);
	CancelSound = LoadOptional<USoundBase>(PogoStickAssets::CancelSound);
	JumpPadLaunchSound = LoadOptional<UAkAudioEvent>(PogoStickAssets::JumpPadLaunchSound);

	LayOutParts(bPlaceholders);
}

namespace
{
/// Rotates and scales a mesh of any shape or orientation so its longest axis points down
/// from its parent's origin, Length long and Thickness wide. Used for the stand-in meshes,
/// which weren't made with the pogo stick's pivots and proportions in mind.
void FitAlongNegativeZ(UStaticMeshComponent* Component, float Length, float Thickness)
{
	const UStaticMesh* mesh = Component->GetStaticMesh();
	if (mesh == nullptr)
	{
		return;
	}

	const FBox bounds = mesh->GetBoundingBox();
	const FVector size = bounds.GetSize();
	const int32 longAxis = size.X >= size.Y && size.X >= size.Z ? 0 : (size.Y >= size.Z ? 1 : 2);

	// Map the long axis onto Z: pitch turns X towards Z, roll turns Y towards Z.
	const FRotator rotation = longAxis == 0 ? FRotator(90.0f, 0.0f, 0.0f)
		: longAxis == 1 ? FRotator(0.0f, 0.0f, 90.0f)
		: FRotator::ZeroRotator;

	FVector scale;
	for (int32 axis = 0; axis < 3; ++axis)
	{
		const float extent = FMath::Max(size[axis], 1.0f);
		const float maxThin = FMath::Max3(axis == 0 ? 0.0f : size.X, axis == 1 ? 0.0f : size.Y, axis == 2 ? 0.0f : size.Z);
		scale[axis] = axis == longAxis ? Length / extent : Thickness / FMath::Max(maxThin, 1.0f);
	}

	// Centre the fitted mesh under the parent's origin, with its top edge at the origin.
	const FTransform fit(rotation, FVector::ZeroVector, scale);
	const FVector center = fit.TransformPosition(bounds.GetCenter());
	Component->SetRelativeTransform(FTransform(rotation, -center - FVector(0.0f, 0.0f, Length * 0.5f), scale));
}
} // namespace

void APogoStickEquipment::LayOutParts(bool bPlaceholders)
{
	if (bPlaceholders)
	{
		// Rough real-world proportions, in cm.
		constexpr float BodyLength = 100.0f;
		constexpr float SpringLength = 25.0f;
		FitAlongNegativeZ(BodyMesh, BodyLength, 4.0f);
		FitAlongNegativeZ(SpringMesh, SpringLength, 8.0f);
		FitAlongNegativeZ(FootMesh, 30.0f, 3.0f);
		SpringTopZ = -BodyLength;
		SpringHeight = SpringLength;
		PartsOffset = FVector::ZeroVector;
	}
	else
	{
		// The real meshes are authored with each part's origin at its top edge (the body's is
		// the handlebar centre), so they need no fitting: just stack them using their bounds,
		// shifted so the right grip lands in the hand.
		PartsOffset = -PogoStickAssets::RightGripOffset;
		BodyMesh->SetRelativeTransform(FTransform(PartsOffset));
		SpringMesh->SetRelativeTransform(FTransform::Identity);
		FootMesh->SetRelativeTransform(FTransform::Identity);
		SpringTopZ = BodyMesh->GetStaticMesh() != nullptr ? BodyMesh->GetStaticMesh()->GetBoundingBox().Min.Z : 0.0f;
		SpringHeight = SpringMesh->GetStaticMesh() != nullptr ? -SpringMesh->GetStaticMesh()->GetBoundingBox().Min.Z : 0.0f;
	}
	UpdateSpringVisual(0.0f);
}

void APogoStickEquipment::UpdateSpringVisual(float ChargeAlpha)
{
	const float squash = 1.0f - ActiveSpringCompression * FMath::Clamp(ChargeAlpha, 0.0f, 1.0f);
	SpringPivot->SetRelativeLocation(PartsOffset + FVector(0.0f, 0.0f, SpringTopZ));
	SpringPivot->SetRelativeScale3D(FVector(1.0f, 1.0f, squash));
	FootPivot->SetRelativeLocation(PartsOffset + FVector(0.0f, 0.0f, SpringTopZ - SpringHeight * squash));
}

void APogoStickEquipment::Equip(AFGCharacterPlayer* Character)
{
	LoadArt();
	UpdateHoldAdjust(FPogoStickSettings::Read(this));
	Super::Equip(Character);
	UpdateArmAnimation(GetInstigatorCameraMode());
	bCharging = false;
	UpdateSpringVisual(0.0f);

	const USceneComponent* attachParent = GetRootComponent()->GetAttachParent();
	UE_LOG(LogPogoStick, Log, TEXT("Equipped: attached to %s socket '%s' (wanted '%s')"),
		*GetNameSafe(attachParent), *GetRootComponent()->GetAttachSocketName().ToString(), *mAttachSocket.ToString());

	LastFallVelocity = FVector::ZeroVector;
	SentMinBounceSpeed = -1.0f; // forces the bounce settings to be sent on the next tick
	if (IsLocalInstigator())
	{
		SetLandedBound(true);
		// The HUD stays up while equipped so the push-off marker can be aimed before charging.
		ShowChargeWidget(true);
	}
}

void APogoStickEquipment::UnEquip()
{
	if (bCharging)
	{
		CancelCharging();
	}
	ShowChargeWidget(false);
	SetLandedBound(false);
	bServerBounceEnabled = false;

	// Leave the no-fall-damage protection in place if the player swaps tools mid-air; Tick
	// keeps checking for the landing. Only drop it here if they're already on the ground.
	if (HasAuthority() && bAwaitingLanding)
	{
		const AFGCharacterPlayer* player = GetInstigatorCharacter();
		const UFGCharacterMovementComponent* movement = player != nullptr ? player->GetFGMovementComponent() : nullptr;
		if (movement == nullptr || movement->IsMovingOnGround())
		{
			SetAirborneFromLaunch(false, false);
		}
	}

	Super::UnEquip();
}

void APogoStickEquipment::HandleDefaultEquipmentActionEvent(EDefaultEquipmentAction Action, EDefaultEquipmentActionEvent ActionEvent)
{
	Super::HandleDefaultEquipmentActionEvent(Action, ActionEvent);

	// Called on both server and client; the owning client drives the charge.
	if (Action != EDefaultEquipmentAction::PrimaryFire || !IsLocalInstigator())
	{
		return;
	}

	if (ActionEvent == EDefaultEquipmentActionEvent::Pressed)
	{
		StartCharging();
	}
	else if (bCharging)
	{
		// Released. A full charge launches on release if there's a surface to push off (with
		// auto-launch off, or when auto-launch was waiting for one); anything else cancels.
		FVector launchDirection;
		if (ChargeElapsed >= ActiveChargeTime && FindPushSurface(FPogoStickSettings::Read(this), launchDirection))
		{
			Launch(launchDirection);
		}
		else
		{
			CancelCharging();
		}
	}
}

void APogoStickEquipment::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateArmAnimation(GetInstigatorCameraMode());

	CooldownRemaining = FMath::Max(0.0f, CooldownRemaining - DeltaSeconds);

	if (bCharging)
	{
		ChargeElapsed += DeltaSeconds;
		const float alpha = FMath::Clamp(ChargeElapsed / ActiveChargeTime, 0.0f, 1.0f);
		UpdateSpringVisual(alpha);

		const FPogoStickSettings settings = FPogoStickSettings::Read(this);
		FVector launchDirection;
		const bool bSurfaceInRange = FindPushSurface(settings, launchDirection);
		if (ChargeWidget != nullptr)
		{
			ChargeWidget->SetCharge(alpha, bSurfaceInRange);
		}

		// A full charge is held until there's something to push off.
		if (alpha >= 1.0f && settings.bAutoLaunch && bSurfaceInRange)
		{
			Launch(launchDirection);
		}
	}

	if (IsLocalInstigator())
	{
		const FPogoStickSettings settings = FPogoStickSettings::Read(this);
		TickBounceLocal(settings);
		UpdatePushPointMarker();
		// Live, so the hold position can be tuned from the mod settings while looking at it.
		UpdateHoldAdjust(settings);
	}
	if (HasAuthority())
	{
		TickBounceServer();
	}

	if (bAwaitingLanding && HasAuthority())
	{
		AirborneTime += DeltaSeconds;
		const AFGCharacterPlayer* player = GetInstigatorCharacter();
		const UFGCharacterMovementComponent* movement = player != nullptr ? player->GetFGMovementComponent() : nullptr;
		if (movement == nullptr || (AirborneTime > MinAirborneTime && movement->IsMovingOnGround()))
		{
			SetAirborneFromLaunch(false, false);
		}
	}
}

void APogoStickEquipment::StartCharging()
{
	if (bCharging || CooldownRemaining > 0.0f)
	{
		return;
	}

	const FPogoStickSettings settings = FPogoStickSettings::Read(this);
	ActiveChargeTime = settings.ChargeTime;
	ActiveSpringCompression = settings.SpringCompression;
	ChargeElapsed = 0.0f;
	bCharging = true;

	ShowChargeWidget(true);
	if (ChargeWidget != nullptr)
	{
		ChargeWidget->SetCharge(0.0f);
		ChargeWidget->SetChargeVisible(true);
	}

	StopChargeSound();
	if (ChargeSound != nullptr)
	{
		const float pitch = FMath::Clamp(AuthoredChargeTime / ActiveChargeTime, 0.25f, 4.0f);
		ChargeAudio = UGameplayStatics::SpawnSound2D(this, ChargeSound, 1.0f, pitch);
	}
}

void APogoStickEquipment::CancelCharging()
{
	bCharging = false;
	ChargeElapsed = 0.0f;
	UpdateSpringVisual(0.0f);
	if (ChargeWidget != nullptr)
	{
		ChargeWidget->SetChargeVisible(false);
	}
	StopChargeSound();
	PlaySound2D(CancelSound);
}

bool APogoStickEquipment::FindPushSurface(const FPogoStickSettings& Settings, FVector& OutLaunchDirection,
	FHitResult* OutHit, float TraceRangeMultiplier) const
{
	const AFGCharacterPlayer* player = GetInstigatorCharacter();
	UWorld* world = GetWorld();
	if (player == nullptr || world == nullptr)
	{
		return false;
	}

	// From the character's eyes rather than the camera, so third person (where the camera sits
	// well behind the player) measures the same distance as first person.
	FVector eyeLocation;
	FRotator viewRotation;
	player->GetActorEyesViewPoint(eyeLocation, viewRotation);
	const FVector viewDirection = viewRotation.Vector();

	FCollisionQueryParams params(SCENE_QUERY_STAT(PogoStickPushSurface), false);
	params.AddIgnoredActor(player);
	params.AddIgnoredActor(this);

	FHitResult localHit;
	FHitResult& hit = OutHit != nullptr ? *OutHit : localHit;
	const float range = Settings.MaxSurfaceDistance * 100.0f;
	const FVector traceEnd = eyeLocation + viewDirection * range * FMath::Max(1.0f, TraceRangeMultiplier);
	if (!world->LineTraceSingleByChannel(hit, eyeLocation, traceEnd, ECC_Visibility, params) || hit.Distance > range)
	{
		return false;
	}

	// Push straight back along the stick: look at the floor and you go up, look at a wall and
	// you're thrown away from it, look at a ceiling and you're slammed down.
	OutLaunchDirection = -viewDirection;
	return true;
}

void APogoStickEquipment::UpdatePushPointMarker()
{
	if (ChargeWidget == nullptr)
	{
		return;
	}

	const AFGCharacterPlayer* player = GetInstigatorCharacter();
	APlayerController* controller = player != nullptr ? Cast<APlayerController>(player->GetController()) : nullptr;

	// Look three times further than the push-off range so surfaces just out of reach show up
	// too, in red, with their distance.
	FVector launchDirection;
	FHitResult hit;
	const FPogoStickSettings settings = FPogoStickSettings::Read(this);
	const bool bInRange = FindPushSurface(settings, launchDirection, &hit, 3.0f);

	FVector2D screenPosition = FVector2D::ZeroVector;
	const bool bVisible = settings.bShowPushPoint && hit.bBlockingHit && controller != nullptr
		&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(controller, hit.ImpactPoint, screenPosition, false);
	ChargeWidget->SetPushPoint(bVisible, screenPosition, hit.Distance / 100.0f, bInRange, settings.bShowPushDistance);
}

void APogoStickEquipment::Launch(const FVector& Direction)
{
	bCharging = false;
	ChargeElapsed = 0.0f;
	UpdateSpringVisual(0.0f);
	if (ChargeWidget != nullptr)
	{
		ChargeWidget->SetChargeVisible(false);
	}
	StopChargeSound();

	AFGCharacterPlayer* player = GetInstigatorCharacter();
	if (player == nullptr)
	{
		return;
	}

	const FPogoStickSettings settings = FPogoStickSettings::Read(this);
	CooldownRemaining = settings.Cooldown;

	const FVector velocity = Direction * settings.LaunchSpeed * 100.0f;
	const bool bOverride = !settings.bKeepMomentum;

	player->LaunchCharacter(velocity, bOverride, bOverride);
	PlayLaunchSound();

	if (HasAuthority())
	{
		SetAirborneFromLaunch(true, settings.bNoFallDamage);
	}
	else
	{
		Server_Launch(velocity, bOverride, settings.bNoFallDamage);
	}

	UE_LOG(LogPogoStick, Verbose, TEXT("Launched at %s (override=%d)"), *velocity.ToString(), bOverride);
}

void APogoStickEquipment::Server_Launch_Implementation(FVector_NetQuantize100 LaunchVelocity, bool bOverrideVelocity, bool bNoFallDamage)
{
	AFGCharacterPlayer* player = GetInstigatorCharacter();
	if (player == nullptr)
	{
		return;
	}

	player->LaunchCharacter(LaunchVelocity.GetClampedToMaxSize(MaxAcceptedLaunchSpeed), bOverrideVelocity, bOverrideVelocity);
	SetAirborneFromLaunch(true, bNoFallDamage);
}

void APogoStickEquipment::SetAirborneFromLaunch(bool bAirborne, bool bNoFallDamage)
{
	if (!HasAuthority())
	{
		return;
	}

	AFGCharacterPlayer* player = GetInstigatorCharacter();
	FObjectProperty* overrideProperty = GetFallDamageOverrideProperty();

	if (bAirborne)
	{
		AirborneTime = 0.0f;

		// A remote client simulates the launch itself; trust its movement until it lands so
		// the server's correction doesn't yank it back. Same approach as the grappling hook.
		if (player != nullptr && !IsLocalInstigator())
		{
			if (UFGCharacterMovementComponent* movement = player->GetFGMovementComponent())
			{
				movement->bIgnoreClientMovementErrorChecksAndCorrection = true;
				movement->bServerAcceptClientAuthoritativePosition = true;
			}
		}

		if (bNoFallDamage && player != nullptr && overrideProperty != nullptr)
		{
			// Another piece of equipment (e.g. Blade Runners) may already have its own curve set.
			// Remember it so it comes back after landing, but don't remember our own curve.
			UCurveFloat* current = Cast<UCurveFloat>(overrideProperty->GetObjectPropertyValue_InContainer(player));
			if (!bAwaitingLanding || current != NoFallDamageCurve)
			{
				PreviousFallDamageOverride = current;
			}

			if (NoFallDamageCurve == nullptr)
			{
				NoFallDamageCurve = NewObject<UCurveFloat>(this, TEXT("PogoNoFallDamage"), RF_Transient);
				NoFallDamageCurve->FloatCurve.AddKey(0.0f, 0.0f);
				NoFallDamageCurve->FloatCurve.AddKey(100000.0f, 0.0f);
			}
			player->SetFallDamageOverride(NoFallDamageCurve);
		}

		bAwaitingLanding = true;
		return;
	}

	if (!bAwaitingLanding)
	{
		return;
	}
	bAwaitingLanding = false;

	if (player == nullptr)
	{
		PreviousFallDamageOverride = nullptr;
		return;
	}

	if (!IsLocalInstigator())
	{
		if (UFGCharacterMovementComponent* movement = player->GetFGMovementComponent())
		{
			movement->bIgnoreClientMovementErrorChecksAndCorrection = false;
			movement->bServerAcceptClientAuthoritativePosition = false;
		}
	}

	// Only restore if our curve is still the active one; if something else replaced it
	// meanwhile, that newer override wins.
	if (overrideProperty != nullptr && NoFallDamageCurve != nullptr
		&& overrideProperty->GetObjectPropertyValue_InContainer(player) == NoFallDamageCurve)
	{
		player->SetFallDamageOverride(PreviousFallDamageOverride);
	}
	PreviousFallDamageOverride = nullptr;
}

void APogoStickEquipment::SetLandedBound(bool bBound)
{
	AFGCharacterPlayer* player = GetInstigatorCharacter();
	if (player == nullptr || bLandedBound == bBound)
	{
		return;
	}

	if (bBound)
	{
		player->LandedDelegate.AddUniqueDynamic(this, &APogoStickEquipment::OnOwnerLanded);
	}
	else
	{
		player->LandedDelegate.RemoveDynamic(this, &APogoStickEquipment::OnOwnerLanded);
	}
	bLandedBound = bBound;
}

void APogoStickEquipment::TickBounceLocal(const FPogoStickSettings& Settings)
{
	const AFGCharacterPlayer* player = GetInstigatorCharacter();
	const UFGCharacterMovementComponent* movement = player != nullptr ? player->GetFGMovementComponent() : nullptr;
	if (movement != nullptr && movement->IsFalling())
	{
		LastFallVelocity = movement->Velocity;
	}

	const float minSpeed = Settings.MinBounceSpeed * 100.0f;
	if (Settings.bBounceOnLanding == bSentBounceEnabled && FMath::IsNearlyEqual(minSpeed, SentMinBounceSpeed))
	{
		return;
	}
	bSentBounceEnabled = Settings.bBounceOnLanding;
	SentMinBounceSpeed = minSpeed;

	if (HasAuthority())
	{
		bServerBounceEnabled = bSentBounceEnabled;
		ServerMinBounceSpeed = minSpeed;
	}
	else
	{
		Server_SetBounceSettings(bSentBounceEnabled, minSpeed);
	}
}

void APogoStickEquipment::Server_SetBounceSettings_Implementation(bool bEnabled, float MinSpeed)
{
	bServerBounceEnabled = bEnabled;
	ServerMinBounceSpeed = FMath::Max(100.0f, MinSpeed);
}

void APogoStickEquipment::TickBounceServer()
{
	if (!bServerBounceEnabled || bAwaitingLanding)
	{
		return;
	}

	const AFGCharacterPlayer* player = GetInstigatorCharacter();
	const UFGCharacterMovementComponent* movement = player != nullptr ? player->GetFGMovementComponent() : nullptr;
	// Slightly below the client's threshold: the server's view of the fall speed can lag, and
	// protecting a landing that ends up not bouncing only means no fall damage.
	if (movement != nullptr && movement->IsFalling() && -movement->Velocity.Z >= ServerMinBounceSpeed * 0.9f)
	{
		SetAirborneFromLaunch(true, true);
	}
}

void APogoStickEquipment::OnOwnerLanded(const FHitResult& Hit)
{
	const FVector fallVelocity = LastFallVelocity;
	LastFallVelocity = FVector::ZeroVector;

	AFGCharacterPlayer* player = GetInstigatorCharacter();
	if (player == nullptr || !IsLocalInstigator())
	{
		return;
	}

	// Holding Primary Fire (charging) or crouching as you land means "stop here". bWantsToCrouch
	// covers crouch being held while the game refuses to actually crouch mid-air.
	const UFGCharacterMovementComponent* movement = player->GetFGMovementComponent();
	const bool bCrouching = player->bIsCrouched || (movement != nullptr && movement->bWantsToCrouch);
	const FPogoStickSettings settings = FPogoStickSettings::Read(this);
	const float fallSpeed = -fallVelocity.Z;
	if (!settings.bBounceOnLanding || bCharging || bCrouching || fallSpeed < settings.MinBounceSpeed * 100.0f)
	{
		return;
	}

	// Like a charged launch, a bounce needs a point of contact: the stick has to be aimed at a
	// surface within push-off range. Looking at the sky as you land means a normal landing.
	FVector unusedDirection;
	if (!FindPushSurface(settings, unusedDirection))
	{
		return;
	}

	const float upSpeed = FMath::Min(fallSpeed * settings.Bounciness, MaxAcceptedLaunchSpeed);
	if (upSpeed <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Keep the horizontal speed so bouncing down a slope carries you along it.
	const FVector bounceVelocity(fallVelocity.X, fallVelocity.Y, upSpeed);
	player->LaunchCharacter(bounceVelocity, true, true);
	PlayLaunchSound();

	if (HasAuthority())
	{
		SetAirborneFromLaunch(true, true);
	}
	else
	{
		Server_Launch(bounceVelocity, true, true);
	}

	UE_LOG(LogPogoStick, Verbose, TEXT("Bounced: fall %.0f cm/s -> up %.0f cm/s"), fallSpeed, upSpeed);
}

void APogoStickEquipment::ShowChargeWidget(bool bShow)
{
	if (!bShow)
	{
		if (ChargeWidget != nullptr)
		{
			ChargeWidget->RemoveFromParent();
			ChargeWidget = nullptr;
		}
		return;
	}

	if (ChargeWidget != nullptr || !IsLocalInstigator())
	{
		return;
	}

	const AFGCharacterPlayer* player = GetInstigatorCharacter();
	AFGPlayerController* controller = player != nullptr ? player->GetFGPlayerController() : nullptr;
	if (controller == nullptr)
	{
		return;
	}

	ChargeWidget = CreateWidget<UPogoChargeWidget>(controller, UPogoChargeWidget::StaticClass());
	if (ChargeWidget != nullptr)
	{
		ChargeWidget->AddToPlayerScreen(10);
		ChargeWidget->SetCharge(0.0f);
	}
}

void APogoStickEquipment::StopChargeSound()
{
	if (ChargeAudio != nullptr)
	{
		ChargeAudio->Stop();
		ChargeAudio = nullptr;
	}
}

void APogoStickEquipment::PlayLaunchSound() const
{
	PlaySound2D(LaunchSound);
	// Played where the player is, like a Jump Pad under their feet. The game's Wwise build only
	// exposes the PostAkEvent* functions; PostEvent is compiled out.
	const AFGCharacterPlayer* player = GetInstigatorCharacter();
	if (JumpPadLaunchSound != nullptr && player != nullptr && IsLocalInstigator())
	{
		UAkGameplayStatics::PostAkEventAtLocation(GetWorld(), JumpPadLaunchSound, player->GetActorLocation(), FRotator::ZeroRotator);
	}
}

void APogoStickEquipment::PlaySound2D(USoundBase* Sound) const
{
	if (Sound != nullptr && IsLocalInstigator())
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}
}
