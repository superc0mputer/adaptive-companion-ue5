#include "Replay/AdaptiveReplayCommandlet.h"
#include "AdaptiveCompanionFramework.h"
#include "Dom/JsonValue.h"
#include "Logging/AdaptiveLogSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Pipeline/AdaptiveNeedTypes.h"
#include "Pipeline/AdaptivePipelineConfig.h"
#include "Pipeline/AdaptivePipelineSettings.h"
#include "Replay/AdaptiveReplayEvaluation.h"
#include "Replay/AdaptiveReplayRunner.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString ResolveLog(const FString& Name)
	{
		const FString Directory = GetDefault<UAdaptiveLogSettings>()->GetLogDirectoryPath();
		for (const FString& Candidate : { Name, FPaths::Combine(FPaths::ProjectDir(), Name), FPaths::Combine(Directory, Name), FPaths::Combine(Directory, Name + TEXT(".jsonl")) })
		{
			if (FPaths::FileExists(Candidate))
			{
				return FPaths::ConvertRelativePathToFull(Candidate);
			}
		}
		return FString();
	}

	FString ToLine(const TSharedRef<FJsonObject>& Object)
	{
		FString Line;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
		FJsonSerializer::Serialize(Object, Writer);
		return Line;
	}

	TSharedRef<FJsonObject> StepToJson(const FAdaptiveReplayStep& Step)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("u"), double(Step.Index));
		Object->SetNumberField(TEXT("t"), Step.Time);
		TSharedRef<FJsonObject> Needs = MakeShared<FJsonObject>();
		for (const TPair<FName, FAdaptiveNeedState>& Need : Step.Needs)
		{
			TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
			State->SetNumberField(TEXT("score"), FMath::RoundToDouble(Need.Value.Score * 1000.0) / 1000.0);
			State->SetNumberField(TEXT("raw"), FMath::RoundToDouble(Need.Value.RawScore * 1000.0) / 1000.0);
			State->SetStringField(TEXT("level"), AdaptiveNeedMath::LevelName(Need.Value.Level));
			State->SetBoolField(TEXT("active"), Need.Value.bActive);
			State->SetStringField(TEXT("reason"), Need.Value.Reason);
			Needs->SetObjectField(Need.Key.ToString(), State);
		}
		Object->SetObjectField(TEXT("needs"), Needs);
		TArray<TSharedPtr<FJsonValue>> Contexts;
		for (const FName Context : Step.Contexts)
		{
			Contexts.Add(MakeShared<FJsonValueString>(Context.ToString()));
		}
		Object->SetArrayField(TEXT("contexts"), Contexts);
		Object->SetStringField(TEXT("decision"), Step.Intervention.IsNone() ? TEXT("none") : Step.Intervention.ToString());
		if (!Step.Intervention.IsNone())
		{
			Object->SetNumberField(TEXT("id"), Step.InterventionId);
		}
		Object->SetStringField(TEXT("reason"), Step.SelectionReason);
		Object->SetStringField(TEXT("running"), Step.Running.IsNone() ? FString() : Step.Running.ToString());
		return Object;
	}
}

