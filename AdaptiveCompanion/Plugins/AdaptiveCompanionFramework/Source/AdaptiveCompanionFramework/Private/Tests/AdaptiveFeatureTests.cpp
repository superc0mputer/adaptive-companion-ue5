#include "Pipeline/AdaptiveFeatureStage.h"
#include "Pipeline/AdaptiveFeatureTypes.h"
#include "Pipeline/AdaptivePipelineFrame.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags FeatureTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	FAdaptiveFeatureDefinition MakeDefinition(const TCHAR* Name, const TCHAR* Signal, EAdaptiveFeatureAggregation Aggregation, float Window, double Min, double Max)
	{
		FAdaptiveFeatureDefinition Definition;
		Definition.Name = Name;
		Definition.Signal = Signal;
		Definition.Aggregation = Aggregation;
		Definition.Window = Window;
		Definition.Min = Min;
		Definition.Max = Max;
		return Definition;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveFeatureAggregationTest, "AdaptiveCompanion.Pipeline.Features.Aggregations", FeatureTestFlags)

bool FAdaptiveFeatureAggregationTest::RunTest(const FString& Parameters)
{
	using namespace AdaptiveFeatureMath;
	using EAgg = EAdaptiveFeatureAggregation;

	const TArray<FAdaptiveFeatureSample> Values = { { 0.0, 10.0 }, { 1.0, 8.0 }, { 2.0, 6.0 }, { 3.0, 4.0 } };
	const TArray<FAdaptiveFeatureSample> Events = { { 0.5, 2.0 }, { 1.5, 3.0 } };
	const TArray<FAdaptiveFeatureSample> NoSamples;

	TestEqual(TEXT("Latest is the newest sample"), Aggregate(EAgg::Latest, Values).Get(-1.0), 4.0);
	TestEqual(TEXT("Mean"), Aggregate(EAgg::Mean, Values).Get(-1.0), 7.0);
	TestEqual(TEXT("Slope of a line is its rate"), Aggregate(EAgg::Slope, Values).Get(0.0), -2.0, 1e-9);
	TestEqual(TEXT("FractionBelow"), Aggregate(EAgg::FractionBelow, Values, {}, 7.0).Get(-1.0), 0.5);
	TestEqual(TEXT("FractionAbove excludes the threshold itself"), Aggregate(EAgg::FractionAbove, Values, {}, 8.0).Get(-1.0), 0.25);
	TestEqual(TEXT("Count"), Aggregate(EAgg::Count, Events).Get(-1.0), 2.0);
	TestEqual(TEXT("Sum"), Aggregate(EAgg::Sum, Events).Get(-1.0), 5.0);
	TestEqual(TEXT("Ratio counts events, not values"), Aggregate(EAgg::Ratio, Events, Values).Get(-1.0), 0.5);

	// noisy samples around a line: least squares still finds its rate
	const TArray<FAdaptiveFeatureSample> Noisy = { { 10.0, 20.1 }, { 10.1, 19.4 }, { 10.2, 19.1 }, { 10.3, 18.3 }, { 10.4, 18.0 }, { 10.5, 17.6 } };
	TestEqual(TEXT("Slope through noise"), Aggregate(EAgg::Slope, Noisy).Get(0.0), -5.0, 0.5);

	TestEqual(TEXT("No events: count 0"), Aggregate(EAgg::Count, NoSamples).Get(-1.0), 0.0);
	TestEqual(TEXT("No events: sum 0"), Aggregate(EAgg::Sum, NoSamples).Get(-1.0), 0.0);
	TestFalse(TEXT("No denominator events: no ratio"), Aggregate(EAgg::Ratio, Events, NoSamples).IsSet());
	TestEqual(TEXT("No numerator events: ratio 0"), Aggregate(EAgg::Ratio, NoSamples, Events).Get(-1.0), 0.0);
	TestFalse(TEXT("No values: no latest"), Aggregate(EAgg::Latest, NoSamples).IsSet());
	TestFalse(TEXT("No values: no mean"), Aggregate(EAgg::Mean, NoSamples).IsSet());
	TestFalse(TEXT("One sample: no slope"), Aggregate(EAgg::Slope, TArray<FAdaptiveFeatureSample>{ { 0.0, 1.0 } }).IsSet());
	TestFalse(TEXT("Slope over less than the minimum span"), Aggregate(EAgg::Slope, Values, {}, 0.0, 5.0).IsSet());
	TestTrue(TEXT("Slope over the minimum span"), Aggregate(EAgg::Slope, Values, {}, 0.0, 3.0).IsSet());

	TestEqual(TEXT("After drops samples up to the start"), After(Values, 1.0).Num(), 2);
	TestEqual(TEXT("After keeps everything before the first"), After(Values, -1.0).Num(), 4);
	TestEqual(TEXT("After drops everything past the last"), After(Values, 3.0).Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveFeatureNormalizeTest, "AdaptiveCompanion.Pipeline.Features.Normalize", FeatureTestFlags)

bool FAdaptiveFeatureNormalizeTest::RunTest(const FString& Parameters)
{
	using namespace AdaptiveFeatureMath;

	TestEqual(TEXT("Linear between min and max"), Normalize(5.0, 0.0, 20.0, false), 0.25);
	TestEqual(TEXT("Clamped below"), Normalize(-3.0, 0.0, 20.0, false), 0.0);
	TestEqual(TEXT("Clamped above"), Normalize(30.0, 0.0, 20.0, false), 1.0);
	TestEqual(TEXT("Inverted"), Normalize(5.0, 0.0, 20.0, true), 0.75);
	TestEqual(TEXT("Inverted and clamped"), Normalize(30.0, 0.0, 20.0, true), 0.0);
	TestEqual(TEXT("Negative range"), Normalize(-1.0, -3.0, 0.0, false), 2.0 / 3.0, 1e-9);
	TestEqual(TEXT("Min equal to max: step, below"), Normalize(0.9, 1.0, 1.0, false), 0.0);
	TestEqual(TEXT("Min equal to max: step, at"), Normalize(1.0, 1.0, 1.0, false), 1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveFeatureStageTest, "AdaptiveCompanion.Pipeline.Features.Stage", FeatureTestFlags)

bool FAdaptiveFeatureStageTest::RunTest(const FString& Parameters)
{
	using EAgg = EAdaptiveFeatureAggregation;

	UAdaptiveFeatureStage* Stage = NewObject<UAdaptiveFeatureStage>();
	Stage->Features.Reset();

	// distance falls by 1 m/s; progress is the slope over 4 s, 0 m/s -> 1, -2 m/s -> 0
	FAdaptiveFeatureDefinition NoProgress = MakeDefinition(TEXT("NoProgress"), TEXT("Distance"), EAgg::Slope, 4.f, -2.0, 0.0);
	NoProgress.ResetEvent = TEXT("Reached");
	NoProgress.OnlyWhileNonZero = TEXT("Distance");
	NoProgress.Default = 0.0;
	Stage->Features.Add(NoProgress);

	Stage->Features.Add(MakeDefinition(TEXT("Health"), TEXT("Health"), EAgg::Latest, 1.f, 0.0, 1.0));
	Stage->Features.Last().bInvert = true;
	Stage->Features.Last().Default = 0.25;

	Stage->Features.Add(MakeDefinition(TEXT("Still"), TEXT("Speed"), EAgg::FractionBelow, 2.f, 0.0, 1.0));
	Stage->Features.Last().Threshold = 0.5;

	FAdaptiveFeatureDefinition MissRate = MakeDefinition(TEXT("MissRate"), TEXT("Hit"), EAgg::Ratio, 3.f, 0.0, 1.0);
	MissRate.Denominator = TEXT("Attack");
	MissRate.bInvert = true;
	MissRate.Default = 0.1;
	Stage->Features.Add(MissRate);

	Stage->Features.Add(MakeDefinition(TEXT("Hits"), TEXT("Hit"), EAgg::Count, 1.f, 0.0, 4.0));

	// skipped with a warning: no signal, ratio without denominator, duplicate name
	Stage->Features.Add(MakeDefinition(TEXT("NoSignal"), TEXT(""), EAgg::Mean, 1.f, 0.0, 1.0));
	Stage->Features.Add(MakeDefinition(TEXT("BadRatio"), TEXT("Hit"), EAgg::Ratio, 1.f, 0.0, 1.0));
	Stage->Features.Add(MakeDefinition(TEXT("Hits"), TEXT("Attack"), EAgg::Count, 1.f, 0.0, 1.0));
	AddExpectedMessage(TEXT("is skipped"), EAutomationExpectedMessageFlags::Contains, 3);

	TMap<FName, double> Values;
	auto Run = [&](double Time, TArray<FAdaptiveTelemetryEvent> Events = {})
	{
		FAdaptivePipelineFrame Frame;
		Frame.Time = Time;
		Frame.Events = MoveTemp(Events);
		Frame.Values = Values;
		Stage->Update(Frame);
		return Frame.Features;
	};

	TMap<FName, double> Features = Run(0.0);
	TestEqual(TEXT("Invalid definitions are skipped"), Features.Num(), 5);
	TestEqual(TEXT("Nothing reported: default"), Features.FindRef(TEXT("Health")), 0.25);
	TestEqual(TEXT("Gate closed: default"), Features.FindRef(TEXT("NoProgress")), 0.0);
	TestEqual(TEXT("No attacks: ratio default"), Features.FindRef(TEXT("MissRate")), 0.1);
	TestEqual(TEXT("No events: count 0"), Features.FindRef(TEXT("Hits")), 0.0);

	// walking toward the objective at 1 m/s, sampled at 10 Hz
	Values.Add(TEXT("Health"), 0.8);
	Values.Add(TEXT("Speed"), 1.0);
	double Time = 0.0;
	for (int32 Step = 1; Step <= 30; ++Step)
	{
		Time = Step * 0.1;
		Values.Add(TEXT("Distance"), 20.0 - Time);
		Features = Run(Time);
		if (Step == 15)
		{
			TestEqual(TEXT("Slope window less than half full: default"), Features.FindRef(TEXT("NoProgress")), 0.0);
		}
	}
	TestEqual(TEXT("Latest, inverted"), Features.FindRef(TEXT("Health")), 0.2, 1e-9);
	TestEqual(TEXT("Moving: not standing still"), Features.FindRef(TEXT("Still")), 0.0);
	TestEqual(TEXT("Progressing at 1 m/s: half way to no progress"), Features.FindRef(TEXT("NoProgress")), 0.5, 1e-6);
	TestEqual(TEXT("Raw value is the slope"), Stage->GetRawValue(TEXT("NoProgress")).Get(0.0), -1.0, 1e-6);

	// standing still for 1 s of the 2 s window, distance unchanged
	for (int32 Step = 1; Step <= 10; ++Step)
	{
		Time += 0.1;
		Values.Add(TEXT("Speed"), 0.0);
		Features = Run(Time);
	}
	TestEqual(TEXT("Half of the window below the threshold"), Features.FindRef(TEXT("Still")), 0.5, 0.06);
	TestTrue(TEXT("Less progress while standing"), Features.FindRef(TEXT("NoProgress")) > 0.5);

	// reaching the objective restarts the window, the distance jumps to the next one
	Features = Run(Time + 0.1, { { TEXT("Reached"), 1.0, Time + 0.05 } });
	Values.Add(TEXT("Distance"), 40.0);
	Time += 0.2;
	Features = Run(Time);
	TestEqual(TEXT("Reset event: window restarts, default until half full"), Features.FindRef(TEXT("NoProgress")), 0.0);
	TestFalse(TEXT("Reset event: no raw value yet"), Stage->GetRawValue(TEXT("NoProgress")).IsSet());

	// no objective: distance 0 closes the gate
	Values.Add(TEXT("Distance"), 0.0);
	for (int32 Step = 1; Step <= 30; ++Step)
	{
		Time += 0.1;
		Features = Run(Time);
	}
	TestEqual(TEXT("Gate closed: default even with constant distance"), Features.FindRef(TEXT("NoProgress")), 0.0);

	// events: two attacks, one hit
	Time += 1.0;
	Features = Run(Time, { { TEXT("Attack"), 1.0, Time - 0.05 }, { TEXT("Attack"), 1.0, Time - 0.04 }, { TEXT("Hit"), 7.0, Time - 0.03 } });
	TestEqual(TEXT("One hit in two attacks: miss rate 0.5"), Features.FindRef(TEXT("MissRate")), 0.5);
	TestEqual(TEXT("Count ignores the value"), Features.FindRef(TEXT("Hits")), 0.25);

	Features = Run(Time + 1.0);
	TestEqual(TEXT("Hit left the 1 s window"), Features.FindRef(TEXT("Hits")), 0.0);
	TestEqual(TEXT("Still in the 3 s window"), Features.FindRef(TEXT("MissRate")), 0.5);

	Features = Run(Time + 3.0);
	TestEqual(TEXT("All attacks left the window: default"), Features.FindRef(TEXT("MissRate")), 0.1);

	// Latest keeps the newest value however long it stays unchanged
	Features = Run(Time + 100.0);
	TestEqual(TEXT("Latest survives trimming"), Features.FindRef(TEXT("Health")), 0.2, 1e-9);

	return true;
}

#endif
