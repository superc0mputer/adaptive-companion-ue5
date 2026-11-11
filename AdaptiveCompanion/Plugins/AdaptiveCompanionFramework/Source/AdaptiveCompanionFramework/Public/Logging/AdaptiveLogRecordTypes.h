#pragma once

#include "CoreMinimal.h"

/**
 *  Record types used by the framework. Every pipeline stage logs under its own type, so the
 *  analysis can filter on the "type" column. Games may add their own types.
 */
namespace AdaptiveLogRecordType
{
	/** First record of every file: build, platform, participant */
	inline const FString SessionStart = TEXT("session_start");

	/** Last record of every file: duration, number of records */
	inline const FString SessionEnd = TEXT("session_end");

	/** A level started or ended within the session */
	inline const FString LevelStart = TEXT("level_start");
	inline const FString LevelEnd = TEXT("level_end");

	/** The pipeline started: update rate, config asset and stage classes */
	inline const FString PipelineStart = TEXT("pipeline_start");

	/** Manual marker, e.g. from the console command ac.Log.Marker */
	inline const FString Marker = TEXT("marker");

	/** Pipeline stages */
	inline const FString Signal = TEXT("signal");
	inline const FString Feature = TEXT("feature");
	inline const FString State = TEXT("state");
	inline const FString Context = TEXT("context");
	inline const FString Objective = TEXT("objective");
	inline const FString Intervention = TEXT("intervention");
}
