#include "Pipeline/AdaptivePipelineSubsystem.h"
#include "Pipeline/AdaptivePipelineConfig.h"
#include "Pipeline/AdaptivePipelineSettings.h"
#include "Pipeline/AdaptivePipelineStages.h"
#include "Pipeline/AdaptiveTelemetryStage.h"
#include "Pipeline/AdaptiveTelemetryTestActor.h"
#include "AdaptiveCompanionFramework.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "DynamicRHI.h"
#include "RenderTimer.h"

DECLARE_CYCLE_STAT(TEXT("Pipeline Update"), STAT_AdaptivePipelineUpdate, STATGROUP_AdaptiveCompanion);

namespace
{
	/** "AdaptiveFeatureStage" → "Feature", for the perf records */
	FString PartName(const UObject& Stage)
	{
		FString Name = Stage.GetClass()->GetName();
		Name.RemoveFromStart(TEXT("Adaptive"));
		Name.RemoveFromEnd(TEXT("Stage"));
		return Name;
	}
}

UAdaptivePipelineSubsystem::FApiTimer::FApiTimer(UAdaptivePipelineSubsystem& InPipeline)
{
	// calls made while an update runs (e.g. "started" from PerformAction) are already in the update's time
	if (InPipeline.bMeasuring && !InPipeline.bUpdating)
	{
		Pipeline = &InPipeline;
		StartCycles = FPlatformTime::Cycles64();
	}
}

UAdaptivePipelineSubsystem::FApiTimer::~FApiTimer()
{
	if (Pipeline)
	{
		Pipeline->Perf.AddApi(FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartCycles));
	}
}

UAdaptivePipelineSubsystem* UAdaptivePipelineSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UAdaptivePipelineSubsystem>() : nullptr;
}

bool UAdaptivePipelineSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && GetDefault<UAdaptivePipelineSettings>()->bEnabled;
}

void UAdaptivePipelineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// the log goes first, so the session start is written before the pipeline start
	Log = Collection.InitializeDependency<UAdaptiveLogSubsystem>();
	Debug = Collection.InitializeDependency<UAdaptiveDebugSubsystem>();

	StartSeconds = FPlatformTime::Seconds();
	Config = GetDefault<UAdaptivePipelineSettings>()->LoadConfig();

	// update order: stage 4 (context) runs before 3 (need), because the need dimensions are gated
	// by the active contexts; it only copies what the game reported, so it doesn't depend on 1-3
	Telemetry = CreateStage(Config->Telemetry.Get());
	CreateStage(Config->Features.Get());
	Context = CreateStage(Config->Context.Get());
	CreateStage(Config->Need.Get());
	CreateStage(Config->Selection.Get());
	Behavior = CreateStage(Config->Behavior.Get());

	Rate = FAdaptiveFixedRate(1.0 / GetUpdateRate());
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAdaptivePipelineSubsystem::Tick));

	if (Log)
	{
		TArray<TSharedPtr<FJsonValue>> StageClasses;
		for (const UAdaptivePipelineStage* Stage : Stages)
		{
			StageClasses.Add(MakeShared<FJsonValueString>(Stage->GetClass()->GetName()));
		}

		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("config"), Config->HasAnyFlags(RF_ClassDefaultObject) ? TEXT("defaults") : Config->GetPathName());
		Payload->SetNumberField(TEXT("update_rate"), GetUpdateRate());
		Payload->SetBoolField(TEXT("pause_with_game"), Config->bPauseWithGame);
		Payload->SetArrayField(TEXT("stages"), StageClasses);
		Log->LogRecord(AdaptiveLogRecordType::PipelineStart, Payload);

		bRecording = GetDefault<UAdaptivePipelineSettings>()->bRecordInputs;
		if (bRecording)
		{
			TSharedRef<FJsonObject> Recording = MakeShared<FJsonObject>();
			Recording->SetNumberField(TEXT("format"), AdaptiveRecording::FormatVersion);
			Recording->SetNumberField(TEXT("update_rate"), GetUpdateRate());
			Log->LogRecord(AdaptiveLogRecordType::RecordingStart, Recording);
		}
	}

	// after pipeline_start, so records the stages write while initializing (feature definitions) follow it
	for (UAdaptivePipelineStage* Stage : Stages)
	{
		Stage->Initialize(*this);
	}

	const UAdaptivePipelineSettings* Settings = GetDefault<UAdaptivePipelineSettings>();
	bMeasuring = Settings->bMeasurePerformance;
	if (bMeasuring)
	{
		TArray<FString> PartNames;
		for (const UAdaptivePipelineStage* Stage : Stages)
		{
			PartNames.Add(PartName(*Stage));
		}
		PartNames.Add(TEXT("Recording"));
		Perf.Reset(PartNames);
		PerfPartNames = PartNames;
		PerfStart = PerfWindowStart = GetTime();
	}
	if (Log)
	{
		Log->OnSessionEnding.AddUObject(this, &UAdaptivePipelineSubsystem::OnSessionEnding);
	}

	UE_LOG(LogAdaptiveCompanionFramework, Display, TEXT("Pipeline running at %g Hz (config: %s)."), GetUpdateRate(), *GetNameSafe(Config));
}

void UAdaptivePipelineSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle.Reset();

	for (int32 Index = Stages.Num() - 1; Index >= 0; --Index)
	{
		Stages[Index]->Deinitialize();
	}
	Stages.Reset();
	Telemetry = nullptr;
	Context = nullptr;
	Behavior = nullptr;

	Super::Deinitialize();
}

template <typename TStage>
TStage* UAdaptivePipelineSubsystem::CreateStage(TStage* Template)
{
	// the template from the config asset supplies the class and its parameters
	TStage* Stage = Template
		? NewObject<TStage>(this, Template->GetClass(), NAME_None, RF_NoFlags, Template)
		: NewObject<TStage>(this);
	Stages.Add(Stage);
	return Stage;
}

void UAdaptivePipelineSubsystem::ReportEvent(FName Name, double Value)
{
	FApiTimer Timer(*this);
	if (Telemetry)
	{
		Telemetry->ReportEvent(Name, Value);
	}
}

void UAdaptivePipelineSubsystem::ReportValue(FName Name, double Value)
{
	FApiTimer Timer(*this);
	if (Telemetry)
	{
		Telemetry->ReportValue(Name, Value);
	}
}

void UAdaptivePipelineSubsystem::SetContext(FName Name, bool bActive)
{
	FApiTimer Timer(*this);
	if (Context)
	{
		Context->SetContext(Name, bActive);
	}
}

bool UAdaptivePipelineSubsystem::IsContextActive(FName Name) const
{
	return Context && Context->IsContextActive(Name);
}

void UAdaptivePipelineSubsystem::RegisterActions(UObject* Handler, const TArray<UAdaptiveCompanionAction*>& Actions)
{
	FApiTimer Timer(*this);
	if (Context)
	{
		Context->RegisterActions(Handler, Actions);
	}
}

void UAdaptivePipelineSubsystem::UnregisterActions(UObject* Handler)
{
	if (Context)
	{
		Context->UnregisterActions(Handler);
	}
}

void UAdaptivePipelineSubsystem::SetObjective(FName Name, FVector Location)
{
	FApiTimer Timer(*this);
	if (Context)
	{
		Context->SetObjective(Name, Location);
	}
}

void UAdaptivePipelineSubsystem::ClearObjective()
{
	FApiTimer Timer(*this);
	if (Context)
	{
		Context->ClearObjective();
	}
}

bool UAdaptivePipelineSubsystem::GetCurrentObjective(FName& Name, FVector& Location) const
{
	Name = Context ? Context->GetObjective() : NAME_None;
	Location = Context ? Context->GetObjectiveLocation() : FVector::ZeroVector;
	return !Name.IsNone();
}

void UAdaptivePipelineSubsystem::ReportInterventionPhase(FName Intervention, FName Phase, const FString& Detail)
{
	FApiTimer Timer(*this);
	if (bRecording)
	{
		// phases reported while an update runs (e.g. "started" from PerformAction) are replayed after it, the others before the next one
		(bUpdating ? PhasesDuring : PhasesBefore).Add({ Intervention, Phase, Detail, GetTime() });
	}

	if (Behavior)
	{
		Behavior->ReportPhase(Intervention, Phase, Detail);
	}
}

