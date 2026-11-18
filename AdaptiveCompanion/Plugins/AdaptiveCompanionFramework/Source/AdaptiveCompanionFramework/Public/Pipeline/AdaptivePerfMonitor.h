#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Summary of a set of samples. Percentiles by nearest rank, so they are always one of the samples */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveStats
{
	int32 Count = 0;
	double Mean = 0.0;
	double P50 = 0.0;
	double P95 = 0.0;
	double P99 = 0.0;
	double Max = 0.0;

	static FAdaptiveStats Of(TArray<float> Samples);

	/** {count, mean, p50, p95, p99, max}, each value × Scale and rounded to Decimals */
	TSharedRef<FJsonObject> ToJson(double Scale, int32 Decimals) const;
};

/** One pipeline update's cost, in seconds */
struct FAdaptiveUpdateCost
{
	double Total = 0.0;

	/** Per stage in update order, then the input recording */
	TArray<double> Parts;

	/** The longest an event of this update waited for it (update time - report time); only counted for updates with events */
	double InputWait = 0.0;

	int32 Events = 0;
	int32 Values = 0;
	int32 Features = 0;
	int32 Needs = 0;
	int32 Actions = 0;
};

/** One frame's cost, in seconds */
struct FAdaptiveFrameCost
{
	double Delta = 0.0;

	/** Time spent in the pipeline's API (ReportEvent, ReportValue, SetContext, ...) outside updates */
	double Api = 0.0;
	int32 ApiCalls = 0;

	/** Engine thread times of the latest finished frame (GGameThreadTime, GRenderThreadTime, RHIGetGPUFrameCycles); 0 if unknown */
	double GameThread = 0.0;
	double RenderThread = 0.0;
	double Gpu = 0.0;
};

/**
 *  Measures what the pipeline costs (#25): every update's total and per-stage time, the time the
 *  game spends calling its API, and the frame times around it. Summaries per window ("perf"
 *  records) and over the whole session ("perf_summary"). Plain C++, so it can be tested without a
 *  game.
 */
class ADAPTIVECOMPANIONFRAMEWORK_API FAdaptivePerfMonitor
{
public:

	/** Names of FAdaptiveUpdateCost::Parts, e.g. the stage classes and "Recording" */
	void Reset(const TArray<FString>& InPartNames);

	void AddUpdate(const FAdaptiveUpdateCost& Cost);

	/** API time of the frame in progress */
	void AddApi(double Seconds)
	{
		FrameApi += Seconds;
		++FrameApiCalls;
	}

	/** Ends the frame in progress, with its API time */
	void EndFrame(double Delta, double GameThread, double RenderThread, double Gpu);

	/** Summary of everything since the previous window, then starts a new one */
	TSharedRef<FJsonObject> TakeWindow(double Duration);

	/** Summary of the whole session */
	TSharedRef<FJsonObject> GetSummary(double Duration) const;

	/** One line for the overlay, from the latest window */
	const FString& GetOverlayText() const { return OverlayText; }

	int32 GetSessionUpdates() const { return Session.Totals.Num(); }

private:

	struct FSamples
	{
		/** Per update, in seconds */
		TArray<float> Totals;
		TArray<TArray<float>> Parts;
		TArray<float> InputWaits;
		int64 Events = 0;
		int64 Values = 0;
		int64 Features = 0;
		int64 Needs = 0;
		int64 Actions = 0;
		double UpdateSum = 0.0;

		/** Per frame, in seconds */
		TArray<float> Frames;
		TArray<float> Api;
		TArray<float> GameThread;
		TArray<float> RenderThread;
		TArray<float> Gpu;
		int64 ApiCalls = 0;
		double ApiSum = 0.0;
		double GameThreadSum = 0.0;

		void Reset(int32 PartCount);
		void AddUpdate(const FAdaptiveUpdateCost& Cost);
		void AddFrame(const FAdaptiveFrameCost& Cost);
	};

	TSharedRef<FJsonObject> Summarize(const FSamples& Samples, double Duration) const;

	TArray<FString> PartNames;
	FSamples Window;
	FSamples Session;

	double FrameApi = 0.0;
	int32 FrameApiCalls = 0;

	FString OverlayText;
};
