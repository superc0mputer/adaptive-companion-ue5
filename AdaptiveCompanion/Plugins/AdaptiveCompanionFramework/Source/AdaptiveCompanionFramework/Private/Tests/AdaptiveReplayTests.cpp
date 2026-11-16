#include "Pipeline/AdaptiveCompanionAction.h"
#include "Pipeline/AdaptivePipelineConfig.h"
#include "Replay/AdaptiveInputRecording.h"
#include "Replay/AdaptiveReplayEvaluation.h"
#include "Replay/AdaptiveReplayRunner.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AdaptiveReplayTests
{
	constexpr EAutomationTestFlags ReplayTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	const FName ReplayCombat = TEXT("Combat");
	const FName ReplayCombatContext = TEXT("Context.Combat");
	const FName ReplayAggro = TEXT("Action.DrawAggro");

	FAdaptiveNeedState MakeNeed(EAdaptiveNeedLevel Level)
	{
		FAdaptiveNeedState State;
		State.Level = Level;
		State.Score = Level == EAdaptiveNeedLevel::Normal ? 0.1 : 0.7;
		return State;
	}

	/**
	 *  20 s at 10 Hz with the default config (Combat.DamageRecent → Combat): three hits at 2 s, the
	 *  companion's action carried out from its selection until 6 s, then quiet
	 */
	TArray<FAdaptiveRecordedUpdate> MakeRecording(const UAdaptiveCompanionAction* Action)
	{
		TArray<FAdaptiveRecordedUpdate> Updates;
		for (int32 Index = 0; Index < 200; ++Index)
		{
			FAdaptiveRecordedUpdate& Update = Updates.AddDefaulted_GetRef();
			Update.Index = Index;
			Update.Time = Index * 0.1;
			if (Index == 0)
			{
				Update.Contexts = TArray<FName>{ ReplayCombatContext };
				Update.bObjectiveChanged = true;
				FAdaptiveRecordedAction Registered;
				Registered.Name = ReplayAggro;
				Registered.Asset = Action->GetPathName();
				Update.Registered = TArray<FAdaptiveRecordedAction>{ Registered };
				FAdaptiveRecordedAction Answer;
				Answer.Name = ReplayAggro;
				Answer.Cost = 0.3;
				Update.Answers.Add(Answer);
				Update.Values.Add(TEXT("Player.Health"), 1.0);
			}
			if (Index == 20)
			{
				for (int32 Hit = 0; Hit < 3; ++Hit)
				{
					Update.Events.Add({ TEXT("Combat.DamageTaken"), 1.0, 1.95 + Hit * 0.01 });
				}
			}
			if (Index == 60)
			{
				Update.PhasesBefore.Add({ ReplayAggro, TEXT("completed"), TEXT("drew 1 of 1 enemies"), 5.95 });
			}
		}
		return Updates;
	}
}

using namespace AdaptiveReplayTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveReplayFormatTest, "AdaptiveCompanion.Replay.Format", ReplayTestFlags)

