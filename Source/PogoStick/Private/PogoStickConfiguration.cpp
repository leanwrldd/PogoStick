#include "PogoStickConfiguration.h"

#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/WidgetExtension/CP_Bool.h"
#include "Configuration/Properties/WidgetExtension/CP_Float.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "PogoStick.h"

namespace
{
// The bare native UConfigProperty* classes register and save fine but render no widget in
// the mod settings menu; the widget logic only exists on SML's Blueprint subclasses.
UClass* LoadConfigPropertyBlueprintClass(const TCHAR* path, UClass* nativeFallback)
{
	UClass* loaded = StaticLoadClass(UConfigProperty::StaticClass(), nullptr, path);
	if (loaded == nullptr)
	{
		UE_LOG(LogPogoStick, Error, TEXT("Could not load %s; falling back to the native class, which won't render any widget in the mod settings menu."), path);
		return nativeFallback;
	}
	return loaded;
}

UClass* GetConfigPropertyFloatClass()
{
	static UClass* cachedClass = LoadConfigPropertyBlueprintClass(
		TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyFloat.BP_ConfigPropertyFloat_C"),
		UConfigPropertyFloat::StaticClass());
	return cachedClass;
}

UClass* GetConfigPropertySectionClass()
{
	static UClass* cachedClass = LoadConfigPropertyBlueprintClass(
		TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertySection.BP_ConfigPropertySection_C"),
		UConfigPropertySection::StaticClass());
	return cachedClass;
}

UClass* GetConfigPropertyBoolClass()
{
	static UClass* cachedClass = LoadConfigPropertyBlueprintClass(
		TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyBool.BP_ConfigPropertyBool_C"),
		UConfigPropertyBool::StaticClass());
	return cachedClass;
}

// MinValue/MaxValue default to 0 on UCP_Float, which marks every nonzero value out of range,
// so each field sets its own range.
void AddFloatProperty(UConfigPropertySection* section, FName name, const TCHAR* displayName, const TCHAR* tooltip,
	float defaultValue, float minValue, float maxValue)
{
	UCP_Float* property = NewObject<UCP_Float>(section, GetConfigPropertyFloatClass(), name);
	property->DisplayName = FText::FromString(displayName);
	property->Tooltip = FText::FromString(tooltip);
	property->DefaultValue = defaultValue;
	property->Value = defaultValue;
	property->WidgetType = ECP_FloatWidgetType::CPF_Spinbox;
	property->MinValue = minValue;
	property->MaxValue = maxValue;
	// Defaults to true on the Blueprint property classes, which locks editing to the main menu.
	property->bRequiresWorldReload = false;
	section->SectionProperties.Add(name.ToString(), property);
}

void AddBoolProperty(UConfigPropertySection* section, FName name, const TCHAR* displayName, const TCHAR* tooltip, bool defaultValue)
{
	UCP_Bool* property = NewObject<UCP_Bool>(section, GetConfigPropertyBoolClass(), name);
	property->DisplayName = FText::FromString(displayName);
	property->Tooltip = FText::FromString(tooltip);
	property->DefaultValue = defaultValue;
	property->Value = defaultValue;
	property->bRequiresWorldReload = false;
	section->SectionProperties.Add(name.ToString(), property);
}

float ReadFloat(const UConfigPropertySection* root, const TCHAR* name, float fallback)
{
	const UConfigPropertyFloat* property = Cast<UConfigPropertyFloat>(root->SectionProperties.FindRef(name));
	return property != nullptr ? property->Value : fallback;
}

bool ReadBool(const UConfigPropertySection* root, const TCHAR* name, bool fallback)
{
	const UConfigPropertyBool* property = Cast<UConfigPropertyBool>(root->SectionProperties.FindRef(name));
	return property != nullptr ? property->Value : fallback;
}
} // namespace

