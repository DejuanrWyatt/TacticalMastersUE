// Typing into the canvas HUD: the address to join, the port, and chat.
//
// The HUD is drawn on a canvas, not built from widgets, so there is no text
// box to click into. Instead, while a field is being typed in, this sits in
// front of Slate's input: characters (which come by way of the viewport,
// TMViewportClient.h) go into the field, Backspace takes one
// off, Ctrl+V pastes, Enter finishes and Esc gives up. It swallows the keys it
// takes, so typing "wasd" into chat does not also fly the camera.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"

class FTMTextInput : public IInputProcessor
{
public:
	/** Starts typing into Field, at most MaxLength characters. Done says whether Enter (true) or Esc (false) ended it. */
	void Begin(FString* Field, int32 MaxLength, TFunction<void(bool)> Done);
	/** Stops without calling Done. */
	void Stop();
	bool IsTyping() const { return Field != nullptr; }

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	/** A typed character, from the viewport (TMViewportClient.h). True if it was taken. */
	bool TakeCharacter(TCHAR Character);
	virtual const TCHAR* GetDebugName() const override { return TEXT("TacticalMastersTextInput"); }

private:
	FString* Field = nullptr;
	int32 MaxLength = 0;
	TFunction<void(bool)> Done;
	void Finish(bool bSubmit);
};
