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

#include "SimAI.h"
#include "SimBattle.h"
#include "SimOrder.h"

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

	/** Sends one of a unit's four abilities at a spot, or says why it cannot. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	FString OrderAbility(int32 UnitId, int32 Slot, float MetresX, float MetresY, int32 FollowId = -1);

	/**
	 * The same, aimed at a unit rather than at a map reference. This is what a
	 * click on somebody amounts to: the spell is aimed at where they are standing
	 * and follows them if it takes a while to arrive.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	FString OrderAbilityAt(int32 UnitId, int32 Slot, int32 TargetUnitId);

	/** What has happened lately, newest last: the fight as a person would read it. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	FString BattleLog() const;

	/** How many lines of it to keep. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters")
	int32 LogLines = 40;

	// The computer player. It is for practising against and for testing with,
	// and it plays by clicking: it hands over an order and the order is checked
	// and applied exactly as one arriving from a person would be. Set both sides
	// to the computer to watch a battle play itself, or neither for two people.

	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Computer")
	bool bComputerPlaysTeam0 = false;

	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Computer")
	bool bComputerPlaysTeam1 = true;

	/** "easy", "medium" or "hard". Hard never settles for a worse option. */
	UPROPERTY(EditAnywhere, Category = "Tactical Masters|Computer")
	FString ComputerSkill = TEXT("hard");

	/** One order from the computer for this unit. Empty if it was accepted. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	FString TakeComputerTurn(int32 UnitId);

	/**
	 * Lets the computer play every side it is set to play until nobody is
	 * waiting on it. Returns how many orders it gave. The cap is there so a
	 * mistake cannot spin the editor.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tactical Masters")
	int32 PlayComputerTurns(int32 MaxOrders = 64);

private:

	/**
	 * The one door every order comes through, whoever gave it: a person at this
	 * machine, the computer player, or -- when there is one -- a message off the
	 * network. Refusing is the same refusal for all three, which is the point.
	 */
	FString Submit(const TMSim::FOrder& Order);

	/** Whether the computer is the one playing this side. */
	bool ComputerPlays(int32 Team) const;

	/**
	 * Turns what the rules just reported into something readable. The events are
	 * presentation only -- nothing in the rules reads them back -- so this is free
	 * to say it however it likes, and free to be wrong without breaking a battle.
	 */
	void Narrate(const TMSim::FTickReport& Report);

	/** How a unit is referred to in the log. */
	FString NameOf(int32 UnitId) const;

	UPROPERTY()
	TArray<FString> Log;

	/** The unit the battle is currently waiting on, or nullptr. */
	const TMSim::FUnit* WaitingOn() const;

	/** The rules. Plain C++, and deliberately unaware of everything above. */
	TMSim::FBattle Battle;

	/** Not part of the rules, and holds nothing the rules need. */
	TMSim::FAIPlayer Computer;

	/** Seconds still to wait before the computer gives its next order. */
	float ThinkRemainder = 0.0f;
	/** Who the last order was for, so a new turn gets the longer pause. */
	int32 ThinkingAbout = -1;

	UPROPERTY()
	TArray<TObjectPtr<USkeletalMeshComponent>> UnitVisuals;

	/** Left over from the last frame, so the clock runs at its own rate. */
	float TickRemainder = 0.0f;

	bool bBuilt = false;
};
