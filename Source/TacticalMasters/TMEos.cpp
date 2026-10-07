#include "TMEos.h"

#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if defined(WITH_EOS_SDK) && WITH_EOS_SDK
#define TM_WITH_EOS 1
// The engine's EOS manager first: it includes the platform's base header the SDK's headers need.
#include "IEOSSDKManager.h"
#include "eos_sdk.h"
#include "eos_connect.h"
#include "eos_lobby.h"
#include "eos_p2p.h"
#else
#define TM_WITH_EOS 0
#endif

DEFINE_LOG_CATEGORY_STATIC(LogTMEos, Log, All);

namespace TMEosPrivate
{
	/** The product's platform, as the engine's EOS manager knows it. */
	const TCHAR* const PlatformName = TEXT("TacticalMasters");
	/** The lobby attributes: the join code, and the online protocol (TMNet.h), so another build is told before it connects. */
	const char* const CodeKey = "CODE";
	const char* const ProtocolKey = "PROTOCOL";
	/** Lobbies are kept in buckets; the game has one. */
	const char* const Bucket = "TM";
	/** Letters and digits that can't be mistaken for each other: no 0/O, 1/I/L. */
	const TCHAR* const CodeLetters = TEXT("ABCDEFGHJKMNPQRSTUVWXYZ23456789");
	constexpr int32 CodeLength = 6;
}

FTMEos& FTMEos::Get()
{
	// Never deleted: the SDK's callbacks hold it until the engine's exit, when Shutdown lets the platform go.
	static FTMEos* Instance = new FTMEos();
	return *Instance;
}

FString FTMEos::MakeCode()
{
	// From a fresh GUID, not FMath's random numbers: those belong to the battles, which must replay exactly.
	const FGuid Guid = FGuid::NewGuid();
	const uint64 Bits = (static_cast<uint64>(Guid.A) << 32) ^ (static_cast<uint64>(Guid.B) << 16) ^ (static_cast<uint64>(Guid.C) << 8) ^ Guid.D;
	const int32 Count = FCString::Strlen(TMEosPrivate::CodeLetters);
	FString Made;
	uint64 Rest = Bits;
	for (int32 i = 0; i < TMEosPrivate::CodeLength; ++i)
	{
		Made.AppendChar(TMEosPrivate::CodeLetters[Rest % Count]);
		Rest /= Count;
	}
	return Made;
}

FString FTMEos::CleanCode(const FString& Typed)
{
	FString Clean;
	for (const TCHAR Character : Typed.ToUpper())
	{
		if (Character == TEXT(' ') || Character == TEXT('-'))
		{
			continue;
		}
		// Only the letters codes are made of: a 0, O, 1, I or L can't be in one.
		int32 Index = INDEX_NONE;
		if (!FString(TMEosPrivate::CodeLetters).FindChar(Character, Index))
		{
			return FString();
		}
		Clean.AppendChar(Character);
	}
	return Clean.Len() == TMEosPrivate::CodeLength ? Clean : FString();
}

FString FTMEos::SocketName() const
{
	// "TM" and the code: only someone who has the code is answered.
	return TEXT("TM") + Code;
}

FString FTMEos::StatusLine() const
{
	switch (State)
	{
	case EState::Off:
		return TEXT("Online services: not started yet.");
	case EState::Unavailable:
		return FString::Printf(TEXT("Join codes aren't available: %s. Direct IP (Advanced) still works."), *StateWhy);
	case EState::SigningIn:
		return TEXT("Online services: signing in...");
	case EState::Ready:
		return TEXT("Online services: ready.");
	case EState::Failed:
	default:
		return FString::Printf(TEXT("Online services couldn't sign in (%s). Leave this screen and come back to try again; direct IP still works."), *StateWhy);
	}
}

void FTMEos::Fail(const FString& Why)
{
	State = EState::Failed;
	StateWhy = Why;
	UE_LOG(LogTMEos, Warning, TEXT("EOS: sign-in failed: %s"), *Why);
}

void FTMEos::ClearLobby()
{
	Lobby = ELobby::None;
	Code.Reset();
	Error.Reset();
	HostUser.Reset();
	LobbyId.Reset();
	bLobbyOwner = false;
	Established.Reset();
	Closed.Reset();
	LeftLobby.Reset();
	bLobbyClosed = false;
}

#if !TM_WITH_EOS

// A build without the SDK: everything says so, and online play by address goes on as before.
struct FTMEos::FImpl {};
void FTMEos::Start(const FString& DisplayName)
{
	State = EState::Unavailable;
	StateWhy = TEXT("this build was made without Epic Online Services");
}
void FTMEos::LoggedIn(const FString& User) {}
void FTMEos::HostLobby(int32 MaxMembers, int32 Protocol) { Lobby = ELobby::Failed; Error = StatusLine(); }
void FTMEos::JoinLobby(const FString& Typed, int32 Protocol) { Lobby = ELobby::Failed; Error = StatusLine(); }
void FTMEos::LeaveLobby() { ClearLobby(); }
void FTMEos::Connect(const FString& User) {}
bool FTMEos::Send(const FString& User, const uint8* Data, int32 Bytes, bool& bFailed) { bFailed = true; return false; }
bool FTMEos::Receive(FString& From, TArray<uint8>& Into) { return false; }
void FTMEos::Disconnect(const FString& User) {}
void FTMEos::DisconnectAll() {}
void FTMEos::Shutdown() {}

#else

struct FTMEos::FImpl
{
	IEOSPlatformHandlePtr Platform;
	EOS_HConnect Connect = nullptr;
	EOS_HLobby Lobbies = nullptr;
	EOS_HP2P P2P = nullptr;
	bool bNotified = false;
	/** Searches still out, by the generation that started them: released when they answer. */
	TMap<uint32, EOS_HLobbySearch> Searches;
	/** The lobby being joined, released when the join answers. */
	EOS_HLobbyDetails Joining = nullptr;
};

