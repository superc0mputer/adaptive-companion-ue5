#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Pipeline/AdaptiveCompanionAction.h"
#include "AdaptiveTestActionHandler.generated.h"

/** Action handler for the automation tests: answers from a map that the test fills */
UCLASS(NotBlueprintable, Transient)
class UAdaptiveTestActionHandler : public UObject, public IAdaptiveCompanionActionHandler
{
	GENERATED_BODY()

public:

	/** Answers by action name; actions not listed are possible at their base cost */
	TMap<FName, FAdaptiveActionCheck> Answers;

	/** How often CheckAction was called */
	int32 Checks = 0;

	/** Whether PerformAction accepts, and the actions it was asked to perform */
	bool bAccept = true;
	TArray<FName> Performed;

	virtual bool PerformAction_Implementation(const UAdaptiveCompanionAction* Action) override
	{
		Performed.Add(Action ? Action->ActionName : NAME_None);
		return bAccept;
	}

	virtual FAdaptiveActionCheck CheckAction_Implementation(const UAdaptiveCompanionAction* Action) override
	{
		++Checks;
		const FAdaptiveActionCheck* Answer = Action ? Answers.Find(Action->ActionName) : nullptr;
		return Answer ? *Answer : FAdaptiveActionCheck();
	}
};