void UAdaptivePipelineSubsystem::Update()
{
	SCOPE_CYCLE_COUNTER(STAT_AdaptivePipelineUpdate);
	TRACE_CPUPROFILER_EVENT_SCOPE(UAdaptivePipelineSubsystem::Update);

	const uint64 UpdateStart = FPlatformTime::Cycles64();
	const double Now = GetTime();

	FAdaptiveUpdateCost Cost;
	Cost.Parts.Reserve(Stages.Num() + 1);

	Frame = FAdaptivePipelineFrame();
	Frame.UpdateIndex = UpdateCount;
	Frame.Time = Now;
	Frame.DeltaTime = UpdateCount > 0 ? Now - LastUpdateTime : 0.0;
	Frame.RunningIntervention = Behavior ? Behavior->UpdateRunning(Now) : NAME_None;

	bUpdating = true;
	for (UAdaptivePipelineStage* Stage : Stages)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*Stage->GetClass()->GetName());
		const uint64 StageStart = FPlatformTime::Cycles64();
		Stage->Update(Frame);
		Cost.Parts.Add(FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StageStart));
	}
	bUpdating = false;

	const uint64 RecordingStart = FPlatformTime::Cycles64();
	if (bRecording && Log)
	{
		const FAdaptiveRecordedUpdate Recorded = Recorder.Record(Frame, MoveTemp(PhasesBefore), MoveTemp(PhasesDuring), Behavior && Behavior->WasLastDispatchRejected());
		PhasesBefore.Reset();
		PhasesDuring.Reset();
		Log->LogRecord(AdaptiveLogRecordType::Input, AdaptiveRecording::ToJson(Recorded));
	}

	++UpdateCount;
	LastUpdateTime = Now;

	if (bMeasuring)
	{
		const uint64 End = FPlatformTime::Cycles64();
		Cost.Parts.Add(FPlatformTime::ToSeconds64(End - RecordingStart));
		Cost.Total = FPlatformTime::ToSeconds64(End - UpdateStart);
		for (const FAdaptiveTelemetryEvent& Event : Frame.Events)
		{
			Cost.InputWait = FMath::Max(Cost.InputWait, Now - Event.Time);
		}
		Cost.Events = Frame.Events.Num();
		Cost.Values = Frame.Values.Num();
		Cost.Features = Frame.Features.Num();
		Cost.Needs = Frame.Needs.Num();
		Cost.Actions = Frame.Actions.Num();
		Perf.AddUpdate(Cost);

		if (Log && Cost.Total * 1000.0 > GetDefault<UAdaptivePipelineSettings>()->SpikeThreshold)
		{
			TSharedRef<FJsonObject> Spike = MakeShared<FJsonObject>();
			Spike->SetNumberField(TEXT("u"), double(Frame.UpdateIndex));
			Spike->SetNumberField(TEXT("total_us"), FMath::RoundToDouble(Cost.Total * 1e7) / 10.0);
			TSharedRef<FJsonObject> Parts = MakeShared<FJsonObject>();
			for (int32 Index = 0; Index < PerfPartNames.Num() && Index < Cost.Parts.Num(); ++Index)
			{
				Parts->SetNumberField(PerfPartNames[Index], FMath::RoundToDouble(Cost.Parts[Index] * 1e7) / 10.0);
			}
			Spike->SetObjectField(TEXT("parts_us"), Parts);
			Log->LogRecord(AdaptiveLogRecordType::PerfSpike, Spike);
		}
	}
}

void UAdaptivePipelineSubsystem::LogPerfWindow(double Now)
{
	const TSharedRef<FJsonObject> Window = Perf.TakeWindow(Now - PerfWindowStart);
	PerfWindowStart = Now;
	if (Log)
	{
		Log->LogRecord(AdaptiveLogRecordType::Perf, Window);
	}
	if (Debug)
	{
		Debug->SetText(EAdaptiveDebugSection::Measurements, TEXT("Cost"), Perf.GetOverlayText());
	}
}

void UAdaptivePipelineSubsystem::OnSessionEnding()
{
	if (bMeasuring && Log)
	{
		Log->LogRecord(AdaptiveLogRecordType::PerfSummary, Perf.GetSummary(GetTime() - PerfStart));
	}
}

