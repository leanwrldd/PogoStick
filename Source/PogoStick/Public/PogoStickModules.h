#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "Module/GameWorldModule.h"
#include "PogoStickModules.generated.h"

/// Registers the mod configuration and fills in the item and recipe data that depends on
/// other assets.
UCLASS()
class POGOSTICK_API UPogoStickGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	UPogoStickGameInstanceModule();

	virtual void DispatchLifecycleEvent(ELifecyclePhase phase) override;

private:
	/// Everything the item, recipe and milestone CDOs were pointed at at runtime. Native CDOs
	/// aren't traced by GC, so without this the icons get collected and the UI crashes.
	UPROPERTY()
	TArray<TObjectPtr<UObject>> KeepAlive;
};

/// Makes sure saves that unlocked Jump Pads before the mod was installed get the recipe too.
UCLASS()
class POGOSTICK_API UPogoStickWorldModule : public UGameWorldModule
{
	GENERATED_BODY()

public:
	UPogoStickWorldModule();

	virtual void DispatchLifecycleEvent(ELifecyclePhase phase) override;
};
