#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AdaptiveMeasurementCommandlets.generated.h"

/**
 *  Exports the measurements of session logs as tables (#25):
 *
 *    UnrealEditor-Cmd AdaptiveCompanion.uproject -run=AdaptiveMeasurements -Logs=<log, folder or session id>[,...] [-Out=<folder>]
 *
 *  Reads the latency, perf and perf_summary records and writes latency.csv (one row per
 *  intervention), latency_summary.csv (per action), perf.csv (one row per perf window),
 *  perf_summary.csv (one row per session) and measurements.md (the summaries as Markdown) into
 *  -Out, by default Saved/AdaptiveMeasurements.
 */
UCLASS()
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveMeasurementsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:

	UAdaptiveMeasurementsCommandlet();

	virtual int32 Main(const FString& Params) override;
};

/**
 *  Measures the pipeline's cost under growing load, without the game (#25):
 *
 *    UnrealEditor-Cmd AdaptiveCompanion.uproject -run=AdaptiveBenchmark [-Log=<recording>] [-Copies=1,2,5,10,20,50] [-Repeat=5] [-Out=<folder>]
 *
 *  Replays a recorded session (default Recordings/arena-pressure-wave.jsonl) with the current
 *  config, once per entry of -Copies: with k copies the features and need dimensions run k times,
 *  each on its own copy of the telemetry, so k = 10 is ten times this game's load. Each is run
 *  -Repeat times after one warm-up run and every update's cost per stage is measured. Writes
 *  benchmark.csv and benchmark.md into -Out, by default Saved/AdaptiveMeasurements. Logging is
 *  off in a replay, so these are the stages' own costs; the in-game perf records include writing
 *  the log.
 */
UCLASS()
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveBenchmarkCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:

	UAdaptiveBenchmarkCommandlet();

	virtual int32 Main(const FString& Params) override;
};