bool FAdaptiveReplayFormatTest::RunTest(const FString& Parameters)
{
	UAdaptiveCompanionAction* Action = NewObject<UAdaptiveCompanionAction>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UAdaptiveCompanionAction::StaticClass(), TEXT("ReplayFormatTestAction")));
	Action->ActionName = ReplayAggro;

	FAdaptiveInputRecorder Recorder;
	auto MakeFrame = [Action](int64 Index, double Health, bool bPossible)
	{
		FAdaptivePipelineFrame Frame;
		Frame.UpdateIndex = Index;
		Frame.Time = Index * 0.1;
		Frame.Values.Add(TEXT("Player.Health"), Health);
		Frame.Values.Add(TEXT("Nav.Speed"), 3.0);
		Frame.Contexts.Add(ReplayCombatContext);
		FAdaptiveActionState& State = Frame.Actions.AddDefaulted_GetRef();
		State.Name = ReplayAggro;
		State.Action = Action;
		State.bContextAllowed = true;
		State.bAvailable = bPossible;
		State.Reason = bPossible ? FString() : FString(TEXT("no enemy near the player"));
		State.Cost = 0.25;
		return Frame;
	};

	const FAdaptiveRecordedUpdate First = Recorder.Record(MakeFrame(0, 1.0, true), {}, {}, false);
	TestEqual(TEXT("First update: every value"), First.Values.Num(), 2);
	TestTrue(TEXT("First update: contexts"), First.Contexts.IsSet());
	TestTrue(TEXT("First update: objective (none)"), First.bObjectiveChanged && First.Objective.IsNone());
	TestTrue(TEXT("First update: registered actions"), First.Registered.IsSet() && First.Registered->Num() == 1);
	TestEqual(TEXT("First update: the game's answer"), First.Answers.Num(), 1);

	FAdaptivePipelineFrame Second = MakeFrame(1, 0.8, false);
	Second.Events.Add({ TEXT("Combat.DamageTaken"), 1.0, 0.07 });
	const FAdaptiveRecordedUpdate Recorded = Recorder.Record(Second, { { ReplayAggro, TEXT("completed"), TEXT("done"), 0.05 } }, { { ReplayAggro, TEXT("started"), FString(), 0.1 } }, true);
	TestEqual(TEXT("Only the changed value"), Recorded.Values.Num(), 1);
	TestFalse(TEXT("Contexts unchanged"), Recorded.Contexts.IsSet());
	TestFalse(TEXT("Objective unchanged"), Recorded.bObjectiveChanged);
	TestFalse(TEXT("Actions unchanged"), Recorded.Registered.IsSet());
	TestEqual(TEXT("The changed answer"), Recorded.Answers.Num(), 1);

	// through JSON and back
	FAdaptiveRecordedUpdate Parsed;
	TestTrue(TEXT("Parses"), AdaptiveRecording::FromJson(*AdaptiveRecording::ToJson(Recorded), Parsed));
	TestEqual(TEXT("Index"), Parsed.Index, Recorded.Index);
	TestEqual(TEXT("Time"), Parsed.Time, Recorded.Time);
	TestEqual(TEXT("Value"), Parsed.Values.FindRef(TEXT("Player.Health")), 0.8);
	TestTrue(TEXT("Event with its time"), Parsed.Events.Num() == 1 && Parsed.Events[0].Time == 0.07 && Parsed.Events[0].Name == FName(TEXT("Combat.DamageTaken")));
	TestTrue(TEXT("Answer"), Parsed.Answers.Num() == 1 && !Parsed.Answers[0].bPossible && Parsed.Answers[0].Reason == TEXT("no enemy near the player") && Parsed.Answers[0].Cost == 0.25);
	TestTrue(TEXT("Phase before"), Parsed.PhasesBefore.Num() == 1 && Parsed.PhasesBefore[0].Detail == TEXT("done") && Parsed.PhasesBefore[0].Time == 0.05);
	TestTrue(TEXT("Phase during"), Parsed.PhasesDuring.Num() == 1 && Parsed.PhasesDuring[0].Phase == FName(TEXT("started")));
	TestTrue(TEXT("Rejected"), Parsed.bRejected);
	TestFalse(TEXT("Absent fields stay absent"), Parsed.Contexts.IsSet() || Parsed.Registered.IsSet() || Parsed.bObjectiveChanged);

	FAdaptiveRecordedUpdate FirstParsed;
	AdaptiveRecording::FromJson(*AdaptiveRecording::ToJson(First), FirstParsed);
	TestTrue(TEXT("Contexts and actions survive"), FirstParsed.Contexts.IsSet() && (*FirstParsed.Contexts)[0] == ReplayCombatContext
		&& FirstParsed.Registered.IsSet() && (*FirstParsed.Registered)[0].Asset == Action->GetPathName());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveReplayRunTest, "AdaptiveCompanion.Replay.Run", ReplayTestFlags)