namespace TMEosPrivate
{
	FString UserText(EOS_ProductUserId Id)
	{
		if (!Id)
		{
			return FString();
		}
		char Buffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {};
		int32_t Length = sizeof(Buffer);
		if (EOS_ProductUserId_ToString(Id, Buffer, &Length) != EOS_EResult::EOS_Success)
		{
			return FString();
		}
		return FString(UTF8_TO_TCHAR(Buffer));
	}

	EOS_ProductUserId UserId(const FString& Text)
	{
		return Text.IsEmpty() ? nullptr : EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*Text));
	}

	FString ResultText(EOS_EResult Result)
	{
		return FString(UTF8_TO_TCHAR(EOS_EResult_ToString(Result)));
	}

	/** An EOS result as the player reads it. */
	FString Words(EOS_EResult Result)
	{
		switch (Result)
		{
		case EOS_EResult::EOS_NoConnection:
		case EOS_EResult::EOS_TimedOut:
		case EOS_EResult::EOS_ServiceFailure:
			return TEXT("Epic's servers didn't answer: is this PC online?");
		case EOS_EResult::EOS_InvalidAuth:
		case EOS_EResult::EOS_AccessDenied:
		case EOS_EResult::EOS_InvalidCredentials:
			return TEXT("Epic refused this game's ids: the product settings may be wrong");
		case EOS_EResult::EOS_TooManyRequests:
			return TEXT("too many tries at once: wait a minute");
		default:
			return FString::Printf(TEXT("Epic Online Services said %s"), *ResultText(Result));
		}
	}

	FString ClosedWords(EOS_EConnectionClosedReason Reason)
	{
		switch (Reason)
		{
		case EOS_EConnectionClosedReason::EOS_CCR_ClosedByPeer:
			return TEXT("left");
		case EOS_EConnectionClosedReason::EOS_CCR_TimedOut:
		case EOS_EConnectionClosedReason::EOS_CCR_ConnectionClosed:
			return TEXT("the connection dropped");
		case EOS_EConnectionClosedReason::EOS_CCR_ConnectionFailed:
		case EOS_EConnectionClosedReason::EOS_CCR_NegotiationFailed:
			return TEXT("couldn't connect, even through Epic's relays");
		case EOS_EConnectionClosedReason::EOS_CCR_TooManyConnections:
			return TEXT("too many connections");
		default:
			return TEXT("the connection closed");
		}
	}

	/** The connections' socket: its name is the lobby's code. */
	EOS_P2P_SocketId Socket(const FString& Name)
	{
		EOS_P2P_SocketId Id = {};
		Id.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
		FCStringAnsi::Strncpy(Id.SocketName, TCHAR_TO_UTF8(*Name), EOS_P2P_SOCKETID_SOCKETNAME_SIZE);
		return Id;
	}

	bool IsOurSocket(const EOS_P2P_SocketId* Id, const FString& Name)
	{
		return Id && !Name.IsEmpty() && FCStringAnsi::Strcmp(Id->SocketName, TCHAR_TO_UTF8(*Name)) == 0;
	}

	void* GenData(uint32 Generation)
	{
		return reinterpret_cast<void*>(static_cast<UPTRINT>(Generation));
	}

	uint32 DataGen(void* ClientData)
	{
		return static_cast<uint32>(reinterpret_cast<UPTRINT>(ClientData));
	}

	/** -tmeosuser=N: a second copy on the same PC signs in as another device (the two-copies test). */
	int32 CopyNumber()
	{
		int32 Number = 0;
		FParse::Value(FCommandLine::Get(), TEXT("tmeosuser="), Number);
		return FMath::Max(0, Number);
	}
}

/** The SDK's callbacks: each only changes FTMEos's state, on the game thread. */
struct FTMEosCallbacks
{
	static void Login()
	{
		FTMEos& Eos = FTMEos::Get();
		const FTCHARToUTF8 Name(*Eos.Name);
		EOS_Connect_Credentials Credentials = {};
		Credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
		Credentials.Token = nullptr;  // a Device ID needs no token: the SDK keeps this PC's.
		Credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
		EOS_Connect_UserLoginInfo Info = {};
		Info.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST;
		Info.DisplayName = Name.Get();
		Info.NsaIdToken = nullptr;
		EOS_Connect_LoginOptions Options = {};
		Options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
		Options.Credentials = &Credentials;
		Options.UserLoginInfo = &Info;
		EOS_Connect_Login(Eos.Impl->Connect, &Options, nullptr, &FTMEosCallbacks::OnLogin);
	}

	static void EOS_CALL OnDeviceId(const EOS_Connect_CreateDeviceIdCallbackInfo* Data)
	{
		// Made now, or made on an earlier run: either way, sign in with it.
		if (Data->ResultCode == EOS_EResult::EOS_Success || Data->ResultCode == EOS_EResult::EOS_DuplicateNotAllowed)
		{
			Login();
			return;
		}
		FTMEos::Get().Fail(TMEosPrivate::Words(Data->ResultCode));
	}