UPogoStickConfiguration::UPogoStickConfiguration(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ConfigId.ModReference = POGOSTICK_MOD_REFERENCE;
	DisplayName = FText::FromString(TEXT("Pogo Stick"));
	Description = FText::FromString(TEXT("Tune how the Pogo Stick charges and launches. Changes apply immediately."));

	UCP_Section* root = NewObject<UCP_Section>(this, GetConfigPropertySectionClass(), TEXT("RootSection"));
	root->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
	RootSection = root;

	const FPogoStickSettings defaults;
	AddFloatProperty(root, TEXT("ChargeTime"), TEXT("Charge Time (seconds)"),
		TEXT("How long Primary Fire must be held before the Pogo Stick is fully charged."),
		defaults.ChargeTime, 0.1f, 10.0f);
	AddFloatProperty(root, TEXT("LaunchSpeed"), TEXT("Launch Strength (m/s)"),
		TEXT("How fast a full charge launches you. You go the opposite way to where you're looking, so look down to go up."),
		defaults.LaunchSpeed, 1.0f, 200.0f);
	AddFloatProperty(root, TEXT("MaxSurfaceDistance"), TEXT("Push-Off Range (m)"),
		TEXT("The Pogo Stick pushes off the surface you're looking at, so it only launches when a floor, wall or ceiling is within this distance of your eyes. Standing up, the floor is about 1.7 m away."),
		defaults.MaxSurfaceDistance, 0.5f, 20.0f);
	AddFloatProperty(root, TEXT("Cooldown"), TEXT("Cooldown (seconds)"),
		TEXT("How long after a launch before you can start charging again."),
		defaults.Cooldown, 0.0f, 10.0f);
	AddBoolProperty(root, TEXT("KeepMomentum"), TEXT("Keep Momentum"),
		TEXT("If enabled, the launch is added to your current speed instead of replacing it, so chained bounces build up speed."),
		defaults.bKeepMomentum);
	AddBoolProperty(root, TEXT("AutoLaunch"), TEXT("Launch When Fully Charged"),
		TEXT("If enabled, you launch the moment the charge completes. If disabled, a full charge is held until you let go, so you can aim first. Letting go early always cancels."),
		defaults.bAutoLaunch);
	AddBoolProperty(root, TEXT("NoFallDamage"), TEXT("No Fall Damage After Launch"),
		TEXT("If enabled, you take no fall damage between a launch and your next landing."),
		defaults.bNoFallDamage);
	AddFloatProperty(root, TEXT("SpringCompression"), TEXT("Spring Squash (visual)"),
		TEXT("How much the spring visibly squashes at full charge. 0 = not at all, 0.9 = almost flat."),
		defaults.SpringCompression, 0.0f, 0.9f);
	AddBoolProperty(root, TEXT("BounceOnLanding"), TEXT("Bounce On Landing"),
		TEXT("If enabled, landing hard with the Pogo Stick equipped bounces you back up instead of hurting you, as long as you're looking at the ground (within Push-Off Range) as you land. Hold Primary Fire or crouch as you land to stop bouncing."),
		defaults.bBounceOnLanding);
	AddFloatProperty(root, TEXT("Bounciness"), TEXT("Bounciness"),
		TEXT("How much of your landing speed a bounce gives back. 0.8 = each bounce is a bit lower than the last, 1 = bounce forever, above 1 = each bounce goes higher."),
		defaults.Bounciness, 0.0f, 1.5f);
	AddFloatProperty(root, TEXT("MinBounceSpeed"), TEXT("Minimum Bounce Speed (m/s)"),
		TEXT("Landings slower than this don't bounce, so small drops and steps feel normal."),
		defaults.MinBounceSpeed, 1.0f, 100.0f);
	AddBoolProperty(root, TEXT("ShowPushPoint"), TEXT("Show Push-Off Point"),
		TEXT("If enabled, a marker shows where the Pogo Stick would push off: green within Push-Off Range, red beyond it."),
		defaults.bShowPushPoint);
	AddBoolProperty(root, TEXT("ShowPushDistance"), TEXT("Show Push-Off Distance"),
		TEXT("If enabled, the distance to the push-off point is shown next to its marker. Has no effect while Show Push-Off Point is disabled."),
		defaults.bShowPushDistance);

	// Hold position: tuned live in game while looking at the stick, then baked in as defaults.
	UCP_Section* hold = NewObject<UCP_Section>(root, GetConfigPropertySectionClass(), TEXT("Hold"));
	hold->DisplayName = FText::FromString(TEXT("Hold Position (advanced)"));
	hold->HasHeader = true;
	hold->HeaderText = FText::FromString(TEXT("Hold Position (advanced)"));
	hold->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
	root->SectionProperties.Add(TEXT("Hold"), hold);

	AddFloatProperty(hold, TEXT("Scale"), TEXT("Size"),
		TEXT("Scales the whole Pogo Stick around your hand, so the grip stays put. Smaller keeps the lower end out of the ground. 1 = full size. Changes show immediately while it's equipped."),
		defaults.HoldScale, 0.2f, 2.0f);
	const TCHAR* offsetTooltip = TEXT("Moves the Pogo Stick relative to your right hand, in cm. Changes show immediately while it's equipped.");
	const TCHAR* rotationTooltip = TEXT("Rotates the Pogo Stick relative to your right hand, in degrees. Changes show immediately while it's equipped.");
	AddFloatProperty(hold, TEXT("OffsetX"), TEXT("Offset X (cm)"), offsetTooltip, defaults.HoldOffset.X, -100.0f, 100.0f);
	AddFloatProperty(hold, TEXT("OffsetY"), TEXT("Offset Y (cm)"), offsetTooltip, defaults.HoldOffset.Y, -100.0f, 100.0f);
	AddFloatProperty(hold, TEXT("OffsetZ"), TEXT("Offset Z (cm)"), offsetTooltip, defaults.HoldOffset.Z, -100.0f, 100.0f);
	AddFloatProperty(hold, TEXT("Pitch"), TEXT("Pitch (deg)"), rotationTooltip, defaults.HoldRotation.Pitch, -180.0f, 180.0f);
	AddFloatProperty(hold, TEXT("Yaw"), TEXT("Yaw (deg)"), rotationTooltip, defaults.HoldRotation.Yaw, -180.0f, 180.0f);
	AddFloatProperty(hold, TEXT("Roll"), TEXT("Roll (deg)"), rotationTooltip, defaults.HoldRotation.Roll, -180.0f, 180.0f);
}

