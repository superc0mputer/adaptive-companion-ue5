#include "Pipeline/AdaptiveBehaviorStage.h"
#include "Pipeline/AdaptiveCompanionAction.h"
#include "Pipeline/AdaptivePipelineFrame.h"
#include "Pipeline/AdaptiveSelectionStage.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags SelectionTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	UAdaptiveCompanionAction* MakeAction(const TCHAR* Name, TMap<FName, float> Relevance, float Intrusiveness, float Cooldown = 10.f)
	{
		UAdaptiveCompanionAction* Action = NewObject<UAdaptiveCompanionAction>();
		Action->ActionName = Name;
		Action->Relevance = MoveTemp(Relevance);
		Action->Intrusiveness = Intrusiveness;
		Action->Cooldown = Cooldown;
		return Action;
	}

	/** The game's three actions from #17 with their asset values */
	struct FGameActions
	{
		UAdaptiveCompanionAction* Hint = MakeAction(TEXT("Action.NavigationHint"), { { TEXT("Navigation"), 1.f }, { TEXT("Disorientation"), 0.8f } }, 0.3f, 15.f);
		UAdaptiveCompanionAction* Callout = MakeAction(TEXT("Action.ThreatCallout"), { { TEXT("Threat"), 1.f } }, 0.2f, 8.f);
		UAdaptiveCompanionAction* Aggro = MakeAction(TEXT("Action.DrawAggro"), { { TEXT("Combat"), 1.f }, { TEXT("Threat"), 0.4f } }, 0.6f, 20.f);
	};

	FAdaptiveActionState Available(const UAdaptiveCompanionAction* Action, double Cost, bool bAvailable = true)
	{
		FAdaptiveActionState State;
		State.Name = Action->ActionName;
		State.Action = Action;
		State.bContextAllowed = true;
		State.bAvailable = bAvailable;
		State.Reason = bAvailable ? FString() : FString(TEXT("needs Context.Combat"));
		State.Cost = Cost;
		return State;
	}

	FAdaptiveNeedState Need(double Score, EAdaptiveNeedLevel Level)
	{
		FAdaptiveNeedState State;
		State.Score = Score;
		State.Level = Level;
		return State;
	}

	const FAdaptiveActionEvaluation* FindEvaluation(const FAdaptivePipelineFrame& Frame, FName Action)
	{
		return Frame.Evaluations.FindByPredicate([Action](const FAdaptiveActionEvaluation& Evaluation) { return Evaluation.Action == Action; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveSelectionUtilityTest, "AdaptiveCompanion.Pipeline.Selection.Utility", SelectionTestFlags)

bool FAdaptiveSelectionUtilityTest::RunTest(const FString& Parameters)
{
	// the worked examples from #18, with the default weights
	FGameActions Actions;
	UAdaptiveSelectionStage* Stage = NewObject<UAdaptiveSelectionStage>();

	FAdaptivePipelineFrame Frame;
	Frame.Time = 100.0;
	Frame.Needs.Add(TEXT("Threat"), Need(0.56, EAdaptiveNeedLevel::Elevated));
	Frame.Needs.Add(TEXT("Combat"), Need(0.1, EAdaptiveNeedLevel::Normal));
	Frame.Actions = { Available(Actions.Hint, 0.2, false), Available(Actions.Callout, 0.1), Available(Actions.Aggro, 0.47) };
	Stage->Update(Frame);

	const FAdaptiveActionEvaluation* Callout = FindEvaluation(Frame, TEXT("Action.ThreatCallout"));
	const FAdaptiveActionEvaluation* Aggro = FindEvaluation(Frame, TEXT("Action.DrawAggro"));
	const FAdaptiveActionEvaluation* Hint = FindEvaluation(Frame, TEXT("Action.NavigationHint"));
	if (!TestNotNull(TEXT("Callout rated"), Callout) || !TestNotNull(TEXT("Aggro rated"), Aggro) || !TestNotNull(TEXT("Hint rated"), Hint))
	{
		return false;
	}

	TestFalse(TEXT("Unavailable actions are filtered out"), Hint->bCandidate);
	TestEqual(TEXT("With the reason"), Hint->Excluded, FString(TEXT("unavailable: needs Context.Combat")));

	// 0.56 + 0.3 − 0.3·0.1 − 0.4·0.2·0.44
	TestEqual(TEXT("Call-out utility"), Callout->Utility, 0.56 + 0.3 - 0.03 - 0.4 * 0.2 * 0.44, 1e-6);
	TestEqual(TEXT("Call-out serves Threat"), Callout->Need, FName(TEXT("Threat")));
	// only through its threat relevance 0.4, since combat isn't elevated
	TestEqual(TEXT("Aggro relevance via threat"), Aggro->Relevance, 0.4 * 0.56, 1e-6);
	TestEqual(TEXT("Aggro utility"), Aggro->Utility, 0.4 * 0.56 + 0.3 - 0.3 * 0.47 - 0.4 * 0.6 * 0.44, 1e-6);
	TestEqual(TEXT("The call-out wins"), Frame.Intervention, FName(TEXT("Action.ThreatCallout")));
	TestTrue(TEXT("Reason names the need"), Frame.SelectionReason.Contains(TEXT("Threat")));

	// intrusiveness weighs less when the need is urgent
	UAdaptiveSelectionStage* Urgent = NewObject<UAdaptiveSelectionStage>();
	FAdaptivePipelineFrame Low;
	Low.Needs.Add(TEXT("Combat"), Need(0.5, EAdaptiveNeedLevel::Elevated));
	Low.Actions = { Available(Actions.Aggro, 0.3) };
	Urgent->Update(Low);
	UAdaptiveSelectionStage* Urgent2 = NewObject<UAdaptiveSelectionStage>();
	FAdaptivePipelineFrame High;
	High.Needs.Add(TEXT("Combat"), Need(0.9, EAdaptiveNeedLevel::High));
	High.Actions = { Available(Actions.Aggro, 0.3) };
	Urgent2->Update(High);
	TestEqual(TEXT("Intrusiveness term at need 0.5"), Low.Evaluations[0].Utility - (0.5 + 0.3 - 0.09), -0.4 * 0.6 * 0.5, 1e-6);
	TestEqual(TEXT("Intrusiveness term at need 0.9"), High.Evaluations[0].Utility - (0.9 + 0.3 - 0.09), -0.4 * 0.6 * 0.1, 1e-6);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveSelectionNothingTest, "AdaptiveCompanion.Pipeline.Selection.Nothing", SelectionTestFlags)

bool FAdaptiveSelectionNothingTest::RunTest(const FString& Parameters)
{
	FGameActions Actions;
	UAdaptiveSelectionStage* Stage = NewObject<UAdaptiveSelectionStage>();

	auto Run = [&](double Time, TMap<FName, FAdaptiveNeedState> Needs, TArray<FAdaptiveActionState> States, FName Running = NAME_None)
	{
		FAdaptivePipelineFrame Frame;
		Frame.Time = Time;
		Frame.Needs = MoveTemp(Needs);
		Frame.Actions = MoveTemp(States);
		Frame.RunningIntervention = Running;
		Stage->Update(Frame);
		return Frame;
	};

	FAdaptivePipelineFrame Frame = Run(0.0, { { TEXT("Navigation"), Need(0.4, EAdaptiveNeedLevel::Normal) } }, { Available(Actions.Hint, 0.2) });
	TestTrue(TEXT("No elevated need: nothing"), Frame.Intervention.IsNone());
	TestEqual(TEXT("Reason"), Frame.SelectionReason, FString(TEXT("no elevated need")));
	TestFalse(TEXT("Not a candidate without an elevated need"), Frame.Evaluations[0].bCandidate);

	FAdaptiveNeedState Inactive = Need(0.9, EAdaptiveNeedLevel::High);
	Inactive.bActive = false;
	Frame = Run(0.5, { { TEXT("Navigation"), Inactive } }, { Available(Actions.Hint, 0.2) });
	TestTrue(TEXT("An inactive need doesn't count"), Frame.Intervention.IsNone());

	Frame = Run(1.0, { { TEXT("Combat"), Need(0.7, EAdaptiveNeedLevel::Elevated) } }, { Available(Actions.Hint, 0.2) });
	TestTrue(TEXT("No action for this need: nothing"), Frame.Intervention.IsNone());
	TestEqual(TEXT("Reason"), Frame.SelectionReason, FString(TEXT("no candidate action")));
	TestEqual(TEXT("The action says why"), Frame.Evaluations[0].Excluded, FString(TEXT("no elevated need it helps with")));

	Frame = Run(2.0, { { TEXT("Navigation"), Need(0.7, EAdaptiveNeedLevel::Elevated) } }, { Available(Actions.Hint, 0.2) }, TEXT("Action.Other"));
	TestTrue(TEXT("Intervention running: nothing"), Frame.Intervention.IsNone());
	TestEqual(TEXT("Reason"), Frame.SelectionReason, FString(TEXT("intervention still running")));

	Stage->MinUtility = 2.0;
	Frame = Run(3.0, { { TEXT("Navigation"), Need(0.7, EAdaptiveNeedLevel::Elevated) } }, { Available(Actions.Hint, 0.2) });
	TestTrue(TEXT("Below the threshold: nothing"), Frame.Intervention.IsNone());
	TestTrue(TEXT("Reason"), Frame.SelectionReason.StartsWith(TEXT("best utility below")));
	TestTrue(TEXT("But it is still a candidate"), Frame.Evaluations[0].bCandidate);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveSelectionRepetitionTest, "AdaptiveCompanion.Pipeline.Selection.Repetition", SelectionTestFlags)

bool FAdaptiveSelectionRepetitionTest::RunTest(const FString& Parameters)
{
	// a navigation need that stays elevated: hint, cooldown, repetition penalty, hint again
	FGameActions Actions;
	UAdaptiveSelectionStage* Stage = NewObject<UAdaptiveSelectionStage>();

	TArray<double> Selected;
	FAdaptivePipelineFrame Frame;
	for (double Time = 0.0; Time < 60.0; Time += 0.1)
	{
		Frame = FAdaptivePipelineFrame();
		Frame.Time = Time;
		Frame.Needs.Add(TEXT("Navigation"), Need(0.65, EAdaptiveNeedLevel::Elevated));
		Frame.Actions = { Available(Actions.Hint, 0.2) };
		Stage->Update(Frame);
		if (!Frame.Intervention.IsNone())
		{
			Selected.Add(Time);
		}
		if (Time > 1.0 && Time < 1.2)
		{
			TestTrue(TEXT("Within the cooldown it is no candidate"), Frame.Evaluations[0].Excluded.StartsWith(TEXT("cooldown")));
		}
		if (Time > 15.2 && Time < 15.4)
		{
			TestTrue(TEXT("After the cooldown it is a candidate again"), Frame.Evaluations[0].bCandidate);
			TestTrue(TEXT("But the repetition penalty keeps it below the threshold"), Frame.Intervention.IsNone() && Frame.Evaluations[0].Utility < Stage->MinUtility);
		}
	}

	if (!TestTrue(FString::Printf(TEXT("Selected more than once in 60 s (%d)"), Selected.Num()), Selected.Num() >= 2))
	{
		return false;
	}
	TestEqual(TEXT("First right away"), Selected[0], 0.0, 1e-6);
	// 0.848 − 0.5·0.5^(t/30) ≥ 0.5 from t ≈ 15.7 s: right after the 15 s cooldown it is still too low
	TestTrue(FString::Printf(TEXT("Second only after cooldown and penalty (%.1f s)"), Selected[1]), Selected[1] > 15.5 && Selected[1] < 17.0);

	// the frequency limit holds between different actions
	UAdaptiveSelectionStage* Limited = NewObject<UAdaptiveSelectionStage>();
	TArray<FName> Picks;
	for (double Time = 0.0; Time < 4.0; Time += 0.1)
	{
		Frame = FAdaptivePipelineFrame();
		Frame.Time = Time;
		Frame.Needs.Add(TEXT("Threat"), Need(0.6, EAdaptiveNeedLevel::Elevated));
		Frame.Needs.Add(TEXT("Combat"), Need(0.8, EAdaptiveNeedLevel::High));
		Frame.Actions = { Available(Actions.Callout, 0.1), Available(Actions.Aggro, 0.2) };
		Limited->Update(Frame);
		if (!Frame.Intervention.IsNone())
		{
			Picks.Add(Frame.Intervention);
		}
	}
	TestEqual(TEXT("Only one intervention within the 5 s limit"), Picks.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveSelectionRunningTest, "AdaptiveCompanion.Pipeline.Selection.Running", SelectionTestFlags)

bool FAdaptiveSelectionRunningTest::RunTest(const FString& Parameters)
{
	UAdaptiveBehaviorStage* Behavior = NewObject<UAdaptiveBehaviorStage>();

	FAdaptivePipelineFrame Frame;
	Frame.Time = 10.0;
	Frame.Intervention = TEXT("Action.ThreatCallout");
	Behavior->Update(Frame);
	TestEqual(TEXT("Dispatched: running"), Behavior->UpdateRunning(11.0), FName(TEXT("Action.ThreatCallout")));

	Behavior->ReportPhase(TEXT("Action.ThreatCallout"), TEXT("started"));
	TestEqual(TEXT("Started: still running"), Behavior->UpdateRunning(12.0), FName(TEXT("Action.ThreatCallout")));
	Behavior->ReportPhase(TEXT("Action.Other"), TEXT("ended"));
	TestEqual(TEXT("Another intervention's end changes nothing"), Behavior->UpdateRunning(12.5), FName(TEXT("Action.ThreatCallout")));
	Behavior->ReportPhase(TEXT("Action.ThreatCallout"), TEXT("ended"));
	TestTrue(TEXT("Ended: not running"), Behavior->UpdateRunning(13.0).IsNone());

	Frame.Time = 20.0;
	Behavior->Update(Frame);
	TestEqual(TEXT("Still running before the timeout"), Behavior->UpdateRunning(49.0), FName(TEXT("Action.ThreatCallout")));
	TestTrue(TEXT("Never reported to end: times out"), Behavior->UpdateRunning(50.0).IsNone());

	return true;
}

#endif