	static void EOS_CALL OnLogin(const EOS_Connect_LoginCallbackInfo* Data)
	{
		FTMEos& Eos = FTMEos::Get();
		if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			Eos.LoggedIn(TMEosPrivate::UserText(Data->LocalUserId));
			return;
		}
		if (Data->ResultCode == EOS_EResult::EOS_InvalidUser && Data->ContinuanceToken)
		{
			// The first time this PC signs in: make its user.
			EOS_Connect_CreateUserOptions Options = {};
			Options.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
			Options.ContinuanceToken = Data->ContinuanceToken;
			EOS_Connect_CreateUser(Eos.Impl->Connect, &Options, nullptr, &FTMEosCallbacks::OnCreateUser);
			return;
		}
		if (Eos.State == FTMEos::EState::Ready)
		{
			// A refresh that failed: the sign-in still holds until it runs out.
			UE_LOG(LogTMEos, Warning, TEXT("EOS: refreshing the sign-in failed: %s"), *TMEosPrivate::ResultText(Data->ResultCode));
			return;
		}
		Eos.Fail(TMEosPrivate::Words(Data->ResultCode));
	}

	static void EOS_CALL OnCreateUser(const EOS_Connect_CreateUserCallbackInfo* Data)
	{
		if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			FTMEos::Get().LoggedIn(TMEosPrivate::UserText(Data->LocalUserId));
			return;
		}
		FTMEos::Get().Fail(TMEosPrivate::Words(Data->ResultCode));
	}

	static void EOS_CALL OnAuthExpiration(const EOS_Connect_AuthExpirationCallbackInfo* Data)
	{
		// A Connect sign-in lasts an hour: sign in again before it runs out.
		UE_LOG(LogTMEos, Log, TEXT("EOS: the sign-in is running out; refreshing it"));
		Login();
	}

	// ------------------------------------------------ lobbies

	static void EOS_CALL OnCreateLobby(const EOS_Lobby_CreateLobbyCallbackInfo* Data)
	{
		FTMEos& Eos = FTMEos::Get();
		const FString Id = Data->LobbyId ? FString(UTF8_TO_TCHAR(Data->LobbyId)) : FString();
		if (TMEosPrivate::DataGen(Data->ClientData) != Eos.Generation)
		{
			// Given up on while it was being made: close it again.
			if (Data->ResultCode == EOS_EResult::EOS_Success)
			{
				Destroy(Id);
			}
			return;
		}
		if (Data->ResultCode != EOS_EResult::EOS_Success)
		{
			Eos.Lobby = FTMEos::ELobby::Failed;
			Eos.Error = TEXT("Couldn't make the game's join code: ") + TMEosPrivate::Words(Data->ResultCode) + TEXT(".");
			return;
		}
		Eos.LobbyId = Id;
		Eos.bLobbyOwner = true;
		// The code and the protocol on it, for the friend's search to find.
		EOS_HLobbyModification Change = nullptr;
		EOS_Lobby_UpdateLobbyModificationOptions ChangeOptions = {};
		ChangeOptions.ApiVersion = EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;
		ChangeOptions.LocalUserId = TMEosPrivate::UserId(Eos.LocalUser);
		const FTCHARToUTF8 LobbyId(*Id);
		ChangeOptions.LobbyId = LobbyId.Get();
		EOS_EResult Result = EOS_Lobby_UpdateLobbyModification(Eos.Impl->Lobbies, &ChangeOptions, &Change);
		if (Result == EOS_EResult::EOS_Success)
		{
			const FTCHARToUTF8 CodeText(*Eos.Code);
			EOS_Lobby_AttributeData CodeData = {};
			CodeData.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
			CodeData.Key = TMEosPrivate::CodeKey;
			CodeData.Value.AsUtf8 = CodeText.Get();
			CodeData.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
			EOS_LobbyModification_AddAttributeOptions Add = {};
			Add.ApiVersion = EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;
			Add.Attribute = &CodeData;
			Add.Visibility = EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;
			Result = EOS_LobbyModification_AddAttribute(Change, &Add);
			if (Result == EOS_EResult::EOS_Success)
			{
				EOS_Lobby_AttributeData ProtocolData = {};
				ProtocolData.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
				ProtocolData.Key = TMEosPrivate::ProtocolKey;
				ProtocolData.Value.AsInt64 = Eos.WantedProtocol;
				ProtocolData.ValueType = EOS_ELobbyAttributeType::EOS_AT_INT64;
				Add.Attribute = &ProtocolData;
				Result = EOS_LobbyModification_AddAttribute(Change, &Add);
			}
			if (Result == EOS_EResult::EOS_Success)
			{
				EOS_Lobby_UpdateLobbyOptions Update = {};
				Update.ApiVersion = EOS_LOBBY_UPDATELOBBY_API_LATEST;
				Update.LobbyModificationHandle = Change;
				EOS_Lobby_UpdateLobby(Eos.Impl->Lobbies, &Update, TMEosPrivate::GenData(Eos.Generation), &FTMEosCallbacks::OnUpdateLobby);
			}
			EOS_LobbyModification_Release(Change);
		}
		if (Result != EOS_EResult::EOS_Success)
		{
			Eos.Lobby = FTMEos::ELobby::Failed;
			Eos.Error = TEXT("Couldn't put the join code on the game: ") + TMEosPrivate::Words(Result) + TEXT(".");
		}
	}

	static void EOS_CALL OnUpdateLobby(const EOS_Lobby_UpdateLobbyCallbackInfo* Data)
	{
		FTMEos& Eos = FTMEos::Get();
		if (TMEosPrivate::DataGen(Data->ClientData) != Eos.Generation)
		{
			return;
		}
		if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			Eos.Lobby = FTMEos::ELobby::Hosting;
			UE_LOG(LogTMEos, Log, TEXT("EOS: hosting with the join code %s"), *Eos.Code);
			return;
		}
		Eos.Lobby = FTMEos::ELobby::Failed;
		Eos.Error = TEXT("Couldn't put the join code on the game: ") + TMEosPrivate::Words(Data->ResultCode) + TEXT(".");
	}

	static void EOS_CALL OnFind(const EOS_LobbySearch_FindCallbackInfo* Data)
	{
		FTMEos& Eos = FTMEos::Get();
		const uint32 Gen = TMEosPrivate::DataGen(Data->ClientData);
		EOS_HLobbySearch Search = nullptr;
		Eos.Impl->Searches.RemoveAndCopyValue(Gen, Search);
		ON_SCOPE_EXIT
		{
			if (Search)
			{
				EOS_LobbySearch_Release(Search);
			}
		};
		if (Gen != Eos.Generation || !Search)
		{
			return;
		}
		if (Data->ResultCode == EOS_EResult::EOS_NotFound)
		{
			Eos.Lobby = FTMEos::ELobby::Failed;
			Eos.Error = FString::Printf(TEXT("No game with the code %s. Check it with the host, whose lobby shows it."), *Eos.Code);
			return;
		}
		if (Data->ResultCode != EOS_EResult::EOS_Success)
		{
			Eos.Lobby = FTMEos::ELobby::Failed;
			Eos.Error = TEXT("Couldn't look for the game: ") + TMEosPrivate::Words(Data->ResultCode) + TEXT(".");
			return;
		}
		EOS_LobbySearch_GetSearchResultCountOptions CountOptions = {};
		CountOptions.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
		const uint32 Count = EOS_LobbySearch_GetSearchResultCount(Search, &CountOptions);
		EOS_HLobbyDetails Picked = nullptr;
		int64 OtherProtocol = -1;
		for (uint32 i = 0; i < Count; ++i)
		{
			EOS_LobbySearch_CopySearchResultByIndexOptions CopyOptions = {};
			CopyOptions.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
			CopyOptions.LobbyIndex = i;
			EOS_HLobbyDetails Details = nullptr;
			if (EOS_LobbySearch_CopySearchResultByIndex(Search, &CopyOptions, &Details) != EOS_EResult::EOS_Success || !Details)
			{
				continue;
			}
			int64 Protocol = -1;
			EOS_LobbyDetails_CopyAttributeByKeyOptions KeyOptions = {};
			KeyOptions.ApiVersion = EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST;
			KeyOptions.AttrKey = TMEosPrivate::ProtocolKey;
			EOS_Lobby_Attribute* Attribute = nullptr;
			if (EOS_LobbyDetails_CopyAttributeByKey(Details, &KeyOptions, &Attribute) == EOS_EResult::EOS_Success && Attribute)
			{
				if (Attribute->Data && Attribute->Data->ValueType == EOS_ELobbyAttributeType::EOS_AT_INT64)
				{
					Protocol = Attribute->Data->Value.AsInt64;
				}
				EOS_Lobby_Attribute_Release(Attribute);
			}
			if (!Picked && Protocol == Eos.WantedProtocol)
			{
				Picked = Details;
				continue;
			}
			OtherProtocol = Protocol;
			EOS_LobbyDetails_Release(Details);
		}
		if (!Picked)
		{
			Eos.Lobby = FTMEos::ELobby::Failed;
			Eos.Error = Count == 0
				? FString::Printf(TEXT("No game with the code %s. Check it with the host, whose lobby shows it."), *Eos.Code)
				: FString::Printf(TEXT("That game is on another build of Tactical Masters (version %lld; this one is %d). Both players need the same build."),
					OtherProtocol, Eos.WantedProtocol);
			return;
		}
		EOS_LobbyDetails_GetLobbyOwnerOptions OwnerOptions = {};
		OwnerOptions.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
		Eos.HostUser = TMEosPrivate::UserText(EOS_LobbyDetails_GetLobbyOwner(Picked, &OwnerOptions));
		Eos.Lobby = FTMEos::ELobby::Joining;
		EOS_Lobby_JoinLobbyOptions Join = {};
		Join.ApiVersion = EOS_LOBBY_JOINLOBBY_API_LATEST;
		Join.LobbyDetailsHandle = Picked;
		Join.LocalUserId = TMEosPrivate::UserId(Eos.LocalUser);
		Join.bPresenceEnabled = EOS_FALSE;
		Join.LocalRTCOptions = nullptr;
		Join.bCrossplayOptOut = EOS_FALSE;
		Join.RTCRoomJoinActionType = EOS_ELobbyRTCRoomJoinActionType::EOS_LRRJAT_ManualJoin;
		if (Eos.Impl->Joining)
		{
			EOS_LobbyDetails_Release(Eos.Impl->Joining);
		}
		Eos.Impl->Joining = Picked;
		EOS_Lobby_JoinLobby(Eos.Impl->Lobbies, &Join, TMEosPrivate::GenData(Eos.Generation), &FTMEosCallbacks::OnJoin);
	}

	static void EOS_CALL OnJoin(const EOS_Lobby_JoinLobbyCallbackInfo* Data)
	{
		FTMEos& Eos = FTMEos::Get();
		if (Eos.Impl->Joining)
		{
			EOS_LobbyDetails_Release(Eos.Impl->Joining);
			Eos.Impl->Joining = nullptr;
		}
		const FString Id = Data->LobbyId ? FString(UTF8_TO_TCHAR(Data->LobbyId)) : FString();
		if (TMEosPrivate::DataGen(Data->ClientData) != Eos.Generation)
		{
			if (Data->ResultCode == EOS_EResult::EOS_Success)
			{
				Leave(Id);
			}
			return;
		}
		if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			Eos.LobbyId = Id;
			Eos.bLobbyOwner = false;
			Eos.Lobby = FTMEos::ELobby::Joined;
			UE_LOG(LogTMEos, Log, TEXT("EOS: joined the game with the code %s"), *Eos.Code);
			return;
		}
		Eos.Lobby = FTMEos::ELobby::Failed;
		switch (Data->ResultCode)
		{
		case EOS_EResult::EOS_Lobby_TooManyPlayers:
			Eos.Error = TEXT("That game is full: four players already.");
			break;
		case EOS_EResult::EOS_NotFound:
		case EOS_EResult::EOS_Lobby_InvalidSession:
			Eos.Error = TEXT("That game has closed.");
			break;
		default:
			Eos.Error = TEXT("Couldn't join the game: ") + TMEosPrivate::Words(Data->ResultCode) + TEXT(".");
			break;
		}
	}

	static void EOS_CALL OnLeft(const EOS_Lobby_LeaveLobbyCallbackInfo* Data)
	{
		UE_LOG(LogTMEos, Log, TEXT("EOS: left a lobby (%s)"), *TMEosPrivate::ResultText(Data->ResultCode));
	}

	static void EOS_CALL OnDestroyed(const EOS_Lobby_DestroyLobbyCallbackInfo* Data)
	{
		UE_LOG(LogTMEos, Log, TEXT("EOS: closed a lobby (%s)"), *TMEosPrivate::ResultText(Data->ResultCode));
	}

	static void Leave(const FString& Id)
	{
		FTMEos& Eos = FTMEos::Get();
		const FTCHARToUTF8 LobbyId(*Id);
		EOS_Lobby_LeaveLobbyOptions Options = {};
		Options.ApiVersion = EOS_LOBBY_LEAVELOBBY_API_LATEST;
		Options.LocalUserId = TMEosPrivate::UserId(Eos.LocalUser);
		Options.LobbyId = LobbyId.Get();
		EOS_Lobby_LeaveLobby(Eos.Impl->Lobbies, &Options, nullptr, &FTMEosCallbacks::OnLeft);
	}

	static void Destroy(const FString& Id)
	{
		FTMEos& Eos = FTMEos::Get();
		const FTCHARToUTF8 LobbyId(*Id);
		EOS_Lobby_DestroyLobbyOptions Options = {};
		Options.ApiVersion = EOS_LOBBY_DESTROYLOBBY_API_LATEST;
		Options.LocalUserId = TMEosPrivate::UserId(Eos.LocalUser);
		Options.LobbyId = LobbyId.Get();
		EOS_Lobby_DestroyLobby(Eos.Impl->Lobbies, &Options, nullptr, &FTMEosCallbacks::OnDestroyed);
	}

	static void EOS_CALL OnMemberStatus(const EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo* Data)
	{
		FTMEos& Eos = FTMEos::Get();
		if (!Data->LobbyId || Eos.LobbyId.IsEmpty() || Eos.LobbyId != UTF8_TO_TCHAR(Data->LobbyId))
		{
			return;
		}
		const FString Who = TMEosPrivate::UserText(Data->TargetUserId);
		const EOS_ELobbyMemberStatus Status = Data->CurrentStatus;
		const bool bGone = Status == EOS_ELobbyMemberStatus::EOS_LMS_LEFT || Status == EOS_ELobbyMemberStatus::EOS_LMS_DISCONNECTED
			|| Status == EOS_ELobbyMemberStatus::EOS_LMS_KICKED;
		if (Status == EOS_ELobbyMemberStatus::EOS_LMS_CLOSED || (bGone && Who == Eos.LocalUser))
		{
			Eos.bLobbyClosed = !Eos.bLobbyOwner;
			return;
		}
		if (!bGone)
		{
			return;
		}
		if (Eos.bLobbyOwner)
		{
			Eos.LeftLobby.AddUnique(Who);
		}
		else if (Who == Eos.HostUser)
		{
			Eos.bLobbyClosed = true;
		}
	}

	// ------------------------------------------------ connections

	static void EOS_CALL OnConnectionRequest(const EOS_P2P_OnIncomingConnectionRequestInfo* Data)
	{
		FTMEos& Eos = FTMEos::Get();
		const FString Name = Eos.SocketName();
		if (Eos.Code.IsEmpty() || !TMEosPrivate::IsOurSocket(Data->SocketId, Name))
		{
			return;  // not this game's: left unanswered
		}
		const FString Who = TMEosPrivate::UserText(Data->RemoteUserId);
		// Hosting: anyone who knows the code (FTMNet turns them away if the game is full or playing).
		// Joined: only the host.
		const bool bWelcome = Eos.Lobby == FTMEos::ELobby::Hosting || (Eos.Lobby == FTMEos::ELobby::Joined && Who == Eos.HostUser);
		if (bWelcome)
		{
			Eos.Connect(Who);
		}
	}

	static void EOS_CALL OnEstablished(const EOS_P2P_OnPeerConnectionEstablishedInfo* Data)
	{
		FTMEos& Eos = FTMEos::Get();
		if (!TMEosPrivate::IsOurSocket(Data->SocketId, Eos.SocketName()))
		{
			return;
		}
		const FString Who = TMEosPrivate::UserText(Data->RemoteUserId);
		UE_LOG(LogTMEos, Log, TEXT("EOS: connected to %s (%s, %s)"), *Who,
			Data->ConnectionType == EOS_EConnectionEstablishedType::EOS_CET_Reconnection ? TEXT("again") : TEXT("new"),
			Data->NetworkType == EOS_ENetworkConnectionType::EOS_NCT_RelayedConnection ? TEXT("through a relay") : TEXT("direct"));
		Eos.Established.AddUnique(Who);
	}

	static void EOS_CALL OnClosed(const EOS_P2P_OnRemoteConnectionClosedInfo* Data)
	{
		FTMEos& Eos = FTMEos::Get();
		if (!TMEosPrivate::IsOurSocket(Data->SocketId, Eos.SocketName()))
		{
			return;
		}
		const FString Who = TMEosPrivate::UserText(Data->RemoteUserId);
		UE_LOG(LogTMEos, Log, TEXT("EOS: the connection to %s closed (%s)"), *Who, *TMEosPrivate::ClosedWords(Data->Reason));
		Eos.Closed.Add({ Who, TMEosPrivate::ClosedWords(Data->Reason) });
	}
};

