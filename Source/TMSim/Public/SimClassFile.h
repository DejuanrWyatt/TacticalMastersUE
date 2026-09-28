// Reading a Tactical Masters class file.
//
// A class file says exactly what the game plays: ten stats, its roles, a look,
// and four abilities in the rules' own terms. Nothing in it is interpreted --
// no formulas, no tags read for meaning -- so reading one is copying numbers
// into the structs the rules already use. The class creator
// (E:\TacticsClassCreator) writes them; its FORMAT.md describes the format, and
// its app/tmclass.mjs validates by the same rules as this.
//
// Strict on purpose. A key the format does not have is an error, not something
// to skip. The Astra files this format replaces were read forgivingly, and a
// misspelt field there quietly made a weaker class with nothing to say so.

#pragma once

#include "SimAbility.h"

#include <string>
#include <vector>

namespace TMSim
{
	/** The name the format gives this file, and the version this reads. */
	inline constexpr const char* ClassFileFormat = "tactical-masters-class";
	inline constexpr int ClassFileVersion = 1;

	/**
	 * Reads one class file. "" on success, with the class and its four abilities
	 * filled in; otherwise every reason it cannot be used, one per line. Nothing
	 * is registered: RegisterJob does that, separately, so a file can be checked
	 * without being loaded.
	 */
	TMSIM_API std::string ReadClassFile(const std::string& Text, FJobDef& OutJob, std::vector<FAbility>& OutAbilities);

	/** Reads a class file and registers it. "" if the class is now in the game. */
	TMSIM_API std::string LoadClassFile(const std::string& Text);
}
