#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPogoStick, Log, All)

/// Mod reference. Must match the plugin folder, the .uplugin filename and the module name.
#define POGOSTICK_MOD_REFERENCE TEXT("PogoStick")

class FPogoStickModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};
