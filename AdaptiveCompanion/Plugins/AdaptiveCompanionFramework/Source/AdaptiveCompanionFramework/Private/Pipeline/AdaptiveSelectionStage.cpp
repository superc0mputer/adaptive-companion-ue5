#include "Pipeline/AdaptiveSelectionStage.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"
#include "Pipeline/AdaptiveCompanionAction.h"

namespace
{
	double Round2(double Value)
	{
		return FMath::RoundToDouble(Value * 100.0) / 100.0;
	}
}

void UAdaptiveSelectionStage::Update(FAdaptivePipelineFrame& Frame)
{
	Evaluate(Frame);
	Decide(Frame);
}

void UAdaptiveSelectionStage::Evaluate(FAdaptivePipelineFrame& Frame) const
{
	for (const FAdaptiveActionState& State : Frame.Actions)
	{
		FAdaptiveActionEvaluation& Evaluation = Frame.Evaluations.AddDefaulted_GetRef();
		Evaluation.Action = State.Name;

		if (!State.bAvailable || !State.Action)
		{
			Evaluation.Excluded = FString::Printf(TEXT("unavailable: %s"), *State.Reason);
			continue;
		}

		const double* Last = LastSelected.Find(State.Name);
		if (Last && Frame.Time - *Last < State.Action->Cooldown)
		{
			Evaluation.Excluded = FString::Printf(TEXT("cooldown, %.0f s left"), FMath::CeilToDouble(State.Action->Cooldown - (Frame.Time - *Last)));
			continue;
		}

		// R: the elevated need this action helps most with
		for (const TPair<FName, FAdaptiveNeedState>& Need : Frame.Needs)
		{
			if (!Need.Value.bActive || Need.Value.Level == EAdaptiveNeedLevel::Normal)
			{
				continue;
			}

			const double Relevance = State.Action->GetRelevance(Need.Key) * Need.Value.Score;
			if (Relevance > Evaluation.Relevance)
			{
				Evaluation.Relevance = Relevance;
				Evaluation.Need = Need.Key;
				Evaluation.NeedScore = Need.Value.Score;
			}
		}

		if (Evaluation.Relevance <= 0.0)
		{
			Evaluation.Excluded = TEXT("no elevated need it helps with");
			continue;
		}

		Evaluation.bCandidate = true;
		Evaluation.Suitability = State.Suitability;
		Evaluation.Repetition = Last ? FMath::Pow(0.5, (Frame.Time - *Last) / RepetitionHalfLife) : 0.0;
		Evaluation.Cost = State.Cost;
		Evaluation.Intrusiveness = FMath::Clamp(double(State.Action->Intrusiveness), 0.0, 1.0);

		// intrusive help weighs less the more urgent the need
		Evaluation.Utility = Weights.Relevance * Evaluation.Relevance
			+ Weights.Suitability * Evaluation.Suitability
			- Weights.Repetition * Evaluation.Repetition
			- Weights.Cost * Evaluation.Cost
			- Weights.Intrusiveness * Evaluation.Intrusiveness * (1.0 - Evaluation.NeedScore);
	}
}

void UAdaptiveSelectionStage::Decide(FAdaptivePipelineFrame& Frame)
{
	const FAdaptiveActionEvaluation* Best = nullptr;
	for (const FAdaptiveActionEvaluation& Evaluation : Frame.Evaluations)
	{
		// ties go to the action registered first
		if (Evaluation.bCandidate && (!Best || Evaluation.Utility > Best->Utility))
		{
			Best = &Evaluation;
		}
	}

	bool bAnyElevated = false;
	for (const TPair<FName, FAdaptiveNeedState>& Need : Frame.Needs)
	{
		bAnyElevated |= Need.Value.bActive && Need.Value.Level != EAdaptiveNeedLevel::Normal;
	}

	if (!bAnyElevated)
	{
		Frame.SelectionReason = TEXT("no elevated need");
	}
	else if (!Frame.RunningIntervention.IsNone())
	{
		Frame.SelectionReason = TEXT("intervention still running");
	}
	else if (Frame.Time - LastInterventionTime < MinTimeBetweenInterventions)
	{
		Frame.SelectionReason = TEXT("too soon after the last intervention");
	}
	else if (!Best)
	{
		Frame.SelectionReason = TEXT("no candidate action");
	}
	else if (Best->Utility < MinUtility)
	{
		Frame.SelectionReason = FString::Printf(TEXT("best utility below %.2f"), MinUtility);
	}
	else
	{
		Frame.Intervention = Best->Action;
		Frame.SelectionReason = FString::Printf(TEXT("highest utility for %s"), *Best->Need.ToString());
		LastSelected.Add(Best->Action, Frame.Time);
		LastInterventionTime = Frame.Time;
	}

	const FString Decision = Frame.Intervention.IsNone() ? TEXT("none") : Frame.Intervention.ToString();
	if (!Frame.Intervention.IsNone() || Decision != LoggedDecision || Frame.SelectionReason != LoggedReason)
	{
		LoggedDecision = Decision;
		LoggedReason = Frame.SelectionReason;
		Log(Frame, Decision);
	}

	ShowInOverlay(Frame, Decision);
}

