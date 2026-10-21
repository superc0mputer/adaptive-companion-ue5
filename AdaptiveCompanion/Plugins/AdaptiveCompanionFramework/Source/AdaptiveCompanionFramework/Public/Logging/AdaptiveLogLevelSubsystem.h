#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AdaptiveLogLevelSubsystem.generated.h"

/**
 *  Writes a level_start record into the session log whenever a game level begins play, so records
 *  can be split by level. game_time restarts with every level; the session time t does not.
 */
UCLASS()
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveLogLevelSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

protected:

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
};
