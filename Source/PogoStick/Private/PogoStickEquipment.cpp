#include "PogoStickEquipment.h"

#include "AkAudioEvent.h"
#include "AkGameplayStatics.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/StaticMesh.h"
#include "FGCharacterMovementComponent.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PogoChargeWidget.h"
#include "PogoStick.h"
#include "PogoStickAssets.h"
#include "PogoStickConfiguration.h"
#include "Sound/SoundBase.h"
#include "UObject/UnrealType.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
// The anim instance re-reads mArmAnimation every frame, so the pose can follow the camera mode.
// First person: the Rebar Gun pose keeps the stick in view (the hold offset was tuned with it).
// Third person: AE_RebarGun (and AE_Generic1Hand/2Hand) make the 3P anim graph scale every
// upper-body bone 2x for a native equipment class, ballooning the character; AE_OneHandEquipment
// is clean there but puts the first-person hand, and the stick, out of view.
constexpr EArmEquipment FirstPersonArmAnimation = EArmEquipment::AE_RebarGun;
constexpr EArmEquipment ThirdPersonArmAnimation = EArmEquipment::AE_OneHandEquipment;

// First person: the stick hangs off the arms' ArmBase bone, which is the camera pivot (it pitches
// with the view and both arms hang from it), so it stays in the middle of the view and the hold
// animation's hands stay on its grips. In ArmBase space +X is left, +Y forward, +Z up, in cm.
// Tools/make_charge_anim.py bakes the hands for these same numbers; change both together.
const FName FirstPersonHoldBone = TEXT("ArmBase");
const FVector FirstPersonHandlebarCentre(0.0f, 38.0f, -12.0f);
constexpr float FirstPersonShaftTilt = 20.0f; // degrees, foot forward
const FVector FirstPersonChargePush(0.0f, 6.0f, -5.0f);
/// How fast the pushed-in stick comes back up after a launch or cancel, per second.
constexpr float ChargePushReturnRate = 4.0f;

/// Upper bound the server accepts from a client, in cm/s, so a modified client can't fling
/// itself across the map. Matches the config's 200 m/s maximum.
constexpr float MaxAcceptedLaunchSpeed = 200.0f * 100.0f;

/// Grace period before the server starts checking for a landing. The launch tick itself can
/// still report the player as on the ground.
constexpr float MinAirborneTime = 0.2f;

const TCHAR* NoFallDamageCurveName = TEXT("PogoNoFallDamage");

/// Any Pogo Stick's no-fall-damage curve, not just this stick's: each stick makes its own.
bool IsNoFallDamageCurve(const UObject* Curve)
{
	return Curve != nullptr && Curve->GetFName() == FName(NoFallDamageCurveName);
}

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
	mArmAnimation = IsLocalInstigator() && CameraMode == ECameraMode::ECM_FirstPerson && !IsThirdPersonBodyShown()
		? FirstPersonArmAnimation : ThirdPersonArmAnimation;
}

bool APogoStickEquipment::IsThirdPersonBodyShown() const
{
	// Photo mode shows the third-person body while the camera mode still says first person.
	const AFGCharacterPlayer* player = GetInstigatorCharacter();
	return player != nullptr && player->Get3PMesh() != nullptr && player->Get3PMesh()->IsVisible();
}