FPogoStickSettings FPogoStickSettings::Read(const UObject* WorldContext)
{
	FPogoStickSettings settings;

	const UWorld* world = WorldContext != nullptr ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* gameInstance = world != nullptr ? world->GetGameInstance() : nullptr;
	const UConfigManager* configManager = gameInstance != nullptr ? gameInstance->GetSubsystem<UConfigManager>() : nullptr;
	if (configManager == nullptr)
	{
		return settings;
	}

	FConfigId configId;
	configId.ModReference = POGOSTICK_MOD_REFERENCE;
	const UConfigPropertySection* root = configManager->GetConfigurationRootSection(configId);
	if (root == nullptr)
	{
		return settings;
	}

	settings.ChargeTime = FMath::Max(0.1f, ReadFloat(root, TEXT("ChargeTime"), settings.ChargeTime));
	settings.LaunchSpeed = FMath::Max(0.0f, ReadFloat(root, TEXT("LaunchSpeed"), settings.LaunchSpeed));
	settings.MaxSurfaceDistance = FMath::Max(0.5f, ReadFloat(root, TEXT("MaxSurfaceDistance"), settings.MaxSurfaceDistance));
	settings.Cooldown = FMath::Max(0.0f, ReadFloat(root, TEXT("Cooldown"), settings.Cooldown));
	settings.bKeepMomentum = ReadBool(root, TEXT("KeepMomentum"), settings.bKeepMomentum);
	settings.bAutoLaunch = ReadBool(root, TEXT("AutoLaunch"), settings.bAutoLaunch);
	settings.bNoFallDamage = ReadBool(root, TEXT("NoFallDamage"), settings.bNoFallDamage);
	settings.SpringCompression = FMath::Clamp(ReadFloat(root, TEXT("SpringCompression"), settings.SpringCompression), 0.0f, 0.9f);
	settings.bBounceOnLanding = ReadBool(root, TEXT("BounceOnLanding"), settings.bBounceOnLanding);
	settings.Bounciness = FMath::Clamp(ReadFloat(root, TEXT("Bounciness"), settings.Bounciness), 0.0f, 1.5f);
	settings.MinBounceSpeed = FMath::Max(1.0f, ReadFloat(root, TEXT("MinBounceSpeed"), settings.MinBounceSpeed));
	settings.bShowPushPoint = ReadBool(root, TEXT("ShowPushPoint"), settings.bShowPushPoint);
	settings.bShowPushDistance = ReadBool(root, TEXT("ShowPushDistance"), settings.bShowPushDistance);

	if (const UConfigPropertySection* hold = Cast<UConfigPropertySection>(root->SectionProperties.FindRef(TEXT("Hold"))))
	{
		settings.HoldOffset = FVector(
			ReadFloat(hold, TEXT("OffsetX"), settings.HoldOffset.X),
			ReadFloat(hold, TEXT("OffsetY"), settings.HoldOffset.Y),
			ReadFloat(hold, TEXT("OffsetZ"), settings.HoldOffset.Z));
		settings.HoldRotation = FRotator(
			ReadFloat(hold, TEXT("Pitch"), settings.HoldRotation.Pitch),
			ReadFloat(hold, TEXT("Yaw"), settings.HoldRotation.Yaw),
			ReadFloat(hold, TEXT("Roll"), settings.HoldRotation.Roll));
		settings.HoldScale = FMath::Clamp(ReadFloat(hold, TEXT("Scale"), settings.HoldScale), 0.2f, 2.0f);
	}
	return settings;
}
