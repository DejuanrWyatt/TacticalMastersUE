// See TMDrawable.h: nothing of ours is drawn with a material cooked without shaders.

#include "TMDrawable.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MaterialShared.h"
#include "RHI.h"
#include "Particles/Material/ParticleModuleMeshMaterial.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleSystem.h"
#include "Particles/TypeData/ParticleModuleTypeDataMesh.h"
#include "UObject/ObjectKey.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace TMDrawable
{
	namespace
	{
		/** Said once per material, so the log names each one without filling up. */
		TSet<FString> Told;

		/** Remembered per effect: an effect's materials don't change while the game runs. */
		TMap<FString, bool> Effects;

		/** Materials already looked at by KeepBrokenLoaded, so each is checked once. */
		TSet<FObjectKey> Checked;

		/** Whether KeepBrokenLoaded runs before each garbage collection yet. */
		bool bWatching = false;
	}

	bool MaterialUsable(const UMaterialInterface* Material, const UWorld* World)
	{
		// In the editor shaders compile as they are wanted, so one not there yet
		// is only late; only a packaged game is missing them for good.
		if (!Material || !FPlatformProperties::RequiresCookedData())
		{
			return true;
		}
		// The platform the game is drawing for (SM6 on DirectX 12, SM5 on 11).
		(void)World;
		const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
		bool bOk = Resource != nullptr && Resource->GetGameThreadShaderMap() != nullptr;
		// 2026-10-02: v16 crashed the same way (SetShaderParameters, the base pass) in
		// the first battle with Narbash bodies on the field, and this check had never
		// caught anything: at run time these four still report a shader map. They are
		// the ones the cook leaves without shaders (package.log, "Shadermap pointer is
		// null"), so they are refused by name, along with every instance of them.
		static const TCHAR* const Broken[] = {
			TEXT("M_Narbash_Legs_Drumsticks"), TEXT("M_Bug_Mesh"), TEXT("M_Bug_ParticleSubUV_Trans"), TEXT("M_Rock_To_Throw")};
		if (bOk)
		{
			const UMaterial* Base = const_cast<UMaterialInterface*>(Material)->GetMaterial();
			const FString BaseName = Base ? Base->GetName() : FString();
			for (const TCHAR* Name : Broken)
			{
				if (BaseName == Name || Material->GetName() == Name)
				{
					bOk = false;
					break;
				}
			}
		}
		if (!bOk)
		{
			const FString Name = Material->GetPathName();
			if (!Told.Contains(Name))
			{
				Told.Add(Name);
				UE_LOG(LogTemp, Warning, TEXT("material cooked without shaders, not drawn: %s"), *Name);
			}
		}
		return bOk;
	}

	bool EffectUsable(const UParticleSystem* System, const UWorld* World)
	{
		if (!System)
		{
			return true;
		}
		const FString Key = System->GetPathName();
		if (const bool* Known = Effects.Find(Key))
		{
			return *Known;
		}
		bool bOk = true;
		for (const UParticleEmitter* Emitter : System->Emitters)
		{
			if (!Emitter)
			{
				continue;
			}
			for (const UParticleLODLevel* Level : Emitter->LODLevels)
			{
				if (!Level)
				{
					continue;
				}
				if (Level->RequiredModule && !MaterialUsable(Level->RequiredModule->Material, World))
				{
					bOk = false;
				}
				// A mesh emitter draws the mesh's own materials, unless told otherwise.
				if (const UParticleModuleTypeDataMesh* Meshes = Cast<UParticleModuleTypeDataMesh>(Level->TypeDataModule))
				{
					if (Meshes->Mesh)
					{
						for (const FStaticMaterial& Slot : Meshes->Mesh->GetStaticMaterials())
						{
							bOk = MaterialUsable(Slot.MaterialInterface, World) && bOk;
						}
					}
				}
				for (const UParticleModule* Module : Level->Modules)
				{
					if (const UParticleModuleMeshMaterial* Swap = Cast<UParticleModuleMeshMaterial>(Module))
					{
						for (const UMaterialInterface* Each : Swap->MeshMaterials)
						{
							bOk = MaterialUsable(Each, World) && bOk;
						}
					}
				}
			}
		}
		if (!bOk)
		{
			UE_LOG(LogTemp, Warning, TEXT("effect not played, it holds a material cooked without shaders: %s"), *Key);
		}
		Effects.Add(Key, bOk);
		return bOk;
	}

	void KeepBrokenLoaded()
	{
		if (!FPlatformProperties::RequiresCookedData() || !IsInGameThread())
		{
			return;
		}
		for (TObjectIterator<UMaterialInterface> It; It; ++It)
		{
			UMaterialInterface* Material = *It;
			// Only whole, live materials: one still being read is looked at next time.
			if (!IsValid(Material) || Material->IsUnreachable() || Material->IsRooted()
				|| Material->HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad | RF_NeedPostLoad | RF_BeginDestroyed))
			{
				continue;
			}
			const FObjectKey Key(Material);
			if (Checked.Contains(Key))
			{
				continue;
			}
			Checked.Add(Key);
			if (!MaterialUsable(Material, nullptr))
			{
				Material->AddToRoot();
				UE_LOG(LogTemp, Log, TEXT("material cooked without shaders kept loaded for good: %s"), *Material->GetPathName());
			}
		}
	}

	void WatchBrokenMaterials()
	{
		if (bWatching)
		{
			return;
		}
		bWatching = true;
		FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddStatic(&KeepBrokenLoaded);
		KeepBrokenLoaded();
	}

	void MendBody(USkeletalMeshComponent* Body)
	{
		if (!Body)
		{
			return;
		}
		const UWorld* World = Body->GetWorld();
		for (int32 Slot = 0; Slot < Body->GetNumMaterials(); ++Slot)
		{
			if (!MaterialUsable(Body->GetMaterial(Slot), World))
			{
				Body->SetMaterial(Slot, UMaterial::GetDefaultMaterial(MD_Surface));
			}
		}
	}
}
