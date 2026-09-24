#include "Net/BBPSpotifyAuth.h"
#include "Async/Async.h"
#include "BBPConfig.h"
#include "HAL/FileManager.h"
#include "BoomBoxPlus.h"
#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/BBPHttp.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

const TCHAR* UBBPSpotifyAuth::RedirectUri = TEXT("http://127.0.0.1:8888/callback");

namespace
{
	constexpr uint32 CallbackPort = 8888;
	constexpr float LoginTimeoutSeconds = 300.f;
	// Seconds before expiry at which an access token is renewed.
	constexpr double TokenMarginSeconds = 60.0;
	const TCHAR* Scopes = TEXT("playlist-read-private playlist-read-collaborative");

	FString MakePage(const FString& Title, const FString& Message)
	{
		return FString::Printf(TEXT("<!doctype html><html><head><meta charset=\"utf-8\"><title>%s</title></head>")
			TEXT("<body style=\"background:#18181a;color:#ebebeb;font-family:sans-serif;text-align:center;padding-top:15vh\">")
			TEXT("<h1 style=\"color:#fa9549\">%s</h1><p>%s</p></body></html>"), *Title, *Title, *Message);
	}
}

void UBBPSpotifyAuth::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadSavedLogin();
}

void UBBPSpotifyAuth::Deinitialize()
{
	if (IsLoginInProgress())
	{
		FinishLogin(TEXT("The game is closing"));
	}
	Super::Deinitialize();
}

void UBBPSpotifyAuth::GetCredentials(FString& OutClientId, FString& OutClientSecret) const
{
	OutClientId = UBBPConfig::GetString(GetGameInstance(), UBBPConfig::SpotifyClientIdKey, FString()).TrimStartAndEnd();
	OutClientSecret = UBBPConfig::GetString(GetGameInstance(), UBBPConfig::SpotifyClientSecretKey, FString()).TrimStartAndEnd();
}

bool UBBPSpotifyAuth::HasAppCredentials() const
{
	FString ClientId, ClientSecret;
	GetCredentials(ClientId, ClientSecret);
	return !ClientId.IsEmpty() && !ClientSecret.IsEmpty();
}

bool UBBPSpotifyAuth::IsConnected() const
{
	FString ClientId, ClientSecret;
	GetCredentials(ClientId, ClientSecret);
	return !RefreshToken.IsEmpty() && RefreshTokenClientId == ClientId && !ClientId.IsEmpty();
}

void UBBPSpotifyAuth::BeginLogin(FBBPOnSpotifyLogin OnDone)
{
	FString ClientId, ClientSecret;
	GetCredentials(ClientId, ClientSecret);
	if (ClientId.IsEmpty() || ClientSecret.IsEmpty())
	{
		OnDone.ExecuteIfBound(TEXT("Add your Spotify Client ID and Secret in the mod settings first"));
		return;
	}
	if (IsLoginInProgress())
	{
		FinishLogin(TEXT("Started again"));
	}

	// Listen on the loopback address only, so no firewall prompt and nothing reachable from other machines.
	TArray<FString> Overrides;
	GConfig->GetArray(TEXT("HTTPServer.Listeners"), TEXT("ListenerOverrides"), Overrides, GEngineIni);
	const FString Override = FString::Printf(TEXT("(Port=%u,BindAddress=127.0.0.1)"), CallbackPort);
	if (!Overrides.Contains(Override))
	{
		Overrides.Add(Override);
		GConfig->SetArray(TEXT("HTTPServer.Listeners"), TEXT("ListenerOverrides"), Overrides, GEngineIni);
	}

	Router = FHttpServerModule::Get().GetHttpRouter(CallbackPort, true);
	if (!Router.IsValid())
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Spotify: could not listen on 127.0.0.1:%u"), CallbackPort);
		OnDone.ExecuteIfBound(FString::Printf(TEXT("Couldn't open port %u for the Spotify sign-in (another program may be using it)"), CallbackPort));
		return;
	}
	RouteHandle = Router->BindRoute(FHttpPath(TEXT("/callback")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateUObject(this, &UBBPSpotifyAuth::HandleCallback));
	if (!RouteHandle.IsValid())
	{
		Router.Reset();
		OnDone.ExecuteIfBound(TEXT("Couldn't start the Spotify sign-in listener"));
		return;
	}
	FHttpServerModule::Get().StartAllListeners();

	LoginState = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	LoginCallback = OnDone;
	TWeakObjectPtr<UBBPSpotifyAuth> WeakThis(this);
	LoginTimeout = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float)
	{
		if (UBBPSpotifyAuth* This = WeakThis.Get())
		{
			This->LoginTimeout.Reset();
			This->FinishLogin(TEXT("Spotify sign-in timed out"));
		}
		return false;
	}), LoginTimeoutSeconds);

	const FString Url = FString::Printf(TEXT("https://accounts.spotify.com/authorize?response_type=code&client_id=%s&scope=%s&redirect_uri=%s&state=%s"),
		*FGenericPlatformHttp::UrlEncode(ClientId), *FGenericPlatformHttp::UrlEncode(Scopes), *FGenericPlatformHttp::UrlEncode(RedirectUri), *LoginState);
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Spotify: opening the sign-in page; waiting on %s"), RedirectUri);
	FString LaunchError;
	FPlatformProcess::LaunchURL(*Url, nullptr, &LaunchError);
	if (!LaunchError.IsEmpty())
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Spotify: could not open the browser: %s"), *LaunchError);
	}
}

