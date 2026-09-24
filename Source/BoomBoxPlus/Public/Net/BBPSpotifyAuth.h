#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BBPSpotifyAuth.generated.h"

struct FHttpServerRequest;
class IHttpRouter;

DECLARE_DELEGATE_OneParam(FBBPOnSpotifyLogin, const FString& /*Error, empty on success*/);

// Signs the player in to Spotify once (in the browser) so whole playlists can be read, and keeps the login.
// Spotify only lists a playlist's songs to a signed-in user; an app's Client ID/Secret alone is refused.
UCLASS()
class BOOMBOXPLUS_API UBBPSpotifyAuth : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Address Spotify sends the browser back to; must be listed as a Redirect URI in the Spotify app.
	static const TCHAR* RedirectUri;

	// Returns true if the mod settings contain a Client ID and Secret.
	bool HasAppCredentials() const;

	// Returns true if a saved login exists for the current Client ID.
	bool IsConnected() const;

	// Returns true while waiting for the player to finish signing in.
	bool IsLoginInProgress() const { return RouteHandle.IsValid(); }

	// Opens Spotify's sign-in page in the browser and waits (up to 5 minutes) for it to come back. Calls OnDone on the game thread.
	void BeginLogin(FBBPOnSpotifyLogin OnDone);

	// Returns a signed-in access token, renewing it when needed. Calls back on the game thread.
	void GetUserToken(TFunction<void(const FString& Token, const FString& Error)> OnDone);

	// Forgets the saved login.
	void Disconnect();

private:
	// Handles the browser's return from Spotify's sign-in page.
	bool HandleCallback(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Exchanges a code or refresh token for an access token.
	void RequestToken(const TArray<TPair<FString, FString>>& Fields, TFunction<void(const FString& Error)> OnDone);

	// Ends the current sign-in attempt, stopping the local listener route.
	void FinishLogin(const FString& Error);

	void LoadSavedLogin();
	void SaveLogin() const;
	FString GetSavePath() const;
	void GetCredentials(FString& OutClientId, FString& OutClientSecret) const;

	FString RefreshToken;
	FString RefreshTokenClientId;
	FString AccessToken;
	double AccessTokenExpires = 0.0;

	TSharedPtr<IHttpRouter> Router;
	FHttpRouteHandle RouteHandle;
	FString LoginState;
	FBBPOnSpotifyLogin LoginCallback;
	FTSTicker::FDelegateHandle LoginTimeout;
};
