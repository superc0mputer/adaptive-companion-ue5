#include "Pipeline/AdaptivePipelineConfig.h"
#include "Pipeline/AdaptivePipelineFrame.h"
#include "Pipeline/AdaptivePipelineStages.h"
#include "Pipeline/AdaptiveTelemetryStage.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptivePipelineFixedRateTest, "AdaptiveCompanion.Pipeline.FixedRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FAdaptivePipelineFixedRateTest::RunTest(const FString& Parameters)
{
	auto CountUpdates = [](double Rate, double FrameRate, double Seconds)
	{
		FAdaptiveFixedRate FixedRate(1.0 / Rate);
		int32 Updates = 0;
		for (int32 FrameIndex = 0; FrameIndex / FrameRate < Seconds; ++FrameIndex)
		{
			Updates += FixedRate.Step(FrameIndex / FrameRate) ? 1 : 0;
		}
		return Updates;
	};

	TestEqual(TEXT("10 Hz at 60 fps for 10 s"), CountUpdates(10.0, 60.0, 10.0), 100);
	TestEqual(TEXT("10 Hz at 144 fps for 10 s"), CountUpdates(10.0, 144.0, 10.0), 100);
	TestEqual(TEXT("30 Hz at 45 fps for 10 s"), CountUpdates(30.0, 45.0, 10.0), 300);
	TestEqual(TEXT("Frame rate below the update rate: one update per frame"), CountUpdates(10.0, 5.0, 10.0), 50);

	FAdaptiveFixedRate FixedRate(0.1);
	TestTrue(TEXT("First step is due"), FixedRate.Step(0.0));
	TestFalse(TEXT("Not due before the interval"), FixedRate.Step(0.05));
	TestTrue(TEXT("Due after a hitch"), FixedRate.Step(1.0));
	TestFalse(TEXT("No burst of missed updates after a hitch"), FixedRate.Step(1.0));
	TestFalse(TEXT("Interval restarts after a hitch"), FixedRate.Step(1.05));
	TestTrue(TEXT("Due one interval after the hitch"), FixedRate.Step(1.1));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptivePipelineTelemetryTest, "AdaptiveCompanion.Pipeline.Telemetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FAdaptivePipelineTelemetryTest::RunTest(const FString& Parameters)
{
	// outside a session: no pipeline, so nothing is logged or shown
	UAdaptiveTelemetryStage* Stage = NewObject<UAdaptiveTelemetryStage>();

	Stage->ReportValue(TEXT("Player.Health"), 0.8);
	Stage->ReportValue(TEXT("Player.Health"), 0.6);
	Stage->ReportValue(TEXT("Player.Speed"), 300.0);
	Stage->ReportEvent(TEXT("Combat.DamageTaken"), 25.0);
	Stage->ReportEvent(TEXT("Combat.DamageTaken"), 10.0);

	FAdaptivePipelineFrame First;
	Stage->Update(First);

	TestEqual(TEXT("Both events passed on"), First.Events.Num(), 2);
	if (First.Events.Num() == 2)
	{
		TestEqual(TEXT("Events keep their order"), First.Events[0].Value, 25.0);
		TestEqual(TEXT("Event name"), First.Events[1].Name, FName(TEXT("Combat.DamageTaken")));
	}
	TestEqual(TEXT("Two values"), First.Values.Num(), 2);
	TestEqual(TEXT("Only the latest value counts"), First.Values.FindRef(TEXT("Player.Health")), 0.6);

	FAdaptivePipelineFrame Second;
	Stage->Update(Second);

	TestTrue(TEXT("Events are only passed on once"), Second.Events.IsEmpty());
	TestEqual(TEXT("Values are kept until changed"), Second.Values.FindRef(TEXT("Player.Speed")), 300.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptivePipelineFeatureNeedTest, "AdaptiveCompanion.Pipeline.FeatureNeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FAdaptivePipelineFeatureNeedTest::RunTest(const FString& Parameters)
{
	// defaults: damage in the last 5 s / 5, Combat elevated from 0.6
	UAdaptiveFeatureStage* Features = NewObject<UAdaptiveFeatureStage>();
	UAdaptiveNeedStage* Need = NewObject<UAdaptiveNeedStage>();

	auto Run = [&](double Time, TArray<FAdaptiveTelemetryEvent> Events)
	{
		FAdaptivePipelineFrame Frame;
		Frame.Time = Time;
		Frame.Events = MoveTemp(Events);
		Features->Update(Frame);
		Need->Update(Frame);
		return Frame;
	};

	FAdaptivePipelineFrame Frame = Run(0.0, {});
	TestEqual(TEXT("No damage, feature 0"), Frame.Features.FindRef(TEXT("Combat.DamageRecent")), 0.0);
	TestFalse(TEXT("No damage, not elevated"), Frame.Needs.FindRef(TEXT("Combat")).bElevated);

	Frame = Run(1.0, { { TEXT("Combat.DamageTaken"), 1.0, 0.9 }, { TEXT("Other.Event"), 100.0, 0.9 } });
	TestEqual(TEXT("Only the configured event counts"), Frame.Features.FindRef(TEXT("Combat.DamageRecent")), 0.2);

	Frame = Run(2.0, { { TEXT("Combat.DamageTaken"), 1.0, 1.9 } });
	TestFalse(TEXT("0.4 is below the threshold"), Frame.Needs.FindRef(TEXT("Combat")).bElevated);

	Frame = Run(3.0, { { TEXT("Combat.DamageTaken"), 1.0, 2.9 } });
	TestEqual(TEXT("Damage is summed over the window"), Frame.Features.FindRef(TEXT("Combat.DamageRecent")), 0.6, 1e-9);
	TestTrue(TEXT("0.6 is elevated"), Frame.Needs.FindRef(TEXT("Combat")).bElevated);
	TestTrue(TEXT("Change is flagged"), Frame.Needs.FindRef(TEXT("Combat")).bChanged);

	Frame = Run(3.1, {});
	TestTrue(TEXT("Still elevated"), Frame.Needs.FindRef(TEXT("Combat")).bElevated);
	TestFalse(TEXT("Change is only flagged once"), Frame.Needs.FindRef(TEXT("Combat")).bChanged);

	Frame = Run(20.0, { { TEXT("Combat.DamageTaken"), 30.0, 20.0 } });
	TestEqual(TEXT("Clamped to 1"), Frame.Features.FindRef(TEXT("Combat.DamageRecent")), 1.0);

	Frame = Run(25.0, {});
	TestEqual(TEXT("Damage leaves the window after 5 s"), Frame.Features.FindRef(TEXT("Combat.DamageRecent")), 0.0);
	TestFalse(TEXT("Back to normal"), Frame.Needs.FindRef(TEXT("Combat")).bElevated);
	TestTrue(TEXT("Change back is flagged"), Frame.Needs.FindRef(TEXT("Combat")).bChanged);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptivePipelineContextTest, "AdaptiveCompanion.Pipeline.Context",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FAdaptivePipelineContextTest::RunTest(const FString& Parameters)
{
	const FName Combat = TEXT("Context.Combat");
	const FName Navigation = TEXT("Context.Navigation");

	UAdaptiveContextStage* Stage = NewObject<UAdaptiveContextStage>();

	auto Run = [&]()
	{
		FAdaptivePipelineFrame Frame;
		Stage->Update(Frame);
		return Frame;
	};

	FAdaptivePipelineFrame Frame = Run();
	TestTrue(TEXT("No context at the start"), Frame.Contexts.IsEmpty());
	TestFalse(TEXT("No change at the start"), Frame.bContextChanged);

	Stage->SetContext(Navigation, true);
	Frame = Run();
	TestTrue(TEXT("Navigation active"), Frame.Contexts.Contains(Navigation));
	TestTrue(TEXT("Change is flagged"), Frame.bContextChanged);

	Frame = Run();
	TestTrue(TEXT("Stays active"), Frame.Contexts.Contains(Navigation));
	TestFalse(TEXT("Change is only flagged once"), Frame.bContextChanged);

	Stage->SetContext(Navigation, true);
	Frame = Run();
	TestFalse(TEXT("Setting the same state again is no change"), Frame.bContextChanged);

	Stage->SetContext(Combat, true);
	Frame = Run();
	TestEqual(TEXT("Several contexts at once"), Frame.Contexts.Num(), 2);
	TestTrue(TEXT("Combat added"), Frame.bContextChanged);
	TestEqual(TEXT("Contexts in the order they became active"), Stage->GetActiveContexts(), TArray<FName>{ Navigation, Combat });

	Stage->SetContext(Navigation, false);
	Frame = Run();
	TestFalse(TEXT("Navigation removed"), Frame.Contexts.Contains(Navigation));
	TestTrue(TEXT("Combat still active"), Frame.Contexts.Contains(Combat));
	TestTrue(TEXT("Removal is flagged"), Frame.bContextChanged);

	Stage->SetContext(Combat, false);
	Stage->SetContext(Combat, true);
	Frame = Run();
	TestFalse(TEXT("Off and on again between two updates is no change"), Frame.bContextChanged);

	Stage->SetContext(NAME_None, true);
	TestFalse(TEXT("NAME_None is ignored"), Stage->IsContextActive(NAME_None));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptivePipelineSelectionTest, "AdaptiveCompanion.Pipeline.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FAdaptivePipelineSelectionTest::RunTest(const FString& Parameters)
{
	UAdaptiveSelectionStage* Selection = NewObject<UAdaptiveSelectionStage>();

	auto Select = [&](bool bElevated, bool bChanged)
	{
		FAdaptivePipelineFrame Frame;
		Frame.Needs.Add(TEXT("Combat"), { 0.7, bElevated, bChanged });
		Selection->Update(Frame);
		return Frame.Intervention;
	};

	TestEqual(TEXT("Selected when the need becomes elevated"), Select(true, true), FName(TEXT("Intervention.Combat.Placeholder")));
	TestTrue(TEXT("Not again while it stays elevated"), Select(true, false).IsNone());
	TestTrue(TEXT("Not when it goes back to normal"), Select(false, true).IsNone());

	FAdaptivePipelineFrame Empty;
	Selection->Update(Empty);
	TestTrue(TEXT("Nothing without the need"), Empty.Intervention.IsNone());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptivePipelineConfigTest, "AdaptiveCompanion.Pipeline.Config",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FAdaptivePipelineConfigTest::RunTest(const FString& Parameters)
{
	const UAdaptivePipelineConfig* Config = GetDefault<UAdaptivePipelineConfig>();

	TestTrue(TEXT("Update rate in range"), Config->UpdateRate >= 1.f && Config->UpdateRate <= 120.f);
	TestTrue(TEXT("Telemetry stage"), Config->Telemetry && Config->Telemetry->GetClass() == UAdaptiveTelemetryStage::StaticClass());
	TestTrue(TEXT("Feature stage"), Config->Features && Config->Features->GetClass() == UAdaptiveFeatureStage::StaticClass());
	TestTrue(TEXT("Need stage"), Config->Need && Config->Need->GetClass() == UAdaptiveNeedStage::StaticClass());
	TestTrue(TEXT("Context stage"), Config->Context && Config->Context->GetClass() == UAdaptiveContextStage::StaticClass());
	TestTrue(TEXT("Selection stage"), Config->Selection && Config->Selection->GetClass() == UAdaptiveSelectionStage::StaticClass());
	TestTrue(TEXT("Behavior stage"), Config->Behavior && Config->Behavior->GetClass() == UAdaptiveBehaviorStage::StaticClass());

	return true;
}

#endif