void FTMEos::Start(const FString& DisplayName)
{
	Name = DisplayName.Left(EOS_CONNECT_USERLOGININFO_DISPLAYNAME_MAX_LENGTH);
	if (State == EState::SigningIn || State == EState::Ready || State == EState::Unavailable)
	{
		return;
	}
	// The product's ids (Docs/design/feat-online-eos.md section 4).
	FString IdsText;
	const FString IdsPath = FPaths::ProjectContentDir() / TEXT("Data/Online/eos-ids.json");
	TSharedPtr<FJsonObject> Ids;
	if (!FFileHelper::LoadFileToString(IdsText, *IdsPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(IdsText), Ids) || !Ids.IsValid())
	{
		State = EState::Unavailable;
		StateWhy = TEXT("this build has no Epic product ids (Content/Data/Online/eos-ids.json)");
		UE_LOG(LogTMEos, Log, TEXT("EOS: no ids at %s, so no join codes"), *IdsPath);
		return;
	}
	IEOSSDKManager* Manager = IEOSSDKManager::Get();
	if (!Manager || !Manager->IsInitialized())
	{
		State = EState::Unavailable;
		StateWhy = TEXT("the Epic Online Services library didn't start");
		return;
	}
	if (!Impl)
	{
		Impl = new FImpl();
	}
	const int32 Copy = TMEosPrivate::CopyNumber();
	if (!Impl->Platform.IsValid())
	{
		FEOSSDKPlatformConfig Config;
		Config.Name = TMEosPrivate::PlatformName;
		Config.ProductId = Ids->GetStringField(TEXT("productId"));
		Config.SandboxId = Ids->GetStringField(TEXT("sandboxId"));
		Config.DeploymentId = Ids->GetStringField(TEXT("deploymentId"));
		Config.ClientId = Ids->GetStringField(TEXT("clientId"));
		Config.ClientSecret = Ids->GetStringField(TEXT("clientSecret"));
		// No Epic overlay, no voice: the game only needs sign-in, lobbies and connections.
		Config.bDisableOverlay = true;
		Config.bDisableSocialOverlay = true;
		Config.bEnableRTC = false;
		Config.bLoadingInEditor = GIsEditor;
		Config.CacheDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / (Copy > 0 ? FString::Printf(TEXT("EOS%d"), Copy) : FString(TEXT("EOS"))));
		Manager->AddPlatformConfig(Config, true);
		Impl->Platform = Manager->CreatePlatform(Config.Name);
		if (!Impl->Platform.IsValid())
		{
			Fail(TEXT("the platform didn't start: check the product ids"));
			return;
		}
		Impl->Connect = EOS_Platform_GetConnectInterface(*Impl->Platform);
		Impl->Lobbies = EOS_Platform_GetLobbyInterface(*Impl->Platform);
		Impl->P2P = EOS_Platform_GetP2PInterface(*Impl->Platform);
		// Let the platform go before the engine's EOS module does, or it would outlive the SDK.
		FCoreDelegates::OnEnginePreExit.AddLambda([]() { FTMEos::Get().Shutdown(); });
	}
	State = EState::SigningIn;
	StateWhy.Reset();
	// This PC's anonymous id, made once and kept by the SDK; a copy started with -tmeosuser=N is another device.
	const FString Model = Copy > 0 ? FString::Printf(TEXT("Windows PC (copy %d)"), Copy) : FString(TEXT("Windows PC"));
	const FTCHARToUTF8 ModelText(*Model);
	EOS_Connect_CreateDeviceIdOptions Options = {};
	Options.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
	Options.DeviceModel = ModelText.Get();
	EOS_Connect_CreateDeviceId(Impl->Connect, &Options, nullptr, &FTMEosCallbacks::OnDeviceId);
	UE_LOG(LogTMEos, Log, TEXT("EOS: signing in"));
}

