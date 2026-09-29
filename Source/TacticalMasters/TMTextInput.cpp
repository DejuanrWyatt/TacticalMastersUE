#include "TMTextInput.h"

#include "HAL/PlatformApplicationMisc.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"

void FTMTextInput::Begin(FString* InField, int32 InMaxLength, TFunction<void(bool)> InDone)
{
	Field = InField;
	MaxLength = InMaxLength;
	Done = MoveTemp(InDone);
}

void FTMTextInput::Stop()
{
	Field = nullptr;
	Done = nullptr;
}

void FTMTextInput::Finish(bool bSubmit)
{
	// Cleared first: Done may start typing into another field.
	TFunction<void(bool)> Then = MoveTemp(Done);
	Field = nullptr;
	Done = nullptr;
	if (Then)
	{
		Then(bSubmit);
	}
}

bool FTMTextInput::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	if (!Field)
	{
		return false;
	}
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Enter)
	{
		Finish(true);
	}
	else if (Key == EKeys::Escape)
	{
		Finish(false);
	}
	else if (Key == EKeys::BackSpace)
	{
		Field->LeftChopInline(1);
	}
	else if (Key == EKeys::V && InKeyEvent.IsControlDown())
	{
		FString Pasted;
		FPlatformApplicationMisc::ClipboardPaste(Pasted);
		Pasted.ReplaceInline(TEXT("\r"), TEXT(""));
		Pasted.ReplaceInline(TEXT("\n"), TEXT(" "));
		*Field = (*Field + Pasted.TrimStartAndEnd()).Left(MaxLength);
	}
	// Mouse buttons still reach the game; every key is the field's while typing.
	return !Key.IsMouseButton();
}

bool FTMTextInput::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	return Field != nullptr && !InKeyEvent.GetKey().IsMouseButton();
}

bool FTMTextInput::TakeCharacter(TCHAR Character)
{
	if (!Field)
	{
		return false;
	}
	// Printable characters only: control characters (Enter, Backspace) are keys above.
	if (Character >= 32 && Character != 127 && Field->Len() < MaxLength)
	{
		Field->AppendChar(Character);
	}
	return true;
}
