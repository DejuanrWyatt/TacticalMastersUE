#include "SimOrderText.h"

#include "SimTypes.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <locale>
#include <sstream>
#include <vector>

namespace TMSim
{
	namespace
	{
		std::string Hex(uint64_t Bits)
		{
			char Buffer[24];
			const std::to_chars_result Written = std::to_chars(Buffer, Buffer + sizeof(Buffer), Bits, 16);
			return std::string(Buffer, Written.ptr);
		}

		std::string FloatText(float Value)
		{
			uint32_t Bits = 0;
			std::memcpy(&Bits, &Value, sizeof(Bits));
			return Hex(Bits);
		}

		std::string DoubleText(double Value)
		{
			uint64_t Bits = 0;
			std::memcpy(&Bits, &Value, sizeof(Bits));
			return Hex(Bits);
		}

		/** The words of a line, split on single spaces; an empty word means a doubled space. */
		std::vector<std::string> Words(const std::string& Text)
		{
			std::vector<std::string> Out;
			size_t Start = 0;
			while (Start <= Text.size())
			{
				const size_t Space = Text.find(' ', Start);
				const size_t End = Space == std::string::npos ? Text.size() : Space;
				Out.push_back(Text.substr(Start, End - Start));
				if (Space == std::string::npos)
				{
					break;
				}
				Start = Space + 1;
			}
			return Out;
		}

		/** Reads the words in turn, remembering the first thing wrong. */
		struct FReader
		{
			const std::vector<std::string>& Words;
			size_t Next = 1;
			std::string Error;

			const std::string* Take(const char* What)
			{
				if (!Error.empty())
				{
					return nullptr;
				}
				if (Next >= Words.size())
				{
					Error = std::string("missing ") + What;
					return nullptr;
				}
				return &Words[Next++];
			}

			int Int(const char* What, int Low, int High)
			{
				const std::string* Word = Take(What);
				long long Value = 0;
				if (!Word)
				{
					return 0;
				}
				const std::from_chars_result Read = std::from_chars(Word->data(), Word->data() + Word->size(), Value);
				if (Word->empty() || Read.ec != std::errc() || Read.ptr != Word->data() + Word->size() || Value < Low || Value > High)
				{
					Error = std::string("bad ") + What + ": " + Word->substr(0, 32);
					return 0;
				}
				return static_cast<int>(Value);
			}

			uint64_t Bits(const char* What, int MaxDigits)
			{
				const std::string* Word = Take(What);
				uint64_t Value = 0;
				if (!Word)
				{
					return 0;
				}
				const std::from_chars_result Read = std::from_chars(Word->data(), Word->data() + Word->size(), Value, 16);
				if (Word->empty() || static_cast<int>(Word->size()) > MaxDigits || Read.ec != std::errc() || Read.ptr != Word->data() + Word->size())
				{
					Error = std::string("bad ") + What + ": " + Word->substr(0, 32);
					return 0;
				}
				return Value;
			}

			float Float(const char* What)
			{
				const uint32_t Raw = static_cast<uint32_t>(Bits(What, 8));
				float Value = 0.0f;
				std::memcpy(&Value, &Raw, sizeof(Value));
				// A position that is not a number would poison every distance it touches.
				if (Error.empty() && !std::isfinite(Value))
				{
					Error = std::string("bad ") + What + ": not a finite number";
				}
				return Value;
			}

			double Double(const char* What)
			{
				const uint64_t Raw = Bits(What, 16);
				double Value = 0.0;
				std::memcpy(&Value, &Raw, sizeof(Value));
				if (Error.empty() && !std::isfinite(Value))
				{
					Error = std::string("bad ") + What + ": not a finite number";
				}
				return Value;
			}
		};

		// Generous bounds: the rules judge whether a unit exists or a serial is
		// current. These only keep a number sane enough to hand over.
		constexpr int MaxId = 1000000;
		constexpr int MaxSerial = 1000000000;
	}

