// Loading in the background, and the bar that says how far along it is.
//
// A Paragon hero is a big read: its mesh brings its textures, and its set of
// clips is dozens of files. Read on the game thread one hero at a time, four
// new ones -- a random team, say -- froze the screen for ten seconds and more
// (Aurora's mesh alone took 4.7 s in an editor build). Here Unreal's own
// loader reads them all at once off the game thread, the units stand in the
// level's plain mesh meanwhile, and the battle's clock waits for them.
//
// The bar also counts what Unreal does after a read: shaders compiled for a
// material seen for the first time (a build run from the editor compiles them
// on demand; a packaged game brings them compiled) and graphics pipelines
// prepared ahead of first use.
//
// Presentation only: nothing here touches the rules. The clock waiting while
// heroes load is time not passing, as a pause is.

#include "TMBattleDirector.h"
#include "TMDrawable.h"

#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "PipelineStateCache.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
	/** Every asset path an animation set names, however deep. */
	void PathsIn(const TSharedPtr<FJsonValue>& Value, TArray<FSoftObjectPath>& Into)
	{
		if (!Value.IsValid())
		{
			return;
		}
		switch (Value->Type)
		{
		case EJson::String:
			if (Value->AsString().StartsWith(TEXT("/")))
			{
				Into.AddUnique(FSoftObjectPath(Value->AsString()));
			}
			break;
		case EJson::Array:
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				PathsIn(Item, Into);
			}
			break;
		case EJson::Object:
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Value->AsObject()->Values)
			{
				PathsIn(Field.Value, Into);
			}
			break;
		default:
			break;
		}
	}

	/** Only a game someone is watching loads in the background: the editor and the tests read as before. */
	bool LoadsInBackground(const UWorld* World)
	{
		return World && World->IsGameWorld() && !FApp::IsUnattended();
	}
}

void ATMBattleDirector::WearBody(USkeletalMeshComponent* Visual, const FTMBody& Body, bool bNoCloth) const
{
	// No cloth simulation (2026-10-01): every render-thread crash in the
	// play tests was in drawing skinned meshes, and the heroes' cloth being made
	// and torn down each time a battle is rebuilt is the likeliest part of it.
	// Capes and robes are drawn skinned, moving with the body. (Before, cloth was
	// simulated unless the set said it flies apart, FTMAnimSet::bCloth.) Set
	// before the mesh, so no cloth is made for it at all.
	//
	// No morph targets either (2026-10-01): picking Berserker crashed every time,
	// in the engine's morph buffers (FMorphVertexBufferPool::GetReadingIndex,
	// "Index == 1") just after Grux's mesh, which carries morph targets, was put
	// on. Units never drive morphs, so they are off for every body.
	(void)bNoCloth;
	Visual->bDisableClothSimulation = true;
	Visual->bDisableMorphTarget = true;
	Visual->ClearMorphTargets();
	// No material curves either (2026-10-02): v17 crashed twice in the base pass
	// (RHISetShaderParameters, a freed uniform buffer) while drawing Sparrow with
	// MID_M_ArrowString3 and MID_M_Sparrow_Torso_Arms -- dynamic materials none of
	// our code makes. The hero's animations carry material curves, and the engine
	// makes a dynamic material for every slot they touch and changes it each
	// frame. Units never need that, so curves are not evaluated on their bodies,
	// and no material from the body worn before is carried onto this one.
	Visual->SetAllowAnimCurveEvaluation(false);
	Visual->EmptyOverrideMaterials();
	Visual->SetSkeletalMeshAsset(Body.Mesh);
	// A slot whose material was cooked without shaders (Narbash's legs) is
	// drawn with the default surface instead (TMDrawable.h).
	TMDrawable::MendBody(Visual);
	Visual->SetRelativeScale3D(FVector(Body.Scale * UnitSize));
}

bool ATMBattleDirector::IsBodyLoaded(const FTMBody& Body) const
{
	return Body.Mesh && Body.Animations;
}

void ATMBattleDirector::UnreadPaths(const TArray<const FTMBody*>& Wanted, TArray<FSoftObjectPath>& Paths) const
{
	for (const FTMBody* Body : Wanted)
	{
		if (!Body || (Body->Mesh && Body->Animations))
		{
			continue;
		}
		Paths.AddUnique(FSoftObjectPath(Body->MeshPath));
		if (const TSharedPtr<FJsonObject>* Set = SetSources.Find(Body->SetName))
		{
			PathsIn(MakeShared<FJsonValueObject>(*Set), Paths);
		}
	}
	Paths.RemoveAll([](const FSoftObjectPath& Path) { return !Path.IsValid() || Path.ResolveObject() != nullptr; });
}

void ATMBattleDirector::SoundAssetPaths(TArray<FSoftObjectPath>& Paths) const
{
	FString Text;
	TSharedPtr<FJsonObject> Root;
	const FString File = FPaths::ProjectContentDir() / TEXT("Data/Sounds/sounds.json");
	if (FFileHelper::LoadFileToString(Text, *File) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())
	{
		PathsIn(MakeShared<FJsonValueObject>(Root), Paths);
	}
}

