#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Logging/AdaptiveSessionLogWriter.h"
#include "AdaptiveLogSubsystem.generated.h"

/**
 *  Central session log that every pipeline stage writes to.
 *  One play session (one game instance, e.g. one PIE run) writes exactly one JSON Lines file
 *  <Saved>/AdaptiveLogs/<SessionId>.jsonl. Every line is one record:
 *
 *    {"seq":3,"t":12.5,"game_time":11.9,"wall":"2026-10-20T13:30:12.345Z",
 *     "session_id":"20261020-133000-1a2b3c4d","participant_id":"P01","type":"signal","payload":{...}}
 *
 *  Records are serialized on the calling thread and written by a background task every
 *  FlushInterval seconds, so logging never waits for the disk. Configured in UAdaptiveLogSettings.
 */
UCLASS()
class ADAPTIVECOMPANIONFRAMEWORK_API UAdaptiveLogSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	/** Returns the session log of the context object's game instance, or nullptr if logging is disabled */
	static UAdaptiveLogSubsystem* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Appends one record. Call from the game thread */
	void LogRecord(const FString& Type, const TSharedRef<FJsonObject>& Payload);

	/** Appends one record whose payload is a USTRUCT, with its properties as fields */
	template <typename TStruct>
	void LogStruct(const FString& Type, const TStruct& Payload)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		FJsonObjectConverter::UStructToJsonObject(TStruct::StaticStruct(), &Payload, Json);
		LogRecord(Type, Json);
	}

	/** Appends one record from Blueprint. Numbers and strings become fields of the payload */
	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Logging", meta = (DisplayName = "Log Record", AutoCreateRefTerm = "Numbers,Strings"))
	void LogValues(const FString& Type, const TMap<FString, double>& Numbers, const TMap<FString, FString>& Strings);

	/** Hands all pending records to the background writer now */
	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Logging")
	void Flush();

	/** Sets the participant ID used in all following records (e.g. from a study menu) */
	UFUNCTION(BlueprintCallable, Category = "Adaptive Companion|Logging")
	void SetParticipantId(const FString& InParticipantId);

	UFUNCTION(BlueprintPure, Category = "Adaptive Companion|Logging")
	FString GetParticipantId() const { return ParticipantId; }

	UFUNCTION(BlueprintPure, Category = "Adaptive Companion|Logging")
	FString GetSessionId() const { return SessionId; }

	/** Absolute path of this session's log file */
	UFUNCTION(BlueprintPure, Category = "Adaptive Companion|Logging")
	FString GetLogFilePath() const { return Writer.GetFilePath(); }

	/** Seconds since the session started. Unlike world time it doesn't pause or reset on level change */
	double GetSessionTime() const;

	/**
	 *  Builds one record line. Non-finite numbers in the payload are written as null, so every line
	 *  stays valid JSON. Public for tests.
	 */
	static FString MakeRecordLine(int64 Seq, double SessionTime, TOptional<double> GameTime, const FDateTime& WallUtc,
		const FString& SessionId, const FString& ParticipantId, const FString& Type, const TSharedRef<FJsonObject>& Payload);

protected:

	bool FlushTick(float DeltaTime);

	FAdaptiveSessionLogWriter Writer;

	FString SessionId;
	FString ParticipantId;

	/** FPlatformTime::Seconds() when the session started */
	double SessionStartSeconds = 0.0;

	/** Number of records written so far, also the next record's sequence number */
	int64 RecordCount = 0;

	int32 MaxPendingBytes = 0;

	FTSTicker::FDelegateHandle FlushTickerHandle;
};
