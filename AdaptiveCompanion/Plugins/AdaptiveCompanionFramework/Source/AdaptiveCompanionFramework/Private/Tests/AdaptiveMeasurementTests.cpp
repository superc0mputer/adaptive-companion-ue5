#include "AdaptiveTestActionHandler.h"
#include "Measurements/AdaptiveMeasurementTables.h"
#include "Pipeline/AdaptiveBehaviorStage.h"
#include "Pipeline/AdaptiveCompanionAction.h"
#include "Pipeline/AdaptiveLatencyTracker.h"
#include "Pipeline/AdaptiveNeedEstimator.h"
#include "Pipeline/AdaptiveNeedStage.h"
#include "Pipeline/AdaptivePerfMonitor.h"
#include "Pipeline/AdaptivePipelineFrame.h"
#include "Pipeline/AdaptiveSelectionStage.h"
#include "Replay/AdaptiveReplayRunner.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AdaptiveMeasurementTests
{
	constexpr EAutomationTestFlags MeasureTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	const FName MeasureHint = TEXT("Action.NavigationHint");
	const FName MeasureCallout = TEXT("Action.ThreatCallout");
	const FName MeasureNeed = TEXT("Disorientation");

	TSharedPtr<FJsonObject> ParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}
}

using namespace AdaptiveMeasurementTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveLatencyTrackerTest, "AdaptiveCompanion.Measurements.Latency", MeasureTestFlags)