void UAdaptiveSelectionStage::Log(const FAdaptivePipelineFrame& Frame, const FString& Decision) const
{
	UAdaptiveLogSubsystem* LogSubsystem = GetLog();
	if (!LogSubsystem)
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> Candidates;
	for (const FAdaptiveActionEvaluation& Evaluation : Frame.Evaluations)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("action"), Evaluation.Action.ToString());
		if (Evaluation.bCandidate)
		{
			Object->SetNumberField(TEXT("utility"), Round2(Evaluation.Utility));
			Object->SetStringField(TEXT("need"), Evaluation.Need.ToString());
			Object->SetNumberField(TEXT("relevance"), Round2(Evaluation.Relevance));
			Object->SetNumberField(TEXT("suitability"), Round2(Evaluation.Suitability));
			Object->SetNumberField(TEXT("repetition"), Round2(Evaluation.Repetition));
			Object->SetNumberField(TEXT("cost"), Round2(Evaluation.Cost));
			Object->SetNumberField(TEXT("intrusiveness"), Round2(Evaluation.Intrusiveness));
		}
		else
		{
			Object->SetStringField(TEXT("excluded"), Evaluation.Excluded);
		}
		Candidates.Add(MakeShared<FJsonValueObject>(Object));
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("decision"), Decision);
	Payload->SetStringField(TEXT("reason"), Frame.SelectionReason);
	Payload->SetArrayField(TEXT("candidates"), Candidates);
	LogSubsystem->LogRecord(AdaptiveLogRecordType::Selection, Payload);

	if (Frame.Intervention.IsNone())
	{
		return;
	}

	// the intervention's first phase, so its record sequence starts here as before
	const FAdaptiveActionEvaluation* Chosen = Frame.Evaluations.FindByPredicate([&Frame](const FAdaptiveActionEvaluation& Evaluation) { return Evaluation.Action == Frame.Intervention; });
	TSharedRef<FJsonObject> Selected = MakeShared<FJsonObject>();
	Selected->SetStringField(TEXT("name"), Frame.Intervention.ToString());
	Selected->SetStringField(TEXT("phase"), TEXT("selected"));
	if (Chosen)
	{
		const FAdaptiveNeedState* Need = Frame.Needs.Find(Chosen->Need);
		Selected->SetStringField(TEXT("need"), Chosen->Need.ToString());
		Selected->SetNumberField(TEXT("score"), Round2(Chosen->NeedScore));
		Selected->SetStringField(TEXT("level"), Need ? AdaptiveNeedMath::LevelName(Need->Level) : TEXT("Normal"));
		Selected->SetNumberField(TEXT("utility"), Round2(Chosen->Utility));
	}
	LogSubsystem->LogRecord(AdaptiveLogRecordType::Intervention, Selected);
}

void UAdaptiveSelectionStage::ShowInOverlay(const FAdaptivePipelineFrame& Frame, const FString& Decision) const
{
	UAdaptiveDebugSubsystem* Debug = GetDebug();
	if (!Debug)
	{
		return;
	}

	Debug->SetText(EAdaptiveDebugSection::Selection, TEXT("Decision"), FString::Printf(TEXT("%s: %s"), *Decision, *Frame.SelectionReason));
	if (!Frame.Intervention.IsNone())
	{
		Debug->SetText(EAdaptiveDebugSection::Selection, TEXT("Selected"), FString::Printf(TEXT("%s at %.1f s"), *Frame.Intervention.ToString(), Frame.Time));
	}

	for (const FAdaptiveActionEvaluation& Evaluation : Frame.Evaluations)
	{
		Debug->SetText(EAdaptiveDebugSection::Selection, Evaluation.Action.ToString(), Evaluation.bCandidate
			? FString::Printf(TEXT("U %.2f for %s (R %.2f S %.2f P %.2f C %.2f I %.2f)"), Evaluation.Utility, *Evaluation.Need.ToString(),
				Evaluation.Relevance, Evaluation.Suitability, Evaluation.Repetition, Evaluation.Cost, Evaluation.Intrusiveness)
			: Evaluation.Excluded);
	}
}
