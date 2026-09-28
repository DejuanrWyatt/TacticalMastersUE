// Just enough JSON to read a class file.
//
// The rules are plain C++ with nothing from the engine, so they cannot borrow
// Unreal's JSON reader; and the standard library has none. This is small on
// purpose: objects, arrays, strings, numbers, true, false and null, and nothing
// clever. Numbers are read with std::from_chars, which ignores the machine's
// locale -- a class file must give the same numbers on every machine that loads
// it, or two players would be playing different classes under the same name.

#pragma once

#include "SimTypes.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace TMSim
{
	struct FJson
	{
		enum class EType : uint8_t { Null, Bool, Number, String, Array, Object };

		EType Type = EType::Null;
		bool Bool = false;
		double Number = 0.0;
		std::string String;
		std::vector<FJson> Array;
		/** Keys in the order the file gave them, which is the order they are checked in. */
		std::vector<std::pair<std::string, FJson>> Object;

		bool IsObject() const { return Type == EType::Object; }
		bool IsArray() const { return Type == EType::Array; }
		bool IsNumber() const { return Type == EType::Number; }
		bool IsString() const { return Type == EType::String; }

		/** A member of an object, or null if there is no such key. */
		TMSIM_API const FJson* Find(const std::string& Key) const;
	};

	/** Parses the whole of Text. "" on success, otherwise what was wrong and where. */
	TMSIM_API std::string ParseJson(const std::string& Text, FJson& Out);
}
