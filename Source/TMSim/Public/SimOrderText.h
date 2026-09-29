// An order as a line of text, and back: how orders travel between two
// machines in an online match (Docs/design/feat-online.md).
//
// The line has to bring back exactly the order that was written, bit for bit,
// or the two games part: a position that comes back a hair different is a
// unit standing somewhere else. So numbers that are not whole are written as
// their bit patterns in hexadecimal rather than as decimals, which would round
// and would read differently under another locale's decimal comma.
//
// Reading is strict, since the text comes from another machine: anything not
// exactly a well-formed order is refused with a reason, never half-read. The
// rules still check the order itself (FBattle::Validate) once it is read.

#pragma once

#include "SimOrder.h"

#include <string>

namespace TMSim
{
	/** One line, e.g. "move 3 7 41200000 40a00000 0". */
	TMSIM_API std::string OrderToText(const FOrder& Order);

	/** Reads a line OrderToText wrote. Returns "" and fills Out, or the reason it was refused. */
	TMSIM_API std::string OrderFromText(const std::string& Text, FOrder& Out);
}