void APogoStickEquipment::RegisterFirstPersonMaterials()
{
	UMaterialInterface* parent = LoadOptional<UMaterialInterface>(PogoStickAssets::FirstPersonParentMaterial);
	UTexture* baseColor = LoadOptional<UTexture>(PogoStickAssets::BaseColorTexture);
	UTexture* normal = LoadOptional<UTexture>(PogoStickAssets::NormalTexture);
	UTexture* orm = LoadOptional<UTexture>(PogoStickAssets::OrmTexture);
	if (parent == nullptr || baseColor == nullptr || normal == nullptr || orm == nullptr)
	{
		return;
	}

	UMaterialInstanceDynamic* material = UMaterialInstanceDynamic::Create(parent, this, TEXT("MID_PogoStick_1p"));

	// The parent's parameter names aren't documented, so match them by name and log them all, so
	// a wrong guess shows up in the game log.
	TArray<FMaterialParameterInfo> infos;
	TArray<FGuid> ids;
	parent->GetAllTextureParameterInfo(infos, ids);
	int32 assigned = 0;
	for (const FMaterialParameterInfo& info : infos)
	{
		const FString name = info.Name.ToString();
		UTexture* current = nullptr;
		parent->GetTextureParameterValue(info, current);
		UTexture* replacement = nullptr;
		if (name.Contains(TEXT("Normal")))
		{
			replacement = normal;
		}
		else if (name.Contains(TEXT("Albedo")) || name.Contains(TEXT("BaseColor")) || name.Contains(TEXT("Diffuse"))
			|| name.Contains(TEXT("Color")))
		{
			replacement = baseColor;
		}
		else if (name.Contains(TEXT("ORM")) || name.Contains(TEXT("MRAO")) || name.Contains(TEXT("Roughness"))
			|| name.Contains(TEXT("Metal")) || name.Contains(TEXT("Mask")))
		{
			replacement = orm;
		}
		if (replacement != nullptr)
		{
			material->SetTextureParameterValueByInfo(info, replacement);
			++assigned;
		}
		UE_LOG(LogPogoStick, Log, TEXT("1p material texture '%s' (was %s) -> %s"),
			*name, *GetNameSafe(current), *GetNameSafe(replacement));
	}
	TArray<FMaterialParameterInfo> scalarInfos;
	parent->GetAllScalarParameterInfo(scalarInfos, ids);
	TArray<FMaterialParameterInfo> vectorInfos;
	parent->GetAllVectorParameterInfo(vectorInfos, ids);
	TArray<FString> otherNames;
	for (const FMaterialParameterInfo& info : scalarInfos)
	{
		otherNames.Add(FString::Printf(TEXT("scalar %s"), *info.Name.ToString()));
	}
	for (const FMaterialParameterInfo& info : vectorInfos)
	{
		otherNames.Add(FString::Printf(TEXT("vector %s"), *info.Name.ToString()));
	}
	UE_LOG(LogPogoStick, Log, TEXT("1p material other parameters: %s"), *FString::Join(otherNames, TEXT(", ")));

	if (assigned == 0)
	{
		UE_LOG(LogPogoStick, Warning, TEXT("No texture parameter matched on %s; keeping the plain material in first person."), *parent->GetName());
		return;
	}

	// The game swaps these in while the holder is in first person and back out in third person.
	for (UStaticMeshComponent* part : {BodyMesh.Get(), SpringMesh.Get(), FootMesh.Get()})
	{
		TArray<UMaterialInterface*> materials;
		materials.Init(material, FMath::Max(1, part->GetNumMaterials()));
		AddFirstPersonComponentAndMaterialsEntry(part->GetFName(), materials);
	}
	UE_LOG(LogPogoStick, Log, TEXT("Registered first-person materials (%d texture parameter(s) set)."), assigned);
}

void APogoStickEquipment::UpdateChargeAim(const FPogoStickSettings& Settings, float DeltaSeconds)
{
	// Eases in while charging and back out after the launch or cancel.
	AimBlend = FMath::FInterpConstantTo(AimBlend, bCharging ? 1.0f : 0.0f, DeltaSeconds, 1.0f / AimBlendTime);
	const AFGCharacterPlayer* player = GetInstigatorCharacter();
	if (AimBlend <= 0.0f || player == nullptr)
	{
		return;
	}

	// Aim at the push-off point, or straight along the view when nothing is in sight, using the
	// same longer trace as the HUD marker so the stick and the marker agree.
	FVector launchDirection;
	FHitResult hit;
	FindPushSurface(Settings, launchDirection, &hit, 3.0f);
	FVector eyeLocation;
	FRotator viewRotation;
	player->GetActorEyesViewPoint(eyeLocation, viewRotation);
	const FVector target = hit.bBlockingHit ? FVector(hit.ImpactPoint)
		: eyeLocation + viewRotation.Vector() * Settings.MaxSurfaceDistance * 300.0f;

	// The stick runs down -Z from the right grip at HoldAdjust's origin, with the handlebar along
	// Y. It only turns around the handlebar, so both grips stay in the hands: the charge animation
	// puts the left hand on the left grip.
	const FVector grip = HoldAdjust->GetComponentLocation();
	const FQuat holdRotation = HoldAdjust->GetComponentQuat();
	const FVector handlebar = holdRotation.GetRightVector();
	const FVector aimDirection = FVector::VectorPlaneProject(target - grip, handlebar).GetSafeNormal();
	if (aimDirection.IsNearlyZero())
	{
		return;
	}
	const FVector down = -holdRotation.GetUpVector();
	const float angle = FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(down, aimDirection), handlebar),
		FVector::DotProduct(down, aimDirection));
	const FQuat aimed = FQuat(handlebar, angle) * holdRotation;
	const float alpha = FMath::InterpEaseInOut(0.0f, 1.0f, AimBlend, 2.0f);
	HoldAdjust->SetWorldRotation(FQuat::Slerp(holdRotation, aimed, alpha));
}