double UAdaptivePipelineSubsystem::GetTime() const
{
	return Log ? Log->GetSessionTime() : FPlatformTime::Seconds() - StartSeconds;
}

float UAdaptivePipelineSubsystem::GetUpdateRate() const
{
	return Config ? FMath::Clamp(Config->UpdateRate, 1.f, 120.f) : 10.f;
}

bool UAdaptivePipelineSubsystem::Tick(float DeltaTime)
{
	// no world while a level is loading
	const UWorld* World = GetGameInstance()->GetWorld();
	if (!World || (Config->bPauseWithGame && World->IsPaused()))
	{
		return true;
	}

	if (Rate.Step(GetTime()))
	{
		Update();
	}

	if (bMeasuring)
	{
		// the engine's times are those of the latest finished frame
		Perf.EndFrame(DeltaTime, FPlatformTime::ToSeconds64(GGameThreadTime), FPlatformTime::ToSeconds64(GRenderThreadTime),
			GDynamicRHI ? FPlatformTime::ToSeconds64(RHIGetGPUFrameCycles()) : 0.0);

		const double Now = GetTime();
		if (Now - PerfWindowStart >= GetDefault<UAdaptivePipelineSettings>()->PerformanceWindow)
		{
			LogPerfWindow(Now);
		}
	}
	return true;
}

// Console commands

static FAutoConsoleCommandWithWorldAndArgs GAdaptivePipelineValueCommand(
	TEXT("ac.Pipeline.Value"),
	TEXT("Reports a continuous value to the pipeline: ac.Pipeline.Value <Name> <Value>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(World);
		if (!Pipeline)
		{
			return;
		}

		double Value = 0.0;
		if (Args.Num() < 2 || !LexTryParseString(Value, *Args[1]))
		{
			UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("Usage: ac.Pipeline.Value <Name> <Value>"));
			return;
		}
		Pipeline->ReportValue(FName(*Args[0]), Value);
	}));

static FAutoConsoleCommandWithWorldAndArgs GAdaptivePipelineEventCommand(
	TEXT("ac.Pipeline.Event"),
	TEXT("Reports an event to the pipeline: ac.Pipeline.Event <Name> [Value], Value defaults to 1"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(World);
		if (!Pipeline)
		{
			return;
		}

		double Value = 1.0;
		if (Args.IsEmpty() || (Args.Num() > 1 && !LexTryParseString(Value, *Args[1])))
		{
			UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("Usage: ac.Pipeline.Event <Name> [Value]"));
			return;
		}
		Pipeline->ReportEvent(FName(*Args[0]), Value);
	}));

static FAutoConsoleCommandWithWorldAndArgs GAdaptivePipelineContextCommand(
	TEXT("ac.Pipeline.Context"),
	TEXT("Activates or deactivates a context: ac.Pipeline.Context <Name> <0|1>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(World);
		if (!Pipeline)
		{
			return;
		}

		bool bActive = false;
		if (Args.Num() < 2 || !LexTryParseString(bActive, *Args[1]))
		{
			UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("Usage: ac.Pipeline.Context <Name> <0|1>"));
			return;
		}
		Pipeline->SetContext(FName(*Args[0]), bActive);
	}));

static FAutoConsoleCommandWithWorldAndArgs GAdaptivePipelineTestActorCommand(
	TEXT("ac.Pipeline.TestActor"),
	TEXT("Spawns a test actor that reports Test.Wave (value) and Test.Pulse (event), or removes it again"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		bool bRemoved = false;
		for (TActorIterator<AAdaptiveTelemetryTestActor> It(World); It; ++It)
		{
			It->Destroy();
			bRemoved = true;
		}

		if (bRemoved)
		{
			UE_LOG(LogAdaptiveCompanionFramework, Display, TEXT("Telemetry test actor removed."));
			return;
		}

		const APlayerController* PlayerController = World->GetFirstPlayerController();
		const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
		const FVector Location = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
		World->SpawnActor<AAdaptiveTelemetryTestActor>(Location, FRotator::ZeroRotator);
		UE_LOG(LogAdaptiveCompanionFramework, Display, TEXT("Telemetry test actor spawned."));
	}));