	std::string OrderToText(const FOrder& Order)
	{
		std::ostringstream Out;
		// Whole numbers too are written the one way, whatever locale the program runs under.
		Out.imbue(std::locale::classic());
		switch (Order.Type)
		{
		case EOrderType::Advance:
			Out << "advance " << Order.Ticks;
			break;
		case EOrderType::Move:
			Out << "move " << Order.UnitId << ' ' << Order.Serial << ' ' << FloatText(Order.To.X) << ' '
				<< FloatText(Order.To.Y) << ' ' << (Order.bSprint ? 1 : 0);
			// Waypoints (2026-10-01) after the rest, and only when there are any,
			// so a plain walk reads exactly as it always has.
			if (!Order.Via.empty())
			{
				Out << ' ' << Order.Via.size();
				for (const FVec2& Point : Order.Via)
				{
					Out << ' ' << FloatText(Point.X) << ' ' << FloatText(Point.Y);
				}
			}
			break;
		case EOrderType::UseAbility:
			Out << "ability " << Order.UnitId << ' ' << Order.Serial << ' ' << Order.Slot << ' '
				<< FloatText(Order.Target.X) << ' ' << FloatText(Order.Target.Y) << ' ' << Order.Follow;
			break;
		case EOrderType::EndTurn:
			Out << "end " << Order.UnitId << ' ' << Order.Serial;
			break;
		case EOrderType::Place:
			Out << "place " << Order.UnitId << ' ' << Order.Serial << ' ' << FloatText(Order.To.X) << ' '
				<< FloatText(Order.To.Y);
			break;
		case EOrderType::Ready:
			Out << "ready " << Order.Team;
			break;
		case EOrderType::Capture:
			Out << "capture " << Order.UnitId << ' ' << Order.Serial << ' ' << Order.Tower;
			break;
		case EOrderType::Take:
			// An item id is lowercase letters, digits and _ (SimItem.cpp), so it is one word.
			Out << "take " << Order.UnitId << ' ' << Order.Serial << ' ' << Order.Cache << ' '
				<< (Order.ItemId.empty() ? std::string("-") : Order.ItemId) << ' ' << Order.GearSlot;
			break;
		case EOrderType::Drop:
			Out << "drop " << Order.UnitId << ' ' << Order.Serial << ' ' << Order.GearSlot;
			break;
		case EOrderType::Equip:
			Out << "equip " << Order.UnitId << ' ' << (Order.ItemId.empty() ? std::string("-") : Order.ItemId) << ' ' << Order.GearSlot;
			break;
		case EOrderType::Tune:
			Out << "tune " << Order.TuneValues.size();
			for (const std::pair<int, double>& Value : Order.TuneValues)
			{
				Out << ' ' << Value.first << ' ' << DoubleText(Value.second);
			}
			break;
		}
		return Out.str();
	}

