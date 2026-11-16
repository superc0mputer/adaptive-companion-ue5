#pragma once

#include "CoreMinimal.h"

/** Names of the companion's actions, as set in their UAdaptiveCompanionAction assets (Content/Companion/Actions) */
namespace CompanionActions
{
	inline const FName NavigationHint = TEXT("Action.NavigationHint");
	inline const FName ThreatCallout = TEXT("Action.ThreatCallout");
	inline const FName DrawAggro = TEXT("Action.DrawAggro");
}
