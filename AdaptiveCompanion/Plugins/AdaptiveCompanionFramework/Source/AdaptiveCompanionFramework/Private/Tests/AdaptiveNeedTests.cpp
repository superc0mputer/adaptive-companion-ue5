#include "Pipeline/AdaptiveNeedEstimator.h"
#include "Pipeline/AdaptiveNeedStage.h"
#include "Pipeline/AdaptiveNeedTypes.h"
#include "Pipeline/AdaptivePipelineFrame.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags NeedTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	FAdaptiveNeedRule MakeRule(double Weight, std::initializer_list<FAdaptiveNeedCondition> Features)
	{
		FAdaptiveNeedRule Rule;
		Rule.Weight = Weight;
		Rule.Features = Features;
		return Rule;
	}

	const FAdaptiveNeedEstimate* FindEstimate(const TArray<FAdaptiveNeedEstimate>& Estimates, FName Dimension)
	{
		return Estimates.FindByPredicate([Dimension](const FAdaptiveNeedEstimate& Estimate) { return Estimate.Dimension == Dimension; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveNeedRuleTest, "AdaptiveCompanion.Pipeline.Need.Rules", NeedTestFlags)

bool FAdaptiveNeedRuleTest::RunTest(const FString& Parameters)
{
	using namespace AdaptiveNeedMath;

	const TMap<FName, double> Features = { { TEXT("A"), 0.8 }, { TEXT("B"), 0.4 }, { TEXT("C"), 0.3 } };

	TestEqual(TEXT("One feature: weight × value"), EvaluateRule(MakeRule(0.5, { { TEXT("A"), false } }), Features), 0.4, 1e-9);
	TestEqual(TEXT("Several features: the smallest decides"), EvaluateRule(MakeRule(1.0, { { TEXT("A"), false }, { TEXT("B"), false } }), Features), 0.4, 1e-9);
	TestEqual(TEXT("Negated feature reads 1 - value"), EvaluateRule(MakeRule(1.0, { { TEXT("A"), false }, { TEXT("C"), true } }), Features), 0.7, 1e-9);
	TestEqual(TEXT("A missing feature counts as 0"), EvaluateRule(MakeRule(1.0, { { TEXT("A"), false }, { TEXT("Missing"), false } }), Features), 0.0);
	TestEqual(TEXT("A rule without features never fires"), EvaluateRule(MakeRule(1.0, {}), Features), 0.0);
	TestEqual(TEXT("Weight is clamped to 1"), EvaluateRule(MakeRule(3.0, { { TEXT("A"), false } }), Features), 0.8, 1e-9);

	TestEqual(TEXT("Combine nothing: 0"), Combine({}), 0.0);
	TestEqual(TEXT("Combine one: itself"), Combine({ 0.6 }), 0.6, 1e-9);
	TestEqual(TEXT("Combine adds up moderate values"), Combine({ 0.5, 0.5 }), 0.75, 1e-9);
	TestEqual(TEXT("Combine: one at 1 gives 1"), Combine({ 1.0, 0.2 }), 1.0, 1e-9);
	TestEqual(TEXT("Combine: zeros change nothing"), Combine({ 0.6, 0.0, 0.0 }), 0.6, 1e-9);
	TestTrue(TEXT("Combine is at least the largest value"), Combine({ 0.3, 0.7, 0.2 }) >= 0.7);

	TestEqual(TEXT("Describe lists the features"), Describe(MakeRule(1.0, { { TEXT("A"), false }, { TEXT("B"), true } })), FString(TEXT("A & not B")));
	FAdaptiveNeedRule Named = MakeRule(1.0, { { TEXT("A"), false } });
	Named.Name = TEXT("Going in circles");
	TestEqual(TEXT("Describe prefers the name"), Describe(Named), FString(TEXT("Going in circles")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveNeedEstimatorTest, "AdaptiveCompanion.Pipeline.Need.Estimator", NeedTestFlags)

bool FAdaptiveNeedEstimatorTest::RunTest(const FString& Parameters)
{
	UAdaptiveRuleNeedEstimator* Estimator = NewObject<UAdaptiveRuleNeedEstimator>();
	Estimator->Dimensions.Reset();

	FAdaptiveNeedDimension Navigation;
	Navigation.Name = TEXT("Navigation");
	Navigation.Rules.Add(MakeRule(1.0, { { TEXT("Nav.NoProgress"), false }, { TEXT("Nav.StandingStill"), true } }));
	Navigation.Rules.Add(MakeRule(0.8, { { TEXT("Nav.Revisits"), false } }));
	Navigation.RequiredContexts.Add(TEXT("Context.Navigation"));
	Navigation.bRequiresObjective = true;
	Estimator->Dimensions.Add(Navigation);

	FAdaptiveNeedDimension Unnamed;
	Estimator->Dimensions.Add(Unnamed);

	FAdaptiveNeedDimension Duplicate;
	Duplicate.Name = TEXT("Navigation");
	Estimator->Dimensions.Add(Duplicate);

	FAdaptiveNeedDimension Always;
	Always.Name = TEXT("Always");
	Always.Rules.Add(MakeRule(0.5, { { TEXT("Nav.Revisits"), false } }));
	Estimator->Dimensions.Add(Always);

	AddExpectedMessage(TEXT("has no name and is skipped"), EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessage(TEXT("has the same name as an earlier dimension"), EAutomationExpectedMessageFlags::Contains, 1);

	FAdaptivePipelineFrame Frame;
	Frame.Features = { { TEXT("Nav.NoProgress"), 0.9 }, { TEXT("Nav.StandingStill"), 0.2 }, { TEXT("Nav.Revisits"), 0.5 } };

	TArray<FAdaptiveNeedEstimate> Estimates;
	Estimator->Estimate(Frame, Estimates);
	TestEqual(TEXT("Invalid dimensions are skipped"), Estimates.Num(), 2);

	const FAdaptiveNeedEstimate* Nav = FindEstimate(Estimates, TEXT("Navigation"));
	const FAdaptiveNeedEstimate* All = FindEstimate(Estimates, TEXT("Always"));
	if (!TestNotNull(TEXT("Navigation estimated"), Nav) || !TestNotNull(TEXT("Always estimated"), All))
	{
		return false;
	}

	TestFalse(TEXT("Without its context the dimension is inactive"), Nav->bActive);
	TestEqual(TEXT("Inactive means 0"), Nav->Score, 0.0);
	TestTrue(TEXT("A dimension without gates is always active"), All->bActive);
	TestEqual(TEXT("Its score"), All->Score, 0.25, 1e-9);

	Frame.Contexts.Add(TEXT("Context.Navigation"));
	Estimates.Reset();
	Estimator->Estimate(Frame, Estimates);
	TestFalse(TEXT("Context alone is not enough without an objective"), FindEstimate(Estimates, TEXT("Navigation"))->bActive);

	Frame.Objective = TEXT("Objective.1");
	Estimates.Reset();
	Estimator->Estimate(Frame, Estimates);
	Nav = FindEstimate(Estimates, TEXT("Navigation"));
	TestTrue(TEXT("Context and objective: active"), Nav->bActive);
	// rules: min(0.9, 1 - 0.2) = 0.8 and 0.8 × 0.5 = 0.4 -> 1 - 0.2 × 0.6 = 0.88
	TestEqual(TEXT("Noisy-OR of the rules"), Nav->Score, 0.88, 1e-9);
	TestEqual(TEXT("The strongest rule is the reason"), Nav->Reason, FString(TEXT("Nav.NoProgress & not Nav.StandingStill")));

	Frame.Features = { { TEXT("Nav.NoProgress"), 0.0 }, { TEXT("Nav.StandingStill"), 0.0 }, { TEXT("Nav.Revisits"), 0.0 } };
	Estimates.Reset();
	Estimator->Estimate(Frame, Estimates);
	Nav = FindEstimate(Estimates, TEXT("Navigation"));
	TestEqual(TEXT("Nothing fires: 0"), Nav->Score, 0.0);
	TestTrue(TEXT("Nothing fires: no reason"), Nav->Reason.IsEmpty());

	return true;
}

namespace
{
	/** A Need stage whose single dimension "X" has the raw score of feature "X" */
	UAdaptiveNeedStage* MakeStage(FName RequiredContext = NAME_None)
	{
		UAdaptiveNeedStage* Stage = NewObject<UAdaptiveNeedStage>();
		FAdaptiveNeedDimension Dimension;
		Dimension.Name = TEXT("X");
		Dimension.Rules.Add(MakeRule(1.0, { { TEXT("X"), false } }));
		if (!RequiredContext.IsNone())
		{
			Dimension.RequiredContexts.Add(RequiredContext);
		}
		CastChecked<UAdaptiveRuleNeedEstimator>(Stage->Estimator)->Dimensions = { Dimension };
		return Stage;
	}

	FAdaptiveNeedState StepStage(UAdaptiveNeedStage* Stage, double Time, double Raw, FName Context = NAME_None)
	{
		FAdaptivePipelineFrame Frame;
		Frame.Time = Time;
		Frame.Features.Add(TEXT("X"), Raw);
		if (!Context.IsNone())
		{
			Frame.Contexts.Add(Context);
		}
		Stage->Update(Frame);
		return Frame.Needs.FindRef(TEXT("X"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveNeedSmoothingTest, "AdaptiveCompanion.Pipeline.Need.Smoothing", NeedTestFlags)

bool FAdaptiveNeedSmoothingTest::RunTest(const FString& Parameters)
{
	using namespace AdaptiveNeedMath;

	TestEqual(TEXT("After one rise half-life: half way up"), Smooth(0.0, 1.0, 0.5, 0.5, 3.0), 0.5, 1e-9);
	TestEqual(TEXT("After one fall half-life: half way down"), Smooth(1.0, 0.0, 3.0, 0.5, 3.0), 0.5, 1e-9);
	TestEqual(TEXT("No time passed: unchanged"), Smooth(0.3, 1.0, 0.0, 0.5, 3.0), 0.3, 1e-9);
	TestEqual(TEXT("Half-life 0: no smoothing"), Smooth(0.3, 0.9, 0.1, 0.0, 3.0), 0.9, 1e-9);
	TestEqual(TEXT("At the target: stays"), Smooth(0.4, 0.4, 0.1, 0.5, 3.0), 0.4, 1e-9);

	// the result depends on the time that passed, not on how many updates it was split into
	double TenHz = 0.0;
	double SixtyHz = 0.0;
	for (int32 Index = 0; Index < 10; ++Index) { TenHz = Smooth(TenHz, 1.0, 0.1, 0.5, 3.0); }
	for (int32 Index = 0; Index < 60; ++Index) { SixtyHz = Smooth(SixtyHz, 1.0, 1.0 / 60.0, 0.5, 3.0); }
	TestEqual(TEXT("Same value after 1 s at 10 Hz and 60 Hz"), TenHz, SixtyHz, 1e-9);
	TestEqual(TEXT("Two half-lives: three quarters"), TenHz, 0.75, 1e-9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveNeedLevelsTest, "AdaptiveCompanion.Pipeline.Need.Levels", NeedTestFlags)

bool FAdaptiveNeedLevelsTest::RunTest(const FString& Parameters)
{
	using namespace AdaptiveNeedMath;
	using L = EAdaptiveNeedLevel;

	const FAdaptiveNeedLevelSettings Settings;   // Elevated 0.5 / 0.35, High 0.75 / 0.6

	TestEqual(TEXT("Below enter: Normal"), NextLevel(L::Normal, 0.49, Settings), L::Normal);
	TestEqual(TEXT("At enter: Elevated"), NextLevel(L::Normal, 0.5, Settings), L::Elevated);
	TestEqual(TEXT("Jump straight to High"), NextLevel(L::Normal, 0.8, Settings), L::High);
	TestEqual(TEXT("Elevated holds between exit and enter"), NextLevel(L::Elevated, 0.4, Settings), L::Elevated);
	TestEqual(TEXT("Elevated holds at its exit"), NextLevel(L::Elevated, 0.35, Settings), L::Elevated);
	TestEqual(TEXT("Below exit: Normal"), NextLevel(L::Elevated, 0.34, Settings), L::Normal);
	TestEqual(TEXT("Elevated to High"), NextLevel(L::Elevated, 0.75, Settings), L::High);
	TestEqual(TEXT("High holds between exit and enter"), NextLevel(L::High, 0.65, Settings), L::High);
	TestEqual(TEXT("High to Elevated"), NextLevel(L::High, 0.5, Settings), L::Elevated);
	TestEqual(TEXT("High straight to Normal"), NextLevel(L::High, 0.1, Settings), L::Normal);

	FAdaptiveNeedLevelSettings Broken;
	Broken.ElevatedEnter = 0.5;
	Broken.ElevatedExit = 0.7;   // above its enter
	Broken.HighEnter = 0.3;      // below Elevated
	Broken.HighExit = 0.9;
	TestEqual(TEXT("Exit above enter is capped, no oscillation"), NextLevel(NextLevel(L::Normal, 0.6, Broken), 0.6, Broken), NextLevel(L::Normal, 0.6, Broken));
	TestEqual(TEXT("High enter below Elevated is raised to it"), NextLevel(L::Normal, 0.4, Broken), L::Normal);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveNeedFlickerTest, "AdaptiveCompanion.Pipeline.Need.Flicker", NeedTestFlags)

bool FAdaptiveNeedFlickerTest::RunTest(const FString& Parameters)
{
	// the threat score from the combat run in #14: an enemy on the edge of the view makes the raw
	// score jump between 0.13 and 0.74 from one update to the next
	UAdaptiveNeedStage* Stage = MakeStage();
	int32 Changes = 0;
	for (int32 Index = 0; Index < 100; ++Index)
	{
		Changes += StepStage(Stage, Index * 0.1, Index % 2 == 0 ? 0.13 : 0.74).bChanged ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("Alternating 0.13 / 0.74 for 10 s changes the level at most once (%d)"), Changes), Changes <= 1);

	// noise of ±0.15 around the Elevated threshold
	Stage = MakeStage();
	FRandomStream Random(42);
	Changes = 0;
	for (int32 Index = 0; Index < 300; ++Index)
	{
		Changes += StepStage(Stage, Index * 0.1, 0.5 + Random.FRandRange(-0.15, 0.15)).bChanged ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("Noise around the threshold for 30 s changes the level at most once (%d)"), Changes), Changes <= 1);

	// without smoothing and hysteresis the same input flickers, so the test does test something
	Stage = MakeStage();
	Stage->Levels.RiseHalfLife = 0.0;
	Stage->Levels.FallHalfLife = 0.0;
	Stage->Levels.ElevatedExit = Stage->Levels.ElevatedEnter;
	Changes = 0;
	for (int32 Index = 0; Index < 100; ++Index)
	{
		Changes += StepStage(Stage, Index * 0.1, Index % 2 == 0 ? 0.13 : 0.74).bChanged ? 1 : 0;
	}
	TestTrue(TEXT("Without both it flickers"), Changes > 50);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveNeedReactionTest, "AdaptiveCompanion.Pipeline.Need.Reaction", NeedTestFlags)

bool FAdaptiveNeedReactionTest::RunTest(const FString& Parameters)
{
	// a lasting full-strength problem reaches Elevated after one rise half-life, whatever it is set to
	for (const double HalfLife : { 0.5, 2.0 })
	{
		UAdaptiveNeedStage* Stage = MakeStage();
		Stage->Levels.RiseHalfLife = HalfLife;
		StepStage(Stage, 0.0, 0.0);

		double ElevatedAt = -1.0;
		double HighAt = -1.0;
		for (int32 Index = 1; Index <= 100; ++Index)
		{
			const FAdaptiveNeedState State = StepStage(Stage, Index * 0.1, 1.0);
			if (ElevatedAt < 0.0 && State.Level != EAdaptiveNeedLevel::Normal) { ElevatedAt = Index * 0.1; }
			if (HighAt < 0.0 && State.Level == EAdaptiveNeedLevel::High) { HighAt = Index * 0.1; }
		}
		// exactly at the half-life, or one update later when rounding leaves it a hair below the threshold
		TestTrue(FString::Printf(TEXT("Rise half-life %.1f s: Elevated after it (%.1f s)"), HalfLife, ElevatedAt), ElevatedAt > HalfLife - 0.01 && ElevatedAt < HalfLife + 0.11);
		TestTrue(FString::Printf(TEXT("Rise half-life %.1f s: High after two (%.1f s)"), HalfLife, HighAt), HighAt > 2.0 * HalfLife - 0.01 && HighAt < 2.0 * HalfLife + 0.11);
	}

	// a per-dimension override wins over the stage-wide settings
	UAdaptiveNeedStage* Stage = MakeStage();
	FAdaptiveNeedLevelSettings Fast;
	Fast.RiseHalfLife = 0.0;
	Stage->DimensionLevels.Add(TEXT("X"), Fast);
	StepStage(Stage, 0.0, 0.0);
	TestEqual(TEXT("Override without smoothing: Elevated at once"), StepStage(Stage, 0.1, 0.6).Level, EAdaptiveNeedLevel::Elevated);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveNeedStageTest, "AdaptiveCompanion.Pipeline.Need.Stage", NeedTestFlags)

bool FAdaptiveNeedStageTest::RunTest(const FString& Parameters)
{
	UAdaptiveNeedStage* Default = NewObject<UAdaptiveNeedStage>();
	UAdaptiveRuleNeedEstimator* Estimator = Cast<UAdaptiveRuleNeedEstimator>(Default->Estimator);
	if (!TestNotNull(TEXT("Rule-based estimator by default"), Estimator))
	{
		return false;
	}
	TestEqual(TEXT("Default: the first loop's Combat dimension"), Estimator->Dimensions.Num(), 1);

	const FName Combat = TEXT("Context.Combat");
	UAdaptiveNeedStage* Stage = MakeStage(Combat);

	FAdaptiveNeedState State = StepStage(Stage, 0.0, 0.9);
	TestFalse(TEXT("Inactive outside its context"), State.bActive);
	TestEqual(TEXT("Inactive: score 0"), State.Score, 0.0);

	double Time = 0.1;
	for (; Time < 4.0; Time += 0.1)
	{
		State = StepStage(Stage, Time, 0.9, Combat);
	}
	TestTrue(TEXT("Active in its context"), State.bActive);
	TestEqual(TEXT("Raw score is passed on"), State.RawScore, 0.9, 1e-9);
	TestEqual(TEXT("Smoothed score close to the raw one after a while"), State.Score, 0.9, 0.01);
	TestEqual(TEXT("0.9 is High"), State.Level, EAdaptiveNeedLevel::High);
	TestEqual(TEXT("Reason is passed on"), State.Reason, FString(TEXT("X")));

	// leaving the context: Normal at once, but the score only decays
	State = StepStage(Stage, Time, 0.9);
	TestEqual(TEXT("Normal as soon as it is inactive"), State.Level, EAdaptiveNeedLevel::Normal);
	TestTrue(TEXT("Change is flagged"), State.bChanged);
	TestTrue(TEXT("The score decays instead of dropping"), State.Score > 0.8);

	Stage->Estimator = nullptr;
	FAdaptivePipelineFrame Frame;
	Stage->Update(Frame);
	TestTrue(TEXT("Without an estimator there are no needs"), Frame.Needs.IsEmpty());

	return true;
}

#endif
