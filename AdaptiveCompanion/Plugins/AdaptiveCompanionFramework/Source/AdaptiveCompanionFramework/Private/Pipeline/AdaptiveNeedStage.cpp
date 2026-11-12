#include "Pipeline/AdaptiveNeedStage.h"
#include "AdaptiveCompanionFramework.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Dom/JsonObject.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"
#include "Pipeline/AdaptiveFeatureTypes.h"
#include "Pipeline/AdaptiveNeedEstimator.h"

UAdaptiveNeedStage::UAdaptiveNeedStage()
{
	Estimator = CreateDefaultSubobject<UAdaptiveRuleNeedEstimator>(TEXT("Estimator"));
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
	Payload->SetNumberField(TEXT("threshold"), Threshold);
	Log->LogRecord(AdaptiveLogRecordType::NeedDefinitions, Payload.ToSharedRef());
}

void UAdaptiveNeedStage::Update(FAdaptivePipelineFrame& Frame)
{
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

		FAdaptiveNeedState State;
		State.Score = FMath::Clamp(Estimate.Score, 0.0, 1.0);
		State.bActive = Estimate.bActive;
		State.Reason = Estimate.Reason;
		State.bElevated = State.bActive && State.Score >= Threshold;
		State.bChanged = State.bElevated != DimensionState.bElevated;
		Frame.Needs.Add(Estimate.Dimension, State);

		const TCHAR* Level = State.bElevated ? TEXT("Elevated") : TEXT("Normal");

		const double Logged = AdaptiveFeatureMath::RoundTo(State.Score, LogStep);
		const bool bLogNeed = !DimensionState.LoggedScore.IsSet() || *DimensionState.LoggedScore != Logged
			|| DimensionState.LoggedReason != State.Reason || DimensionState.bLoggedActive != State.bActive;
		if (bLogNeed)
		{
			DimensionState.LoggedScore = Logged;
			DimensionState.LoggedReason = State.Reason;
			DimensionState.bLoggedActive = State.bActive;

			if (Log)
			{
				TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
				Payload->SetStringField(TEXT("need"), Estimate.Dimension.ToString());
				Payload->SetNumberField(TEXT("score"), Logged);
				Payload->SetBoolField(TEXT("active"), State.bActive);
				Payload->SetStringField(TEXT("reason"), State.Reason);
				Log->LogRecord(AdaptiveLogRecordType::Need, Payload);
			}
		}

		if (State.bChanged)
		{
			DimensionState.bElevated = State.bElevated;

			if (Log)
			{
				TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
				Payload->SetStringField(TEXT("need"), Estimate.Dimension.ToString());
				Payload->SetNumberField(TEXT("score"), State.Score);
				Payload->SetStringField(TEXT("level"), Level);
				Log->LogRecord(AdaptiveLogRecordType::State, Payload);
			}
		}

		if (Debug)
		{
			const FString Name = Estimate.Dimension.ToString();
			Debug->SetBar(EAdaptiveDebugSection::Need, Name, State.Score);
			Debug->SetText(EAdaptiveDebugSection::Need, Name + TEXT(" why"),
				!State.bActive ? FString(TEXT("inactive in this context"))
				: FString::Printf(TEXT("%s, %s"), Level, State.Reason.IsEmpty() ? TEXT("nothing contributes") : *State.Reason));
		}
	}
}
