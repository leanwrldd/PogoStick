#pragma once

#include "CoreMinimal.h"

/// Paths of the art assets this mod loads at runtime. They're loaded by path rather than
/// through Blueprint assets, so a missing one only logs a warning instead of breaking the mod.
namespace PogoStickAssets
{
inline const TCHAR* BodyMesh = TEXT("/PogoStick/Meshes/SM_PogoStick_Body.SM_PogoStick_Body");
inline const TCHAR* SpringMesh = TEXT("/PogoStick/Meshes/SM_PogoStick_Spring.SM_PogoStick_Spring");
inline const TCHAR* FootMesh = TEXT("/PogoStick/Meshes/SM_PogoStick_Foot.SM_PogoStick_Foot");

/// Where the right-hand grip sits on SM_PogoStick_Body, relative to its origin (the centre of
/// the handlebar), in cm. From the art notes; the rubber runs Y 10 to 21.5.
inline const FVector RightGripOffset(0.0f, 15.75f, 0.0f);

inline const TCHAR* SmallIcon = TEXT("/PogoStick/Icons/IconPogoStick_64.IconPogoStick_64");
inline const TCHAR* BigIcon = TEXT("/PogoStick/Icons/IconPogoStick_256.IconPogoStick_256");

// Base-game stand-ins, used until the mod's own art is imported. Found with UnrealPak -List.
inline const TCHAR* PlaceholderRodMesh = TEXT("/Game/FactoryGame/Resource/Parts/IronRod/Mesh/SM_IronRod_01.SM_IronRod_01");
inline const TCHAR* PlaceholderSpringMesh = TEXT("/Game/FactoryGame/Resource/Parts/SteelPipe/SM_SteelPipe_01.SM_SteelPipe_01");
inline const TCHAR* PlaceholderSmallIcon = TEXT("/Game/FactoryGame/Resource/Equipment/NailGun/UI/IconDesc_RebarGun_64.IconDesc_RebarGun_64");
inline const TCHAR* PlaceholderBigIcon = TEXT("/Game/FactoryGame/Resource/Equipment/NailGun/UI/IconDesc_RebarGun_256.IconDesc_RebarGun_256");

// The Jump Pad's launch sound, played on every launch and bounce. A Wwise event, like all the
// game's sounds. Found with UnrealPak -List.
inline const TCHAR* JumpPadLaunchSound = TEXT("/Game/WwiseAudio/Events/Buildable/Factory/Traversal/Play_F_Launch_JumpPad.Play_F_Launch_JumpPad");

inline const TCHAR* ChargeSound = TEXT("/PogoStick/Sounds/S_Pogo_Charge.S_Pogo_Charge");
inline const TCHAR* LaunchSound = TEXT("/PogoStick/Sounds/S_Pogo_Launch.S_Pogo_Launch");
inline const TCHAR* CancelSound = TEXT("/PogoStick/Sounds/S_Pogo_Cancel.S_Pogo_Cancel");
}