bool ATMBattleDirector::LoadBodiesInBackground()
{
	if (!LoadsInBackground(GetWorld()))
	{
		return false;
	}
	TArray<const FTMBody*> Wanted;
	for (const TMSim::FUnit& Unit : Battle.Units)
	{
		Wanted.Add(BodyFor(Unit));
	}
	TArray<FSoftObjectPath> Paths;
	UnreadPaths(Wanted, Paths);
	// The effects and sounds too, so nothing is read from disk mid-battle --
	// Cast Studio's published looks among them.
	LoadCastLooks();
	LookAssetPaths(Paths);
	SoundAssetPaths(Paths);
	Paths.RemoveAll([](const FSoftObjectPath& Path) { return !Path.IsValid() || Path.ResolveObject() != nullptr; });
	bool bAnyUnworn = false;
	for (const FTMBody* Body : Wanted)
	{
		bAnyUnworn |= Body && !IsBodyLoaded(*Body);
	}
	if (!bAnyUnworn && Paths.Num() == 0)
	{
		return false;
	}
	if (BodyLoad.IsValid())
	{
		BodyLoad->CancelHandle();
		BodyLoad.Reset();
	}
	if (Paths.Num() == 0)
	{
		// Read already (rolled ahead, say), only not yet worn: nothing to wait for.
		DressUnits();
		return false;
	}
	const double Began = FPlatformTime::Seconds();
	BodyLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
		FStreamableDelegate::CreateWeakLambda(this, [this, Began, Count = Paths.Num()]()
		{
			UE_LOG(LogTemp, Log, TEXT("heroes loaded in the background: %d files in %.1f s"), Count, FPlatformTime::Seconds() - Began);
			// Held, not let go: the old set goes only now, once the new one is in.
			BodyKept = BodyLoad;
			BodyLoad.Reset();
			TMDrawable::KeepBrokenLoaded();
			DressUnits();
		}),
		FStreamableManager::AsyncLoadHighPriority);
	return BodyLoad.IsValid();
}

void ATMBattleDirector::DressUnits()
{
	// All read by now, so wearing them is only finding them.
	for (int32 i = 0; i < UnitVisuals.Num() && i < static_cast<int32>(Battle.Units.size()); ++i)
	{
		const FTMBody* Body = BodyFor(Battle.Units[i]);
		if (UnitVisuals[i] && Body && MeshOf(*Body))
		{
			WearBody(UnitVisuals[i], *Body);
		}
	}
	ResetMotion();
	RefreshVisuals();
}

void ATMBattleDirector::RollNextRandomTeam(int32 Team)
{
	// class_list.gd:62-77, sensible_team: a class for each wanted role, preferring
	// one not already picked so the team is not four of a kind. The dice here are
	// the menu's own, nothing to do with a battle.
	static FRandomStream Pick(static_cast<int32>(FDateTime::UtcNow().GetTicks() & 0x7FFFFFFF));
	const char* Wanted[4] = { "tank", "damage", "damage", "support" };
	std::vector<std::string>& Chosen = NextRandom[Team];
	Chosen.clear();
	for (const char* WantedRole : Wanted)
	{
		std::vector<std::string> Fresh;
		std::vector<std::string> Any;
		for (const TMSim::FJobDef* Job : TMSim::AllJobs())
		{
			if (!TMSim::JobHasRole(Job->Id, WantedRole))
			{
				continue;
			}
			Any.push_back(Job->Id);
			if (std::find(Chosen.begin(), Chosen.end(), Job->Id) == Chosen.end())
			{
				Fresh.push_back(Job->Id);
			}
		}
		const std::vector<std::string>& From = !Fresh.empty() ? Fresh : Any;
		if (From.empty())
		{
			Chosen.push_back(TMSim::AllJobs().front()->Id);
			continue;
		}
		Chosen.push_back(From[Pick.RandRange(0, static_cast<int32>(From.size()) - 1)]);
	}

	// Its heroes read now, at a low priority, while the setup screen is being
	// looked at: when Random is pressed they are already in.
	if (AheadLoad[Team].IsValid())
	{
		AheadLoad[Team]->CancelHandle();
		AheadLoad[Team].Reset();
	}
	if (!LoadsInBackground(GetWorld()) || !LoadCharacterMap())
	{
		return;
	}
	TArray<const FTMBody*> Worn;
	for (const std::string& JobId : Chosen)
	{
		Worn.Add(BodyForJob(JobId));
	}
	TArray<FSoftObjectPath> Paths;
	UnreadPaths(Worn, Paths);
	if (Paths.Num() > 0)
	{
		AheadLoad[Team] = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths, FStreamableDelegate(),
			FStreamableManager::DefaultAsyncLoadPriority);
	}
}

bool ATMBattleDirector::LoadingProgress(FString& What, float& Fraction) const
{
	if (BodyLoad.IsValid() && BodyLoad->IsLoadingInProgress())
	{
		int32 Loaded = 0;
		int32 Requested = 0;
		BodyLoad->GetLoadedCount(Loaded, Requested);
		What = FString::Printf(TEXT("Loading heroes  %d / %d"), Loaded, Requested);
		Fraction = BodyLoad->GetProgress();
		return true;
	}
#if WITH_EDITOR
	// A build run from the editor compiles a material's shaders the first time
	// it is drawn. They are kept afterwards, so this is slow once per machine.
	const int32 Shaders = GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
	if (Shaders > 0)
	{
		ShaderJobsPeak = FMath::Max(ShaderJobsPeak, Shaders);
		What = FString::Printf(TEXT("Preparing shaders  %d left"), Shaders);
		Fraction = 1.0f - static_cast<float>(Shaders) / static_cast<float>(ShaderJobsPeak);
		return true;
	}
	ShaderJobsPeak = 0;
#endif
	const int32 Pipelines = static_cast<int32>(PipelineStateCache::NumActivePrecacheRequests());
	if (Pipelines > 0)
	{
		PipelinesPeak = FMath::Max(PipelinesPeak, Pipelines);
		What = FString::Printf(TEXT("Preparing graphics  %d left"), Pipelines);
		Fraction = 1.0f - static_cast<float>(Pipelines) / static_cast<float>(PipelinesPeak);
		return true;
	}
	PipelinesPeak = 0;
	return false;
}
