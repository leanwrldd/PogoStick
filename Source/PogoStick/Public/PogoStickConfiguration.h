#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "PogoStickConfiguration.generated.h"

/// The in-game mod settings (Mods > Pogo Stick). Built entirely in C++, no Blueprint asset.
/// Every value is read at the moment it is used, so edits take effect without a restart.
UCLASS()
class POGOSTICK_API UPogoStickConfiguration : public UModConfiguration
{
	GENERATED_BODY()

public:
	UPogoStickConfiguration(const FObjectInitializer& ObjectInitializer);
};

/// A snapshot of the current settings, with the defaults used if the config can't be read.
struct POGOSTICK_API FPogoStickSettings
{
	/// Seconds Primary Fire must be held before the stick is fully charged.
	float ChargeTime = 2.0f;
	/// Launch speed in metres per second.
	float LaunchSpeed = 25.0f;
	/// How close (m) the surface you're looking at must be to push off it.
	float MaxSurfaceDistance = 3.5f;
	/// Seconds after a launch before the stick can be charged again.
	float Cooldown = 0.5f;
	/// Adds the launch to the player's current velocity instead of replacing it.
	bool bKeepMomentum = false;
	/// Launch the moment charging completes. If false, a full charge is held until release.
	bool bAutoLaunch = true;
	/// No fall damage between a launch and the next landing.
	bool bNoFallDamage = true;
	/// How far the spring visibly squashes at full charge, 0 to 0.9 of its length.
	float SpringCompression = 0.5f;
	/// Bounce back up when landing hard while the stick is equipped.
	bool bBounceOnLanding = true;
	/// Fraction of the landing speed kept by a bounce. Below 1, bounces die down.
	float Bounciness = 0.8f;
	/// Landings slower than this (m/s) don't bounce.
	float MinBounceSpeed = 12.0f;
	/// HUD marker on the point the push-off check hits.
	bool bShowPushPoint = true;
	/// The distance readout next to that marker.
	bool bShowPushDistance = true;
	/// Where the stick sits relative to the hand socket (cm, socket axes), and its rotation.
	/// Y +22.5 tuned in game (2026-11-01) with the Rebar Gun arm pose, still used in first person.
	FVector HoldOffset = FVector(0.0f, 22.5f, 0.0f);
	FRotator HoldRotation = FRotator::ZeroRotator;
	/// Uniform scale around the grip. Below 1 the lower end sits higher, so it clips the ground less.
	float HoldScale = 0.7f;

	static FPogoStickSettings Read(const UObject* WorldContext);
};
