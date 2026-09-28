#include "TMSettings.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString SettingsFile()
	{
		return FPaths::ProjectSavedDir() / TEXT("TacticalMasters/settings.json");
	}

	/** keybinds.gd:15-38: every action, what it is called, and its keys. */
	const TArray<FTMActionInfo>& Actions()
	{
		static const TArray<FTMActionInfo> List =
		{
			{ TEXT("move"), TEXT("Move"), { EKeys::SpaceBar } },
			{ TEXT("sprint"), TEXT("Sprint (further, no ability)"), { EKeys::LeftShift } },
			{ TEXT("ability_1"), TEXT("Ability 1"), { EKeys::One } },
			{ TEXT("ability_2"), TEXT("Ability 2"), { EKeys::Two } },
			{ TEXT("ability_3"), TEXT("Ability 3"), { EKeys::Three } },
			{ TEXT("ability_4"), TEXT("Ability 4 (Ultimate)"), { EKeys::Four } },
			{ TEXT("end_turn"), TEXT("End turn"), { EKeys::Enter } },
			{ TEXT("cancel"), TEXT("Cancel"), { EKeys::Escape } },
			{ TEXT("next_unit"), TEXT("Next ready unit"), { EKeys::Tab } },
			{ TEXT("pause"), TEXT("Pause"), { EKeys::P } },
			{ TEXT("unit_guide"), TEXT("Unit Guide"), { EKeys::U } },
			{ TEXT("log"), TEXT("Show / hide combat log"), { EKeys::L } },
			{ TEXT("center_camera"), TEXT("Center camera on unit"), { EKeys::C } },
			{ TEXT("cam_forward"), TEXT("Camera forward"), { EKeys::W, EKeys::Up } },
			{ TEXT("cam_back"), TEXT("Camera back"), { EKeys::S, EKeys::Down } },
			{ TEXT("cam_left"), TEXT("Camera left"), { EKeys::A, EKeys::Left } },
			{ TEXT("cam_right"), TEXT("Camera right"), { EKeys::D, EKeys::Right } },
			{ TEXT("cam_up"), TEXT("Camera up"), { EKeys::R } },
			{ TEXT("cam_down"), TEXT("Camera down"), { EKeys::F } },
			{ TEXT("cam_rotate_left"), TEXT("Rotate camera left"), { EKeys::Q } },
			{ TEXT("cam_rotate_right"), TEXT("Rotate camera right"), { EKeys::E } },
			{ TEXT("edit_layout"), TEXT("Edit layout / lock it"), { EKeys::F2 } },
		};
		return List;
	}
}

FTMSettings& FTMSettings::Get()
{
	static FTMSettings Settings;
	return Settings;
}

FTMSettings::FTMSettings()
{
	ResetKeys();
	Load();
}

const FTMActionInfo& FTMSettings::Info(ETMAction Action)
{
	return Actions()[static_cast<int32>(Action)];
}

bool FTMSettings::Is(const FKey& Key, ETMAction Action) const
{
	return Bound[static_cast<int32>(Action)].Contains(Key);
}

FString FTMSettings::KeyName(ETMAction Action) const
{
	const TArray<FKey>& Keys = Bound[static_cast<int32>(Action)];
	return Keys.Num() > 0 ? Keys[0].GetDisplayName(false).ToString() : FString(TEXT("-"));
}

void FTMSettings::Rebind(ETMAction Action, const FKey& Key)
{
	const int32 Mine = static_cast<int32>(Action);
	if (Bound[Mine].Num() > 0 && Bound[Mine][0] == Key)
	{
		return;
	}
	const FKey Old = Bound[Mine].Num() > 0 ? Bound[Mine][0] : FKey();
	// Whoever had it gets this action's old main key, so nothing is left bare.
	for (int32 Other = 0; Other < static_cast<int32>(ETMAction::Count); ++Other)
	{
		if (Other != Mine && Bound[Other].Contains(Key))
		{
			const int32 At = Bound[Other].IndexOfByKey(Key);
			if (Old.IsValid() && !Bound[Other].Contains(Old))
			{
				Bound[Other][At] = Old;
			}
			else
			{
				Bound[Other].RemoveAt(At);
			}
		}
	}
	Bound[Mine].Remove(Key);
	if (Bound[Mine].Num() > 0)
	{
		Bound[Mine][0] = Key;
	}
	else
	{
		Bound[Mine].Add(Key);
	}
	Save();
}

void FTMSettings::ResetKeys()
{
	for (int32 i = 0; i < static_cast<int32>(ETMAction::Count); ++i)
	{
		Bound[i] = Actions()[i].Defaults;
	}
}

void FTMSettings::ResetLayout()
{
	Layout.Reset();
	CardOrder[0].Reset();
	CardOrder[1].Reset();
	Save();
}

void FTMSettings::ResetOptions()
{
	CameraSpeed = 1.0f;
	UiScale = 1.0f;
	bFullscreen = false;
	bColorblind = false;
	bTurnSquares = true;
	ResetKeys();
	Save();
	Apply();
}

FLinearColor FTMSettings::TeamColour(int32 Team) const
{
	// settings.gd:16-17: blue and red, or blue and orange.
	if (Team == 0)
	{
		return FLinearColor(0.25f, 0.5f, 0.9f);
	}
	return bColorblind ? FLinearColor(0.95f, 0.6f, 0.1f) : FLinearColor(0.85f, 0.25f, 0.22f);
}

