#include "PogoStickItems.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "PogoStick.h"
#include "PogoStickAssets.h"
#include "PogoStickEquipment.h"

namespace
{
// Verified against the shipped FactoryGame pak (UnrealPak -List), 2026-11-01.
constexpr const TCHAR* WorkshopComponentPath = TEXT("/Game/FactoryGame/Buildable/-Shared/WorkBench/BP_WorkshopComponent.BP_WorkshopComponent_C");

struct FIngredientInfo
{
	const TCHAR* Path;
	int32 Amount;
};

const FIngredientInfo Ingredients[] = {
	{ TEXT("/Game/FactoryGame/Resource/Parts/IronRod/Desc_IronRod.Desc_IronRod_C"), 10 },
	{ TEXT("/Game/FactoryGame/Resource/Parts/IronPlate/Desc_IronPlate.Desc_IronPlate_C"), 5 },
	{ TEXT("/Game/FactoryGame/Resource/Parts/IronScrew/Desc_IronScrew.Desc_IronScrew_C"), 25 },
};

// The vanilla Jump Pads milestone (Tier 2); the Pogo Stick unlocks with it.
constexpr const TCHAR* JumpPadsSchematicPath = TEXT("/Game/FactoryGame/Schematics/Progression/Schematic_2-3.Schematic_2-3_C");
} // namespace

UPogoStickDescriptor::UPogoStickDescriptor()
{
	mUseDisplayNameAndDescription = true;
	mDisplayName = FText::FromString(TEXT("Pogo Stick"));
	mDescription = FText::FromString(TEXT(
		"A spring-loaded FICSIT-approved traversal device.\n\n"
		"Hold Primary Fire to charge. When fully charged, it launches you in the opposite direction to where you're looking: look down to go up."));
	mStackSize = EStackSize::SS_ONE;
	// The editor fills this in when a Blueprint item is saved; a native item has to set it
	// itself. Left at 0, every inventory treats the item as having no room ("not enough space").
	mCachedStackSize = 1;
	mCanBeDiscarded = true;
	mRememberPickUp = false;
	mEquipmentClass = APogoStickEquipment::StaticClass();
}

