#include "SimJson.h"

#include <charconv>

namespace TMSim
{
	const FJson* FJson::Find(const std::string& Key) const
	{
		for (const std::pair<std::string, FJson>& Member : Object)
		{
			if (Member.first == Key)
			{
				return &Member.second;
			}
		}
		return nullptr;
	}

	namespace
	{
		struct FReader
		{
			const std::string& Text;
			size_t At = 0;
			std::string Error;
			int Depth = 0;

			explicit FReader(const std::string& InText) : Text(InText) {}

			bool Fail(const char* What)
			{
				if (Error.empty())
				{
					Error = std::string(What) + " at character " + std::to_string(At);
				}
				return false;
			}

			void Skip()
			{
				while (At < Text.size() && (Text[At] == ' ' || Text[At] == '\t' || Text[At] == '\n' || Text[At] == '\r'))
				{
					++At;
				}
			}

			bool Word(const char* Expected)
			{
				const std::string Want(Expected);
				if (Text.compare(At, Want.size(), Want) != 0)
				{
					return Fail("unexpected text");
				}
				At += Want.size();
				return true;
			}

			/** A code point as UTF-8, for a \u escape. */
			static void AppendUtf8(std::string& Out, unsigned CodePoint)
			{
				if (CodePoint < 0x80)
				{
					Out += static_cast<char>(CodePoint);
				}
				else if (CodePoint < 0x800)
				{
					Out += static_cast<char>(0xC0 | (CodePoint >> 6));
					Out += static_cast<char>(0x80 | (CodePoint & 0x3F));
				}
				else if (CodePoint < 0x10000)
				{
					Out += static_cast<char>(0xE0 | (CodePoint >> 12));
					Out += static_cast<char>(0x80 | ((CodePoint >> 6) & 0x3F));
					Out += static_cast<char>(0x80 | (CodePoint & 0x3F));
				}
				else
				{
					Out += static_cast<char>(0xF0 | (CodePoint >> 18));
					Out += static_cast<char>(0x80 | ((CodePoint >> 12) & 0x3F));
					Out += static_cast<char>(0x80 | ((CodePoint >> 6) & 0x3F));
					Out += static_cast<char>(0x80 | (CodePoint & 0x3F));
				}
			}

			bool Hex4(unsigned& Out)
			{
				if (At + 4 > Text.size())
				{
					return Fail("a short \\u escape");
				}
				Out = 0;
				for (int i = 0; i < 4; ++i)
				{
					const char C = Text[At++];
					Out <<= 4;
					if (C >= '0' && C <= '9') { Out |= static_cast<unsigned>(C - '0'); }
					else if (C >= 'a' && C <= 'f') { Out |= static_cast<unsigned>(C - 'a' + 10); }
					else if (C >= 'A' && C <= 'F') { Out |= static_cast<unsigned>(C - 'A' + 10); }
					else { return Fail("a bad \\u escape"); }
				}
				return true;
			}

			bool ReadString(std::string& Out)
			{
				++At;  // the opening quote
				while (At < Text.size())
				{
					const char C = Text[At++];
					if (C == '"')
					{
						return true;
					}
					if (C != '\\')
					{
						Out += C;
						continue;
					}
					if (At >= Text.size())
					{
						break;
					}
					const char E = Text[At++];
					switch (E)
					{
					case '"': Out += '"'; break;
					case '\\': Out += '\\'; break;
					case '/': Out += '/'; break;
					case 'b': Out += '\b'; break;
					case 'f': Out += '\f'; break;
					case 'n': Out += '\n'; break;
					case 'r': Out += '\r'; break;
					case 't': Out += '\t'; break;
					case 'u':
					{
						unsigned Code = 0;
						if (!Hex4(Code))
						{
							return false;
						}
						// A surrogate pair is one character split in two.
						if (Code >= 0xD800 && Code <= 0xDBFF && At + 6 <= Text.size() && Text[At] == '\\' && Text[At + 1] == 'u')
						{
							At += 2;
							unsigned Low = 0;
							if (!Hex4(Low))
							{
								return false;
							}
							Code = 0x10000 + ((Code - 0xD800) << 10) + (Low - 0xDC00);
						}
						AppendUtf8(Out, Code);
						break;
					}
					default:
						return Fail("a bad escape");
					}
				}
				return Fail("an unfinished string");
			}

