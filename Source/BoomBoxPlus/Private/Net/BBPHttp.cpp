#include "Net/BBPHttp.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"

namespace
{
	constexpr float TimeoutSeconds = 15.f;
}

void BBPHttp::Send(const FString& Verb, const FString& Url, const TMap<FString, FString>& Headers, const FString& Body,
	TFunction<void(const FString& Body, int32 Code, const FString& Error)> OnDone)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(Verb);
	for (const TPair<FString, FString>& Header : Headers)
	{
		Request->SetHeader(Header.Key, Header.Value);
	}
	if (!Body.IsEmpty())
	{
		Request->SetContentAsString(Body);
	}
	Request->SetTimeout(TimeoutSeconds);
	Request->OnProcessRequestComplete().BindLambda([OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
	{
		if (!bSucceeded || !Response.IsValid())
		{
			OnDone(FString(), 0, TEXT("network error"));
			return;
		}
		OnDone(Response->GetContentAsString(), Response->GetResponseCode(), FString());
	});
	if (!Request->ProcessRequest())
	{
		OnDone(FString(), 0, TEXT("could not start request"));
	}
}

FString BBPHttp::FormBody(const TArray<TPair<FString, FString>>& Fields)
{
	FString Body;
	for (const TPair<FString, FString>& Field : Fields)
	{
		Body += FString::Printf(TEXT("%s%s=%s"), Body.IsEmpty() ? TEXT("") : TEXT("&"), *Field.Key, *FGenericPlatformHttp::UrlEncode(Field.Value));
	}
	return Body;
}

FString BBPHttp::BasicAuth(const FString& ClientId, const FString& ClientSecret)
{
	return TEXT("Basic ") + FBase64::Encode(ClientId + TEXT(":") + ClientSecret);
}
