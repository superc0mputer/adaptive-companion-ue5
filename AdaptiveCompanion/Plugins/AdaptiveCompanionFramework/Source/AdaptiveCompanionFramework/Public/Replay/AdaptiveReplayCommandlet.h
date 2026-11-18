#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AdaptiveReplayCommandlet.generated.h"

/**
 *  Replays a recorded session through the pipeline without the game (#24):
 *
 *    UnrealEditor-Cmd AdaptiveCompanion.uproject -run=AdaptiveReplay -Log=<session> [-Labels=<file>] [-Out=<folder>]
 *
 *  -Log is a session log path, or a session id / file name in the log folder. -Labels defaults to
 *  <session>.labels.json next to it, if there is one. Runs the replay twice and checks that both
 *  runs agree, compares it with what the live session logged, and with the labels if there are
 *  any. Writes <session>.replay.jsonl (one line per update) and <session>.replay.json (the summary)
 *  next to the log, or into -Out.
 */
UCLASS()
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveReplayCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:

	UAdaptiveReplayCommandlet();

	virtual int32 Main(const FString& Params) override;

	/** Full path of a session log given as a path (absolute, or relative to the project or the repository), a file name or session id in the log folder; empty if none exists */
	static FString ResolveLog(const FString& Name);
};
