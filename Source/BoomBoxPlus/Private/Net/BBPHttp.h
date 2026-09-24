#pragma once

#include "CoreMinimal.h"

namespace BBPHttp
{
	// Sends a request with the given headers and body; calls back on the game thread with the body and HTTP code
	// (0 and an error message if there was no response).
	void Send(const FString& Verb, const FString& Url, const TMap<FString, FString>& Headers, const FString& Body,
		TFunction<void(const FString& Body, int32 Code, const FString& Error)> OnDone);

	// Returns an application/x-www-form-urlencoded body for the given fields.
	FString FormBody(const TArray<TPair<FString, FString>>& Fields);

	// Returns the "Basic ..." Authorization header value for a Spotify app.
	FString BasicAuth(const FString& ClientId, const FString& ClientSecret);
}