UAdaptiveReplayCommandlet::UAdaptiveReplayCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UAdaptiveReplayCommandlet::Main(const FString& Params)
{
	FString LogName;
	if (!FParse::Value(*Params, TEXT("Log="), LogName))
	{
		UE_LOG(LogAdaptiveCompanionFramework, Error, TEXT("AdaptiveReplay: usage -run=AdaptiveReplay -Log=<session log or id> [-Labels=<file>] [-Out=<folder>]"));
		return 1;
	}

	const FString SessionLogPath = ResolveLog(LogName);
	FAdaptiveSessionRecording Recording;
	FString Error;
	if (SessionLogPath.IsEmpty() || !FAdaptiveSessionRecording::LoadSessionLog(SessionLogPath, Recording, Error))
	{
		UE_LOG(LogAdaptiveCompanionFramework, Error, TEXT("AdaptiveReplay: %s: %s"), *LogName, SessionLogPath.IsEmpty() ? TEXT("not found") : *Error);
		return 1;
	}

	const FString Base = FPaths::GetBaseFilename(SessionLogPath);
	FString LabelsPath;
	if (!FParse::Value(*Params, TEXT("Labels="), LabelsPath))
	{
		const FString Default = FPaths::Combine(FPaths::GetPath(SessionLogPath), Base + TEXT(".labels.json"));
		LabelsPath = FPaths::FileExists(Default) ? Default : FString();
	}
	FAdaptiveReplayLabels Labels;
	const bool bLabels = !LabelsPath.IsEmpty();
	if (bLabels && !FAdaptiveReplayLabels::LoadFromFile(LabelsPath, Labels, Error))
	{
		UE_LOG(LogAdaptiveCompanionFramework, Error, TEXT("AdaptiveReplay: labels %s: %s"), *LabelsPath, *Error);
		return 1;
	}

	const UAdaptivePipelineConfig* PipelineConfig = GetDefault<UAdaptivePipelineSettings>()->LoadConfig();

	// twice, to show that a replay depends only on the recording and the config
	UAdaptiveReplayRunner* Runner = NewObject<UAdaptiveReplayRunner>();
	Runner->AddToRoot();
	Runner->Setup(PipelineConfig);
	const TArray<FAdaptiveReplayStep> Steps = Runner->Run(Recording.Updates);
	const TArray<FString> Warnings = Runner->GetWarnings();
	Runner->Setup(PipelineConfig);
	const bool bDeterministic = Runner->Run(Recording.Updates) == Steps;
	Runner->RemoveFromRoot();

	for (const FString& Warning : Warnings)
	{
		UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("AdaptiveReplay: %s"), *Warning);
	}

	// one update apart at most: the live log stamps a record when it is written, a few µs after the update's time
	const double MaxOffset = Recording.Updates.Num() > 1 ? (Recording.Updates.Last().Time - Recording.Updates[0].Time) / (Recording.Updates.Num() - 1) : 0.1;
	const FAdaptiveLiveComparison Live = FAdaptiveLiveComparison::Compare(Steps, Recording, MaxOffset);
	const FAdaptiveReplayEvaluation Evaluation = FAdaptiveReplayEvaluation::Evaluate(Steps, Labels);

	FString OutDir;
	if (!FParse::Value(*Params, TEXT("Out="), OutDir))
	{
		OutDir = FPaths::GetPath(SessionLogPath);
	}

	TArray<FString> Lines;
	for (const FAdaptiveReplayStep& Step : Steps)
	{
		Lines.Add(ToLine(StepToJson(Step)));
	}
	const FString StepsPath = FPaths::Combine(OutDir, Base + TEXT(".replay.jsonl"));
	FFileHelper::SaveStringArrayToFile(Lines, *StepsPath);

	TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetStringField(TEXT("recording"), SessionLogPath);
	Summary->SetStringField(TEXT("session_id"), Recording.SessionId);
	Summary->SetStringField(TEXT("recorded_config"), Recording.Config);
	Summary->SetStringField(TEXT("replay_config"), PipelineConfig ? PipelineConfig->GetPathName() : TEXT("defaults"));
	Summary->SetNumberField(TEXT("updates"), Steps.Num());
	Summary->SetNumberField(TEXT("duration"), Steps.IsEmpty() ? 0.0 : Steps.Last().Time - Steps[0].Time);
	Summary->SetBoolField(TEXT("deterministic"), bDeterministic);
	TArray<TSharedPtr<FJsonValue>> WarningArray;
	for (const FString& Warning : Warnings)
	{
		WarningArray.Add(MakeShared<FJsonValueString>(Warning));
	}
	Summary->SetArrayField(TEXT("warnings"), WarningArray);
	Summary->SetObjectField(TEXT("live"), Live.ToJson());
	if (bLabels)
	{
		Summary->SetStringField(TEXT("labels_file"), LabelsPath);
		Summary->SetObjectField(TEXT("labels"), Evaluation.ToJson());
	}
	FString SummaryText;
	FJsonSerializer::Serialize(Summary, TJsonWriterFactory<>::Create(&SummaryText));
	const FString SummaryPath = FPaths::Combine(OutDir, Base + TEXT(".replay.json"));
	FFileHelper::SaveStringToFile(SummaryText, *SummaryPath);

	FString Report = FString::Printf(TEXT("AdaptiveReplay: %s, %d updates (%.1f s)\n"), *Base, Steps.Num(), Steps.IsEmpty() ? 0.0 : Steps.Last().Time - Steps[0].Time);
	Report += FString::Printf(TEXT("Deterministic: %s (two runs %s)\n"), bDeterministic ? TEXT("yes") : TEXT("NO"), bDeterministic ? TEXT("identical") : TEXT("differ"));
	Report += FString::Printf(TEXT("Live session: level changes %d of %d matched (replay %d), selections %d of %d matched (replay %d)%s\n"),
		Live.MatchedLevelChanges, Live.LiveLevelChanges, Live.ReplayLevelChanges, Live.MatchedSelections, Live.LiveSelections, Live.ReplaySelections,
		Live.Matches() ? TEXT(": same as live") : TEXT(""));
	for (const FString& Difference : Live.Differences)
	{
		Report += TEXT("  ") + Difference + TEXT("\n");
	}
	if (bLabels)
	{
		Report += Evaluation.ToText();
	}
	Report += FString::Printf(TEXT("Wrote %s and %s"), *StepsPath, *SummaryPath);

	TArray<FString> ReportLines;
	Report.ParseIntoArrayLines(ReportLines);
	for (const FString& Line : ReportLines)
	{
		UE_LOG(LogAdaptiveCompanionFramework, Display, TEXT("%s"), *Line);
	}
	return bDeterministic ? 0 : 1;
}
