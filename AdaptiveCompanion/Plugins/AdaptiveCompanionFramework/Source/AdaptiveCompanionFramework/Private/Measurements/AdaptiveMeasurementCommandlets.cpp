#include "Measurements/AdaptiveMeasurementCommandlets.h"
#include "AdaptiveCompanionFramework.h"
#include "HAL/FileManager.h"
#include "Logging/AdaptiveLogSettings.h"
#include "Measurements/AdaptiveMeasurementTables.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Pipeline/AdaptivePerfMonitor.h"
#include "Pipeline/AdaptivePipelineConfig.h"
#include "Pipeline/AdaptivePipelineSettings.h"
#include "Replay/AdaptiveReplayCommandlet.h"
#include "Replay/AdaptiveReplayEvaluation.h"
#include "Replay/AdaptiveReplayRunner.h"

namespace
{
	FString DefaultOutDir()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AdaptiveMeasurements")));
	}

	bool Save(const FString& Text, const FString& Path)
	{
		const bool bSaved = FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		if (!bSaved)
		{
			UE_LOG(LogAdaptiveCompanionFramework, Error, TEXT("Couldn't write %s"), *Path);
		}
		return bSaved;
	}

	void LogLines(const FString& Text)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			UE_LOG(LogAdaptiveCompanionFramework, Display, TEXT("%s"), *Line);
		}
	}

	/** Session logs named by -Logs: files, folders (every .jsonl in them, not replay output) or session ids */
	TArray<FString> FindLogs(const FString& Spec)
	{
		TArray<FString> Logs;
		TArray<FString> Parts;
		Spec.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			const FString Trimmed = Part.TrimStartAndEnd();
			bool bFolder = false;
			for (const FString& Folder : { Trimmed, FPaths::Combine(FPaths::ProjectDir(), Trimmed), FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), Trimmed) })
			{
				if (!IFileManager::Get().DirectoryExists(*Folder))
				{
					continue;
				}
				TArray<FString> Files;
				IFileManager::Get().FindFiles(Files, *FPaths::Combine(Folder, TEXT("*.jsonl")), true, false);
				Files.Sort();
				for (const FString& File : Files)
				{
					if (!File.EndsWith(TEXT(".replay.jsonl")))
					{
						Logs.AddUnique(FPaths::ConvertRelativePathToFull(FPaths::Combine(Folder, File)));
					}
				}
				bFolder = true;
				break;
			}
			if (bFolder)
			{
				continue;
			}

			const FString File = UAdaptiveReplayCommandlet::ResolveLog(Trimmed);
			if (File.IsEmpty())
			{
				UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("No session log %s"), *Trimmed);
			}
			else
			{
				Logs.AddUnique(File);
			}
		}
		return Logs;
	}

	const TCHAR* BuildConfigName()
	{
#if UE_BUILD_SHIPPING
		return TEXT("Shipping");
#elif UE_BUILD_DEVELOPMENT
		return TEXT("Development");
#else
		return TEXT("Debug");
#endif
	}
}