bool UBBPSpotifyAuth::HandleCallback(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString* State = Request.QueryParams.Find(TEXT("state"));
	const FString* Code = Request.QueryParams.Find(TEXT("code"));
	const FString* Error = Request.QueryParams.Find(TEXT("error"));

	if (!State || *State != LoginState)
	{
		OnComplete(FHttpServerResponse::Create(MakePage(TEXT("BoomBoxPlus"), TEXT("This sign-in link has expired. Start again from the game.")), TEXT("text/html")));
		return true;
	}
	if (Error || !Code)
	{
		const FString Reason = Error ? *Error : FString(TEXT("no code"));
		OnComplete(FHttpServerResponse::Create(MakePage(TEXT("Spotify not connected"), FString::Printf(TEXT("Spotify said: %s. You can close this tab."), *Reason)), TEXT("text/html")));
		// Unbinding inside the route's own handler isn't safe; finish on the next tick.
		TWeakObjectPtr<UBBPSpotifyAuth> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Reason]()
		{
			if (UBBPSpotifyAuth* This = WeakThis.Get())
			{
				This->FinishLogin(Reason == TEXT("access_denied") ? FString(TEXT("Spotify sign-in was cancelled")) : FString::Printf(TEXT("Spotify sign-in failed: %s"), *Reason));
			}
		});
		return true;
	}

	OnComplete(FHttpServerResponse::Create(MakePage(TEXT("Spotify connected"), TEXT("You can close this tab and go back to Satisfactory.")), TEXT("text/html")));
	const FString AuthCode = *Code;
	TWeakObjectPtr<UBBPSpotifyAuth> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, AuthCode]()
	{
		UBBPSpotifyAuth* This = WeakThis.Get();
		if (!This)
		{
			return;
		}
		This->RequestToken({ { TEXT("grant_type"), TEXT("authorization_code") }, { TEXT("code"), AuthCode }, { TEXT("redirect_uri"), RedirectUri } },
			[WeakThis](const FString& TokenError)
		{
			if (UBBPSpotifyAuth* Self = WeakThis.Get())
			{
				Self->FinishLogin(TokenError);
			}
		});
	});
	return true;
}

void UBBPSpotifyAuth::FinishLogin(const FString& Error)
{
	if (Router.IsValid() && RouteHandle.IsValid())
	{
		Router->UnbindRoute(RouteHandle);
	}
	RouteHandle.Reset();
	Router.Reset();
	if (LoginTimeout.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(LoginTimeout);
		LoginTimeout.Reset();
	}
	LoginState.Reset();
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Spotify: sign-in %s%s"), Error.IsEmpty() ? TEXT("succeeded") : TEXT("ended: "), *Error);
	FBBPOnSpotifyLogin Callback = MoveTemp(LoginCallback);
	LoginCallback.Unbind();
	Callback.ExecuteIfBound(Error);
}