void UPogoStickDescriptor::ApplyRuntimeData(TArray<TObjectPtr<UObject>>& KeepAlive)
{
	UPogoStickDescriptor* cdo = GetMutableDefault<UPogoStickDescriptor>();

	// Let the game derive the stack size from mStackSize as it would for an asset, but never
	// leave it at 0.
	cdo->UpdateCachedStackSize();
	if (cdo->mCachedStackSize <= 0)
	{
		cdo->mCachedStackSize = 1;
	}
	UE_LOG(LogPogoStick, Log, TEXT("Descriptor stack size: %d"), UFGItemDescriptor::GetStackSize(UPogoStickDescriptor::StaticClass()));

	// Fall back to base-game stand-ins until the mod's own art is imported.
	auto loadOr = [](auto* typeTag, const TCHAR* path, const TCHAR* placeholderPath)
	{
		using T = std::remove_pointer_t<decltype(typeTag)>;
		T* loaded = LoadObject<T>(nullptr, path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (loaded == nullptr)
		{
			UE_LOG(LogPogoStick, Warning, TEXT("Missing %s; using placeholder %s."), path, placeholderPath);
			loaded = LoadObject<T>(nullptr, placeholderPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		return loaded;
	};
	cdo->mSmallIcon = loadOr(static_cast<UTexture2D*>(nullptr), PogoStickAssets::SmallIcon, PogoStickAssets::PlaceholderSmallIcon);
	cdo->mPersistentBigIcon = loadOr(static_cast<UTexture2D*>(nullptr), PogoStickAssets::BigIcon, PogoStickAssets::PlaceholderBigIcon);
	cdo->mConveyorMesh = loadOr(static_cast<UStaticMesh*>(nullptr), PogoStickAssets::BodyMesh, PogoStickAssets::PlaceholderRodMesh);
	KeepAlive.Add(cdo->mSmallIcon);
	KeepAlive.Add(cdo->mPersistentBigIcon);
	KeepAlive.Add(cdo->mConveyorMesh);

	UE_LOG(LogPogoStick, Log, TEXT("Descriptor art: small icon %s, big icon %s, conveyor mesh %s"),
		cdo->mSmallIcon != nullptr ? TEXT("ok") : TEXT("MISSING"),
		cdo->mPersistentBigIcon != nullptr ? TEXT("ok") : TEXT("MISSING"),
		cdo->mConveyorMesh != nullptr ? TEXT("ok") : TEXT("MISSING"));
}

UPogoStickRecipe::UPogoStickRecipe()
{
	mDisplayNameOverride = false;
	mProduct.Add(FItemAmount(UPogoStickDescriptor::StaticClass(), 1));
	mManufactoringDuration = 4.0f;
	mManualManufacturingMultiplier = 1.0f;
	mProducedIn.Add(TSoftClassPtr<UObject>(FSoftObjectPath(WorkshopComponentPath)));
}

void UPogoStickRecipe::ApplyRuntimeData(TArray<TObjectPtr<UObject>>& KeepAlive)
{
	UPogoStickRecipe* cdo = GetMutableDefault<UPogoStickRecipe>();
	if (!cdo->mIngredients.IsEmpty())
	{
		UE_LOG(LogPogoStick, Log, TEXT("Recipe ingredients already set; leaving them alone."));
		return;
	}

	for (const FIngredientInfo& ingredient : Ingredients)
	{
		UClass* itemClass = StaticLoadClass(UFGItemDescriptor::StaticClass(), nullptr, ingredient.Path);
		if (itemClass == nullptr)
		{
			UE_LOG(LogPogoStick, Error, TEXT("Could not load ingredient %s; the recipe will be missing it."), ingredient.Path);
			continue;
		}
		cdo->mIngredients.Add(FItemAmount(itemClass, ingredient.Amount));
		KeepAlive.Add(itemClass);
	}
	UE_LOG(LogPogoStick, Log, TEXT("Recipe has %d ingredient(s)."), cdo->mIngredients.Num());
}

TSubclassOf<UFGSchematic> PogoStickUnlock::LoadJumpPadsMilestone()
{
	return StaticLoadClass(UFGSchematic::StaticClass(), nullptr, JumpPadsSchematicPath);
}

void PogoStickUnlock::AddToJumpPadsMilestone(TArray<TObjectPtr<UObject>>& KeepAlive)
{
	UClass* jumpPads = LoadJumpPadsMilestone();
	if (jumpPads == nullptr)
	{
		UE_LOG(LogPogoStick, Error, TEXT("Could not load %s; the Pogo Stick recipe won't be unlockable."), JumpPadsSchematicPath);
		return;
	}
	UFGSchematic* cdo = jumpPads->GetDefaultObject<UFGSchematic>();
	KeepAlive.Add(jumpPads);

	// mUnlocks is protected on UFGSchematic and this isn't our subclass, so go through reflection.
	FArrayProperty* unlocksProperty = FindFProperty<FArrayProperty>(UFGSchematic::StaticClass(), TEXT("mUnlocks"));
	FObjectProperty* innerProperty = unlocksProperty != nullptr ? CastField<FObjectProperty>(unlocksProperty->Inner) : nullptr;
	if (innerProperty == nullptr)
	{
		UE_LOG(LogPogoStick, Error, TEXT("UFGSchematic::mUnlocks not found; the Pogo Stick recipe won't be unlockable."));
		return;
	}

	FScriptArrayHelper unlocks(unlocksProperty, unlocksProperty->ContainerPtrToValuePtr<void>(cdo));
	for (int32 index = 0; index < unlocks.Num(); ++index)
	{
		if (Cast<UPogoStickUnlockRecipe>(innerProperty->GetObjectPropertyValue(unlocks.GetRawPtr(index))) != nullptr)
		{
			UE_LOG(LogPogoStick, Log, TEXT("Jump Pads milestone already unlocks the Pogo Stick."));
			return;
		}
	}

	UPogoStickUnlockRecipe* unlock = NewObject<UPogoStickUnlockRecipe>(cdo, TEXT("UnlockPogoStickRecipe"));
	const int32 newIndex = unlocks.AddValue();
	innerProperty->SetObjectPropertyValue(unlocks.GetRawPtr(newIndex), unlock);
	KeepAlive.Add(unlock);

	UE_LOG(LogPogoStick, Log, TEXT("Added the Pogo Stick recipe to %s (now %d unlocks)."), *jumpPads->GetName(), unlocks.Num());
}

UPogoStickUnlockRecipe::UPogoStickUnlockRecipe()
{
	mRecipes.Add(UPogoStickRecipe::StaticClass());
}
