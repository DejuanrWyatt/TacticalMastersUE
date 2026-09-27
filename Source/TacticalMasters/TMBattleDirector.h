// Puts the ported rules on screen.
//
// The rules live in TMSim and know nothing about Unreal -- no actors, no
// meshes, no world. This is the piece in the middle: it owns a battle, steps
// its clock, and keeps one visible unit in the level for each unit in the
// simulation. Nothing here decides anything about the game; it reads what the
// simulation says and shows it.
//
// The controls are CallInEditor so a battle can be built and stepped without
// entering Play, which is how it is checked from outside the editor.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SimBattle.h"

#include "TMBattleDirector.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UPointLightComponent;

UCLASS()
class TACTICALMASTERS_API ATMBattleDirector : public AActor
{
	GENERATED_BODY()

public:
	ATMBattleDirector();

	/** The mesh every unit wears until classes have their own. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters")
	TSoftObjectPtr<USkeletalMesh> UnitMesh;

	/** Metres to Unreal units. One tile is one metre in the rules. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters")
	float TileSize = 100.0f;

	/** How high the top of a tile sits, so units stand on the board. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters")
	float BoardHeight = 20.0f;

	/** The light under whoever's turn it is. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters")
	FLinearColor ReadyColour = FLinearColor(1.0f, 0.78f, 0.25f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "Tactical Masters")
	float ReadyLightBrightness = 12000.0f;

	/** The mesh each tile of the board is built from. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters")
	TSoftObjectPtr<UStaticMesh> TileMesh;

	/** Builds a battle and a visible unit for each of its units. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	void BuildBattle();

	/** Runs the clock on. Ten of these is a second of battle. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	void StepTicks(int32 Ticks = 10);

	/** What the simulation currently says, as text, for checking against it. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	FString DescribeBattle() const;

	/** Clears the visible units and forgets the battle. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	void ClearBattle();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	/** Moves each visible unit to where the simulation has it. */
	void RefreshVisuals();

	/** Builds the board out of the map the rules are actually using. */
	void BuildBoard();

	/** Metres in the rules to Unreal's centimetres. */
	FVector WorldFromMetres(const TMSim::FVec2& Point, int Level) const;

	UPROPERTY()
	TArray<TObjectPtr<class UStaticMeshComponent>> TileVisuals;

	UPROPERTY()
	TArray<TObjectPtr<class UPointLightComponent>> ReadyLights;

	/** Where a unit standing at this spot on the board belongs in the world. */
	FVector WorldFor(const TMSim::FUnit& Unit) const;

public:
	/** Walks a unit to a spot on the board, in metres. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	FString MoveUnitTo(int32 UnitId, float MetresX, float MetresY, bool bSprint = false);

	/** Ends a unit's turn, as giving no further orders would. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	void EndUnitTurn(int32 UnitId);

	/** Every spot a unit could walk to this turn, for checking and for drawing. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	int32 ReachableCount(int32 UnitId) const;

private:

	/** The rules. Plain C++, and deliberately unaware of everything above. */
	TMSim::FBattle Battle;

	UPROPERTY()
	TArray<TObjectPtr<USkeletalMeshComponent>> UnitVisuals;

	/** Left over from the last frame, so the clock runs at its own rate. */
	float TickRemainder = 0.0f;

	bool bBuilt = false;
};