void UBBPSpotifyAuth::RequestToken(const TArray<TPair<FString, FString>>& Fields, TFunction<void(const FString& Error)> OnDone)
{
	FString ClientId, ClientSecret;
	GetCredentials(ClientId, ClientSecret);
	TMap<FString, FString> Headers;
	Headers.Add(TEXT("Authorization"), BBPHttp::BasicAuth(ClientId, ClientSecret));
	Headers.Add(TEXT("Content-Type"), TEXT("application/x-www-form-urlencoded"));
	TWeakObjectPtr<UBBPSpotifyAuth> WeakThis(this);
	BBPHttp::Send(TEXT("POST"), TEXT("https://accounts.spotify.com/api/token"), Headers, BBPHttp::FormBody(Fields),
		[WeakThis, ClientId, OnDone](const FString& Body, int32 Code, const FString& Error)
	{
		UBBPSpotifyAuth* This = WeakThis.Get();
		if (!This)
		{
			return;
		}
		TSharedPtr<FJsonObject> Json;
		FString NewAccess, NewRefresh;
		double ExpiresIn = 3600.0;
		if (Code == 200 && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Json) && Json.IsValid())
		{
			Json->TryGetStringField(TEXT("access_token"), NewAccess);
			Json->TryGetStringField(TEXT("refresh_token"), NewRefresh);
			Json->TryGetNumberField(TEXT("expires_in"), ExpiresIn);
		}
		if (NewAccess.IsEmpty())
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Spotify: token request failed (HTTP %d%s%s): %s"), Code, Error.IsEmpty() ? TEXT("") : TEXT(", "), *Error, *Body.Left(300));
			if (Code == 400 && Body.Contains(TEXT("invalid_grant")))
			{
				// The saved login was revoked or expired for good.
				This->Disconnect();
				OnDone(TEXT("The Spotify login expired; press Connect Spotify again"));
				return;
			}
			OnDone(Code == 0 ? FString(TEXT("Couldn't reach Spotify")) : FString::Printf(TEXT("Spotify refused the sign-in (HTTP %d)"), Code));
			return;
		}
		This->AccessToken = NewAccess;
		This->AccessTokenExpires = FPlatformTime::Seconds() + ExpiresIn;
		if (!NewRefresh.IsEmpty())
		{
			This->RefreshToken = NewRefresh;
			This->RefreshTokenClientId = ClientId;
			This->SaveLogin();
		}
		OnDone(FString());
	});
}

void UBBPSpotifyAuth::GetUserToken(TFunction<void(const FString& Token, const FString& Error)> OnDone)
{
	if (!IsConnected())
	{
		OnDone(FString(), TEXT("Spotify isn't connected"));
		return;
	}
	if (!AccessToken.IsEmpty() && FPlatformTime::Seconds() < AccessTokenExpires - TokenMarginSeconds)
	{
		OnDone(AccessToken, FString());
		return;
	}
	TWeakObjectPtr<UBBPSpotifyAuth> WeakThis(this);
	RequestToken({ { TEXT("grant_type"), TEXT("refresh_token") }, { TEXT("refresh_token"), RefreshToken } }, [WeakThis, OnDone](const FString& Error)
	{
		UBBPSpotifyAuth* This = WeakThis.Get();
		OnDone(This && Error.IsEmpty() ? This->AccessToken : FString(), Error);
	});
}

void UBBPSpotifyAuth::Disconnect()
{
	RefreshToken.Reset();
	RefreshTokenClientId.Reset();
	AccessToken.Reset();
	IFileManager::Get().Delete(*GetSavePath());
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Spotify: saved login removed"));
}

FString UBBPSpotifyAuth::GetSavePath() const
{
	return FPaths::ProjectSavedDir() / TEXT("BoomBoxPlus") / TEXT("SpotifyLogin.json");
}

void UBBPSpotifyAuth::LoadSavedLogin()
{
	FString Text;
	TSharedPtr<FJsonObject> Json;
	if (FFileHelper::LoadFileToString(Text, *GetSavePath()) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid())
	{
		Json->TryGetStringField(TEXT("refresh_token"), RefreshToken);
		Json->TryGetStringField(TEXT("client_id"), RefreshTokenClientId);
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Spotify: saved login %s"), RefreshToken.IsEmpty() ? TEXT("empty") : TEXT("found"));
	}
}

void UBBPSpotifyAuth::SaveLogin() const
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("refresh_token"), RefreshToken);
	Json->SetStringField(TEXT("client_id"), RefreshTokenClientId);
	FString Text;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
	if (!FFileHelper::SaveStringToFile(Text, *GetSavePath()))
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Spotify: could not save the login to %s"), *GetSavePath());
	}
}