bool FAdaptiveLatencyTrackerTest::RunTest(const FString& Parameters)
{
	FAdaptiveLatencyTracker Tracker;

	FAdaptiveLatency Latency;
	Latency.Id = 1;
	Latency.Action = MeasureHint;
	Latency.Need = MeasureNeed;
	Latency.Threshold = 9.5;
	Latency.LevelChange = 10.0;
	Latency.Selected = 10.2;
	Latency.Dispatched = 10.2001;
	Latency.SelectedFrame = 100;
	Tracker.Start(Latency);

	TestEqual(TEXT("Another action's start doesn't count"), Tracker.OnStarted(MeasureCallout, 10.21, 101, true), 0);
	TestEqual(TEXT("Started: waits for its frame"), Tracker.OnStarted(MeasureHint, 10.2171, 101, true), 1);
	TestEqual(TEXT("Not finished before the frame"), Tracker.TakeFinished().Num(), 0);
	Tracker.OnEnded(MeasureHint, TEXT("completed"));
	TestEqual(TEXT("An end after the start still waits for the frame"), Tracker.GetOpen().Num(), 1);
	Tracker.OnPresented(1, 10.2421, 103);

	TArray<FAdaptiveLatency> Finished = Tracker.TakeFinished();
	if (!TestEqual(TEXT("Finished once on screen"), Finished.Num(), 1))
	{
		return false;
	}
	const FAdaptiveLatency& Done = Finished[0];
	TestEqual(TEXT("Smoothing"), Done.GetSmoothing(), 0.5, 1e-9);
	TestEqual(TEXT("Selection wait"), Done.GetSelectionWait(), 0.2, 1e-9);
	TestEqual(TEXT("Pipeline"), Done.GetPipeline(), 0.0001, 1e-9);
	TestEqual(TEXT("Handoff"), Done.GetHandoff(), 0.017, 1e-9);
	TestEqual(TEXT("Render"), Done.GetRender(), 0.025, 1e-9);
	TestEqual(TEXT("Reaction: selection to screen"), Done.GetReaction(), 0.0421, 1e-9);
	TestEqual(TEXT("Response: level change to screen"), Done.GetResponse(), 0.2421, 1e-9);
	TestEqual(TEXT("Total: threshold to screen"), Done.GetTotal(), 0.7421, 1e-9);
	TestEqual(TEXT("Frames"), Done.PresentedFrame - Done.SelectedFrame, uint64(3));
	TestTrue(TEXT("Complete"), Done.Incomplete.IsEmpty());

	// ended before it started: finished with the phase, no later spans
	Latency.Id = 2;
	Tracker.Start(Latency);
	Tracker.OnEnded(MeasureHint, TEXT("rejected"));
	Finished = Tracker.TakeFinished();
	if (TestEqual(TEXT("Rejected: finished"), Finished.Num(), 1))
	{
		TestEqual(TEXT("With the phase"), Finished[0].Incomplete, FString(TEXT("rejected")));
		TestTrue(TEXT("No handoff"), Finished[0].GetHandoff() < 0.0);
		TestTrue(TEXT("Pipeline still measured"), Finished[0].GetPipeline() >= 0.0);
	}

	// nothing rendered: finished at the start
	Latency.Id = 3;
	Tracker.Start(Latency);
	TestEqual(TEXT("Without a probe nothing waits"), Tracker.OnStarted(MeasureHint, 10.3, 105, false), 0);
	Finished = Tracker.TakeFinished();
	TestTrue(TEXT("Without a probe: not presented"), Finished.Num() == 1 && Finished[0].Incomplete == TEXT("not presented") && Finished[0].GetReaction() < 0.0);

	// a frame that never comes times out
	Latency.Id = 4;
	Tracker.Start(Latency);
	Tracker.OnStarted(MeasureHint, 11.0, 110, true);
	Tracker.Update(11.5);
	TestEqual(TEXT("Still waiting after 0.5 s"), Tracker.GetOpen().Num(), 1);
	Tracker.Update(12.1);
	Finished = Tracker.TakeFinished();
	TestTrue(TEXT("Timed out after 1 s"), Finished.Num() == 1 && Finished[0].Incomplete == TEXT("not presented"));

	// open at the session's end
	Latency.Id = 5;
	Tracker.Start(Latency);
	Finished = Tracker.Flush(TEXT("session ended"));
	TestTrue(TEXT("Flushed with the reason"), Finished.Num() == 1 && Finished[0].Incomplete == TEXT("session ended"));
	TestEqual(TEXT("Nothing open after the flush"), Tracker.GetOpen().Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveLatencyNeedTimesTest, "AdaptiveCompanion.Measurements.NeedTimes", MeasureTestFlags)

bool FAdaptiveLatencyNeedTimesTest::RunTest(const FString& Parameters)
{
	// one dimension "X" with the raw score of feature "X", default levels (enter 0.5, rise half-life 0.5 s)
	UAdaptiveNeedStage* Stage = NewObject<UAdaptiveNeedStage>();
	FAdaptiveNeedRule Rule;
	Rule.Features.Add({ TEXT("X"), false });
	FAdaptiveNeedDimension Dimension;
	Dimension.Name = TEXT("X");
	Dimension.Rules.Add(Rule);
	CastChecked<UAdaptiveRuleNeedEstimator>(Stage->Estimator)->Dimensions = { Dimension };

	auto Step = [Stage](double Time, double Raw)
	{
		FAdaptivePipelineFrame Frame;
		Frame.Time = Time;
		Frame.Features.Add(TEXT("X"), Raw);
		Stage->Update(Frame);
		return Frame.Needs.FindRef(TEXT("X"));
	};

	FAdaptiveNeedState State = Step(0.0, 0.0);
	TestEqual(TEXT("Normal: no times"), State.ElevatedSince, -1.0);
	TestEqual(TEXT("Normal: no threshold"), State.ThresholdTime, -1.0);

	// raw 1 from 1.0 s: the smoothed score follows with the rise half-life
	double Time = 0.1;
	for (; Time < 0.95; Time += 0.1)
	{
		Step(Time, 0.0);
	}
	double LevelChange = -1.0;
	for (Time = 1.0; Time < 3.0 && LevelChange < 0.0; Time += 0.1)
	{
		State = Step(Time, 1.0);
		if (State.Level != EAdaptiveNeedLevel::Normal)
		{
			LevelChange = Time;
		}
	}
	if (!TestTrue(TEXT("Rises to Elevated"), LevelChange > 0.0))
	{
		return false;
	}
	TestEqual(TEXT("Elevated since the update it rose"), State.ElevatedSince, LevelChange, 1e-9);
	TestEqual(TEXT("Threshold when the raw score crossed 0.5"), State.ThresholdTime, 1.0, 1e-9);
	TestTrue(TEXT("The smoothing delays the level"), State.ElevatedSince > State.ThresholdTime);

	// stays elevated: the times stay
	const FAdaptiveNeedState Later = Step(Time + 0.5, 1.0);
	TestEqual(TEXT("Elevated since stays"), Later.ElevatedSince, State.ElevatedSince, 1e-9);
	TestEqual(TEXT("Threshold stays"), Later.ThresholdTime, 1.0, 1e-9);

	// back to Normal resets both
	for (double Back = Time + 0.6; Back < Time + 15.0; Back += 0.1)
	{
		State = Step(Back, 0.0);
	}
	TestEqual(TEXT("Normal again"), State.Level, EAdaptiveNeedLevel::Normal);
	TestEqual(TEXT("Reset: elevated since"), State.ElevatedSince, -1.0);
	TestEqual(TEXT("Reset: threshold"), State.ThresholdTime, -1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveLatencyStageTest, "AdaptiveCompanion.Measurements.Stage", MeasureTestFlags)

bool FAdaptiveLatencyStageTest::RunTest(const FString& Parameters)
{
	UAdaptiveCompanionAction* Action = NewObject<UAdaptiveCompanionAction>();
	Action->ActionName = MeasureHint;
	Action->Relevance.Add(MeasureNeed, 1.f);
	Action->Cooldown = 0.f;
	UAdaptiveTestActionHandler* Handler = NewObject<UAdaptiveTestActionHandler>();

	FAdaptiveActionState State;
	State.Name = MeasureHint;
	State.Action = Action;
	State.Handler = Handler;
	State.bContextAllowed = true;
	State.bAvailable = true;

	UAdaptiveSelectionStage* Selection = NewObject<UAdaptiveSelectionStage>();
	Selection->MinTimeBetweenInterventions = 0.f;
	UAdaptiveBehaviorStage* Behavior = NewObject<UAdaptiveBehaviorStage>();
	TArray<FAdaptiveLatency> Latencies;
	Behavior->OnLatency = [&Latencies](const FAdaptiveLatency& Latency) { Latencies.Add(Latency); };

	FAdaptivePipelineFrame Frame;
	Frame.Time = 4.0;
	FAdaptiveNeedState Need;
	Need.Score = 0.6;
	Need.Level = EAdaptiveNeedLevel::Elevated;
	Need.ElevatedSince = 3.5;
	Need.ThresholdTime = 3.0;
	Frame.Needs.Add(MeasureNeed, Need);
	Frame.Actions = { State };
	Selection->Update(Frame);
	Behavior->Update(Frame);
	if (!TestEqual(TEXT("Selected"), Frame.Intervention, MeasureHint))
	{
		return false;
	}
	if (!TestEqual(TEXT("Its latency is open"), Behavior->GetLatencies().GetOpen().Num(), 1))
	{
		return false;
	}
	const FAdaptiveLatency& Open = Behavior->GetLatencies().GetOpen()[0];
	TestEqual(TEXT("Id from the selection"), Open.Id, Frame.InterventionId);
	TestEqual(TEXT("For the need it was selected for"), Open.Need, MeasureNeed);
	TestEqual(TEXT("Threshold from the need"), Open.Threshold, 3.0);
	TestEqual(TEXT("Level change from the need"), Open.LevelChange, 3.5);
	TestEqual(TEXT("Selected at the update's time"), Open.Selected, 4.0);
	TestFalse(TEXT("The first for this episode"), Open.bRepeat);

	// outside a game nothing is rendered: it finishes at "started"
	Behavior->ReportPhase(MeasureHint, TEXT("started"));
	if (TestEqual(TEXT("Finished at started without a probe"), Latencies.Num(), 1))
	{
		TestEqual(TEXT("Selection wait"), Latencies[0].GetSelectionWait(), 0.5, 1e-9);
		TestEqual(TEXT("Not presented"), Latencies[0].Incomplete, FString(TEXT("not presented")));
	}

	// selected again while the need stays up: a repeat
	Behavior->ReportPhase(MeasureHint, TEXT("completed"));
	FAdaptivePipelineFrame Again = Frame;
	Again.Time = 20.0;
	Again.Intervention = NAME_None;
	Again.Evaluations.Reset();
	Again.Needs[MeasureNeed].Score = 0.95;
	Again.RunningIntervention = Behavior->UpdateRunning(Again.Time);
	Selection->Update(Again);
	Behavior->Update(Again);
	if (TestEqual(TEXT("Selected again"), Again.Intervention, MeasureHint))
	{
		Behavior->ReportPhase(MeasureHint, TEXT("started"));
		TestTrue(TEXT("Same episode: a repeat"), Latencies.Num() == 2 && Latencies[1].bRepeat);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptivePerfMonitorTest, "AdaptiveCompanion.Measurements.Perf", MeasureTestFlags)

bool FAdaptivePerfMonitorTest::RunTest(const FString& Parameters)
{
	// nearest rank: 1..100 → p50 = 50, p95 = 95, p99 = 99
	TArray<float> Samples;
	for (int32 Value = 100; Value >= 1; --Value)
	{
		Samples.Add(float(Value));
	}
	const FAdaptiveStats Stats = FAdaptiveStats::Of(Samples);
	TestEqual(TEXT("Count"), Stats.Count, 100);
	TestEqual(TEXT("Mean"), Stats.Mean, 50.5, 1e-6);
	TestEqual(TEXT("p50"), Stats.P50, 50.0);
	TestEqual(TEXT("p95"), Stats.P95, 95.0);
	TestEqual(TEXT("p99"), Stats.P99, 99.0);
	TestEqual(TEXT("Max"), Stats.Max, 100.0);
	TestEqual(TEXT("Empty"), FAdaptiveStats::Of({}).Count, 0);
	TestEqual(TEXT("One sample: every percentile is it"), FAdaptiveStats::Of({ 7.f }).P99, 7.0);

	FAdaptivePerfMonitor Monitor;
	Monitor.Reset({ TEXT("Telemetry"), TEXT("Recording") });
	for (int32 Index = 0; Index < 10; ++Index)
	{
		FAdaptiveUpdateCost Cost;
		Cost.Total = 100e-6;
		Cost.Parts = { 60e-6, 40e-6 };
		Cost.InputWait = 0.05;
		Cost.Events = 2;
		Cost.Values = 10;
		Monitor.AddUpdate(Cost);
	}
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Monitor.AddApi(5e-6);
		Monitor.EndFrame(1.0 / 60.0, 0.01, 0.008, 0.012);
	}

	const TSharedRef<FJsonObject> Window = Monitor.TakeWindow(1.0);
	TestEqual(TEXT("Updates"), Window->GetNumberField(TEXT("updates")), 10.0);
	TestEqual(TEXT("Frames"), Window->GetNumberField(TEXT("frames")), 60.0);
	TestEqual(TEXT("Update mean in µs"), Window->GetObjectField(TEXT("update_us"))->GetNumberField(TEXT("mean")), 100.0, 0.01);
	TestEqual(TEXT("Per part"), Window->GetObjectField(TEXT("parts_us"))->GetObjectField(TEXT("Telemetry"))->GetNumberField(TEXT("mean")), 60.0, 0.01);
	TestEqual(TEXT("API per frame"), Window->GetObjectField(TEXT("api_us_per_frame"))->GetNumberField(TEXT("mean")), 5.0, 0.01);
	TestEqual(TEXT("Input wait"), Window->GetObjectField(TEXT("input_wait_ms"))->GetNumberField(TEXT("max")), 50.0, 0.01);
	TestEqual(TEXT("Events per update"), Window->GetObjectField(TEXT("load_per_update"))->GetNumberField(TEXT("events")), 2.0);
	// 10 × 100 µs + 60 × 5 µs = 1.3 ms over 60 frames
	TestEqual(TEXT("Pipeline per frame"), Window->GetNumberField(TEXT("pipeline_us_per_frame")), 1300.0 / 60.0, 0.01);
	// 1.3 ms of 60 × 10 ms game thread
	TestEqual(TEXT("Share of the game thread"), Window->GetNumberField(TEXT("game_thread_share_pct")), 1.3 / 600.0 * 100.0, 1e-4);
	TestEqual(TEXT("Game thread mean"), Window->GetNumberField(TEXT("game_thread_ms")), 10.0, 1e-6);
	TestFalse(TEXT("Overlay text"), Monitor.GetOverlayText().IsEmpty());

	const TSharedRef<FJsonObject> Empty = Monitor.TakeWindow(1.0);
	TestEqual(TEXT("The next window starts empty"), Empty->GetNumberField(TEXT("updates")), 0.0);
	TestEqual(TEXT("The session keeps everything"), Monitor.GetSummary(2.0)->GetNumberField(TEXT("updates")), 10.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveMeasurementTablesTest, "AdaptiveCompanion.Measurements.Tables", MeasureTestFlags)

bool FAdaptiveMeasurementTablesTest::RunTest(const FString& Parameters)
{
	FAdaptiveMeasurementTables Tables;
	Tables.AddRecord(TEXT("s1"), TEXT("latency"), 12.0, ParseJson(TEXT(R"({"id":1,"name":"Action.NavigationHint","need":"Disorientation",
		"times":{"threshold":9.5,"level_change":10,"selected":10.2,"dispatched":10.2001,"started":10.2171,"presented":10.2421},
		"ms":{"smoothing":500,"selection_wait":200,"pipeline":0.1,"handoff":17,"render":25,"reaction":42.1,"response":242.1,"total":742.1},
		"frames":{"handoff":1,"render":2}})")));
	Tables.AddRecord(TEXT("s1"), TEXT("latency"), 16.0, ParseJson(TEXT(R"({"id":3,"name":"Action.NavigationHint","need":"Disorientation","repeat":true,
		"times":{"threshold":9.5,"level_change":10,"selected":15.2,"dispatched":15.2001,"started":15.2171,"presented":15.2421},
		"ms":{"smoothing":500,"selection_wait":5200,"pipeline":0.1,"handoff":17,"render":25,"reaction":42.1,"response":5242.1,"total":5742.1},
		"frames":{"handoff":1,"render":2}})")));
	Tables.AddRecord(TEXT("s1"), TEXT("latency"), 20.0, ParseJson(TEXT(R"({"id":2,"name":"Action.ThreatCallout","need":"Threat",
		"times":{"threshold":18,"level_change":18.4,"selected":18.4,"dispatched":18.4001,"started":null,"presented":null},
		"ms":{"smoothing":400,"selection_wait":0,"pipeline":0.1,"handoff":null,"render":null,"reaction":null,"response":null,"total":null},
		"frames":{},"incomplete":"failed"})")));
	Tables.AddRecord(TEXT("s1"), TEXT("perf"), 5.0, ParseJson(TEXT(R"({"duration":5,"updates":50,"frames":300,
		"update_us":{"count":50,"mean":80,"p50":75,"p95":120,"p99":150,"max":160},
		"parts_us":{"Telemetry":{"count":50,"mean":10,"p50":10,"p95":12,"p99":13,"max":14},"Recording":{"count":50,"mean":20,"p50":20,"p95":22,"p99":23,"max":24}},
		"api_us_per_frame":{"count":300,"mean":3,"p50":2,"p95":6,"p99":8,"max":9},"api_calls_per_frame":4,
		"input_wait_ms":{"count":50,"mean":40,"p50":40,"p95":90,"p99":95,"max":99},
		"load_per_update":{"events":1,"values":20,"features":14,"needs":4,"actions":3},
		"frame_ms":{"count":300,"mean":16.7,"p50":16.6,"p95":18,"p99":20,"max":22},
		"game_thread_ms":9,"render_thread_ms":7,"gpu_ms":11,"pipeline_us_per_frame":16.3,"game_thread_share_pct":0.18})")));
	Tables.AddRecord(TEXT("s1"), TEXT("signal"), 1.0, ParseJson(TEXT(R"({"name":"x"})")));

	const FAdaptiveTable Latency = Tables.GetLatencyTable();
	TestEqual(TEXT("One row per intervention"), Latency.Rows.Num(), 3);
	const int32 ResponseColumn = Latency.Columns.IndexOfByKey(TEXT("response_ms"));
	const int32 IncompleteColumn = Latency.Columns.IndexOfByKey(TEXT("incomplete"));
	if (TestTrue(TEXT("Columns"), ResponseColumn != INDEX_NONE && IncompleteColumn != INDEX_NONE))
	{
		TestEqual(TEXT("Response"), Latency.Rows[0][ResponseColumn], FString(TEXT("242.1")));
		TestEqual(TEXT("Null is an empty cell"), Latency.Rows[2][ResponseColumn], FString());
		TestEqual(TEXT("Incomplete"), Latency.Rows[2][IncompleteColumn], FString(TEXT("failed")));
	}

	const FAdaptiveTable Summary = Tables.GetLatencySummary();
	TestEqual(TEXT("Per action plus all"), Summary.Rows.Num(), 3);
	TestEqual(TEXT("All last"), Summary.Rows.Last()[0], FString(TEXT("all")));
	const int32 OnScreen = Summary.Columns.IndexOfByKey(TEXT("on screen"));
	TestTrue(TEXT("All: 3 interventions, 2 on screen"), OnScreen != INDEX_NONE && Summary.Rows.Last()[1] == TEXT("3") && Summary.Rows.Last()[OnScreen] == TEXT("2"));
	const int32 ResponseMax = Summary.Columns.IndexOfByKey(TEXT("level change → screen max ms"));
	const int32 ReactionMax = Summary.Columns.IndexOfByKey(TEXT("selection → screen max ms"));
	TestTrue(TEXT("Repeats leave the response out"), ResponseMax != INDEX_NONE && Summary.Rows[0][ResponseMax] == TEXT("242.1"));
	TestTrue(TEXT("Repeats count for the reaction"), ReactionMax != INDEX_NONE && Summary.Rows[0][ReactionMax] == TEXT("42.1"));

	const FAdaptiveTable Perf = Tables.GetPerfWindowTable();
	TestEqual(TEXT("One row per window"), Perf.Rows.Num(), 1);
	const int32 PartColumn = Perf.Columns.IndexOfByKey(TEXT("Recording_mean_us"));
	TestTrue(TEXT("A column per part"), PartColumn != INDEX_NONE && Perf.Rows[0][PartColumn] == TEXT("20"));
	TestEqual(TEXT("No summary records"), Tables.GetPerfSummaryTable().Rows.Num(), 0);

	const FString Csv = Latency.ToCsv();
	TestTrue(TEXT("CSV header"), Csv.StartsWith(TEXT("session,id,action,need,repeat,threshold_s,")));
	TestTrue(TEXT("Markdown fills missing cells"), Summary.ToMarkdown().Contains(TEXT("| – |")));
	TestEqual(TEXT("Numbers without trailing zeros"), FAdaptiveTable::Number(12.50, 2), FString(TEXT("12.5")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveBenchmarkCopiesTest, "AdaptiveCompanion.Measurements.Copies", MeasureTestFlags)

bool FAdaptiveBenchmarkCopiesTest::RunTest(const FString& Parameters)
{
	// the defaults: one feature (Combat.DamageRecent on Combat.DamageTaken), one dimension; hits at 2 s
	TArray<FAdaptiveRecordedUpdate> Recording;
	for (int32 Index = 0; Index < 50; ++Index)
	{
		FAdaptiveRecordedUpdate& Update = Recording.AddDefaulted_GetRef();
		Update.Index = Index;
		Update.Time = Index * 0.1;
		if (Index == 20)
		{
			Update.Events.Add({ TEXT("Combat.DamageTaken"), 3.0, 1.95 });
		}
	}

	UAdaptiveReplayRunner* Runner = NewObject<UAdaptiveReplayRunner>();
	Runner->Setup(nullptr);
	const TArray<FAdaptiveReplayStep> Single = Runner->Run(Recording);

	Runner->bMeasure = true;
	Runner->Setup(nullptr, 3);
	const TArray<FAdaptiveReplayStep> Tripled = Runner->Run(Recording);
	TestEqual(TEXT("Three times the features"), Runner->GetFeatureCount(), 3);
	TestEqual(TEXT("Three times the dimensions"), Runner->GetDimensionCount(), 3);
	TestEqual(TEXT("A cost per update"), Runner->GetCosts().Num(), Recording.Num());
	TestEqual(TEXT("A part per stage"), Runner->GetCosts()[0].Parts.Num(), Runner->GetStageNames().Num());
	TestEqual(TEXT("Each copy gets the events"), Runner->GetCosts()[20].Events, 3);
	TestTrue(TEXT("Measured"), Runner->GetCosts()[20].Total > 0.0);

	const FName Copy = TEXT("Combat#2");
	const FAdaptiveNeedState* Original = Tripled[25].Needs.Find(TEXT("Combat"));
	const FAdaptiveNeedState* Copied = Tripled[25].Needs.Find(Copy);
	if (TestNotNull(TEXT("The original dimension"), Original) && TestNotNull(TEXT("The copy"), Copied))
	{
		TestEqual(TEXT("The copy computes the same"), Copied->Score, Original->Score, 1e-12);
		TestTrue(TEXT("The hits raise it"), Original->RawScore > 0.0);
		TestEqual(TEXT("The original as without copies"), Original->Score, Single[25].Needs.FindRef(TEXT("Combat")).Score, 1e-12);
	}

	// copies never change the config's estimator
	Runner->Setup(nullptr);
	TestEqual(TEXT("A new setup starts from the config again"), Runner->GetDimensionCount(), 1);

	return true;
}

#endif
