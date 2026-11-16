#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class AActor;
class APlayerController;

/**
 *  The threat call-out's cue on screen (#21): a pulsing red arrow at the edge of the screen that points
 *  towards the threat, with "Behind you!", "Left!" or "Right!" next to it. Forward is up, so an enemy
 *  behind the player puts the arrow at the bottom. Hidden while the threat is in view or gone.
 *
 *  Plain Slate added to the game viewport, so it needs no widget asset and shows in every build
 *  configuration (unlike the debug-text barks). Created and removed by the Threat Callout task.
 */
class ADAPTIVECOMPANION_API SCompanionThreatIndicator : public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SCompanionThreatIndicator) {}
		SLATE_ARGUMENT(TWeakObjectPtr<APlayerController>, PlayerController)
		SLATE_ARGUMENT(TWeakObjectPtr<AActor>, Threat)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** "Behind you!" beyond 135 degrees from the view direction, otherwise "Left!" or "Right!" */
	static FString DirectionText(float RelativeYaw);

	/** Horizontal angle of the threat relative to the camera's view direction, -180..180, positive to the right. False without camera or threat */
	static bool GetRelativeYaw(const APlayerController& Controller, const AActor& Threat, float& OutYaw);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D::ZeroVector; }

private:

	TWeakObjectPtr<APlayerController> PlayerController;
	TWeakObjectPtr<AActor> Threat;
	double StartTime = 0.0;
};