void FTMEos::LoggedIn(const FString& User)
{
	LocalUser = User;
	State = EState::Ready;
	StateWhy.Reset();
	UE_LOG(LogTMEos, Log, TEXT("EOS: signed in as %s"), *User);
	if (Impl->bNotified)
	{
		return;
	}
	Impl->bNotified = true;
	const EOS_ProductUserId Me = TMEosPrivate::UserId(LocalUser);
	{
		EOS_Connect_AddNotifyAuthExpirationOptions Options = {};
		Options.ApiVersion = EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST;
		EOS_Connect_AddNotifyAuthExpiration(Impl->Connect, &Options, nullptr, &FTMEosCallbacks::OnAuthExpiration);
	}
	{
		EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions Options = {};
		Options.ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYMEMBERSTATUSRECEIVED_API_LATEST;
		EOS_Lobby_AddNotifyLobbyMemberStatusReceived(Impl->Lobbies, &Options, nullptr, &FTMEosCallbacks::OnMemberStatus);
	}
	// Every socket, filtered by name in the callbacks: the name changes with each game's code.
	{
		EOS_P2P_AddNotifyPeerConnectionRequestOptions Options = {};
		Options.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
		Options.LocalUserId = Me;
		Options.SocketId = nullptr;
		EOS_P2P_AddNotifyPeerConnectionRequest(Impl->P2P, &Options, nullptr, &FTMEosCallbacks::OnConnectionRequest);
	}
	{
		EOS_P2P_AddNotifyPeerConnectionEstablishedOptions Options = {};
		Options.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST;
		Options.LocalUserId = Me;
		Options.SocketId = nullptr;
		EOS_P2P_AddNotifyPeerConnectionEstablished(Impl->P2P, &Options, nullptr, &FTMEosCallbacks::OnEstablished);
	}
	{
		EOS_P2P_AddNotifyPeerConnectionClosedOptions Options = {};
		Options.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST;
		Options.LocalUserId = Me;
		Options.SocketId = nullptr;
		EOS_P2P_AddNotifyPeerConnectionClosed(Impl->P2P, &Options, nullptr, &FTMEosCallbacks::OnClosed);
	}
}

