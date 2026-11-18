#include "Pipeline/AdaptivePerfMonitor.h"

namespace
{
	double RoundTo(double Value, int32 Decimals)
	{
		const double Factor = FMath::Pow(10.0, Decimals);
		return FMath::RoundToDouble(Value * Factor) / Factor;
	}

	/** Mean only, for the per-frame engine times where the percentiles add little */
	double MeanOf(const TArray<float>& Samples)
	{
		double Sum = 0.0;
		for (const float Sample : Samples)
		{
			Sum += Sample;
		}
		return Samples.IsEmpty() ? 0.0 : Sum / Samples.Num();
	}
}

FAdaptiveStats FAdaptiveStats::Of(TArray<float> Samples)
{
	FAdaptiveStats Stats;
	Stats.Count = Samples.Num();
	if (Samples.IsEmpty())
	{
		return Stats;
	}

	Samples.Sort();
	double Sum = 0.0;
	for (const float Sample : Samples)
	{
		Sum += Sample;
	}
	auto Rank = [&Samples](double Percentile)
	{
		// nearest rank: the smallest sample with at least Percentile of the samples at or below it
		const int32 Index = FMath::Clamp(FMath::CeilToInt32(Percentile * Samples.Num()) - 1, 0, Samples.Num() - 1);
		return double(Samples[Index]);
	};
	Stats.Mean = Sum / Samples.Num();
	Stats.P50 = Rank(0.50);
	Stats.P95 = Rank(0.95);
	Stats.P99 = Rank(0.99);
	Stats.Max = Samples.Last();
	return Stats;
}

TSharedRef<FJsonObject> FAdaptiveStats::ToJson(double Scale, int32 Decimals) const
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("count"), Count);
	Object->SetNumberField(TEXT("mean"), RoundTo(Mean * Scale, Decimals));
	Object->SetNumberField(TEXT("p50"), RoundTo(P50 * Scale, Decimals));
	Object->SetNumberField(TEXT("p95"), RoundTo(P95 * Scale, Decimals));
	Object->SetNumberField(TEXT("p99"), RoundTo(P99 * Scale, Decimals));
	Object->SetNumberField(TEXT("max"), RoundTo(Max * Scale, Decimals));
	return Object;
}

void FAdaptivePerfMonitor::FSamples::Reset(int32 PartCount)
{
	*this = FSamples();
	Parts.SetNum(PartCount);
}

void FAdaptivePerfMonitor::FSamples::AddUpdate(const FAdaptiveUpdateCost& Cost)
{
	Totals.Add(float(Cost.Total));
	UpdateSum += Cost.Total;
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		Parts[Index].Add(Cost.Parts.IsValidIndex(Index) ? float(Cost.Parts[Index]) : 0.f);
	}
	// only updates that had events: the others waited for nothing
	if (Cost.Events > 0)
	{
		InputWaits.Add(float(Cost.InputWait));
	}
	Events += Cost.Events;
	Values += Cost.Values;
	Features += Cost.Features;
	Needs += Cost.Needs;
	Actions += Cost.Actions;
}

void FAdaptivePerfMonitor::FSamples::AddFrame(const FAdaptiveFrameCost& Cost)
{
	Frames.Add(float(Cost.Delta));
	Api.Add(float(Cost.Api));
	ApiSum += Cost.Api;
	ApiCalls += Cost.ApiCalls;
	if (Cost.GameThread > 0.0)
	{
		GameThread.Add(float(Cost.GameThread));
		GameThreadSum += Cost.GameThread;
	}
	if (Cost.RenderThread > 0.0)
	{
		RenderThread.Add(float(Cost.RenderThread));
	}
	if (Cost.Gpu > 0.0)
	{
		Gpu.Add(float(Cost.Gpu));
	}
}

void FAdaptivePerfMonitor::Reset(const TArray<FString>& InPartNames)
{
	PartNames = InPartNames;
	Window.Reset(PartNames.Num());
	Session.Reset(PartNames.Num());
	FrameApi = 0.0;
	FrameApiCalls = 0;
	OverlayText.Reset();
}

void FAdaptivePerfMonitor::AddUpdate(const FAdaptiveUpdateCost& Cost)
{
	Window.AddUpdate(Cost);
	Session.AddUpdate(Cost);
}

void FAdaptivePerfMonitor::EndFrame(double Delta, double GameThread, double RenderThread, double Gpu)
{
	FAdaptiveFrameCost Cost;
	Cost.Delta = Delta;
	Cost.Api = FrameApi;
	Cost.ApiCalls = FrameApiCalls;
	Cost.GameThread = GameThread;
	Cost.RenderThread = RenderThread;
	Cost.Gpu = Gpu;
	Window.AddFrame(Cost);
	Session.AddFrame(Cost);
	FrameApi = 0.0;
	FrameApiCalls = 0;
}

