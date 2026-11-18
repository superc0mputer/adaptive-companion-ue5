#include "Pipeline/AdaptiveLatencyTracker.h"

void FAdaptiveLatencyTracker::Start(const FAdaptiveLatency& Latency)
{
	Open.Add(Latency);
}

int32 FAdaptiveLatencyTracker::OnStarted(FName Action, double Time, uint64 Frame, bool bWaitForPresent)
{
	// the latest of that action that hasn't started yet
	for (int32 Index = Open.Num() - 1; Index >= 0; --Index)
	{
		FAdaptiveLatency& Latency = Open[Index];
		if (Latency.Action != Action || Latency.Started >= 0.0)
		{
			continue;
		}

		Latency.Started = Time;
		Latency.StartedFrame = Frame;
		if (bWaitForPresent)
		{
			return Latency.Id;
		}

		Latency.Incomplete = TEXT("not presented");
		Finished.Add(MoveTemp(Latency));
		Open.RemoveAt(Index);
		return 0;
	}
	return 0;
}

void FAdaptiveLatencyTracker::OnPresented(int32 Id, double Time, uint64 Frame)
{
	const int32 Index = Open.IndexOfByPredicate([Id](const FAdaptiveLatency& Latency) { return Latency.Id == Id; });
	if (Index == INDEX_NONE)
	{
		return;
	}

	Open[Index].Presented = Time;
	Open[Index].PresentedFrame = Frame;
	Finished.Add(MoveTemp(Open[Index]));
	Open.RemoveAt(Index);
}

void FAdaptiveLatencyTracker::OnEnded(FName Action, FName Phase)
{
	// one that started still gets its frame: the cue was spawned, even if it is gone again by then
	for (int32 Index = Open.Num() - 1; Index >= 0; --Index)
	{
		if (Open[Index].Action == Action && Open[Index].Started < 0.0)
		{
			Open[Index].Incomplete = Phase.ToString();
			Finished.Add(MoveTemp(Open[Index]));
			Open.RemoveAt(Index);
		}
	}
}

void FAdaptiveLatencyTracker::Update(double Now)
{
	for (int32 Index = Open.Num() - 1; Index >= 0; --Index)
	{
		if (Open[Index].Started >= 0.0 && Now - Open[Index].Started > PresentTimeout)
		{
			Open[Index].Incomplete = TEXT("not presented");
			Finished.Add(MoveTemp(Open[Index]));
			Open.RemoveAt(Index);
		}
	}
}

TArray<FAdaptiveLatency> FAdaptiveLatencyTracker::TakeFinished()
{
	Finished.Sort([](const FAdaptiveLatency& A, const FAdaptiveLatency& B) { return A.Id < B.Id; });
	return MoveTemp(Finished);
}

TArray<FAdaptiveLatency> FAdaptiveLatencyTracker::Flush(const FString& Reason)
{
	for (FAdaptiveLatency& Latency : Open)
	{
		Latency.Incomplete = Reason;
		Finished.Add(MoveTemp(Latency));
	}
	Open.Reset();
	return TakeFinished();
}
