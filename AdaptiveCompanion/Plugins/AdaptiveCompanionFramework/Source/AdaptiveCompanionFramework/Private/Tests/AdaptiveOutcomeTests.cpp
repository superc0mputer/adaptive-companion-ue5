#include "AdaptiveTestActionHandler.h"
#include "Pipeline/AdaptiveBehaviorStage.h"
#include "Pipeline/AdaptiveCompanionAction.h"
#include "Pipeline/AdaptiveOutcomeTracker.h"
#include "Pipeline/AdaptivePipelineFrame.h"
#include "Pipeline/AdaptiveSelectionStage.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags OutcomeTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	const FName Threat = TEXT("Threat");
	const FName Unseen = TEXT("Threat.EnemiesUnseen");
	const FName Combat = TEXT("Context.Combat");
	const FName Callout = TEXT("Action.ThreatCallout");

	/** A frame at update Index (10 Hz) with the threat need, the unseen-enemies feature and the combat context */
	FAdaptivePipelineFrame MakeFrame(int32 Index, double NeedScore, double UnseenValue, bool bCombat = true)
	{
		FAdaptivePipelineFrame Frame;
		Frame.Time = Index * 0.1;
		FAdaptiveNeedState Need;
		Need.Score = NeedScore;
		Need.Level = NeedScore >= 0.5 ? EAdaptiveNeedLevel::Elevated : EAdaptiveNeedLevel::Normal;
		Frame.Needs.Add(Threat, Need);
		Frame.Features.Add(Unseen, UnseenValue);
		if (bCombat)
		{
			Frame.Contexts.Add(Combat);
		}
		return Frame;
	}

	/** Runs updates First..Last with the given values and returns the outcomes that finished */
	TArray<FAdaptiveOutcome> Run(FAdaptiveOutcomeTracker& Tracker, int32 First, int32 Last, double NeedScore, double UnseenValue, bool bCombat = true)
	{
		TArray<FAdaptiveOutcome> Finished;
		for (int32 Index = First; Index <= Last; ++Index)
		{
			Finished.Append(Tracker.Update(MakeFrame(Index, NeedScore, UnseenValue, bCombat)));
		}
		return Finished;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveOutcomeMeasureTest, "AdaptiveCompanion.Pipeline.Outcome.Measure", OutcomeTestFlags)