void APogoStickEquipment::UpdateHold(const FPogoStickSettings& Settings, float DeltaSeconds)
{
	AFGCharacterPlayer* player = GetInstigatorCharacter();
	USkeletalMeshComponent* mesh = player != nullptr ? player->GetMesh() : nullptr;
	UAnimInstance* anim = mesh != nullptr ? mesh->GetAnimInstance() : nullptr;
	const bool bThirdPersonBody = IsThirdPersonBodyShown();
	const bool bFirstPerson = anim != nullptr && ChargeAnimation != nullptr && IsLocalInstigator()
		&& GetInstigatorCameraMode() == ECameraMode::ECM_FirstPerson && !bThirdPersonBody;
	if (!bFirstPerson)
	{
		ReleaseFirstPersonHold();
		// Third person: the stick hangs from the hand socket, placed by the Hold settings. The parts
		// are laid out with the right grip at HoldAdjust's origin, so scaling shrinks it around the hand.
		FTransform hold(Settings.HoldRotation, Settings.HoldOffset, FVector(Settings.HoldScale));
		if (bThirdPersonBody && GetInstigatorCameraMode() == ECameraMode::ECM_FirstPerson)
		{
			// Photo mode: the game leaves the equipment on the hidden first-person hand, so the
			// stick moves to the same socket on the body, keeping the root's offset from it.
			USkeletalMeshComponent* body = player->Get3PMesh();
			if (HoldAdjust->GetAttachParent() != body)
			{
				HoldAdjust->AttachToComponent(body, FAttachmentTransformRules::KeepRelativeTransform, mAttachSocket);
			}
			hold = hold * GetRootComponent()->GetRelativeTransform();
		}
		if (!HoldAdjust->GetRelativeTransform().Equals(hold))
		{
			HoldAdjust->SetRelativeTransform(hold);
		}
		return;
	}

	// The push follows the charge, eased, and springs back afterwards.
	const float charge = bCharging ? FMath::Clamp(ChargeElapsed / FMath::Max(0.1f, ActiveChargeTime), 0.0f, 1.0f) : 0.0f;
	const float target = FMath::SmoothStep(0.0f, 1.0f, charge);
	ChargePush = bCharging ? target : FMath::FInterpConstantTo(ChargePush, target, DeltaSeconds, ChargePushReturnRate);

	if (HoldAdjust->GetAttachParent() != mesh || HoldAdjust->GetAttachSocketName() != FirstPersonHoldBone)
	{
		HoldAdjust->AttachToComponent(mesh, FAttachmentTransformRules::KeepRelativeTransform, FirstPersonHoldBone);
	}
	// HoldAdjust's origin is the right grip, its +Y runs from the left grip to the right one and the
	// shaft runs down -Z. The right side is -X here.
	const float tilt = FMath::DegreesToRadians(FirstPersonShaftTilt);
	const FVector handlebar(-1.0f, 0.0f, 0.0f);
	const FVector up(0.0f, -FMath::Sin(tilt), FMath::Cos(tilt));
	const FVector rightGrip = FirstPersonHandlebarCentre + FirstPersonChargePush * ChargePush
		+ handlebar * PogoStickAssets::RightGripOffset.Y * Settings.HoldScale;
	const FTransform hold(FRotationMatrix::MakeFromYZ(handlebar, up).ToQuat(), rightGrip, FVector(Settings.HoldScale));
	if (!HoldAdjust->GetRelativeTransform().Equals(hold))
	{
		HoldAdjust->SetRelativeTransform(hold);
	}

	// Both hands on the grips: the hold animation, frozen at the frame matching the push. Played
	// again whenever something else in the slot (an emote, say) has replaced it.
	if (ChargeMontage == nullptr || !anim->Montage_IsPlaying(ChargeMontage))
	{
		ChargeMontage = anim->PlaySlotAnimationAsDynamicMontage(ChargeAnimation, PogoStickAssets::ChargeAnimationSlot,
			HoldBlendTime, HoldBlendTime);
		if (ChargeMontage != nullptr)
		{
			ChargeMontage->bEnableAutoBlendOut = false;
			anim->Montage_SetPlayRate(ChargeMontage, 0.0f);
		}
	}
	if (ChargeMontage != nullptr)
	{
		// Kept short of the very end, which would finish the montage.
		const float length = ChargeMontage->GetPlayLength();
		anim->Montage_SetPosition(ChargeMontage, FMath::Min(ChargePush * length, length - 0.01f));
	}
}