void FTMSettings::Load()
{
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *SettingsFile())
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		return;
	}
	double Number = 0.0;
	if (Root->TryGetNumberField(TEXT("camera_speed"), Number)) { CameraSpeed = FMath::Clamp(static_cast<float>(Number), 0.5f, 2.0f); }
	if (Root->TryGetNumberField(TEXT("ui_scale"), Number)) { UiScale = FMath::Clamp(static_cast<float>(Number), 0.9f, 1.3f); }
	Root->TryGetBoolField(TEXT("fullscreen"), bFullscreen);
	Root->TryGetBoolField(TEXT("colorblind"), bColorblind);
	Root->TryGetBoolField(TEXT("turn_squares"), bTurnSquares);
	const TSharedPtr<FJsonObject>* Places = nullptr;
	if (Root->TryGetObjectField(TEXT("layout"), Places))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Places)->Values)
		{
			const TArray<TSharedPtr<FJsonValue>>& XY = Entry.Value->AsArray();
			if (XY.Num() == 2)
			{
				Layout.Add(Entry.Key, FVector2D(XY[0]->AsNumber(), XY[1]->AsNumber()));
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Orders = nullptr;
	if (Root->TryGetArrayField(TEXT("turn_card_order"), Orders) && Orders->Num() == 2)
	{
		for (int32 Team = 0; Team < 2; ++Team)
		{
			for (const TSharedPtr<FJsonValue>& Id : (*Orders)[Team]->AsArray())
			{
				CardOrder[Team].Add(static_cast<int32>(Id->AsNumber()));
			}
		}
	}
	const TSharedPtr<FJsonObject>* Keys = nullptr;
	if (Root->TryGetObjectField(TEXT("keys"), Keys))
	{
		for (int32 i = 0; i < static_cast<int32>(ETMAction::Count); ++i)
		{
			const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
			if ((*Keys)->TryGetArrayField(Actions()[i].Id, Names))
			{
				TArray<FKey> Read;
				for (const TSharedPtr<FJsonValue>& Name : *Names)
				{
					const FKey Key(*Name->AsString());
					if (Key.IsValid())
					{
						Read.Add(Key);
					}
				}
				if (Read.Num() > 0)
				{
					Bound[i] = Read;
				}
			}
		}
	}
	const TSharedPtr<FJsonObject>* Rules = nullptr;
	if (Root->TryGetObjectField(TEXT("tuning"), Rules))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Rules)->Values)
		{
			Tuning.Add(Entry.Key, Entry.Value->AsNumber());
		}
	}
}

void FTMSettings::Save() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("camera_speed"), CameraSpeed);
	Root->SetNumberField(TEXT("ui_scale"), UiScale);
	Root->SetBoolField(TEXT("fullscreen"), bFullscreen);
	Root->SetBoolField(TEXT("colorblind"), bColorblind);
	Root->SetBoolField(TEXT("turn_squares"), bTurnSquares);
	TSharedRef<FJsonObject> Places = MakeShared<FJsonObject>();
	for (const TPair<FString, FVector2D>& Entry : Layout)
	{
		Places->SetArrayField(Entry.Key, { MakeShared<FJsonValueNumber>(Entry.Value.X), MakeShared<FJsonValueNumber>(Entry.Value.Y) });
	}
	Root->SetObjectField(TEXT("layout"), Places);
	TArray<TSharedPtr<FJsonValue>> Orders;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		TArray<TSharedPtr<FJsonValue>> Ids;
		for (int32 Id : CardOrder[Team])
		{
			Ids.Add(MakeShared<FJsonValueNumber>(Id));
		}
		Orders.Add(MakeShared<FJsonValueArray>(Ids));
	}
	Root->SetArrayField(TEXT("turn_card_order"), Orders);
	TSharedRef<FJsonObject> Keys = MakeShared<FJsonObject>();
	for (int32 i = 0; i < static_cast<int32>(ETMAction::Count); ++i)
	{
		TArray<TSharedPtr<FJsonValue>> Names;
		for (const FKey& Key : Bound[i])
		{
			Names.Add(MakeShared<FJsonValueString>(Key.GetFName().ToString()));
		}
		Keys->SetArrayField(Actions()[i].Id, Names);
	}
	Root->SetObjectField(TEXT("keys"), Keys);
	TSharedRef<FJsonObject> Rules = MakeShared<FJsonObject>();
	for (const TPair<FString, double>& Entry : Tuning)
	{
		Rules->SetNumberField(Entry.Key, Entry.Value);
	}
	Root->SetObjectField(TEXT("tuning"), Rules);
	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
	FFileHelper::SaveStringToFile(Text, *SettingsFile());
}

void FTMSettings::Apply() const
{
	if (!GEngine || FApp::IsUnattended())
	{
		return;
	}
	if (UGameUserSettings* Display = GEngine->GetGameUserSettings())
	{
		const EWindowMode::Type Want = bFullscreen ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed;
		if (Display->GetFullscreenMode() != Want)
		{
			Display->SetFullscreenMode(Want);
			Display->ApplySettings(false);
		}
	}
}