void FTMEos::HostLobby(int32 MaxMembers, int32 Protocol)
{
	LeaveLobby();
	if (State != EState::Ready)
	{
		Lobby = ELobby::Failed;
		Error = StatusLine();
		return;
	}
	// -tmcode=ABCDEF picks the code, for a test that starts both copies at once.
	FString Forced;
	FParse::Value(FCommandLine::Get(), TEXT("tmcode="), Forced);
	Code = CleanCode(Forced).IsEmpty() ? MakeCode() : CleanCode(Forced);
	WantedProtocol = Protocol;
	Lobby = ELobby::Creating;
	EOS_Lobby_CreateLobbyOptions Options = {};
	Options.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
	Options.LocalUserId = TMEosPrivate::UserId(LocalUser);
	Options.MaxLobbyMembers = static_cast<uint32_t>(FMath::Clamp(MaxMembers, 2, 64));
	// Advertised so the code can be searched for; the game never lists them, so only those with the code find it.
	Options.PermissionLevel = EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
	Options.bPresenceEnabled = EOS_FALSE;
	Options.bAllowInvites = EOS_FALSE;
	Options.BucketId = TMEosPrivate::Bucket;
	// The host is the referee: when it goes, the game ends.
	Options.bDisableHostMigration = EOS_TRUE;
	Options.bEnableRTCRoom = EOS_FALSE;
	Options.LocalRTCOptions = nullptr;
	Options.LobbyId = nullptr;
	Options.bEnableJoinById = EOS_FALSE;
	Options.bRejoinAfterKickRequiresInvite = EOS_FALSE;
	Options.AllowedPlatformIds = nullptr;
	Options.AllowedPlatformIdsCount = 0;
	Options.bCrossplayOptOut = EOS_FALSE;
	Options.RTCRoomJoinActionType = EOS_ELobbyRTCRoomJoinActionType::EOS_LRRJAT_ManualJoin;
	EOS_Lobby_CreateLobby(Impl->Lobbies, &Options, TMEosPrivate::GenData(Generation), &FTMEosCallbacks::OnCreateLobby);
	UE_LOG(LogTMEos, Log, TEXT("EOS: making a game with the code %s"), *Code);
}

