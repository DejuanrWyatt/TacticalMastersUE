// The recorded results the rules tests compare against, and how they are
// recorded again.
//
// Each table in Tests/Baselines holds situations and what the rules made of
// them: a battle's clock tick by tick, what each ability does, where a walk
// goes, what the computer player chooses, whole battles. The tests play every
// situation again and insist the rules still do the same. That is what catches
// a change nobody meant -- a reordered dice roll, a rounding that moved.
//
// A change that IS meant (a rule retuned on purpose) makes those tests fail
// until the tables say what the rules now do. Then run the tests with
// --rebaseline (scripts\test.bat --rebaseline): each table test writes its
// table again with what the rules do now, the same situations with the new
// results, and says how many lines changed. Read the diff before committing it:
// every changed line should be one the change was meant to make.
//
// Never edit a table by hand to make a test pass.
//
// (The tables were first recorded from the Godot version of the game, which
// the rules were ported from. The game is its own reference now.)

#pragma once

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace TMBaseline
{
	/** Whether the command line asks for the tables to be written again. */
	inline bool Asked(int Argc, char** Argv)
	{
		for (int i = 1; i < Argc; ++i)
		{
			if (std::strcmp(Argv[i], "--rebaseline") == 0)
			{
				return true;
			}
		}
		return false;
	}

	/** The arguments that are not --rebaseline, in order: the paths a test reads. */
	inline std::vector<std::string> Paths(int Argc, char** Argv)
	{
		std::vector<std::string> Out;
		for (int i = 1; i < Argc; ++i)
		{
			if (std::strcmp(Argv[i], "--rebaseline") != 0)
			{
				Out.push_back(Argv[i]);
			}
		}
		return Out;
	}

	/** A number as the tables write one: "8.0" for a whole one, otherwise as few digits as say it exactly. */
	inline std::string Number(double Value)
	{
		char Buffer[64];
		if (Value == static_cast<double>(static_cast<long long>(Value)) && Value > -1e15 && Value < 1e15)
		{
			std::snprintf(Buffer, sizeof(Buffer), "%.1f", Value);
		}
		else
		{
			std::snprintf(Buffer, sizeof(Buffer), "%.10g", Value);
		}
		return Buffer;
	}

	/** Line with its "Key=..." word given a new value; the line as it was if it has no such word. */
	inline std::string WithValue(const std::string& Line, const std::string& Key, const std::string& Value)
	{
		const std::string Needle = Key + "=";
		size_t At = 0;
		while ((At = Line.find(Needle, At)) != std::string::npos)
		{
			if (At == 0 || Line[At - 1] == ' ')
			{
				const size_t End = Line.find(' ', At);
				return Line.substr(0, At + Needle.size()) + Value + (End == std::string::npos ? std::string() : Line.substr(End));
			}
			At += Needle.size();
		}
		return Line;
	}

	/**
	 * A table being written again. Every line read is kept as it was unless the
	 * test sets what the rules make of it now; at the end it is written over the
	 * table, if anything changed.
	 */
	struct FWriter
	{
		bool bOn = false;
		std::string Path;
		std::vector<std::string> Lines;
		int Changed = 0;

		/** A line read from the table, kept as it is unless Set replaces it. */
		void Read(const std::string& Line)
		{
			if (bOn)
			{
				Lines.push_back(Line);
			}
		}

		/**
		 * What the rules make of the line read last. Called only where the
		 * rules disagree with the table, so a line they agree with stays
		 * exactly as it was written.
		 */
		void Set(const std::string& Line)
		{
			if (bOn && !Lines.empty() && Lines.back() != Line)
			{
				Lines.back() = Line;
				++Changed;
			}
		}

		/** The same, for a line read earlier: its index in Lines (Lines.size() - 1 when it was read). */
		void SetAt(size_t Index, const std::string& Line)
		{
			if (bOn && Index < Lines.size() && Lines[Index] != Line)
			{
				Lines[Index] = Line;
				++Changed;
			}
		}

		/** The index the next line read will have. */
		size_t Next() const { return Lines.size(); }

		/** Writes the table again, if anything changed. False if it could not be written. */
		bool Finish()
		{
			if (!bOn)
			{
				return true;
			}
			if (Changed == 0)
			{
				std::printf("rebaseline: %s already says what the rules do\n", Path.c_str());
				return true;
			}
			// The table's own line endings, so a diff shows only what changed.
			bool bCrlf = false;
			{
				std::ifstream Old(Path, std::ios::binary);
				std::string First;
				bCrlf = std::getline(Old, First) && !First.empty() && First.back() == '\r';
			}
			std::ofstream Out(Path, std::ios::binary);
			for (const std::string& Line : Lines)
			{
				Out << Line << (bCrlf ? "\r\n" : "\n");
			}
			if (!Out)
			{
				std::printf("rebaseline: could not write %s\n", Path.c_str());
				return false;
			}
			std::printf("rebaseline: %s written again, %d line(s) changed\n", Path.c_str(), Changed);
			return true;
		}
	};
}