void APogoStickEquipment::ReleaseFirstPersonHold()
{
	const AFGCharacterPlayer* player = GetInstigatorCharacter();
	const bool bOnBody = player != nullptr && HoldAdjust->GetAttachParent() == player->Get3PMesh() && IsThirdPersonBodyShown()
		&& GetInstigatorCameraMode() == ECameraMode::ECM_FirstPerson;
	if (HoldAdjust->GetAttachParent() != Root && !bOnBody)
	{
		HoldAdjust->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
	}
	ChargePush = 0.0f;
	if (ChargeMontage != nullptr)
	{
		UAnimInstance* anim = player != nullptr && player->GetMesh() != nullptr ? player->GetMesh()->GetAnimInstance() : nullptr;
		if (anim != nullptr)
		{
			anim->Montage_Stop(HoldBlendTime, ChargeMontage);
		}
		ChargeMontage = nullptr;
	}
}

void APogoStickEquipment::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseFirstPersonHold();
	StopChargeSound();
	ShowChargeWidget(false);
	SetLandedBound(false);
	bBounceProtected = false;
	SetAirborneFromLaunch(false, false);
	UpdateFallProtection();
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

	if (!bPlaceholders)
	{
		RegisterFirstPersonMaterials();
	}

	ChargeClicks.Reset();
	for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(PogoStickAssets::ChargeClickPositions)); ++i)
	{
		const FString name = FString::Printf(TEXT("%s%02d"), PogoStickAssets::ChargeClickPrefix, i + 1);
		ChargeClicks.Add(LoadOptional<USoundBase>(*FString::Printf(TEXT("%s/%s.%s"), PogoStickAssets::ChargeClickFolder, *name, *name)));
	}
	LaunchSound = LoadOptional<USoundBase>(PogoStickAssets::LaunchSound);
	CancelSound = LoadOptional<USoundBase>(PogoStickAssets::CancelSound);
	JumpPadLaunchSound = LoadOptional<UAkAudioEvent>(PogoStickAssets::JumpPadLaunchSound);
	ChargeAnimation = LoadOptional<UAnimSequence>(PogoStickAssets::ChargeAnimation);

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
	// Kept just above zero: a mesh scaled flat has no usable normals.
	const float squash = FMath::Max(0.02f, 1.0f - ActiveSpringCompression * FMath::Clamp(ChargeAlpha, 0.0f, 1.0f));
	SpringPivot->SetRelativeLocation(PartsOffset + FVector(0.0f, 0.0f, SpringTopZ));
	SpringPivot->SetRelativeScale3D(FVector(1.0f, 1.0f, squash));
	FootPivot->SetRelativeLocation(PartsOffset + FVector(0.0f, 0.0f, SpringTopZ - SpringHeight * squash));
}

void APogoStickEquipment::Equip(AFGCharacterPlayer* Character)
{
	LoadArt();
	Super::Equip(Character);
	LastEquippedCharacter = Character;

	// Safety net: a no-fall-damage curve left behind by another Pogo Stick would otherwise
	// protect the player for good. If one is there while nothing is being protected, remove it.
	FObjectProperty* overrideProperty = GetFallDamageOverrideProperty();
	if (HasAuthority() && Character != nullptr && overrideProperty != nullptr && !bNoFallDamageApplied
		&& IsNoFallDamageCurve(overrideProperty->GetObjectPropertyValue_InContainer(Character)))
	{
		Character->SetFallDamageOverride(nullptr);
	}
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
	ReleaseFirstPersonHold();
	ShowChargeWidget(false);
	SetLandedBound(false);
	bServerBounceEnabled = false;
	bBounceProtected = false;
	UpdateFallProtection();

	// Putting the stick away ends the launch's protection, even mid-air: no stick, no help.
	SetAirborneFromLaunch(false, false);

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
		PlayDueChargeClick();

		const FPogoStickSettings settings = FPogoStickSettings::Read(this);
		FVector launchDirection;
		const bool bSurfaceInRange = FindPushSurface(settings, launchDirection);

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
		UpdateHold(settings, DeltaSeconds);
		UpdateChargeAim(settings, DeltaSeconds);
	}
	if (HasAuthority())
	{
		TickBounceServer();
	}

	TickAwaitingLanding(DeltaSeconds);
}

