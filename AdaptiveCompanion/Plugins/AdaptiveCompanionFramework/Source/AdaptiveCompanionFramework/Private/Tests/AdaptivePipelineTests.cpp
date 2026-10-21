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
