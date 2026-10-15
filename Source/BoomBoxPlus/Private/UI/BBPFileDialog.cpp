#include "UI/BBPFileDialog.h"
#include "Async/Async.h"
#include "BoomBoxPlus.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SWindow.h"
#include <atomic>
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include <commdlg.h>
#include <objbase.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	std::atomic<bool> bDialogOpen{ false };

#if PLATFORM_WINDOWS
	// Returns the game window's handle, or null.
	HWND GetGameWindow()
	{
		if (!GEngine || !GEngine->GameViewport)
		{
			return nullptr;
		}
		const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow();
		const TSharedPtr<FGenericWindow> Native = Window.IsValid() ? Window->GetNativeWindow() : nullptr;
		return Native.IsValid() ? static_cast<HWND>(Native->GetOSWindowHandle()) : nullptr;
	}
#endif
}

bool BBPFileDialog::OpenListFile(const FString& Title, TFunction<void(const FString& Path)> OnChosen)
{
#if PLATFORM_WINDOWS
	bool bExpected = false;
	if (!bDialogOpen.compare_exchange_strong(bExpected, true))
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: an Open dialog is already showing"));
		return false;
	}
	const HWND Owner = GetGameWindow();
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: showing Open dialog '%s'"), *Title);
	Async(EAsyncExecution::Thread, [Owner, Title, OnChosen]()
	{
		const HRESULT ComResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		TCHAR FileName[MAX_PATH * 4] = { 0 };
		// Filter pairs: description, then patterns, each null-terminated; the list ends with an extra null.
		static const TCHAR Filter[] = TEXT("Lists (*.txt, *.csv, *.tsv, *.m3u, *.m3u8, *.pls)\0*.txt;*.csv;*.tsv;*.m3u;*.m3u8;*.pls\0All files (*.*)\0*.*\0");
		OPENFILENAMEW Dialog = {};
		Dialog.lStructSize = sizeof(Dialog);
		Dialog.hwndOwner = Owner;
		Dialog.lpstrFilter = Filter;
		Dialog.lpstrFile = FileName;
		Dialog.nMaxFile = UE_ARRAY_COUNT(FileName);
		Dialog.lpstrTitle = *Title;
		Dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
		const bool bChosen = GetOpenFileNameW(&Dialog) != 0;
		const DWORD Error = bChosen ? 0 : CommDlgExtendedError();
		if (SUCCEEDED(ComResult))
		{
			CoUninitialize();
		}
		const FString Path = bChosen ? FString(FileName) : FString();
		AsyncTask(ENamedThreads::GameThread, [Path, Error, OnChosen]()
		{
			bDialogOpen = false;
			if (Error != 0)
			{
				UE_LOG(LogBoomBoxPlus, Warning, TEXT("UI: Open dialog failed (error 0x%x)"), Error);
			}
			UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: Open dialog closed: %s"), Path.IsEmpty() ? TEXT("cancelled") : *Path);
			OnChosen(Path);
		});
	});
	return true;
#else
	return false;
#endif
}
