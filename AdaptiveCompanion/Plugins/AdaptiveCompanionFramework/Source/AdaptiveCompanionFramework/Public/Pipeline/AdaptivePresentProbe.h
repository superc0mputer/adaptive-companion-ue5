#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Templates/SharedPointer.h"

class ISlateViewportProvider;
class SWindow;
class UGameInstance;

/**
 *  Finds out when something the game did shows up on screen (#25). Request(Id) on the game thread
 *  marks the current frame; the probe reports the time the render thread had that frame's game
 *  window ready to present (Slate's OnBackBufferReadyToPresent, right before the present call).
 *
 *  Render commands run in the order they are enqueued, and a frame's window is drawn after
 *  everything the game enqueued during its tick, so the first present after the request's command
 *  is the frame that contains the game's change. GPU execution and display scan-out come after
 *  this point and are not included.
 *
 *  Only exists where something is rendered: Create returns null in commandlets, with -nullrhi and
 *  without a game viewport.
 */
class ADAPTIVECOMPANIONFRAMEWORK_API FAdaptivePresentProbe : public TSharedFromThis<FAdaptivePresentProbe, ESPMode::ThreadSafe>
{
public:

	struct FPresent
	{
		int32 Id = 0;

		/** FPlatformTime::Seconds() on the render thread */
		double Seconds = 0.0;

		/** GFrameCounterRenderThread of the presented frame */
		uint64 Frame = 0;
	};

	/** Null if nothing is rendered */
	static TSharedPtr<FAdaptivePresentProbe, ESPMode::ThreadSafe> Create(UGameInstance* GameInstance);

	~FAdaptivePresentProbe();

	/** Game thread: report when the frame drawn now is ready to present */
	void Request(int32 Id);

	/** Game thread: the next reported frame, false if none is waiting */
	bool Poll(FPresent& OutPresent) { return Presented.Dequeue(OutPresent); }

	/** Game thread: stop listening, before the game instance goes away */
	void Shutdown();

private:

	struct FRequest
	{
		int32 Id = 0;

		/** Only compared, never dereferenced on the render thread */
		const SWindow* Window = nullptr;
	};

	void OnBackBufferReady(SWindow& Window, ISlateViewportProvider& Viewport);

	TWeakObjectPtr<UGameInstance> GameInstance;
	FDelegateHandle Handle;

	/** Render thread only */
	TArray<FRequest> Pending;

	TQueue<FPresent, EQueueMode::Mpsc> Presented;
};