void FTMEos::JoinLobby(const FString& Typed, int32 Protocol)
{
	LeaveLobby();
	const FString Clean = CleanCode(Typed);
	if (Clean.IsEmpty())
	{
		Lobby = ELobby::Failed;
		Error = TEXT("A join code is six letters and digits (never 0, O, 1, I or L), as the host's lobby shows it.");
		return;
	}
	if (State != EState::Ready)
	{
		Lobby = ELobby::Failed;
		Error = StatusLine();
		return;
	}
	Code = Clean;
	WantedProtocol = Protocol;
	Lobby = ELobby::Searching;
	EOS_HLobbySearch Search = nullptr;
	EOS_Lobby_CreateLobbySearchOptions SearchOptions = {};
	SearchOptions.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
	SearchOptions.MaxResults = 10;
	EOS_EResult Result = EOS_Lobby_CreateLobbySearch(Impl->Lobbies, &SearchOptions, &Search);
	if (Result == EOS_EResult::EOS_Success)
	{
		const FTCHARToUTF8 CodeText(*Code);
		EOS_Lobby_AttributeData Parameter = {};
		Parameter.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
		Parameter.Key = TMEosPrivate::CodeKey;
		Parameter.Value.AsUtf8 = CodeText.Get();
		Parameter.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
		EOS_LobbySearch_SetParameterOptions Set = {};
		Set.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
		Set.Parameter = &Parameter;
		Set.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
		Result = EOS_LobbySearch_SetParameter(Search, &Set);
		if (Result == EOS_EResult::EOS_Success)
		{
			// Only the game's own bucket.
			EOS_Lobby_AttributeData BucketParameter = {};
			BucketParameter.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
			BucketParameter.Key = EOS_LOBBY_SEARCH_BUCKET_ID;
			BucketParameter.Value.AsUtf8 = TMEosPrivate::Bucket;
			BucketParameter.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
			Set.Parameter = &BucketParameter;
			Result = EOS_LobbySearch_SetParameter(Search, &Set);
		}
	}
	if (Result != EOS_EResult::EOS_Success)
	{
		if (Search)
		{
			EOS_LobbySearch_Release(Search);
		}
		Lobby = ELobby::Failed;
		Error = TEXT("Couldn't look for the game: ") + TMEosPrivate::Words(Result) + TEXT(".");
		return;
	}
	Impl->Searches.Add(Generation, Search);
	EOS_LobbySearch_FindOptions Find = {};
	Find.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
	Find.LocalUserId = TMEosPrivate::UserId(LocalUser);
	EOS_LobbySearch_Find(Search, &Find, TMEosPrivate::GenData(Generation), &FTMEosCallbacks::OnFind);
	UE_LOG(LogTMEos, Log, TEXT("EOS: looking for the game with the code %s"), *Code);
}

void FTMEos::LeaveLobby()
{
	// Whatever is still on its way for the old lobby now only tidies up when it answers.
	++Generation;
	if (Impl && State == EState::Ready && !LobbyId.IsEmpty())
	{
		if (bLobbyOwner)
		{
			FTMEosCallbacks::Destroy(LobbyId);
		}
		else
		{
			FTMEosCallbacks::Leave(LobbyId);
		}
	}
	ClearLobby();
}

