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

namespace TMDrawable
{
	namespace
	{
		/** Said once per material, so the log names each one without filling up. */
		TSet<FString> Told;

		/** Remembered per effect: an effect's materials don't change while the game runs. */
		TMap<FString, bool> Effects;
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
		const bool bOk = Resource != nullptr && Resource->GetGameThreadShaderMap() != nullptr;
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