TSharedRef<FJsonObject> FAdaptivePerfMonitor::Summarize(const FSamples& Samples, double Duration) const
{
	constexpr double Us = 1e6;
	constexpr double Ms = 1e3;

	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("duration"), RoundTo(Duration, 3));
	Object->SetNumberField(TEXT("updates"), Samples.Totals.Num());
	Object->SetNumberField(TEXT("frames"), Samples.Frames.Num());

	// what an update costs on the game thread, in µs
	Object->SetObjectField(TEXT("update_us"), FAdaptiveStats::Of(Samples.Totals).ToJson(Us, 1));
	TSharedRef<FJsonObject> Parts = MakeShared<FJsonObject>();
	for (int32 Index = 0; Index < PartNames.Num() && Index < Samples.Parts.Num(); ++Index)
	{
		Parts->SetObjectField(PartNames[Index], FAdaptiveStats::Of(Samples.Parts[Index]).ToJson(Us, 1));
	}
	Object->SetObjectField(TEXT("parts_us"), Parts);

	// the game's calls into the API between updates, per frame
	Object->SetObjectField(TEXT("api_us_per_frame"), FAdaptiveStats::Of(Samples.Api).ToJson(Us, 1));
	Object->SetNumberField(TEXT("api_calls_per_frame"), Samples.Frames.IsEmpty() ? 0.0 : RoundTo(double(Samples.ApiCalls) / Samples.Frames.Num(), 2));

	// how long telemetry waits for the next update: the fixed rate's share of the latency
	Object->SetObjectField(TEXT("input_wait_ms"), FAdaptiveStats::Of(Samples.InputWaits).ToJson(Ms, 2));

	// the load the costs belong to, per update
	const double Updates = FMath::Max(1, Samples.Totals.Num());
	TSharedRef<FJsonObject> Load = MakeShared<FJsonObject>();
	Load->SetNumberField(TEXT("events"), RoundTo(Samples.Events / Updates, 2));
	Load->SetNumberField(TEXT("values"), RoundTo(Samples.Values / Updates, 2));
	Load->SetNumberField(TEXT("features"), RoundTo(Samples.Features / Updates, 2));
	Load->SetNumberField(TEXT("needs"), RoundTo(Samples.Needs / Updates, 2));
	Load->SetNumberField(TEXT("actions"), RoundTo(Samples.Actions / Updates, 2));
	Object->SetObjectField(TEXT("load_per_update"), Load);

	// the frame around it
	Object->SetObjectField(TEXT("frame_ms"), FAdaptiveStats::Of(Samples.Frames).ToJson(Ms, 2));
	Object->SetNumberField(TEXT("game_thread_ms"), RoundTo(MeanOf(Samples.GameThread) * Ms, 3));
	Object->SetNumberField(TEXT("render_thread_ms"), RoundTo(MeanOf(Samples.RenderThread) * Ms, 3));
	Object->SetNumberField(TEXT("gpu_ms"), RoundTo(MeanOf(Samples.Gpu) * Ms, 3));

	// the pipeline's share: updates plus API time, per frame and against the game thread's busy time
	const double PipelineSum = Samples.UpdateSum + Samples.ApiSum;
	Object->SetNumberField(TEXT("pipeline_us_per_frame"), Samples.Frames.IsEmpty() ? 0.0 : RoundTo(PipelineSum / Samples.Frames.Num() * Us, 2));
	Object->SetNumberField(TEXT("game_thread_share_pct"), Samples.GameThreadSum > 0.0 ? RoundTo(PipelineSum / Samples.GameThreadSum * 100.0, 4) : 0.0);
	return Object;
}

TSharedRef<FJsonObject> FAdaptivePerfMonitor::TakeWindow(double Duration)
{
	TSharedRef<FJsonObject> Summary = Summarize(Window, Duration);

	const FAdaptiveStats Updates = FAdaptiveStats::Of(Window.Totals);
	const double PerFrame = Window.Frames.IsEmpty() ? 0.0 : (Window.UpdateSum + Window.ApiSum) / Window.Frames.Num();
	const double GameThread = MeanOf(Window.GameThread);
	OverlayText = FString::Printf(TEXT("update %.0f µs mean, %.0f µs p95, %.0f µs max · %.1f µs/frame (%.3f %% of game thread %.2f ms) · frame %.2f ms"),
		Updates.Mean * 1e6, Updates.P95 * 1e6, Updates.Max * 1e6, PerFrame * 1e6,
		GameThread > 0.0 ? PerFrame / GameThread * 100.0 : 0.0, GameThread * 1e3, MeanOf(Window.Frames) * 1e3);

	Window.Reset(PartNames.Num());
	return Summary;
}

TSharedRef<FJsonObject> FAdaptivePerfMonitor::GetSummary(double Duration) const
{
	return Summarize(Session, Duration);
}