	std::string OrderFromText(const std::string& Text, FOrder& Out)
	{
		if (Text.empty() || Text.size() > 4096)
		{
			return "an order must be one line of at most 4096 characters";
		}
		const std::vector<std::string> List = Words(Text);
		FReader Read{ List };
		FOrder Order;
		const std::string& Kind = List[0];
		if (Kind == "advance")
		{
			Order.Type = EOrderType::Advance;
			Order.Ticks = Read.Int("ticks", 0, 1000000);
		}
		else if (Kind == "move")
		{
			Order.Type = EOrderType::Move;
			Order.UnitId = Read.Int("unit", 0, MaxId);
			Order.Serial = Read.Int("serial", 0, MaxSerial);
			Order.To.X = Read.Float("x");
			Order.To.Y = Read.Float("y");
			Order.bSprint = Read.Int("sprint", 0, 1) == 1;
			if (Read.Error.empty() && Read.Next < List.size())
			{
				const int Count = Read.Int("waypoints", 1, MaxWaypoints);
				for (int i = 0; i < Count && Read.Error.empty(); ++i)
				{
					FVec2 Point;
					Point.X = Read.Float("waypoint x");
					Point.Y = Read.Float("waypoint y");
					Order.Via.push_back(Point);
				}
			}
		}
		else if (Kind == "ability")
		{
			Order.Type = EOrderType::UseAbility;
			Order.UnitId = Read.Int("unit", 0, MaxId);
			Order.Serial = Read.Int("serial", 0, MaxSerial);
			Order.Slot = Read.Int("slot", 0, AbilitySlots - 1);
			Order.Target.X = Read.Float("x");
			Order.Target.Y = Read.Float("y");
			Order.Follow = Read.Int("follow", -1, MaxId);
		}
		else if (Kind == "end")
		{
			Order.Type = EOrderType::EndTurn;
			Order.UnitId = Read.Int("unit", 0, MaxId);
			Order.Serial = Read.Int("serial", 0, MaxSerial);
		}
		else if (Kind == "place")
		{
			Order.Type = EOrderType::Place;
			Order.UnitId = Read.Int("unit", 0, MaxId);
			Order.Serial = Read.Int("serial", 0, MaxSerial);
			Order.To.X = Read.Float("x");
			Order.To.Y = Read.Float("y");
		}
		else if (Kind == "ready")
		{
			Order.Type = EOrderType::Ready;
			Order.Team = Read.Int("team", 0, 1);
		}
		else if (Kind == "capture")
		{
			Order.Type = EOrderType::Capture;
			Order.UnitId = Read.Int("unit", 0, MaxId);
			Order.Serial = Read.Int("serial", 0, MaxSerial);
			// The rules judge whether there is such a tower; this only keeps it sane.
			Order.Tower = Read.Int("tower", 0, 64);
		}
		else if (Kind == "take")
		{
			Order.Type = EOrderType::Take;
			Order.UnitId = Read.Int("unit", 0, MaxId);
			Order.Serial = Read.Int("serial", 0, MaxSerial);
			Order.Cache = Read.Int("cache", 0, 100000);
			if (const std::string* Item = Read.Take("item"))
			{
				bool bWord = !Item->empty() && Item->size() <= 31;
				for (const char C : *Item)
				{
					bWord = bWord && ((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_');
				}
				if (!bWord)
				{
					Read.Error = "bad item: " + Item->substr(0, 32);
				}
				Order.ItemId = *Item;
			}
			Order.GearSlot = Read.Int("slot", -1, 2);
		}
		else if (Kind == "equip")
		{
			Order.Type = EOrderType::Equip;
			Order.UnitId = Read.Int("unit", 0, MaxId);
			if (const std::string* Item = Read.Take("item"))
			{
				bool bWord = !Item->empty() && Item->size() <= 31;
				for (const char C : *Item)
				{
					bWord = bWord && ((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_');
				}
				if (!bWord)
				{
					Read.Error = "bad item: " + Item->substr(0, 32);
				}
				Order.ItemId = *Item;
			}
			Order.GearSlot = Read.Int("slot", -1, 2);
		}
		else if (Kind == "drop")
		{
			Order.Type = EOrderType::Drop;
			Order.UnitId = Read.Int("unit", 0, MaxId);
			Order.Serial = Read.Int("serial", 0, MaxSerial);
			Order.GearSlot = Read.Int("slot", 0, 2);
		}
		else if (Kind == "tune")
		{
			Order.Type = EOrderType::Tune;
			const int Keys = static_cast<int>(TuningKeys().size());
			const int Count = Read.Int("count", 0, Keys);
			for (int i = 0; i < Count && Read.Error.empty(); ++i)
			{
				const int Key = Read.Int("rule", 0, Keys - 1);
				const double Value = Read.Double("value");
				Order.TuneValues.emplace_back(Key, Value);
			}
		}
		else
		{
			return "unknown order: " + Kind.substr(0, 32);
		}
		if (!Read.Error.empty())
		{
			return Kind + ": " + Read.Error;
		}
		if (Read.Next != List.size())
		{
			return Kind + ": more than an order";
		}
		Out = Order;
		return std::string();
	}
}
