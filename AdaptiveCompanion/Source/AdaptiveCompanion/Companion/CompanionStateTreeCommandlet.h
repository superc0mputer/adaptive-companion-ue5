#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CompanionStateTreeCommandlet.generated.h"

/**
 *  Adds a state for every companion action to the Intervention branch of the companion's StateTree
 *  (#19). Run with the editor closed:
 *
 *    UnrealEditor-Cmd AdaptiveCompanion.uproject -run=CompanionStateTree
 *
 *  For each action on the companion class from Project Settings > Game > Companion that has no
 *  state yet, it adds a child state entered by "Is Current Intervention <action>" that runs the
 *  Intervention Placeholder task. A last child, UnknownAction, reports actions without a state as
 *  failed. Existing states are left as they are, so behaviors added in the editor survive. The
 *  Intervention state itself loses its own tasks (its children do the work) and goes back to
 *  Default when a child completes or fails. The tree is compiled and saved.
 *
 *  Editor only; does nothing in other builds.
 */
UCLASS()
class UCompanionStateTreeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:

	UCompanionStateTreeCommandlet();

	virtual int32 Main(const FString& Params) override;
};
