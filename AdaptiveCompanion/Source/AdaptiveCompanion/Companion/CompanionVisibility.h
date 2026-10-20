#pragma once

#include "CoreMinimal.h"

class AActor;
class APlayerController;

namespace CompanionVisibility
{
	/**
	 *  True if the player's camera can see a character standing at Location: the capsule center or the head
	 *  is inside the camera's field of view (widened by ViewMarginAngle) and not hidden behind geometry.
	 *  Used to keep companion "tricks" like teleporting out of the player's sight.
	 *
	 *  @param Character	The character to test. Ignored by the line of sight traces, it does not need to be at Location
	 */
	ADAPTIVECOMPANION_API bool IsVisibleToPlayer(const APlayerController& PlayerController, const AActor& Character, const FVector& Location, float HalfHeight, float ViewMarginAngle);
}
