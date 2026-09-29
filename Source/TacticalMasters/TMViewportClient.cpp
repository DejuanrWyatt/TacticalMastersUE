#include "TMViewportClient.h"

TFunction<bool(TCHAR)> UTMViewportClient::Typist;

bool UTMViewportClient::InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character)
{
	if (Typist && Typist(Character))
	{
		return true;
	}
	return Super::InputChar(InViewport, ControllerId, Character);
}