void APogoStickEquipment::TickAwaitingLanding(float DeltaSeconds)
{
	if (!bAwaitingLanding || !HasAuthority())
	{
		return;
	}

	AirborneTime += DeltaSeconds;
	const AFGCharacterPlayer* player = GetProtectedCharacter();
	const UFGCharacterMovementComponent* movement = player != nullptr ? player->GetFGMovementComponent() : nullptr;
	if (movement == nullptr || (AirborneTime > MinAirborneTime && movement->IsMovingOnGround()))
	{
		SetAirborneFromLaunch(false, false);
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

	StopChargeSound();
	NextChargeClick = 0;
	PlayDueChargeClick();
}

void APogoStickEquipment::CancelCharging()
{
	bCharging = false;
	ChargeElapsed = 0.0f;
	UpdateSpringVisual(0.0f);
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

	if (settings.bShowCrosshairIndicator && CooldownRemaining > 0.0f)
	{
		ChargeWidget->SetCrosshairState(EPogoCrosshairState::Cooldown);
	}
	else if (!settings.bShowCrosshairIndicator)
	{
		ChargeWidget->SetCrosshairState(EPogoCrosshairState::None);
	}
	else if (bInRange)
	{
		ChargeWidget->SetCrosshairState(EPogoCrosshairState::Ready);
	}
	else
	{
		const bool bFullyCharged = bCharging && ChargeElapsed >= ActiveChargeTime;
		ChargeWidget->SetCrosshairState(bFullyCharged ? EPogoCrosshairState::ChargedNoSurface : EPogoCrosshairState::None);
	}
}

void APogoStickEquipment::Launch(const FVector& Direction)
{
	bCharging = false;
	ChargeElapsed = 0.0f;
	UpdateSpringVisual(0.0f);
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

	AFGCharacterPlayer* player = GetProtectedCharacter();

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

		bAwaitingLanding = true;
		bLaunchNoFallDamage = bNoFallDamage;
		UpdateFallProtection();
		return;
	}

	if (!bAwaitingLanding)
	{
		return;
	}
	bAwaitingLanding = false;
	bLaunchNoFallDamage = false;

	if (player != nullptr && !IsLocalInstigator())
	{
		if (UFGCharacterMovementComponent* movement = player->GetFGMovementComponent())
		{
			movement->bIgnoreClientMovementErrorChecksAndCorrection = false;
			movement->bServerAcceptClientAuthoritativePosition = false;
		}
	}
	UpdateFallProtection();
}

void APogoStickEquipment::UpdateFallProtection()
{
	// A charged launch protects until the next landing (the No Fall Damage setting). A bounce
	// protects only while the client says the coming landing would bounce, so cancelling the
	// bounce (crouch or Primary Fire held, or not aiming at the ground) means normal fall damage.
	SetNoFallDamage((bAwaitingLanding && bLaunchNoFallDamage) || bBounceProtected);
}

AFGCharacterPlayer* APogoStickEquipment::GetProtectedCharacter() const
{
	AFGCharacterPlayer* player = GetInstigatorCharacter();
	return player != nullptr ? player : LastEquippedCharacter.Get();
}

