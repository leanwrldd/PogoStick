#pragma once

#include "CoreMinimal.h"

/// The dropped Pogo Stick (an AFGItemPickup_Spawnable drawn with the body mesh) showed no
/// pick-up prompt. These hooks log its collision setup, and whether the look-at trace reaches
/// it, and give it a trace-only hit box so the prompt can find it either way.
namespace PogoStickPickup
{
void InstallHooks();
}
