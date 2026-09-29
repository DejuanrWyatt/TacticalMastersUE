// The game's viewport client, for one thing: typed characters.
//
// Slate's input pre-processors see key presses but not the characters they
// type (a shifted key, an accent, a character pasted in by the system). The
// viewport gets those, so while one of the HUD's text fields is being typed in
// (TMTextInput.h) its characters go there instead of to the game. Named in
// Config/DefaultEngine.ini as the game's viewport client.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"

#include "TMViewportClient.generated.h"

UCLASS()
class UTMViewportClient : public UGameViewportClient
{
	GENERATED_BODY()

public:
	/** Where typed characters go while a field is being typed in; false to let the game have them. */
	static TFunction<bool(TCHAR)> Typist;

	virtual bool InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character) override;
};