UAdaptiveMeasurementsCommandlet::UAdaptiveMeasurementsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UAdaptiveMeasurementsCommandlet::Main(const FString& Params)
{
	FString Spec;
	if (!FParse::Value(*Params, TEXT("Logs="), Spec, false))
	{
		UE_LOG(LogAdaptiveCompanionFramework, Error, TEXT("AdaptiveMeasurements: usage -run=AdaptiveMeasurements -Logs=<log, folder or session id>[,...] [-Out=<folder>]"));
		return 1;
	}

	FAdaptiveMeasurementTables Tables;
	for (const FString& Log : FindLogs(Spec))
	{
		FString Error;
		if (!Tables.LoadSessionLog(Log, Error))
		{
			UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("AdaptiveMeasurements: %s: %s"), *Log, *Error);
		}
	}
	if (Tables.GetSessionCount() == 0)
	{
		UE_LOG(LogAdaptiveCompanionFramework, Error, TEXT("AdaptiveMeasurements: no session logs found for %s"), *Spec);
		return 1;
	}

	FString OutDir;
	if (!FParse::Value(*Params, TEXT("Out="), OutDir))
	{
		OutDir = DefaultOutDir();
	}
	IFileManager::Get().MakeDirectory(*OutDir, true);

	const FAdaptiveTable Latency = Tables.GetLatencyTable();
	const FAdaptiveTable LatencySummary = Tables.GetLatencySummary();
	const FAdaptiveTable Perf = Tables.GetPerfWindowTable();
	const FAdaptiveTable PerfSummary = Tables.GetPerfSummaryTable();

	bool bSaved = Save(Latency.ToCsv(), FPaths::Combine(OutDir, TEXT("latency.csv")));
	bSaved &= Save(LatencySummary.ToCsv(), FPaths::Combine(OutDir, TEXT("latency_summary.csv")));
	bSaved &= Save(Perf.ToCsv(), FPaths::Combine(OutDir, TEXT("perf.csv")));
	bSaved &= Save(PerfSummary.ToCsv(), FPaths::Combine(OutDir, TEXT("perf_summary.csv")));

	FString Markdown = FString::Printf(TEXT("# Measurements\n\n%d session logs, %d interventions.\n\n## Latency\n\n"), Tables.GetSessionCount(), Tables.GetLatencyCount());
	Markdown += LatencySummary.ToMarkdown();
	Markdown += TEXT("\n## Cost per session\n\n");
	Markdown += PerfSummary.ToMarkdown();
	bSaved &= Save(Markdown, FPaths::Combine(OutDir, TEXT("measurements.md")));

	LogLines(Markdown);
	UE_LOG(LogAdaptiveCompanionFramework, Display, TEXT("AdaptiveMeasurements: wrote latency.csv, latency_summary.csv, perf.csv, perf_summary.csv and measurements.md to %s"), *OutDir);
	return bSaved ? 0 : 1;
}

