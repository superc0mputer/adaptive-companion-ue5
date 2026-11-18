#include "Pipeline/AdaptivePresentProbe.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "CoreGlobals.h"
#include "Rendering/SlateRenderer.h"
#include "RenderingThread.h"
#include "Widgets/SWindow.h"

TSharedPtr<FAdaptivePresentProbe, ESPMode::ThreadSafe> FAdaptivePresentProbe::Create(UGameInstance* GameInstance)
{
	if (!GameInstance || IsRunningCommandlet() || !FApp::CanEverRender() || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		return nullptr;
	}

	TSharedPtr<FAdaptivePresentProbe, ESPMode::ThreadSafe> Probe = MakeShared<FAdaptivePresentProbe, ESPMode::ThreadSafe>();
	Probe->GameInstance = GameInstance;
	Probe->Handle = FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddThreadSafeSP(Probe.ToSharedRef(), &FAdaptivePresentProbe::OnBackBufferReady);
	return Probe;
}

FAdaptivePresentProbe::~FAdaptivePresentProbe()
{
	// normally removed in Shutdown; the delegate only holds a weak pointer, so a late call can't reach a dead probe
}

void FAdaptivePresentProbe::Shutdown()
{
	if (Handle.IsValid() && FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
	{
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(Handle);
	}
	Handle.Reset();
}

void FAdaptivePresentProbe::Request(int32 Id)
{
	// the game's own window: in PIE the editor presents its other windows too
	const UGameInstance* Instance = GameInstance.Get();
	const UGameViewportClient* ViewportClient = Instance ? Instance->GetGameViewportClient() : nullptr;
	const TSharedPtr<SWindow> Window = ViewportClient ? ViewportClient->GetWindow() : nullptr;

	FRequest NewRequest{ Id, Window.Get() };
	ENQUEUE_RENDER_COMMAND(AdaptivePresentProbe)([Probe = AsShared(), NewRequest](FRHICommandListImmediate&)
	{
		Probe->Pending.Add(NewRequest);
	});
}

void FAdaptivePresentProbe::OnBackBufferReady(SWindow& Window, ISlateViewportProvider& Viewport)
{
	if (Pending.IsEmpty())
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	for (int32 Index = Pending.Num() - 1; Index >= 0; --Index)
	{
		if (!Pending[Index].Window || Pending[Index].Window == &Window)
		{
			Presented.Enqueue({ Pending[Index].Id, Now, GFrameCounterRenderThread });
			Pending.RemoveAtSwap(Index);
		}
	}
}
