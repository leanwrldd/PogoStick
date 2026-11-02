#pragma once

#include "CoreMinimal.h"
#include "FGRecipe.h"
#include "FGSchematic.h"
#include "Resources/FGEquipmentDescriptor.h"
#include "Unlocks/FGUnlockRecipe.h"
#include "PogoStickItems.generated.h"

// The item and its recipe are native classes rather than
// Blueprint assets. Everything that doesn't need another asset is set in the constructors;
// anything that does (icons, base-game ingredients) is filled in on the class default object
// by ApplyRuntimeData(), once the game's content is loadable.
//
// Native CDOs are created during startup and sit in the GC's disregard set, so the garbage
// collector never traces what they reference. Every object ApplyRuntimeData() points a CDO at
// goes into KeepAlive, which the game instance module holds as a UPROPERTY; without that the
// icons are collected seconds into play and the UI crashes drawing them.

UCLASS()
class POGOSTICK_API UPogoStickDescriptor : public UFGEquipmentDescriptor
{
	GENERATED_BODY()

public:
	UPogoStickDescriptor();

	static void ApplyRuntimeData(TArray<TObjectPtr<UObject>>& KeepAlive);
};

UCLASS()
class POGOSTICK_API UPogoStickRecipe : public UFGRecipe
{
	GENERATED_BODY()

public:
	UPogoStickRecipe();

	static void ApplyRuntimeData(TArray<TObjectPtr<UObject>>& KeepAlive);
};

/// Unlocks the Pogo Stick recipe. Added to the vanilla Jump Pads milestone's unlocks, so the
/// Pogo Stick comes with Jump Pads rather than as a milestone of its own.
///
/// UFGUnlockRecipe is abstract (the game only uses Blueprint subclasses of it), so
/// NewObject/CreateDefaultSubobject can't make one. Its AddRecipe()/Init() are editor-only, so
/// the recipe is set on the protected mRecipes from this subclass's constructor instead.
UCLASS(EditInlineNew, DefaultToInstanced)
class POGOSTICK_API UPogoStickUnlockRecipe : public UFGUnlockRecipe
{
	GENERATED_BODY()

public:
	UPogoStickUnlockRecipe();
};

namespace PogoStickUnlock
{
/// The vanilla Jump Pads milestone (Tier 2), which the Pogo Stick unlocks with.
POGOSTICK_API TSubclassOf<UFGSchematic> LoadJumpPadsMilestone();

/// Appends a UPogoStickUnlockRecipe to the Jump Pads milestone's unlocks, on its class default
/// object, so purchasing (or re-applying it on load) also unlocks the Pogo Stick recipe.
POGOSTICK_API void AddToJumpPadsMilestone(TArray<TObjectPtr<UObject>>& KeepAlive);
}
