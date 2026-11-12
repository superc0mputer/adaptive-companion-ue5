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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveNeedStageTest, "AdaptiveCompanion.Pipeline.Need.Stage", NeedTestFlags)

bool FAdaptiveNeedStageTest::RunTest(const FString& Parameters)
{
	UAdaptiveNeedStage* Stage = NewObject<UAdaptiveNeedStage>();
	UAdaptiveRuleNeedEstimator* Estimator = Cast<UAdaptiveRuleNeedEstimator>(Stage->Estimator);
	if (!TestNotNull(TEXT("Rule-based estimator by default"), Estimator))
	{
		return false;
	}
	TestEqual(TEXT("Default: the first loop's Combat dimension"), Estimator->Dimensions.Num(), 1);

	FAdaptiveNeedDimension Threat;
	Threat.Name = TEXT("Threat");
	Threat.Rules.Add(MakeRule(1.0, { { TEXT("Threat.Proximity"), false } }));
	Threat.RequiredContexts.Add(TEXT("Context.Combat"));
	Estimator->Dimensions = { Threat };

	FAdaptivePipelineFrame Frame;
	Frame.Features.Add(TEXT("Threat.Proximity"), 0.9);
	Stage->Update(Frame);
	const FAdaptiveNeedState* State = Frame.Needs.Find(TEXT("Threat"));
	if (!TestNotNull(TEXT("Threat in the frame"), State))
	{
		return false;
	}
	TestFalse(TEXT("Inactive outside its context"), State->bActive);
	TestFalse(TEXT("An inactive dimension is never elevated"), State->bElevated);

	Frame = FAdaptivePipelineFrame();
	Frame.Features.Add(TEXT("Threat.Proximity"), 0.9);
	Frame.Contexts.Add(TEXT("Context.Combat"));
	Stage->Update(Frame);
	State = Frame.Needs.Find(TEXT("Threat"));
	TestTrue(TEXT("Active in combat"), State->bActive);
	TestTrue(TEXT("0.9 is elevated"), State->bElevated);
	TestTrue(TEXT("Change is flagged"), State->bChanged);
	TestEqual(TEXT("Reason is passed on"), State->Reason, FString(TEXT("Threat.Proximity")));

	// leaving combat drops the dimension to 0 and back to normal right away (smoothing is #15)
	Frame = FAdaptivePipelineFrame();
	Frame.Features.Add(TEXT("Threat.Proximity"), 0.9);
	Stage->Update(Frame);
	State = Frame.Needs.Find(TEXT("Threat"));
	TestFalse(TEXT("Back to normal outside combat"), State->bElevated);
	TestTrue(TEXT("Change back is flagged"), State->bChanged);

	Stage->Estimator = nullptr;
	Frame = FAdaptivePipelineFrame();
	Stage->Update(Frame);
	TestTrue(TEXT("Without an estimator there are no needs"), Frame.Needs.IsEmpty());

	return true;
}

#endif