			bool ReadNumber(double& Out)
			{
				const size_t Start = At;
				if (At < Text.size() && Text[At] == '-')
				{
					++At;
				}
				while (At < Text.size() && ((Text[At] >= '0' && Text[At] <= '9') || Text[At] == '.'
					|| Text[At] == 'e' || Text[At] == 'E' || Text[At] == '+' || Text[At] == '-'))
				{
					++At;
				}
				const char* First = Text.data() + Start;
				const char* Last = Text.data() + At;
				const std::from_chars_result Result = std::from_chars(First, Last, Out);
				if (Result.ec != std::errc() || Result.ptr != Last)
				{
					At = Start;
					return Fail("a bad number");
				}
				return true;
			}

			bool ReadValue(FJson& Out)
			{
				Skip();
				if (At >= Text.size())
				{
					return Fail("the end of the text");
				}
				// Deep enough for any class file, shallow enough that a hostile one
				// cannot run the stack out.
				if (Depth > 64)
				{
					return Fail("nesting too deep");
				}
				const char C = Text[At];
				if (C == '{')
				{
					Out.Type = FJson::EType::Object;
					++At;
					++Depth;
					Skip();
					if (At < Text.size() && Text[At] == '}')
					{
						++At;
						--Depth;
						return true;
					}
					while (true)
					{
						Skip();
						if (At >= Text.size() || Text[At] != '"')
						{
							return Fail("a key");
						}
						std::string Key;
						if (!ReadString(Key))
						{
							return false;
						}
						Skip();
						if (At >= Text.size() || Text[At] != ':')
						{
							return Fail("a colon");
						}
						++At;
						FJson Value;
						if (!ReadValue(Value))
						{
							return false;
						}
						Out.Object.emplace_back(std::move(Key), std::move(Value));
						Skip();
						if (At < Text.size() && Text[At] == ',')
						{
							++At;
							continue;
						}
						if (At < Text.size() && Text[At] == '}')
						{
							++At;
							--Depth;
							return true;
						}
						return Fail("a comma or }");
					}
				}
				if (C == '[')
				{
					Out.Type = FJson::EType::Array;
					++At;
					++Depth;
					Skip();
					if (At < Text.size() && Text[At] == ']')
					{
						++At;
						--Depth;
						return true;
					}
					while (true)
					{
						FJson Value;
						if (!ReadValue(Value))
						{
							return false;
						}
						Out.Array.push_back(std::move(Value));
						Skip();
						if (At < Text.size() && Text[At] == ',')
						{
							++At;
							continue;
						}
						if (At < Text.size() && Text[At] == ']')
						{
							++At;
							--Depth;
							return true;
						}
						return Fail("a comma or ]");
					}
				}
				if (C == '"')
				{
					Out.Type = FJson::EType::String;
					return ReadString(Out.String);
				}
				if (C == 't')
				{
					Out.Type = FJson::EType::Bool;
					Out.Bool = true;
					return Word("true");
				}
				if (C == 'f')
				{
					Out.Type = FJson::EType::Bool;
					Out.Bool = false;
					return Word("false");
				}
				if (C == 'n')
				{
					Out.Type = FJson::EType::Null;
					return Word("null");
				}
				Out.Type = FJson::EType::Number;
				return ReadNumber(Out.Number);
			}
		};
	}

	std::string ParseJson(const std::string& Text, FJson& Out)
	{
		FReader Reader(Text);
		// A byte-order mark is not JSON, but editors write one.
		if (Text.size() >= 3 && static_cast<unsigned char>(Text[0]) == 0xEF
			&& static_cast<unsigned char>(Text[1]) == 0xBB && static_cast<unsigned char>(Text[2]) == 0xBF)
		{
			Reader.At = 3;
		}
		Out = FJson();
		if (!Reader.ReadValue(Out))
		{
			return Reader.Error;
		}
		Reader.Skip();
		if (Reader.At != Text.size())
		{
			Reader.Fail("text after the end");
			return Reader.Error;
		}
		return std::string();
	}
}