bool FAdaptiveReplayRunTest::RunTest(const FString& Parameters)
{
	UAdaptiveCompanionAction* Action = NewObject<UAdaptiveCompanionAction>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UAdaptiveCompanionAction::StaticClass(), TEXT("ReplayRunTestAction")));
	Action->ActionName = ReplayAggro;
	Action->Relevance.Add(ReplayCombat, 1.f);
	Action->RequiredContexts.Add(ReplayCombatContext);
	Action->Cooldown = 30.f;
	const TArray<FAdaptiveRecordedUpdate> Recording = MakeRecording(Action);

	// the defaults: one feature, one rule, the default levels and selection
	UAdaptiveReplayRunner* Runner = NewObject<UAdaptiveReplayRunner>();
	Runner->Setup(nullptr);
	const TArray<FAdaptiveReplayStep> Steps = Runner->Run(Recording);
	TestEqual(TEXT("No warnings"), Runner->GetWarnings().Num(), 0);
	if (!TestEqual(TEXT("One step per update"), Steps.Num(), Recording.Num()))
	{
		return false;
	}

	const FAdaptiveReplayStep* Selected = Steps.FindByPredicate([](const FAdaptiveReplayStep& Step) { return !Step.Intervention.IsNone(); });
	if (!TestNotNull(TEXT("The hits lead to a selection"), Selected))
	{
		return false;
	}
	TestEqual(TEXT("Of the registered action"), Selected->Intervention, ReplayAggro);
	TestEqual(TEXT("With id 1"), Selected->InterventionId, 1);
	TestTrue(TEXT("After the hits"), Selected->Time > 2.0 && Selected->Time < 5.0);
	TestEqual(TEXT("Running in the next update"), Steps[Selected->Index + 1].Running, ReplayAggro);
	TestTrue(TEXT("The recorded end stops it"), Steps[60].Running.IsNone());
	TestEqual(TEXT("Contexts as recorded"), Steps[100].Contexts, TArray<FName>{ ReplayCombatContext });

	// the same recording again: the same result, value for value
	Runner->Setup(nullptr);
	TestTrue(TEXT("A second run gives the same steps"), Runner->Run(Recording) == Steps);

	// the game refusing: the intervention ends at once
	TArray<FAdaptiveRecordedUpdate> Refused = Recording;
	Refused[Selected->Index].bRejected = true;
	Runner->Setup(nullptr);
	const TArray<FAdaptiveReplayStep> RefusedSteps = Runner->Run(Refused);
	TestTrue(TEXT("Rejected: not running afterwards"), RefusedSteps[Selected->Index + 1].Running.IsNone());

	// an answer that the action isn't possible: nothing selected
	TArray<FAdaptiveRecordedUpdate> Impossible = Recording;
	Impossible[0].Answers[0].bPossible = false;
	Runner->Setup(nullptr);
	TestFalse(TEXT("Impossible action: nothing selected"), Runner->Run(Impossible).ContainsByPredicate([](const FAdaptiveReplayStep& Step) { return !Step.Intervention.IsNone(); }));

	// an asset that doesn't exist: a warning, and the replay still runs
	TArray<FAdaptiveRecordedUpdate> Missing = Recording;
	(*Missing[0].Registered)[0].Asset = TEXT("/Game/DoesNotExist.DoesNotExist");
	Runner->Setup(nullptr);
	TestEqual(TEXT("Missing asset: still one step per update"), Runner->Run(Missing).Num(), Recording.Num());
	TestEqual(TEXT("Missing asset: a warning"), Runner->GetWarnings().Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveReplayLabelsTest, "AdaptiveCompanion.Replay.Labels", ReplayTestFlags)

