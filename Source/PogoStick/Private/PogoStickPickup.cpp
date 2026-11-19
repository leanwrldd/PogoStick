#include "PogoStickPickup.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FGCharacterPlayer.h"
#include "FGItemPickup_Spawnable.h"
#include "FGUseableInterface.h"
#include "Patching/NativeHookManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "PogoStick.h"
#include "PogoStickItems.h"
#include "TimerManager.h"

namespace
{
// The game's "Interact" trace channel (DefaultEngine.ini, ECC_GameTraceChannel13).
constexpr ECollisionChannel InteractChannel = ECC_GameTraceChannel13;

const TCHAR* ResponseName(ECollisionResponse Response)
{
	switch (Response)
	{
	case ECR_Ignore: return TEXT("Ignore");
	case ECR_Overlap: return TEXT("Overlap");
	case ECR_Block: return TEXT("Block");
	default: return TEXT("?");
	}
}

bool IsPogoStickPickup(const AFGItemPickup_Spawnable* Pickup)
{
	return Pickup != nullptr && Pickup->GetPickupItemClass() == UPogoStickDescriptor::StaticClass();
}

UStaticMeshComponent* FindPickupMesh(AFGItemPickup_Spawnable* Pickup)
{
	// mMeshComponent is protected; it's the pickup's only static mesh component.
	return Pickup->FindComponentByClass<UStaticMeshComponent>();
}

void LogPickupCollision(AFGItemPickup_Spawnable* Pickup, const TCHAR* When)
{
	UStaticMeshComponent* mesh = FindPickupMesh(Pickup);
	if (mesh == nullptr)
	{
		UE_LOG(LogPogoStick, Warning, TEXT("Pickup %s (%s): no static mesh component."), *Pickup->GetName(), When);
		return;
	}
	const UStaticMesh* staticMesh = mesh->GetStaticMesh();
	const UBodySetup* bodySetup = staticMesh != nullptr ? staticMesh->GetBodySetup() : nullptr;
	UE_LOG(LogPogoStick, Log,
		TEXT("Pickup %s (%s): class %s, mesh %s, collision %d, profile %s, object type %d, Visibility %s, Interact %s, ")
		TEXT("simple shapes %d, trace flag %d, physics state %d, hidden %d, extent %s"),
		*Pickup->GetName(), When, *Pickup->GetClass()->GetName(),
		staticMesh != nullptr ? *staticMesh->GetName() : TEXT("none"),
		static_cast<int32>(mesh->GetCollisionEnabled()), *mesh->GetCollisionProfileName().ToString(),
		static_cast<int32>(mesh->GetCollisionObjectType()),
		ResponseName(mesh->GetCollisionResponseToChannel(ECC_Visibility)),
		ResponseName(mesh->GetCollisionResponseToChannel(InteractChannel)),
		bodySetup != nullptr ? bodySetup->AggGeom.GetElementCount() : -1,
		bodySetup != nullptr ? static_cast<int32>(bodySetup->CollisionTraceFlag.GetValue()) : -1,
		mesh->IsPhysicsStateCreated(), mesh->bHiddenInGame,
		*mesh->Bounds.BoxExtent.ToString());
}

/// A query-only box over the dropped stick that the look-at trace can hit whatever the mesh's own
/// collision does. It only answers traces (Visibility and Interact), so nothing collides with it.
void AddPickupHitBox(AFGItemPickup_Spawnable* Pickup)
{
	UStaticMeshComponent* mesh = FindPickupMesh(Pickup);
	if (mesh == nullptr || mesh->GetStaticMesh() == nullptr || Pickup->FindComponentByClass<UBoxComponent>() != nullptr)
	{
		return;
	}
	const FBox localBounds = mesh->GetStaticMesh()->GetBoundingBox();
	UBoxComponent* box = NewObject<UBoxComponent>(Pickup, TEXT("PogoStickPickupHitBox"));
	box->SetupAttachment(mesh);
	box->SetRelativeLocation(localBounds.GetCenter());
	box->SetBoxExtent(localBounds.GetExtent().ComponentMax(FVector(8.0f)));
	box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	box->SetCollisionObjectType(ECC_WorldDynamic);
	box->SetCollisionResponseToAllChannels(ECR_Ignore);
	box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	box->SetCollisionResponseToChannel(InteractChannel, ECR_Block);
	box->SetGenerateOverlapEvents(false);
	box->SetHiddenInGame(true);
	box->RegisterComponent();
	UE_LOG(LogPogoStick, Log, TEXT("Pickup %s: added hit box, extent %s"), *Pickup->GetName(), *box->GetUnscaledBoxExtent().ToString());
}
} // namespace

void PogoStickPickup::InstallHooks()
{
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGItemPickup_Spawnable::BeginPlay, GetMutableDefault<AFGItemPickup_Spawnable>(),
		[](AFGItemPickup_Spawnable* self)
		{
			if (!IsPogoStickPickup(self) || self->GetWorld() == nullptr)
			{
				return;
			}
			LogPickupCollision(self, TEXT("BeginPlay"));
			// Next tick too: the Blueprint may set the mesh up after BeginPlay.
			TWeakObjectPtr<AFGItemPickup_Spawnable> weak(self);
			self->GetWorldTimerManager().SetTimerForNextTick([weak]()
			{
				if (AFGItemPickup_Spawnable* pickup = weak.Get())
				{
					LogPickupCollision(pickup, TEXT("next tick"));
					AddPickupHitBox(pickup);
				}
			});
		});

	// Shows whether the look-at trace ever reaches the dropped stick, and what it decides.
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGItemPickup_Spawnable::UpdateUseState_Implementation, GetMutableDefault<AFGItemPickup_Spawnable>(),
		[](AFGItemPickup_Spawnable* self, AFGCharacterPlayer* byCharacter, const FVector& atLocation, UPrimitiveComponent* componentHit, FUseState& out_useState)
		{
			static TWeakObjectPtr<AFGItemPickup_Spawnable> lastLogged;
			if (!IsPogoStickPickup(self) || lastLogged.Get() == self)
			{
				return;
			}
			lastLogged = self;
			const TSubclassOf<UFGUseState> state = out_useState.GetState();
			UE_LOG(LogPogoStick, Log, TEXT("Pickup %s looked at: hit %s, state %s, usable %d"),
				*self->GetName(), componentHit != nullptr ? *componentHit->GetName() : TEXT("none"),
				state != nullptr ? *state->GetName() : TEXT("none"), UFGUseState::CanUseInState(state));
		});

	UE_LOG(LogPogoStick, Log, TEXT("Pickup hooks installed."));
}
