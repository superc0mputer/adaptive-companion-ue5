#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** A table of strings with named columns; an empty cell is a missing value */
struct ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveTable
{
	TArray<FString> Columns;
	TArray<TArray<FString>> Rows;

	/** Adds a row, filling the cells by column name; unknown names are added as columns */
	void AddRow(const TArray<TPair<FString, FString>>& Cells);

	FString ToCsv() const;
	FString ToMarkdown() const;

	/** Value rounded to Decimals, without trailing zeros */
	static FString Number(double Value, int32 Decimals = 3);
};

/**
 *  Turns the measurement records of session logs (#25) into tables: one row per intervention
 *  (latency), a latency summary per action, one row per perf window and one per session.
 */
class ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveMeasurementTables
{
public:

	/** Reads the latency, perf and perf_summary records of one session log */
	bool LoadSessionLog(const FString& Path, FString& OutError);

	/** One record; Session names the file it came from */
	void AddRecord(const FString& Session, const FString& Type, double Time, const TSharedPtr<FJsonObject>& Payload);

	FAdaptiveTable GetLatencyTable() const;

	/** Per action and over all: how many reached the screen, and median, p95 and max of the spans */
	FAdaptiveTable GetLatencySummary() const;

	FAdaptiveTable GetPerfWindowTable() const;
	FAdaptiveTable GetPerfSummaryTable() const;

	int32 GetSessionCount() const { return Sessions.Num(); }
	int32 GetLatencyCount() const { return Latencies.Num(); }

private:

	struct FRecord
	{
		FString Session;
		double Time = 0.0;
		TSharedPtr<FJsonObject> Payload;
	};

	static void AddPerfCells(const FJsonObject& Payload, TArray<TPair<FString, FString>>& Cells, bool bFull);

	TArray<FString> Sessions;
	TArray<FRecord> Latencies;
	TArray<FRecord> PerfWindows;
	TArray<FRecord> PerfSummaries;
};
