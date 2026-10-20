#include "Logging/AdaptiveLogSubsystem.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSettings.h"
#include "AdaptiveCompanionFramework.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GeneralProjectSettings.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/JsonSerializer.h"

DECLARE_CYCLE_STAT(TEXT("Log Record"), STAT_AdaptiveLogRecord, STATGROUP_AdaptiveCompanion);

namespace
{
	TSharedPtr<FJsonValue> SanitizeValue(const TSharedPtr<FJsonValue>& Value);

	/** Replaces NaN and infinity with null, which plain JSON (and pandas) can't read otherwise */
	void SanitizeObject(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return;
		}

		for (auto& Field : Object->Values)
		{
			Field.Value = SanitizeValue(Field.Value);
		}
	}

	TSharedPtr<FJsonValue> SanitizeValue(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return MakeShared<FJsonValueNull>();
		}

		switch (Value->Type)
		{
		case EJson::Number:
			return FMath::IsFinite(Value->AsNumber()) ? Value : MakeShared<FJsonValueNull>();

		case EJson::Object:
			SanitizeObject(Value->AsObject());
			return Value;

		case EJson::Array:
		{
			TArray<TSharedPtr<FJsonValue>> Elements = Value->AsArray();
			for (TSharedPtr<FJsonValue>& Element : Elements)
			{
				Element = SanitizeValue(Element);
			}
			return MakeShared<FJsonValueArray>(Elements);
		}

		default:
			return Value;
		}
	}

	FString MakeSessionId()
	{
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8).ToLower();
		return FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S-")) + Suffix;
	}
}

UAdaptiveLogSubsystem* UAdaptiveLogSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UAdaptiveLogSubsystem>() : nullptr;
}

bool UAdaptiveLogSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && GetDefault<UAdaptiveLogSettings>()->bEnabled;
}

void UAdaptiveLogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UAdaptiveLogSettings* Settings = GetDefault<UAdaptiveLogSettings>();

	SessionId = MakeSessionId();
	SessionStartSeconds = FPlatformTime::Seconds();
	MaxPendingBytes = Settings->MaxPendingKB * 1024;

	ParticipantId = Settings->DefaultParticipantId;
	FParse::Value(FCommandLine::Get(), TEXT("ParticipantId="), ParticipantId);

	const FString FilePath = FPaths::Combine(Settings->GetLogDirectoryPath(), SessionId + TEXT(".jsonl"));
	if (!Writer.Open(FilePath))
	{
		return;
	}

	FlushTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UAdaptiveLogSubsystem::FlushTick), Settings->FlushInterval);

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AdaptiveCompanionFramework"));

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("project"), FApp::GetProjectName());
	Payload->SetStringField(TEXT("project_version"), GetDefault<UGeneralProjectSettings>()->ProjectVersion);
	Payload->SetStringField(TEXT("plugin_version"), Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : FString());
	Payload->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Payload->SetStringField(TEXT("build_config"), LexToString(FApp::GetBuildConfiguration()));
	Payload->SetStringField(TEXT("platform"), FPlatformProperties::IniPlatformName());
	Payload->SetBoolField(TEXT("editor"), GIsEditor);
	Payload->SetStringField(TEXT("start_utc"), FDateTime::UtcNow().ToIso8601());
	LogRecord(AdaptiveLogRecordType::SessionStart, Payload);

	// write the header right away, so even a crashed session leaves an identifiable file
	Writer.Flush();

	UE_LOG(LogAdaptiveCompanionFramework, Display, TEXT("Session log: %s"), *FilePath);
}

void UAdaptiveLogSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(FlushTickerHandle);
	FlushTickerHandle.Reset();

	if (Writer.IsOpen())
	{
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetNumberField(TEXT("duration"), GetSessionTime());
		// includes this record
		Payload->SetNumberField(TEXT("records"), RecordCount + 1);
		LogRecord(AdaptiveLogRecordType::SessionEnd, Payload);

		Writer.Close();
	}

	Super::Deinitialize();
}

double UAdaptiveLogSubsystem::GetSessionTime() const
{
	return FPlatformTime::Seconds() - SessionStartSeconds;
}

