// Whether a material, a body or a particle effect can be drawn in this build
// (2026-10-01).
//
// Some of the Fab content was cooked with materials that failed to compile:
// textures missing from the pack (Narbash's drumstick legs, Morigesh's bugs,
// Rampage's rock), cooked anyway with no shaders at all ("Shadermap pointer
// is null" in package.log). The engine is meant to draw its default material
// in their place, but a renderer handed one of them now and then reads freed
// memory on the render thread -- the SetShaderParameters crashes from the
// play tests, DX11 and DX12 alike. So nothing of ours is drawn with one: a
// body's slot gets the default surface material, and an effect that holds one
// is not played at all.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UParticleSystem;
class USkeletalMeshComponent;
class UWorld;

namespace TMDrawable
{
	/** Whether the material has its shaders here; a missing material is the engine's default, so fine. */
	bool MaterialUsable(const UMaterialInterface* Material, const UWorld* World);

	/** Whether every material a Cascade effect draws with has its shaders. Worked out once per effect. */
	bool EffectUsable(const UParticleSystem* System, const UWorld* World);

	/** Gives each of the body's slots whose material can't be drawn the default surface material. */
	void MendBody(USkeletalMeshComponent* Body);

	/**
	 * Keeps every material in memory that can't be drawn here, for the rest of
	 * the run (2026-10-04). Unloading one is what crashed v13-v19: the garbage
	 * collector freed M_Bug_Mesh (read in with Morigesh's clips when the heroes
	 * load), and the next frame the renderer read the freed memory
	 * (SetShaderParameters, reading 0xffffffffffffffff). Every crash of the v19
	 * play test was that, about two minutes into an idle menu or in a battle,
	 * and "gc.CollectGarbageEveryFrame 1" made it happen at once. Kept, they are
	 * never unloaded. Cheap: a material is looked at once.
	 */
	void KeepBrokenLoaded();

	/** Runs KeepBrokenLoaded before every garbage collection from now on. Once per run, however often called. */
	void WatchBrokenMaterials();
}