void FTMEos::Connect(const FString& User)
{
	if (!Impl || State != EState::Ready || User.IsEmpty())
	{
		return;
	}
	const EOS_P2P_SocketId Socket = TMEosPrivate::Socket(SocketName());
	EOS_P2P_AcceptConnectionOptions Accept = {};
	Accept.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
	Accept.LocalUserId = TMEosPrivate::UserId(LocalUser);
	Accept.RemoteUserId = TMEosPrivate::UserId(User);
	Accept.SocketId = &Socket;
	EOS_P2P_AcceptConnection(Impl->P2P, &Accept);
	// A knock opens it from this side (the other side's request is accepted as it comes).
	const uint8 Knock = 1;
	EOS_P2P_SendPacketOptions Packet = {};
	Packet.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
	Packet.LocalUserId = Accept.LocalUserId;
	Packet.RemoteUserId = Accept.RemoteUserId;
	Packet.SocketId = &Socket;
	Packet.Channel = KnockChannel;
	Packet.DataLengthBytes = 1;
	Packet.Data = &Knock;
	Packet.bAllowDelayedDelivery = EOS_TRUE;
	Packet.Reliability = EOS_EPacketReliability::EOS_PR_ReliableOrdered;
	Packet.bDisableAutoAcceptConnection = EOS_FALSE;
	EOS_P2P_SendPacket(Impl->P2P, &Packet);
}

bool FTMEos::Send(const FString& User, const uint8* Data, int32 Bytes, bool& bFailed)
{
	bFailed = false;
	if (!Impl || State != EState::Ready)
	{
		bFailed = true;
		return false;
	}
	const EOS_P2P_SocketId Socket = TMEosPrivate::Socket(SocketName());
	EOS_P2P_SendPacketOptions Options = {};
	Options.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
	Options.LocalUserId = TMEosPrivate::UserId(LocalUser);
	Options.RemoteUserId = TMEosPrivate::UserId(User);
	Options.SocketId = &Socket;
	Options.Channel = StreamChannel;
	Options.DataLengthBytes = static_cast<uint32_t>(FMath::Clamp(Bytes, 0, MaxPacket));
	Options.Data = Data;
	Options.bAllowDelayedDelivery = EOS_TRUE;
	Options.Reliability = EOS_EPacketReliability::EOS_PR_ReliableOrdered;
	Options.bDisableAutoAcceptConnection = EOS_FALSE;
	const EOS_EResult Result = EOS_P2P_SendPacket(Impl->P2P, &Options);
	if (Result == EOS_EResult::EOS_Success)
	{
		return true;
	}
	// A full queue only means later; anything else, the connection can't carry on.
	bFailed = Result != EOS_EResult::EOS_LimitExceeded;
	if (bFailed)
	{
		UE_LOG(LogTMEos, Warning, TEXT("EOS: couldn't send to %s: %s"), *User, *TMEosPrivate::ResultText(Result));
	}
	return false;
}

bool FTMEos::Receive(FString& From, TArray<uint8>& Into)
{
	if (!Impl || State != EState::Ready)
	{
		return false;
	}
	const FString Mine = SocketName();
	uint8 Buffer[MaxPacket];
	for (;;)
	{
		EOS_P2P_ReceivePacketOptions Options = {};
		Options.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
		Options.LocalUserId = TMEosPrivate::UserId(LocalUser);
		Options.MaxDataSizeBytes = MaxPacket;
		Options.RequestedChannel = nullptr;
		EOS_ProductUserId Peer = nullptr;
		EOS_P2P_SocketId Socket = {};
		Socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
		uint8_t Channel = 0;
		uint32_t Written = 0;
		const EOS_EResult Result = EOS_P2P_ReceivePacket(Impl->P2P, &Options, &Peer, &Socket, &Channel, Buffer, &Written);
		if (Result != EOS_EResult::EOS_Success)
		{
			return false;  // EOS_NotFound: nothing more for now
		}
		if (Channel != StreamChannel || !TMEosPrivate::IsOurSocket(&Socket, Mine))
		{
			continue;  // a knock, or something left over from an earlier game
		}
		From = TMEosPrivate::UserText(Peer);
		Into.Reset();
		Into.Append(Buffer, static_cast<int32>(Written));
		return true;
	}
}

void FTMEos::Disconnect(const FString& User)
{
	if (!Impl || State != EState::Ready || User.IsEmpty() || Code.IsEmpty())
	{
		return;
	}
	const EOS_P2P_SocketId Socket = TMEosPrivate::Socket(SocketName());
	EOS_P2P_CloseConnectionOptions Options = {};
	Options.ApiVersion = EOS_P2P_CLOSECONNECTION_API_LATEST;
	Options.LocalUserId = TMEosPrivate::UserId(LocalUser);
	Options.RemoteUserId = TMEosPrivate::UserId(User);
	Options.SocketId = &Socket;
	EOS_P2P_CloseConnection(Impl->P2P, &Options);
}

void FTMEos::DisconnectAll()
{
	if (!Impl || State != EState::Ready || Code.IsEmpty())
	{
		return;
	}
	const EOS_P2P_SocketId Socket = TMEosPrivate::Socket(SocketName());
	EOS_P2P_CloseConnectionsOptions Options = {};
	Options.ApiVersion = EOS_P2P_CLOSECONNECTIONS_API_LATEST;
	Options.LocalUserId = TMEosPrivate::UserId(LocalUser);
	Options.SocketId = &Socket;
	EOS_P2P_CloseConnections(Impl->P2P, &Options);
}

void FTMEos::Shutdown()
{
	if (!Impl)
	{
		return;
	}
	DisconnectAll();
	LeaveLobby();
	for (const TPair<uint32, EOS_HLobbySearch>& Search : Impl->Searches)
	{
		EOS_LobbySearch_Release(Search.Value);
	}
	Impl->Searches.Reset();
	if (Impl->Joining)
	{
		EOS_LobbyDetails_Release(Impl->Joining);
		Impl->Joining = nullptr;
	}
	Impl->Platform.Reset();
	Impl->Connect = nullptr;
	Impl->Lobbies = nullptr;
	Impl->P2P = nullptr;
	Impl->bNotified = false;
	State = EState::Off;
}

#endif