void APogoStickEquipment::SetNoFallDamage(bool bEnabled)
{
	if (!HasAuthority() || bEnabled == bNoFallDamageApplied)
	{
		return;
	}
	AFGCharacterPlayer* player = GetProtectedCharacter();
	FObjectProperty* overrideProperty = GetFallDamageOverrideProperty();
	if (player == nullptr || overrideProperty == nullptr)
	{
		bNoFallDamageApplied = false;
		PreviousFallDamageOverride = nullptr;
		return;
	}

	if (bEnabled)
	{
		// Another piece of equipment (e.g. Blade Runners) may already have its own curve set.
		// Remember it so it comes back afterwards, but never another Pogo Stick's curve, or
		// restoring it would leave the player protected.
		UObject* current = overrideProperty->GetObjectPropertyValue_InContainer(player);
		PreviousFallDamageOverride = IsNoFallDamageCurve(current) ? nullptr : Cast<UCurveFloat>(current);
		if (NoFallDamageCurve == nullptr)
		{
			NoFallDamageCurve = NewObject<UCurveFloat>(this, NoFallDamageCurveName, RF_Transient);
			NoFallDamageCurve->FloatCurve.AddKey(0.0f, 0.0f);
			NoFallDamageCurve->FloatCurve.AddKey(100000.0f, 0.0f);
		}
		player->SetFallDamageOverride(NoFallDamageCurve);
		bNoFallDamageApplied = true;
		return;
	}

	// Only restore if our curve is still the active one; if something else replaced it
	// meanwhile, that newer override wins.
	if (overrideProperty->GetObjectPropertyValue_InContainer(player) == NoFallDamageCurve)
	{
		player->SetFallDamageOverride(PreviousFallDamageOverride);
	}
	PreviousFallDamageOverride = nullptr;
	bNoFallDamageApplied = false;
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

	// Whether landing now would bounce, as far as the client can tell before the landing: the
	// same conditions OnOwnerLanded checks, minus the fall speed, which the server measures.
	const float minSpeed = Settings.MinBounceSpeed * 100.0f;
	FVector unusedDirection;
	const bool bWouldBounce = Settings.bBounceOnLanding && !bCharging && !IsCrouchHeld()
		&& FindPushSurface(Settings, unusedDirection);
	if (bWouldBounce == bSentBounceEnabled && FMath::IsNearlyEqual(minSpeed, SentMinBounceSpeed))
	{
		return;
	}
	bSentBounceEnabled = bWouldBounce;
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
	const AFGCharacterPlayer* player = GetInstigatorCharacter();
	const UFGCharacterMovementComponent* movement = player != nullptr ? player->GetFGMovementComponent() : nullptr;
	// Fall damage is decided on the server as the player lands, before a bounce RPC could
	// arrive, so protect any fast fall the client says would bounce. Slightly below the client's
	// threshold: the server's view of the fall speed can lag. Dropped once the player is off the
	// fall, which is after the landing's damage has been decided.
	bBounceProtected = bServerBounceEnabled && movement != nullptr && movement->IsFalling()
		&& -movement->Velocity.Z >= ServerMinBounceSpeed * 0.9f;
	UpdateFallProtection();
}

bool APogoStickEquipment::IsCrouchHeld() const
{
	// bWantsToCrouch covers crouch being held while the game refuses to crouch mid-air.
	const AFGCharacterPlayer* player = GetInstigatorCharacter();
	const UFGCharacterMovementComponent* movement = player != nullptr ? player->GetFGMovementComponent() : nullptr;
	return player != nullptr && (player->bIsCrouched || (movement != nullptr && movement->bWantsToCrouch));
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

	// Holding Primary Fire (charging) or crouching as you land means "stop here", with normal
	// fall damage: the server only protects landings this client reported as bouncing.
	const FPogoStickSettings settings = FPogoStickSettings::Read(this);
	const float fallSpeed = -fallVelocity.Z;
	if (!settings.bBounceOnLanding || bCharging || IsCrouchHeld() || fallSpeed < settings.MinBounceSpeed * 100.0f)
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

	// No launch protection for the bounce's own flight: its landing is protected only if that
	// one would bounce too, so it can be cancelled like any other.
	if (HasAuthority())
	{
		SetAirborneFromLaunch(true, false);
	}
	else
	{
		Server_Launch(bounceVelocity, true, false);
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
	}
}

void APogoStickEquipment::PlayDueChargeClick()
{
	if (!IsLocalInstigator())
	{
		return;
	}
	const float alpha = ChargeElapsed / FMath::Max(0.1f, ActiveChargeTime);
	// On a short charge several clicks fall due in one frame. Play only the latest: stacking
	// them would turn a dozen clicks into one loud bang. The last one, at full charge, always plays.
	int32 due = INDEX_NONE;
	while (NextChargeClick < ChargeClicks.Num() && PogoStickAssets::ChargeClickPositions[NextChargeClick] <= alpha)
	{
		due = NextChargeClick++;
	}
	if (due != INDEX_NONE && ChargeClicks[due] != nullptr)
	{
		ChargeAudio = UGameplayStatics::SpawnSound2D(this, ChargeClicks[due]);
	}
}

void APogoStickEquipment::StopChargeSound()
{
	if (ChargeAudio != nullptr)
	{
		ChargeAudio->FadeOut(ChargeSoundFadeTime, 0.0f);
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
