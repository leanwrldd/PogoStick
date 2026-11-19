#include "PogoStick.h"

#include "PogoStickPickup.h"

DEFINE_LOG_CATEGORY(LogPogoStick)

void FPogoStickModule::StartupModule()
{
	// Hooks only patch the shipped game; in the editor there's nothing to hook.
	if constexpr (!WITH_EDITOR)
	{
		PogoStickPickup::InstallHooks();
	}
}

IMPLEMENT_MODULE(FPogoStickModule, PogoStick)
