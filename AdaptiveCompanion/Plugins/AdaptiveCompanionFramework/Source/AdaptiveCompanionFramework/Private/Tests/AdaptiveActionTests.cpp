#include "AdaptiveTestActionHandler.h"
#include "Pipeline/AdaptiveCompanionAction.h"
#include "Pipeline/AdaptiveContextStage.h"
#include "Pipeline/AdaptivePipelineFrame.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags ActionTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	UAdaptiveCompanionAction* MakeAction(const TCHAR* Name, TArray<FName> Required = {}, TArray<FName> Blocked = {})
	{
		UAdaptiveCompanionAction* Action = NewObject<UAdaptiveCompanionAction>();
		Action->ActionName = Name;
		Action->RequiredContexts = MoveTemp(Required);
		Action->BlockedContexts = MoveTemp(Blocked);
		return Action;
	}

	const FAdaptiveActionState* FindAction(const FAdaptivePipelineFrame& Frame, FName Name)
	{
		return Frame.Actions.FindByPredicate([Name](const FAdaptiveActionState& State) { return State.Name == Name; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveActionDefinitionTest, "AdaptiveCompanion.Pipeline.Actions.Definition", ActionTestFlags)

bool FAdaptiveActionDefinitionTest::RunTest(const FString& Parameters)
{
	const FName Combat = TEXT("Context.Combat");
	const FName Navigation = TEXT("Context.Navigation");

	UAdaptiveCompanionAction* Action = MakeAction(TEXT("Action.Test"), { Navigation }, { TEXT("Context.Cutscene") });
	Action->Relevance.Add(TEXT("Navigation"), 1.f);
	Action->Relevance.Add(TEXT("Disorientation"), 0.8f);
	Action->Suitability.Add(Combat, 0.2f);
	Action->Suitability.Add(TEXT("Context.Dark"), 0.5f);

	FString Reason;
	TestFalse(TEXT("Missing required context"), Action->AllowsContexts({}, Reason));
	TestEqual(TEXT("Reason names it"), Reason, FString(TEXT("needs Context.Navigation")));
	TestTrue(TEXT("Required context active"), Action->AllowsContexts({ Navigation }, Reason));
	TestFalse(TEXT("Blocked context active"), Action->AllowsContexts({ Navigation, TEXT("Context.Cutscene") }, Reason));
	TestEqual(TEXT("Reason names the blocking one"), Reason, FString(TEXT("not during Context.Cutscene")));

	TestEqual(TEXT("Suitability 1 where nothing is listed"), Action->GetSuitability({ Navigation }), 1.0);
	TestEqual(TEXT("Suitability of a listed context"), Action->GetSuitability({ Navigation, Combat }), 0.2, 1e-6);
	TestEqual(TEXT("Several listed: the lowest"), Action->GetSuitability({ TEXT("Context.Dark"), Combat }), 0.2, 1e-6);

	TestEqual(TEXT("Relevance of a listed need"), Action->GetRelevance(TEXT("Disorientation")), 0.8, 1e-6);
	TestEqual(TEXT("Relevance 0 where not listed"), Action->GetRelevance(TEXT("Combat")), 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveActionRegistrationTest, "AdaptiveCompanion.Pipeline.Actions.Registration", ActionTestFlags)

bool FAdaptiveActionRegistrationTest::RunTest(const FString& Parameters)
{
	UAdaptiveContextStage* Stage = NewObject<UAdaptiveContextStage>();
	UAdaptiveTestActionHandler* Handler = NewObject<UAdaptiveTestActionHandler>();
	UAdaptiveTestActionHandler* Other = NewObject<UAdaptiveTestActionHandler>();

	UAdaptiveCompanionAction* Hint = MakeAction(TEXT("Action.Hint"));
	UAdaptiveCompanionAction* Callout = MakeAction(TEXT("Action.Callout"));
	UAdaptiveCompanionAction* Unnamed = MakeAction(TEXT(""));
	UAdaptiveCompanionAction* Duplicate = MakeAction(TEXT("Action.Hint"));

	AddExpectedMessage(TEXT("is empty and is skipped"), EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessage(TEXT("has no action name and is skipped"), EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessage(TEXT("already registered and is skipped"), EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessage(TEXT("doesn't implement IAdaptiveCompanionActionHandler"), EAutomationExpectedMessageFlags::Contains, 1);

	Stage->RegisterActions(Handler, { Hint, nullptr, Unnamed, Callout });
	TestEqual(TEXT("Valid actions registered, invalid skipped"), Stage->GetRegisteredActions().Num(), 2);

	Stage->RegisterActions(Other, { Duplicate });
	TestEqual(TEXT("A name can only be registered once"), Stage->GetRegisteredActions().Num(), 2);

	Stage->RegisterActions(NewObject<UAdaptiveCompanionAction>(), { MakeAction(TEXT("Action.Other")) });
	TestEqual(TEXT("A handler without the interface is rejected"), Stage->GetRegisteredActions().Num(), 2);

	Stage->RegisterActions(Handler, { Hint });
	TestEqual(TEXT("Registering again replaces the handler's actions"), Stage->GetRegisteredActions().Num(), 1);

	FAdaptivePipelineFrame Frame;
	Stage->Update(Frame);
	TestEqual(TEXT("Registered actions are in the frame"), Frame.Actions.Num(), 1);

	Stage->UnregisterActions(Handler);
	Frame = FAdaptivePipelineFrame();
	Stage->Update(Frame);
	TestTrue(TEXT("Unregistered: gone"), Frame.Actions.IsEmpty());

	// a handler that is destroyed without unregistering is dropped at the next update
	Stage->RegisterActions(Other, { Callout });
	Other->MarkAsGarbage();   // weak pointers no longer resolve to it; no full GC, which would also collect the test's objects
	Frame = FAdaptivePipelineFrame();
	Stage->Update(Frame);
	TestTrue(TEXT("Destroyed handler: its actions are dropped"), Frame.Actions.IsEmpty());
	TestTrue(TEXT("And no longer registered"), Stage->GetRegisteredActions().IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveActionAvailabilityTest, "AdaptiveCompanion.Pipeline.Actions.Availability", ActionTestFlags)

bool FAdaptiveActionAvailabilityTest::RunTest(const FString& Parameters)
{
	const FName Combat = TEXT("Context.Combat");

	UAdaptiveContextStage* Stage = NewObject<UAdaptiveContextStage>();
	UAdaptiveTestActionHandler* Handler = NewObject<UAdaptiveTestActionHandler>();

	UAdaptiveCompanionAction* Callout = MakeAction(TEXT("Action.Callout"), { Combat });
	Callout->BaseCost = 0.3f;
	Callout->Suitability.Add(Combat, 0.9f);
	UAdaptiveCompanionAction* Always = MakeAction(TEXT("Action.Always"));
	Stage->RegisterActions(Handler, { Callout, Always });

	FAdaptivePipelineFrame Frame;
	Stage->Update(Frame);
	const FAdaptiveActionState* State = FindAction(Frame, TEXT("Action.Callout"));
	if (!TestNotNull(TEXT("Callout in the frame"), State))
	{
		return false;
	}
	TestFalse(TEXT("Outside its context: not allowed"), State->bContextAllowed);
	TestFalse(TEXT("Outside its context: not available"), State->bAvailable);
	TestEqual(TEXT("Reason names the context"), State->Reason, FString(TEXT("needs Context.Combat")));
	TestEqual(TEXT("The game isn't asked when the context rules it out"), Handler->Checks, 1);
	TestTrue(TEXT("Action without contexts is available"), FindAction(Frame, TEXT("Action.Always"))->bAvailable);

	Stage->SetContext(Combat, true);
	FAdaptiveActionCheck No;
	No.bPossible = false;
	No.Reason = TEXT("no enemy out of view");
	Handler->Answers.Add(TEXT("Action.Callout"), No);
	Frame = FAdaptivePipelineFrame();
	Stage->Update(Frame);
	State = FindAction(Frame, TEXT("Action.Callout"));
	TestTrue(TEXT("In its context: allowed"), State->bContextAllowed);
	TestFalse(TEXT("The game says no"), State->bAvailable);
	TestEqual(TEXT("The game's reason"), State->Reason, FString(TEXT("no enemy out of view")));
	TestEqual(TEXT("Suitability in combat"), State->Suitability, 0.9, 1e-6);

	FAdaptiveActionCheck Yes;
	Yes.Cost = 0.7f;
	Handler->Answers.Add(TEXT("Action.Callout"), Yes);
	Frame = FAdaptivePipelineFrame();
	Stage->Update(Frame);
	State = FindAction(Frame, TEXT("Action.Callout"));
	TestTrue(TEXT("The game says yes"), State->bAvailable);
	TestTrue(TEXT("No reason when available"), State->Reason.IsEmpty());
	TestEqual(TEXT("The game's current cost"), State->Cost, 0.7, 1e-6);
	TestEqual(TEXT("Base cost when the game reports none"), FindAction(Frame, TEXT("Action.Always"))->Cost, 0.2, 1e-6);
	TestTrue(TEXT("The handler is passed on"), State->Handler == Handler);

	return true;
}

#endif
