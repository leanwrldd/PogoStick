#include "PogoStickModules.h"

#include "Engine/World.h"
#include "FGRecipeManager.h"
#include "FGSchematicManager.h"
#include "PogoStick.h"
#include "PogoStickConfiguration.h"
#include "PogoStickItems.h"

UPogoStickGameInstanceModule::UPogoStickGameInstanceModule()
{
	// Without this SML never instantiates a native module, and silently so.
	bRootModule = true;

	ModConfigurations.Add(UPogoStickConfiguration::StaticClass());
}

void UPogoStickGameInstanceModule::DispatchLifecycleEvent(ELifecyclePhase phase)
{
	Super::DispatchLifecycleEvent(phase);

	// INITIALIZATION rather than POST_INITIALIZATION so the data is in place before other mods
	// (ContentLib) inspect registered content.
	if (phase == ELifecyclePhase::INITIALIZATION)
	{
		UE_LOG(LogPogoStick, Log, TEXT("Game instance module loaded."));
		KeepAlive.Reset();
		UPogoStickDescriptor::ApplyRuntimeData(KeepAlive);
		UPogoStickRecipe::ApplyRuntimeData(KeepAlive);
		PogoStickUnlock::AddToJumpPadsMilestone(KeepAlive);
		KeepAlive.Remove(nullptr);
		UE_LOG(LogPogoStick, Log, TEXT("Holding %d runtime-loaded object(s) against GC."), KeepAlive.Num());
	}
}

UPogoStickWorldModule::UPogoStickWorldModule()
{
	bRootModule = true;
}

void UPogoStickWorldModule::DispatchLifecycleEvent(ELifecyclePhase phase)
{
	Super::DispatchLifecycleEvent(phase);

	if (phase != ELifecyclePhase::POST_INITIALIZATION)
	{
		return;
	}

	UWorld* world = GetWorld();
	if (world == nullptr || world->GetNetMode() == NM_Client)
	{
		return;
	}

	// A save that bought Jump Pads before this mod was installed never ran our unlock. The game
	// re-applies purchased unlocks on load, which should cover it; this checks, and logs which
	// path applied, so a test log shows whether the fallback was ever needed.
	AFGSchematicManager* schematicManager = AFGSchematicManager::Get(world);
	AFGRecipeManager* recipeManager = AFGRecipeManager::Get(world);
	const TSubclassOf<UFGSchematic> jumpPads = PogoStickUnlock::LoadJumpPadsMilestone();
	if (schematicManager == nullptr || recipeManager == nullptr || jumpPads == nullptr)
	{
		UE_LOG(LogPogoStick, Warning, TEXT("Couldn't check the Pogo Stick unlock (schematics %d, recipes %d, jump pads %d)."),
			schematicManager != nullptr, recipeManager != nullptr, jumpPads != nullptr);
		return;
	}

	if (!schematicManager->IsSchematicPurchased(jumpPads))
	{
		UE_LOG(LogPogoStick, Log, TEXT("Jump Pads not unlocked yet; the Pogo Stick comes with it."));
	}
	else if (recipeManager->IsRecipeAvailable(UPogoStickRecipe::StaticClass()))
	{
		UE_LOG(LogPogoStick, Log, TEXT("Jump Pads unlocked and the Pogo Stick recipe is available."));
	}
	else
	{
		recipeManager->AddAvailableRecipe(UPogoStickRecipe::StaticClass());
		UE_LOG(LogPogoStick, Log, TEXT("Jump Pads was already unlocked; granted the Pogo Stick recipe."));
	}
}
