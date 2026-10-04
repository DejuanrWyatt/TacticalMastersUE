#include "CastAnimation.h"

#include "SimJson.h"

#include <algorithm>
#include <set>

namespace TMCast
{
	namespace
	{
		/**
		 * An object path as Unreal writes one: "/Game/Pack/Clip.Clip". Anything
		 * else could not name a clip, and loading it would only log a miss.
		 */
		bool IsObjectPath(const std::string& Path)
		{
			if (Path.size() < 4 || Path[0] != '/' || Path.find('.') == std::string::npos)
			{
				return false;
			}
			for (const char C : Path)
			{
				if (C == ' ' || C == '"' || C == '\\' || static_cast<unsigned char>(C) < 32)
				{
					return false;
				}
			}
			return true;
		}

		void ReadPick(const TMSim::FJson& Json, const std::string& Where, bool bRelease, FClipPick& Out,
			std::vector<std::string>& Problems)
		{
			if (!Json.IsObject())
			{
				Problems.push_back(Where + ": should be {\"clip\": ...}; left out");
				return;
			}
			for (const std::pair<std::string, TMSim::FJson>& Member : Json.Object)
			{
				if (Member.first == "clip")
				{
					if (!Member.second.IsString() || !IsObjectPath(Member.second.String))
					{
						Problems.push_back(Where + ".clip: not a clip's object path; left out");
					}
					else
					{
						Out.Clip = Member.second.String;
					}
				}
				else if (Member.first == "contact" && bRelease)
				{
					if (!Member.second.IsNumber() || Member.second.Number < 0.0 || Member.second.Number > 1.0)
					{
						Problems.push_back(Where + ".contact: should be a share of the clip, 0 to 1; left out");
					}
					else
					{
						Out.Contact = Member.second.Number;
					}
				}
				else
				{
					// A newer creator may write more than this game reads: said, not refused.
					Problems.push_back(Where + "." + Member.first + ": not read by this version");
				}
			}
		}
	}

	const FAnimationPicks* FAnimationFile::Find(const std::string& Ability, const std::string& Set) const
	{
		const auto ByAbility = Abilities.find(Ability);
		if (ByAbility == Abilities.end())
		{
			return nullptr;
		}
		const auto BySet = ByAbility->second.find(Set);
		return BySet == ByAbility->second.end() ? nullptr : &BySet->second;
	}

	std::vector<std::string> FAnimationFile::Clips() const
	{
		std::set<std::string> All;
		for (const auto& Ability : Abilities)
		{
			for (const auto& Set : Ability.second)
			{
				for (const FClipPick* Pick : { &Set.second.Windup, &Set.second.Release, &Set.second.Loop, &Set.second.Recover })
				{
					if (!Pick->Clip.empty())
					{
						All.insert(Pick->Clip);
					}
				}
			}
		}
		return std::vector<std::string>(All.begin(), All.end());
	}

	bool ReadAnimationFile(const std::string& Text, FAnimationFile& Out)
	{
		Out = FAnimationFile();
		TMSim::FJson Root;
		const std::string Error = TMSim::ParseJson(Text, Root);
		if (!Error.empty())
		{
			Out.Problems.push_back("not JSON: " + Error);
			return false;
		}
		const TMSim::FJson* Format = Root.IsObject() ? Root.Find("format") : nullptr;
		if (!Format || !Format->IsString() || Format->String != AnimationFormat())
		{
			Out.Problems.push_back(std::string("not an ability animation file (format should be \"") + AnimationFormat() + "\")");
			return false;
		}
		const TMSim::FJson* Schema = Root.Find("schemaVersion");
		if (!Schema || !Schema->IsNumber() || Schema->Number < 1.0)
		{
			Out.Problems.push_back("no schemaVersion");
			return false;
		}
		if (Schema->Number > AnimationSchema)
		{
			// Written by a newer creator than this game: guessing at it could play
			// the wrong clips, so none are played and the game says so.
			Out.Problems.push_back("schemaVersion " + std::to_string(static_cast<int>(Schema->Number))
				+ " is newer than this game reads (" + std::to_string(AnimationSchema) + ")");
			return false;
		}
		const TMSim::FJson* Abilities = Root.Find("abilities");
		if (!Abilities)
		{
			return true;  // nothing picked yet
		}
		if (!Abilities->IsObject())
		{
			Out.Problems.push_back("abilities should be an object");
			return false;
		}
		for (const std::pair<std::string, TMSim::FJson>& Ability : Abilities->Object)
		{
			if (Ability.first.empty() || !Ability.second.IsObject())
			{
				Out.Problems.push_back("ability '" + Ability.first + "': should be an object of animation sets; left out");
				continue;
			}
			for (const std::pair<std::string, TMSim::FJson>& Set : Ability.second.Object)
			{
				const std::string Where = Ability.first + "." + Set.first;
				if (Set.first.empty() || !Set.second.IsObject())
				{
					Out.Problems.push_back(Where + ": should be an object of picks; left out");
					continue;
				}
				FAnimationPicks Picks;
				for (const std::pair<std::string, TMSim::FJson>& Slot : Set.second.Object)
				{
					const std::string At = Where + "." + Slot.first;
					if (Slot.first == "windup") { ReadPick(Slot.second, At, false, Picks.Windup, Out.Problems); }
					else if (Slot.first == "release") { ReadPick(Slot.second, At, true, Picks.Release, Out.Problems); }
					else if (Slot.first == "loop") { ReadPick(Slot.second, At, false, Picks.Loop, Out.Problems); }
					else if (Slot.first == "recover") { ReadPick(Slot.second, At, false, Picks.Recover, Out.Problems); }
					else { Out.Problems.push_back(At + ": not read by this version"); }
				}
				if (!Picks.IsEmpty())
				{
					Out.Abilities[Ability.first][Set.first] = Picks;
				}
			}
		}
		return true;
	}

	double ContactSeconds(double LeadSeconds, double ReleaseSeconds, double Share)
	{
		const double Lead = std::max(0.0, LeadSeconds);
		if (ReleaseSeconds <= 0.0)
		{
			return Lead;
		}
		return Lead + std::min(1.0, std::max(0.0, Share)) * ReleaseSeconds;
	}
}