bool FAdaptiveReplayLabelsTest::RunTest(const FString& Parameters)
{
	FAdaptiveReplayLabels Labels;
	FString Error;
	TestFalse(TEXT("Invalid JSON"), FAdaptiveReplayLabels::LoadFromString(TEXT("{"), Labels, Error));
	TestFalse(TEXT("from after to"), FAdaptiveReplayLabels::LoadFromString(TEXT("{\"labels\":[{\"from\":5,\"to\":2}]}"), Labels, Error));
	if (!TestTrue(TEXT("Loads"), FAdaptiveReplayLabels::LoadFromString(
		TEXT("{\"tolerance\":1,\"labels\":[{\"from\":2,\"to\":6,\"needs\":[\"Combat\"],\"actions\":[\"Action.DrawAggro\"],\"note\":\"hits\"}]}"), Labels, Error)))
	{
		return false;
	}

	// 10 s at 10 Hz: Combat Elevated from 3.5 s to 7.5 s, the action selected at 4 s
	TArray<FAdaptiveReplayStep> Steps;
	for (int32 Index = 0; Index < 100; ++Index)
	{
		FAdaptiveReplayStep& Step = Steps.AddDefaulted_GetRef();
		Step.Index = Index;
		Step.Time = Index * 0.1;
		Step.Needs.Add(ReplayCombat, MakeNeed(Step.Time >= 3.45 && Step.Time < 7.45 ? EAdaptiveNeedLevel::Elevated : EAdaptiveNeedLevel::Normal));
		Step.Needs.Add(TEXT("Threat"), MakeNeed(EAdaptiveNeedLevel::Normal));
		if (Index == 40)
		{
			Step.Intervention = ReplayAggro;
		}
	}

	const FAdaptiveReplayEvaluation Evaluation = FAdaptiveReplayEvaluation::Evaluate(Steps, Labels);
	// skipped: 2.0–2.9 and 6.0–6.9 (tolerance 1 s after each boundary)
	TestEqual(TEXT("Skipped updates"), Evaluation.SkippedUpdates, 20);
	TestEqual(TEXT("Counted updates"), Evaluation.CountedUpdates, 80);
	const FAdaptiveNeedConfusion& C = Evaluation.Needs.FindChecked(ReplayCombat);
	// expected 3.0–5.9 (30 updates): elevated from 3.5 → 25 TP, 5 FN; 7.0–7.4 elevated, not expected → 5 FP
	TestEqual(TEXT("True positives"), C.TruePositives, 25);
	TestEqual(TEXT("False negatives"), C.FalseNegatives, 5);
	TestEqual(TEXT("False positives"), C.FalsePositives, 5);
	TestEqual(TEXT("True negatives"), C.TrueNegatives, 45);
	TestEqual(TEXT("Precision"), C.Precision().Get(0.0), 25.0 / 30.0, 1e-9);
	TestEqual(TEXT("Recall"), C.Recall().Get(0.0), 25.0 / 30.0, 1e-9);
	const FAdaptiveNeedConfusion& Threat = Evaluation.Needs.FindChecked(TEXT("Threat"));
	TestFalse(TEXT("Never expected, never elevated: no precision"), Threat.Precision().IsSet());
	TestEqual(TEXT("All true negatives"), Threat.TrueNegatives, 80);

	if (TestEqual(TEXT("One label result"), Evaluation.Labels.Num(), 1))
	{
		TestEqual(TEXT("Detection delay"), Evaluation.Labels[0].Delays.FindRef(ReplayCombat).Get(-1.0), 1.5, 1e-6);
		TestTrue(TEXT("The fitting action was selected"), Evaluation.Labels[0].bFittingAction.Get(false));
		TestEqual(TEXT("Selected list"), Evaluation.Labels[0].Selected, TArray<FName>{ ReplayAggro });
	}

	// never elevated: no delay
	for (FAdaptiveReplayStep& Step : Steps)
	{
		Step.Needs.Add(ReplayCombat, MakeNeed(EAdaptiveNeedLevel::Normal));
		Step.Intervention = NAME_None;
	}
	const FAdaptiveReplayEvaluation Missed = FAdaptiveReplayEvaluation::Evaluate(Steps, Labels);
	TestFalse(TEXT("Missed: no delay"), Missed.Labels[0].Delays.FindRef(ReplayCombat).IsSet());
	TestFalse(TEXT("Missed: no fitting action"), Missed.Labels[0].bFittingAction.Get(true));

	return true;
}

#endif
