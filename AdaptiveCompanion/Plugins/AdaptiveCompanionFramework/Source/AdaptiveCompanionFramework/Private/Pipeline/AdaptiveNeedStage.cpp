#include "Pipeline/AdaptiveNeedStage.h"
#include "AdaptiveCompanionFramework.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Dom/JsonObject.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"
#include "Pipeline/AdaptiveFeatureTypes.h"
#include "Pipeline/AdaptiveNeedEstimator.h"

namespace
{
	TSharedRef<FJsonObject> LevelSettingsToJson(const FAdaptiveNeedLevelSettings& Settings)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("rise_half_life"), Settings.RiseHalfLife);
		Object->SetNumberField(TEXT("fall_half_life"), Settings.FallHalfLife);
		Object->SetNumberField(TEXT("elevated_enter"), Settings.ElevatedEnter);
		Object->SetNumberField(TEXT("elevated_exit"), Settings.ElevatedExit);
		Object->SetNumberField(TEXT("high_enter"), Settings.HighEnter);
		Object->SetNumberField(TEXT("high_exit"), Settings.HighExit);
		return Object;
	}
}

UAdaptiveNeedStage::UAdaptiveNeedStage()
{
	Estimator = CreateDefaultSubobject<UAdaptiveRuleNeedEstimator>(TEXT("Estimator"));
}

const FAdaptiveNeedLevelSettings& UAdaptiveNeedStage::GetLevelSettings(FName Dimension) const
{
	const FAdaptiveNeedLevelSettings* Override = DimensionLevels.Find(Dimension);
	return Override ? *Override : Levels;
}

void UAdaptiveNeedStage::Initialize(UAdaptivePipelineSubsystem& InPipeline)
{
	Super::Initialize(InPipeline);

	if (!Estimator)
	{
		UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("The Need stage has no estimator, so no needs are estimated."));
	}

	UAdaptiveLogSubsystem* Log = GetLog();
	if (!Log)
	{
		return;
	}

	TSharedPtr<FJsonObject> Payload = Estimator ? Estimator->Describe() : nullptr;
	if (!Payload)
	{
		Payload = MakeShared<FJsonObject>();
	}
	Payload->SetStringField(TEXT("estimator"), Estimator ? Estimator->GetClass()->GetName() : TEXT("none"));
	Payload->SetObjectField(TEXT("levels"), LevelSettingsToJson(Levels));

	TSharedRef<FJsonObject> Overrides = MakeShared<FJsonObject>();
	for (const TPair<FName, FAdaptiveNeedLevelSettings>& Pair : DimensionLevels)
	{
		Overrides->SetObjectField(Pair.Key.ToString(), LevelSettingsToJson(Pair.Value));
	}
	Payload->SetObjectField(TEXT("dimension_levels"), Overrides);

	Log->LogRecord(AdaptiveLogRecordType::NeedDefinitions, Payload.ToSharedRef());
}

void UAdaptiveNeedStage::Update(FAdaptivePipelineFrame& Frame)
{
	// from the frame times rather than Frame.DeltaTime, so smoothing also works on frames built by hand (tests)
	const double DeltaTime = PreviousTime.IsSet() ? FMath::Max(Frame.Time - *PreviousTime, 0.0) : 0.0;
	PreviousTime = Frame.Time;

	if (!Estimator)
	{
		return;
	}

	TArray<FAdaptiveNeedEstimate> Estimates;
	Estimator->Estimate(Frame, Estimates);

	UAdaptiveLogSubsystem* Log = GetLog();
	UAdaptiveDebugSubsystem* Debug = GetDebug();

	for (const FAdaptiveNeedEstimate& Estimate : Estimates)
	{
		FDimensionState& DimensionState = States.FindOrAdd(Estimate.Dimension);
		const FAdaptiveNeedLevelSettings& Settings = GetLevelSettings(Estimate.Dimension);

		FAdaptiveNeedState State;
		State.RawScore = FMath::Clamp(Estimate.Score, 0.0, 1.0);
		State.bActive = Estimate.bActive;
		State.Reason = Estimate.Reason;

		// an inactive dimension decays toward 0, but its help doesn't apply, so it is Normal at once
		const double Target = State.bActive ? State.RawScore : 0.0;
		DimensionState.Smoothed = AdaptiveNeedMath::Smooth(DimensionState.Smoothed, Target, DeltaTime, Settings.RiseHalfLife, Settings.FallHalfLife);
		State.Score = DimensionState.Smoothed;
		State.Level = State.bActive ? AdaptiveNeedMath::NextLevel(DimensionState.Level, State.Score, Settings) : EAdaptiveNeedLevel::Normal;
		State.bChanged = State.Level != DimensionState.Level;
		Frame.Needs.Add(Estimate.Dimension, State);

		const TCHAR* LevelName = AdaptiveNeedMath::LevelName(State.Level);

		const double LoggedScore = AdaptiveFeatureMath::RoundTo(State.Score, LogStep);
		const double LoggedRaw = AdaptiveFeatureMath::RoundTo(State.RawScore, LogStep);
		const bool bLogNeed = !DimensionState.LoggedScore.IsSet() || *DimensionState.LoggedScore != LoggedScore
			|| *DimensionState.LoggedRaw != LoggedRaw || DimensionState.LoggedReason != State.Reason || DimensionState.bLoggedActive != State.bActive;
		if (bLogNeed)
		{
			DimensionState.LoggedScore = LoggedScore;
			DimensionState.LoggedRaw = LoggedRaw;
			DimensionState.LoggedReason = State.Reason;
			DimensionState.bLoggedActive = State.bActive;

			if (Log)
			{
				TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
				Payload->SetStringField(TEXT("need"), Estimate.Dimension.ToString());
				Payload->SetNumberField(TEXT("score"), LoggedScore);
				Payload->SetNumberField(TEXT("raw"), LoggedRaw);
				Payload->SetBoolField(TEXT("active"), State.bActive);
				Payload->SetStringField(TEXT("reason"), State.Reason);
				Log->LogRecord(AdaptiveLogRecordType::Need, Payload);
			}
		}

		if (State.bChanged)
		{
			if (Log)
			{
				TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
				Payload->SetStringField(TEXT("need"), Estimate.Dimension.ToString());
				Payload->SetStringField(TEXT("level"), LevelName);
				Payload->SetStringField(TEXT("previous"), AdaptiveNeedMath::LevelName(DimensionState.Level));
				Payload->SetNumberField(TEXT("score"), LoggedScore);
				Payload->SetNumberField(TEXT("raw"), LoggedRaw);
				Payload->SetBoolField(TEXT("active"), State.bActive);
				Payload->SetStringField(TEXT("reason"), State.Reason);
				Log->LogRecord(AdaptiveLogRecordType::State, Payload);
			}

			DimensionState.Level = State.Level;
		}

		if (Debug)
		{
			const FString Name = Estimate.Dimension.ToString();
			Debug->SetBar(EAdaptiveDebugSection::Need, Name, State.Score);
			Debug->SetText(EAdaptiveDebugSection::Need, Name + TEXT(" why"),
				!State.bActive ? FString::Printf(TEXT("%s, inactive in this context"), LevelName)
				: FString::Printf(TEXT("%s, raw %.2f, %s"), LevelName, State.RawScore, State.Reason.IsEmpty() ? TEXT("nothing contributes") : *State.Reason));
		}
	}
}