bool FAdaptiveOutcomeMeasureTest::RunTest(const FString& Parameters)
{
	FAdaptiveOutcomeTracker Tracker;

	// 0–1.9 s at 0.6, 2.0 s at 0.8; selected at 2.0 s
	Run(Tracker, 0, 19, 0.6, 1.0);
	Run(Tracker, 20, 20, 0.8, 1.0);
	Tracker.Start(1, Callout, Threat, { Unseen, Unseen, NAME_None }, 2.0);
	TestEqual(TEXT("One outcome open"), Tracker.GetOpen().Num(), 1);
	const FAdaptiveOutcome& Open = Tracker.GetOpen()[0];
	TestEqual(TEXT("The need first, each feature once"), Open.Measures.Num(), 2);
	TestEqual(TEXT("Baseline: the value at the selection"), Open.Measures[0].Baseline.Get(-1.0), 0.8, 1e-9);
	TestEqual(TEXT("Baseline level"), Open.BaselineLevel, EAdaptiveNeedLevel::Elevated);

	// running until 3.0 s: nothing finishes, whatever the values
	TestEqual(TEXT("Nothing finishes while it runs"), Run(Tracker, 21, 30, 0.7, 1.0).Num(), 0);
	TestFalse(TEXT("No other action's end counts"), Tracker.End(TEXT("Action.Other"), TEXT("completed"), FString(), 3.0));
	TestTrue(TEXT("Ended"), Tracker.End(Callout, TEXT("completed"), TEXT("threat in view"), 3.0));
	TestEqual(TEXT("End value: the latest update"), Tracker.GetOpen()[0].Measures[0].End.Get(-1.0), 0.7, 1e-9);

	// 10 s after the end at 0.2 and 0: finishes with the last of them
	TestEqual(TEXT("Not before the window is over"), Run(Tracker, 31, 129, 0.2, 0.0).Num(), 0);
	const TArray<FAdaptiveOutcome> Finished = Run(Tracker, 130, 130, 0.2, 0.0);
	if (!TestEqual(TEXT("Finishes 10 s after the end"), Finished.Num(), 1))
	{
		return false;
	}

	const FAdaptiveOutcome& Outcome = Finished[0];
	TestEqual(TEXT("Id"), Outcome.Id, 1);
	TestEqual(TEXT("Phase"), Outcome.Phase, FName(TEXT("completed")));
	TestEqual(TEXT("Detail"), Outcome.Detail, FString(TEXT("threat in view")));
	TestTrue(TEXT("Full window"), Outcome.CutShort.IsEmpty());
	TestEqual(TEXT("After duration"), Outcome.AfterDuration, 10.0, 1e-6);
	TestEqual(TEXT("After: the window's mean"), Outcome.Measures[0].After.Get(-1.0), 0.2, 1e-9);
	TestEqual(TEXT("Change"), Outcome.Measures[0].GetChange().Get(0.0), 0.2 - 0.8, 1e-9);
	TestTrue(TEXT("Relieved: back to Normal"), Outcome.IsRelieved());
	TestEqual(TEXT("Feature baseline"), Outcome.Measures[1].Baseline.Get(-1.0), 1.0, 1e-9);
	TestEqual(TEXT("Feature after"), Outcome.Measures[1].After.Get(-1.0), 0.0, 1e-9);
	TestEqual(TEXT("Nothing open any more"), Tracker.GetOpen().Num(), 0);

	// a feature without data has no values and no change
	FAdaptiveOutcomeTracker Missing;
	Run(Missing, 0, 20, 0.8, 1.0);
	Missing.Start(2, Callout, Threat, { TEXT("Nav.Unknown") }, 2.0);
	Missing.End(Callout, TEXT("completed"), FString(), 2.0);
	const TArray<FAdaptiveOutcome> NoData = Run(Missing, 21, 120, 0.8, 1.0);
	if (TestEqual(TEXT("Finishes"), NoData.Num(), 1))
	{
		TestFalse(TEXT("No baseline without data"), NoData[0].Measures[1].Baseline.IsSet());
		TestFalse(TEXT("No change without data"), NoData[0].Measures[1].GetChange().IsSet());
		TestFalse(TEXT("Still Elevated: not relieved"), NoData[0].IsRelieved());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveOutcomeCutShortTest, "AdaptiveCompanion.Pipeline.Outcome.CutShort", OutcomeTestFlags)

bool FAdaptiveOutcomeCutShortTest::RunTest(const FString& Parameters)
{
	// the next intervention ends the window with what was measured so far
	{
		FAdaptiveOutcomeTracker Tracker;
		Run(Tracker, 0, 20, 0.8, 1.0);
		Tracker.Start(1, Callout, Threat, {}, 2.0);
		Tracker.End(Callout, TEXT("completed"), FString(), 2.0);
		Run(Tracker, 21, 50, 0.4, 0.0);
		FAdaptivePipelineFrame Next = MakeFrame(51, 0.6, 1.0);
		Next.Intervention = TEXT("Action.DrawAggro");
		const TArray<FAdaptiveOutcome> Finished = Tracker.Update(Next);
		if (TestEqual(TEXT("Next intervention: finished"), Finished.Num(), 1))
		{
			TestEqual(TEXT("Reason"), Finished[0].CutShort, FString(TEXT("next intervention")));
			TestEqual(TEXT("Only the updates before it count"), Finished[0].Measures[0].After.Get(-1.0), 0.4, 1e-9);
			TestEqual(TEXT("Measured duration"), Finished[0].AfterDuration, 3.0, 1e-6);
		}
	}

	// the fight ending changes the contexts: what follows is a different situation
	{
		FAdaptiveOutcomeTracker Tracker;
		Run(Tracker, 0, 20, 0.8, 1.0);
		Tracker.Start(1, Callout, Threat, {}, 2.0);
		Tracker.End(Callout, TEXT("completed"), FString(), 2.0);
		Run(Tracker, 21, 30, 0.3, 0.0);
		const TArray<FAdaptiveOutcome> Finished = Run(Tracker, 31, 31, 0.0, 0.0, false);
		if (TestEqual(TEXT("Context change: finished"), Finished.Num(), 1))
		{
			TestEqual(TEXT("Reason"), Finished[0].CutShort, FString(TEXT("context changed")));
			TestEqual(TEXT("The update after the change doesn't count"), Finished[0].Measures[0].After.Get(-1.0), 0.3, 1e-9);
		}
	}

	// cut short right after the end: no after values, the end values remain
	{
		FAdaptiveOutcomeTracker Tracker;
		Run(Tracker, 0, 20, 0.8, 1.0);
		Tracker.Start(1, Callout, Threat, {}, 2.0);
		Tracker.End(Callout, TEXT("failed"), TEXT("no enemy out of view"), 2.0);
		const TArray<FAdaptiveOutcome> Finished = Run(Tracker, 21, 21, 0.0, 0.0, false);
		if (TestEqual(TEXT("Finished"), Finished.Num(), 1))
		{
			TestFalse(TEXT("No after value"), Finished[0].Measures[0].After.IsSet());
			TestEqual(TEXT("End value kept"), Finished[0].Measures[0].End.Get(-1.0), 0.8, 1e-9);
			TestFalse(TEXT("No change without an after value"), Finished[0].Measures[0].GetChange().IsSet());
			TestEqual(TEXT("But the change while it ran"), Finished[0].Measures[0].GetEndChange().Get(-1.0), 0.0, 1e-9);
			TestFalse(TEXT("Not relieved: the need was Elevated at the end"), Finished[0].IsRelieved());
		}
	}

	// the session ends: a running and a measuring outcome are both finished
	{
		FAdaptiveOutcomeTracker Tracker;
		Run(Tracker, 0, 20, 0.8, 1.0);
		Tracker.Start(1, Callout, Threat, {}, 2.0);
		Tracker.End(Callout, TEXT("completed"), FString(), 2.0);
		Run(Tracker, 21, 25, 0.8, 1.0);
		Tracker.Start(2, TEXT("Action.DrawAggro"), Threat, {}, 2.5);
		const TArray<FAdaptiveOutcome> Finished = Tracker.Flush(TEXT("session ended"));
		if (TestEqual(TEXT("Both finished"), Finished.Num(), 2))
		{
			TestEqual(TEXT("Reason"), Finished[1].CutShort, FString(TEXT("session ended")));
			TestTrue(TEXT("The running one has no phase"), Finished[1].Phase.IsNone());
			TestFalse(TEXT("And no end"), Finished[1].HasEnded());
		}
		TestEqual(TEXT("Nothing open"), Tracker.GetOpen().Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveOutcomeStageTest, "AdaptiveCompanion.Pipeline.Outcome.Stage", OutcomeTestFlags)

bool FAdaptiveOutcomeStageTest::RunTest(const FString& Parameters)
{
	UAdaptiveCompanionAction* Action = NewObject<UAdaptiveCompanionAction>();
	Action->ActionName = Callout;
	Action->Relevance.Add(Threat, 1.f);
	Action->OutcomeFeatures = { Unseen };
	UAdaptiveTestActionHandler* Handler = NewObject<UAdaptiveTestActionHandler>();

	FAdaptiveActionState State;
	State.Name = Callout;
	State.Action = Action;
	State.Handler = Handler;
	State.bContextAllowed = true;
	State.bAvailable = true;
	State.Cost = 0.1;

	// selection numbers the interventions, behavior links the outcome to that number
	UAdaptiveSelectionStage* Selection = NewObject<UAdaptiveSelectionStage>();
	Selection->MinTimeBetweenInterventions = 0.f;
	UAdaptiveBehaviorStage* Behavior = NewObject<UAdaptiveBehaviorStage>();
	Behavior->OutcomeWindow = 5.f;
	TArray<FAdaptiveOutcome> Outcomes;
	Behavior->OnOutcome = [&Outcomes](const FAdaptiveOutcome& Outcome) { Outcomes.Add(Outcome); };

	auto Step = [&](int32 Index, double NeedScore, double UnseenValue, bool bWithAction)
	{
		FAdaptivePipelineFrame Frame = MakeFrame(Index, NeedScore, UnseenValue);
		Frame.RunningIntervention = Behavior->UpdateRunning(Frame.Time);
		if (bWithAction)
		{
			Frame.Actions = { State };
		}
		Selection->Update(Frame);
		Behavior->Update(Frame);
		return Frame;
	};

	for (int32 Index = 0; Index < 20; ++Index)
	{
		Step(Index, 0.8, 1.0, false);
	}
	const FAdaptivePipelineFrame Selected = Step(20, 0.8, 1.0, true);
	TestEqual(TEXT("Selected"), Selected.Intervention, Callout);
	TestEqual(TEXT("The first intervention has id 1"), Selected.InterventionId, 1);
	if (!TestEqual(TEXT("Its outcome is open"), Behavior->GetOutcomes().GetOpen().Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("For the need the selection chose"), Behavior->GetOutcomes().GetOpen()[0].Need, Threat);
	TestEqual(TEXT("With the action's outcome feature"), Behavior->GetOutcomes().GetOpen()[0].Measures.Num(), 2);

	Behavior->ReportPhase(Callout, TEXT("started"));
	TestFalse(TEXT("Started doesn't end it"), Behavior->GetOutcomes().GetOpen()[0].HasEnded());
	Step(21, 0.6, 1.0, false);
	Behavior->ReportPhase(Callout, TEXT("completed"), TEXT("threat in view"));
	TestTrue(TEXT("Completed ends it"), Behavior->GetOutcomes().GetOpen()[0].HasEnded());
	for (int32 Index = 22; Index <= 71; ++Index)
	{
		Step(Index, 0.1, 0.0, false);
	}
	if (TestEqual(TEXT("Outcome finished after the window"), Outcomes.Num(), 1))
	{
		TestEqual(TEXT("Linked by id"), Outcomes[0].Id, 1);
		TestEqual(TEXT("Detail from the game"), Outcomes[0].Detail, FString(TEXT("threat in view")));
		TestTrue(TEXT("Relieved"), Outcomes[0].IsRelieved());
	}

	// a rejected intervention gets an outcome too, with the next id
	Handler->bAccept = false;
	const FAdaptivePipelineFrame Rejected = Step(300, 0.8, 1.0, true);
	TestEqual(TEXT("The second intervention has id 2"), Rejected.InterventionId, 2);
	TestEqual(TEXT("Its outcome ended at once"), Behavior->GetOutcomes().GetOpen().Num() == 1 && Behavior->GetOutcomes().GetOpen()[0].Phase == FName(TEXT("rejected")), true);

	return true;
}

#endif