UAdaptiveBenchmarkCommandlet::UAdaptiveBenchmarkCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UAdaptiveBenchmarkCommandlet::Main(const FString& Params)
{
	FString LogName = TEXT("Recordings/arena-pressure-wave.jsonl");
	FParse::Value(*Params, TEXT("Log="), LogName);
	const FString RecordingPath = UAdaptiveReplayCommandlet::ResolveLog(LogName);
	FAdaptiveSessionRecording Recording;
	FString Error;
	if (RecordingPath.IsEmpty() || !FAdaptiveSessionRecording::LoadSessionLog(RecordingPath, Recording, Error))
	{
		UE_LOG(LogAdaptiveCompanionFramework, Error, TEXT("AdaptiveBenchmark: %s: %s"), *LogName, RecordingPath.IsEmpty() ? TEXT("not found") : *Error);
		return 1;
	}

	FString CopiesSpec = TEXT("1,2,5,10,20,50");
	FParse::Value(*Params, TEXT("Copies="), CopiesSpec, false);
	TArray<FString> CopiesParts;
	CopiesSpec.ParseIntoArray(CopiesParts, TEXT(","));
	TArray<int32> CopiesList;
	for (const FString& Part : CopiesParts)
	{
		CopiesList.Add(FMath::Max(1, FCString::Atoi(*Part)));
	}
	int32 Repeat = 5;
	FParse::Value(*Params, TEXT("Repeat="), Repeat);
	Repeat = FMath::Max(1, Repeat);

	const UAdaptivePipelineConfig* Config = GetDefault<UAdaptivePipelineSettings>()->LoadConfig();
	UAdaptiveReplayRunner* Runner = NewObject<UAdaptiveReplayRunner>();
	Runner->AddToRoot();
	Runner->bMeasure = true;

	FAdaptiveTable Table;
	for (const int32 Copies : CopiesList)
	{
		TArray<float> Totals;
		TArray<TArray<float>> Parts;
		TArray<FString> StageNames;
		int64 Events = 0;
		int64 Values = 0;
		int32 Features = 0;
		int32 Dimensions = 0;

		// one warm-up run, so caches and FName tables are filled
		for (int32 Run = 0; Run <= Repeat; ++Run)
		{
			Runner->Setup(Config, Copies);
			Runner->Run(Recording.Updates);
			if (Run == 0)
			{
				StageNames = Runner->GetStageNames();
				Parts.SetNum(StageNames.Num());
				Features = Runner->GetFeatureCount();
				Dimensions = Runner->GetDimensionCount();
				continue;
			}
			for (const FAdaptiveUpdateCost& Cost : Runner->GetCosts())
			{
				Totals.Add(float(Cost.Total));
				for (int32 Index = 0; Index < Parts.Num() && Index < Cost.Parts.Num(); ++Index)
				{
					Parts[Index].Add(float(Cost.Parts[Index]));
				}
				Events += Cost.Events;
				Values += Cost.Values;
			}
		}

		const FAdaptiveStats Stats = FAdaptiveStats::Of(Totals);
		const double Updates = FMath::Max(1, Totals.Num());
		TArray<TPair<FString, FString>> Cells;
		Cells.Add({ TEXT("copies"), FString::FromInt(Copies) });
		Cells.Add({ TEXT("features"), FString::FromInt(Features) });
		Cells.Add({ TEXT("need dimensions"), FString::FromInt(Dimensions) });
		Cells.Add({ TEXT("events/update"), FAdaptiveTable::Number(Events / Updates, 2) });
		Cells.Add({ TEXT("values/update"), FAdaptiveTable::Number(Values / Updates, 1) });
		Cells.Add({ TEXT("updates"), FString::FromInt(Totals.Num()) });
		Cells.Add({ TEXT("mean µs"), FAdaptiveTable::Number(Stats.Mean * 1e6, 1) });
		Cells.Add({ TEXT("p50 µs"), FAdaptiveTable::Number(Stats.P50 * 1e6, 1) });
		Cells.Add({ TEXT("p95 µs"), FAdaptiveTable::Number(Stats.P95 * 1e6, 1) });
		Cells.Add({ TEXT("p99 µs"), FAdaptiveTable::Number(Stats.P99 * 1e6, 1) });
		Cells.Add({ TEXT("max µs"), FAdaptiveTable::Number(Stats.Max * 1e6, 1) });
		for (int32 Index = 0; Index < StageNames.Num(); ++Index)
		{
			Cells.Add({ StageNames[Index] + TEXT(" mean µs"), FAdaptiveTable::Number(FAdaptiveStats::Of(Parts[Index]).Mean * 1e6, 1) });
		}
		Table.AddRow(Cells);
		UE_LOG(LogAdaptiveCompanionFramework, Display, TEXT("AdaptiveBenchmark: %d copies (%d features, %d dimensions): %.1f µs mean, %.1f µs p95, %.1f µs max per update"),
			Copies, Features, Dimensions, Stats.Mean * 1e6, Stats.P95 * 1e6, Stats.Max * 1e6);
	}
	Runner->RemoveFromRoot();

	FString OutDir;
	if (!FParse::Value(*Params, TEXT("Out="), OutDir))
	{
		OutDir = DefaultOutDir();
	}
	IFileManager::Get().MakeDirectory(*OutDir, true);

	const FString Markdown = FString::Printf(TEXT("# Benchmark\n\nRecording %s (%d updates), config %s, %d runs per row after one warm-up, %s build.\n\n"),
		*FPaths::GetCleanFilename(RecordingPath), Recording.Updates.Num(), Config ? *Config->GetPathName() : TEXT("defaults"), Repeat, BuildConfigName()) + Table.ToMarkdown();

	bool bSaved = Save(Table.ToCsv(), FPaths::Combine(OutDir, TEXT("benchmark.csv")));
	bSaved &= Save(Markdown, FPaths::Combine(OutDir, TEXT("benchmark.md")));
	LogLines(Markdown);
	UE_LOG(LogAdaptiveCompanionFramework, Display, TEXT("AdaptiveBenchmark: wrote benchmark.csv and benchmark.md to %s"), *OutDir);
	return bSaved ? 0 : 1;
}