void UAdaptiveLogSubsystem::LogRecord(const FString& Type, const TSharedRef<FJsonObject>& Payload)
{
	if (!Writer.IsOpen())
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_AdaptiveLogRecord);
	TRACE_CPUPROFILER_EVENT_SCOPE(UAdaptiveLogSubsystem::LogRecord);

	const UWorld* World = GetGameInstance()->GetWorld();
	const TOptional<double> GameTime = World ? TOptional<double>(World->GetTimeSeconds()) : TOptional<double>();

	Writer.AppendLine(MakeRecordLine(RecordCount, GetSessionTime(), GameTime, FDateTime::UtcNow(), SessionId, ParticipantId, Type, Payload));
	++RecordCount;

	if (Writer.GetPendingBytes() >= MaxPendingBytes)
	{
		Writer.Flush();
	}
}

void UAdaptiveLogSubsystem::LogValues(const FString& Type, const TMap<FString, double>& Numbers, const TMap<FString, FString>& Strings)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	for (const TPair<FString, double>& Number : Numbers)
	{
		Payload->SetNumberField(Number.Key, Number.Value);
	}
	for (const TPair<FString, FString>& String : Strings)
	{
		Payload->SetStringField(String.Key, String.Value);
	}
	LogRecord(Type, Payload);
}

void UAdaptiveLogSubsystem::Flush()
{
	Writer.Flush();
}

void UAdaptiveLogSubsystem::SetParticipantId(const FString& InParticipantId)
{
	ParticipantId = InParticipantId;
}

bool UAdaptiveLogSubsystem::FlushTick(float DeltaTime)
{
	Writer.Flush();
	return true;
}

FString UAdaptiveLogSubsystem::MakeRecordLine(int64 Seq, double SessionTime, TOptional<double> GameTime, const FDateTime& WallUtc,
	const FString& SessionId, const FString& ParticipantId, const FString& Type, const TSharedRef<FJsonObject>& Payload)
{
	SanitizeObject(Payload);

	TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
	Record->SetNumberField(TEXT("seq"), Seq);
	Record->SetNumberField(TEXT("t"), SessionTime);
	if (GameTime.IsSet() && FMath::IsFinite(GameTime.GetValue()))
	{
		Record->SetNumberField(TEXT("game_time"), GameTime.GetValue());
	}
	else
	{
		Record->SetField(TEXT("game_time"), MakeShared<FJsonValueNull>());
	}
	Record->SetStringField(TEXT("wall"), WallUtc.ToIso8601());
	Record->SetStringField(TEXT("session_id"), SessionId);
	Record->SetStringField(TEXT("participant_id"), ParticipantId);
	Record->SetStringField(TEXT("type"), Type);
	Record->SetObjectField(TEXT("payload"), Payload);

	FString Line;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
	FJsonSerializer::Serialize(Record, JsonWriter);
	return Line;
}

// Console commands

static FAutoConsoleCommandWithWorldAndArgs GAdaptiveLogMarkerCommand(
	TEXT("ac.Log.Marker"),
	TEXT("Writes a marker record with the given text into the session log, e.g. ac.Log.Marker start of fight"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UAdaptiveLogSubsystem* Log = UAdaptiveLogSubsystem::Get(World))
		{
			TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("text"), FString::Join(Args, TEXT(" ")));
			Log->LogRecord(AdaptiveLogRecordType::Marker, Payload);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GAdaptiveLogBenchmarkCommand(
	TEXT("ac.Log.Benchmark"),
	TEXT("Logs N test records (default 10000) in one frame and prints the cost per record"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UAdaptiveLogSubsystem* Log = UAdaptiveLogSubsystem::Get(World);
		if (!Log)
		{
			return;
		}

		const int32 Count = Args.Num() > 0 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 10000;

		const double Start = FPlatformTime::Seconds();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			// roughly the size of a signal record with a few values
			TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("name"), TEXT("Benchmark.Signal"));
			Payload->SetNumberField(TEXT("value"), Index * 0.001);
			Payload->SetNumberField(TEXT("index"), Index);
			Log->LogRecord(AdaptiveLogRecordType::Signal, Payload);
		}
		const double LogSeconds = FPlatformTime::Seconds() - Start;

		const double FlushStart = FPlatformTime::Seconds();
		Log->Flush();
		const double FlushSeconds = FPlatformTime::Seconds() - FlushStart;

		UE_LOG(LogAdaptiveCompanionFramework, Display, TEXT("ac.Log.Benchmark: %d records in %.3f ms (%.2f us/record), handing off the flush took %.3f ms."),
			Count, LogSeconds * 1000.0, LogSeconds * 1e6 / Count, FlushSeconds * 1000.0);
	}));
