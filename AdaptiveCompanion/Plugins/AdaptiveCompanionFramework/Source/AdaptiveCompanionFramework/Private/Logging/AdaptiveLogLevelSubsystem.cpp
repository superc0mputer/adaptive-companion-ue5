#include "Logging/AdaptiveLogLevelSubsystem.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"

bool UAdaptiveLogLevelSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UAdaptiveLogLevelSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	UAdaptiveLogSubsystem* Log = UAdaptiveLogSubsystem::Get(&InWorld);
	if (!Log)
	{
		return;
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("map"), UWorld::RemovePIEPrefix(InWorld.GetMapName()));
	Payload->SetStringField(TEXT("game_mode"), GetNameSafe(InWorld.GetAuthGameMode() ? InWorld.GetAuthGameMode()->GetClass() : nullptr));
	Log->LogRecord(AdaptiveLogRecordType::LevelStart, Payload);
}
