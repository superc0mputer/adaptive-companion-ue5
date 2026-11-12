#include "Pipeline/AdaptiveFeatureStage.h"
#include "AdaptiveCompanionFramework.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"

namespace
{
	/** A Slope needs samples spanning at least this share of its window, so a few samples right after a reset don't give a wild rate */
	constexpr double MinSlopeCoverage = 0.5;

	double RoundTo(double Value, double Step)
	{
		// for steps like 0.01 divide by the whole number 100 instead of multiplying by 0.01, so the
		// result is the double closest to 0.83 and the log shows 0.83, not 0.8300000000000001
		const double Inverse = FMath::RoundToDouble(1.0 / Step);
		if (Inverse >= 1.0 && FMath::IsNearlyEqual(Inverse * Step, 1.0))
		{
			return FMath::RoundToDouble(Value * Inverse) / Inverse;
		}
		return FMath::RoundToDouble(Value / Step) * Step;
	}
}

UAdaptiveFeatureStage::UAdaptiveFeatureStage()
{
	// the first loop's feature (#6), so a config without a list still drives the naive Need stage
	FAdaptiveFeatureDefinition DamageRecent;
	DamageRecent.Name = TEXT("Combat.DamageRecent");
	DamageRecent.Signal = TEXT("Combat.DamageTaken");
	DamageRecent.Aggregation = EAdaptiveFeatureAggregation::Sum;
	DamageRecent.Window = 5.f;
	DamageRecent.Max = 5.0;
	Features.Add(DamageRecent);
}

void UAdaptiveFeatureStage::Initialize(UAdaptivePipelineSubsystem& InPipeline)
{
	Super::Initialize(InPipeline);

	PrepareStates();

	UAdaptiveLogSubsystem* Log = GetLog();
	if (!Log)
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> Definitions;
	for (int32 Index = 0; Index < Features.Num(); ++Index)
	{
		if (!States[Index].bValid)
		{
			continue;
		}

		const FAdaptiveFeatureDefinition& Definition = Features[Index];
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Definition.Name.ToString());
		Object->SetStringField(TEXT("signal"), Definition.Signal.ToString());
		Object->SetStringField(TEXT("aggregation"), StaticEnum<EAdaptiveFeatureAggregation>()->GetNameStringByValue(static_cast<int64>(Definition.Aggregation)));
		if (Definition.Aggregation == EAdaptiveFeatureAggregation::Ratio)
		{
			Object->SetStringField(TEXT("denominator"), Definition.Denominator.ToString());
		}
		if (Definition.Aggregation == EAdaptiveFeatureAggregation::FractionBelow || Definition.Aggregation == EAdaptiveFeatureAggregation::FractionAbove)
		{
			Object->SetNumberField(TEXT("threshold"), Definition.Threshold);
		}
		Object->SetNumberField(TEXT("window"), Definition.Window);
		Object->SetNumberField(TEXT("min"), Definition.Min);
		Object->SetNumberField(TEXT("max"), Definition.Max);
		Object->SetBoolField(TEXT("invert"), Definition.bInvert);
		Object->SetNumberField(TEXT("default"), Definition.Default);
		if (!Definition.ResetEvent.IsNone())
		{
			Object->SetStringField(TEXT("reset_event"), Definition.ResetEvent.ToString());
		}
		if (!Definition.OnlyWhileNonZero.IsNone())
		{
			Object->SetStringField(TEXT("only_while_non_zero"), Definition.OnlyWhileNonZero.ToString());
		}
		Definitions.Add(MakeShared<FJsonValueObject>(Object));
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetArrayField(TEXT("features"), Definitions);
	Log->LogRecord(AdaptiveLogRecordType::FeatureDefinitions, Payload);
}

void UAdaptiveFeatureStage::PrepareStates()
{
	if (States.Num() == Features.Num())
	{
		return;
	}

	States.Reset();
	States.SetNum(Features.Num());

	TSet<FName> Names;
	for (int32 Index = 0; Index < Features.Num(); ++Index)
	{
		const FAdaptiveFeatureDefinition& Definition = Features[Index];

		const TCHAR* Problem = nullptr;
		if (Definition.Name.IsNone())
		{
			Problem = TEXT("has no name");
		}
		else if (Definition.Signal.IsNone())
		{
			Problem = TEXT("has no signal");
		}
		else if (Definition.Aggregation == EAdaptiveFeatureAggregation::Ratio && Definition.Denominator.IsNone())
		{
			Problem = TEXT("is a Ratio without a denominator");
		}
		else if (Names.Contains(Definition.Name))
		{
			Problem = TEXT("has the same name as an earlier feature");
		}

		if (Problem)
		{
			UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("Feature %d (%s) %s and is skipped."), Index, *Definition.Name.ToString(), Problem);
			continue;
		}

		Names.Add(Definition.Name);
		States[Index].bValid = true;
	}
}

void UAdaptiveFeatureStage::Update(FAdaptivePipelineFrame& Frame)
{
	PrepareStates();

	// how long each signal's samples are needed: the longest window that reads it
	TMap<FName, double> EventKeep;
	TMap<FName, double> ValueKeep;
	for (int32 Index = 0; Index < Features.Num(); ++Index)
	{
		if (!States[Index].bValid)
		{
			continue;
		}

		const FAdaptiveFeatureDefinition& Definition = Features[Index];
		const double Window = Definition.Window;
		if (AdaptiveFeatureMath::UsesEvents(Definition.Aggregation))
		{
			EventKeep.FindOrAdd(Definition.Signal) = FMath::Max(EventKeep.FindRef(Definition.Signal), Window);
			if (Definition.Aggregation == EAdaptiveFeatureAggregation::Ratio)
			{
				EventKeep.FindOrAdd(Definition.Denominator) = FMath::Max(EventKeep.FindRef(Definition.Denominator), Window);
			}
		}
		else
		{
			// Latest only needs the newest sample, which trimming always keeps
			const double Keep = Definition.Aggregation == EAdaptiveFeatureAggregation::Latest ? 0.0 : Window;
			ValueKeep.FindOrAdd(Definition.Signal) = FMath::Max(ValueKeep.FindRef(Definition.Signal), Keep);
		}
	}

	for (const FAdaptiveTelemetryEvent& Event : Frame.Events)
	{
		if (EventKeep.Contains(Event.Name))
		{
			EventSamples.FindOrAdd(Event.Name).Add({ Event.Time, Event.Value });
		}
	}

	for (const TPair<FName, double>& Keep : ValueKeep)
	{
		if (const double* Value = Frame.Values.Find(Keep.Key))
		{
			ValueSamples.FindOrAdd(Keep.Key).Add({ Frame.Time, *Value });
		}
	}

	UAdaptiveDebugSubsystem* Debug = GetDebug();

	for (int32 Index = 0; Index < Features.Num(); ++Index)
	{
		FFeatureState& State = States[Index];
		if (!State.bValid)
		{
			continue;
		}

		const FAdaptiveFeatureDefinition& Definition = Features[Index];

		if (!Definition.ResetEvent.IsNone())
		{
			for (const FAdaptiveTelemetryEvent& Event : Frame.Events)
			{
				if (Event.Name == Definition.ResetEvent)
				{
					State.ResetTime = FMath::Max(State.ResetTime, Event.Time);
				}
			}
		}

		const bool bGateOpen = Definition.OnlyWhileNonZero.IsNone() || Frame.Values.FindRef(Definition.OnlyWhileNonZero) != 0.0;
		if (!bGateOpen)
		{
			// restart the window, so samples from before the gate closed don't count once it opens again
			State.ResetTime = Frame.Time;
			State.Raw.Reset();
		}
		else
		{
			const bool bEvents = AdaptiveFeatureMath::UsesEvents(Definition.Aggregation);
			const TMap<FName, TArray<FAdaptiveFeatureSample>>& Samples = bEvents ? EventSamples : ValueSamples;

			const double WindowStart = Definition.Aggregation == EAdaptiveFeatureAggregation::Latest
				? State.ResetTime
				: FMath::Max(Frame.Time - Definition.Window, State.ResetTime);

			auto InWindow = [&](FName Signal)
			{
				const TArray<FAdaptiveFeatureSample>* Found = Samples.Find(Signal);
				return Found ? AdaptiveFeatureMath::After(*Found, WindowStart) : TConstArrayView<FAdaptiveFeatureSample>();
			};

			State.Raw = AdaptiveFeatureMath::Aggregate(Definition.Aggregation, InWindow(Definition.Signal),
				Definition.Aggregation == EAdaptiveFeatureAggregation::Ratio ? InWindow(Definition.Denominator) : TConstArrayView<FAdaptiveFeatureSample>(),
				Definition.Threshold, Definition.Window * MinSlopeCoverage);
		}

		const double Value = State.Raw.IsSet()
			? AdaptiveFeatureMath::Normalize(*State.Raw, Definition.Min, Definition.Max, Definition.bInvert)
			: FMath::Clamp(Definition.Default, 0.0, 1.0);

		Frame.Features.Add(Definition.Name, Value);

		const double Logged = RoundTo(Value, LogStep);
		if (!State.LoggedValue.IsSet() || *State.LoggedValue != Logged)
		{
			State.LoggedValue = Logged;
			LogFeature(Definition, Logged, State.Raw);
		}

		if (Debug)
		{
			Debug->SetBar(EAdaptiveDebugSection::Features, Definition.Name.ToString(), Value);
		}
	}

	// drop samples no window needs any more, but keep each value's newest one for Latest
	for (TPair<FName, TArray<FAdaptiveFeatureSample>>& Pair : EventSamples)
	{
		const double KeepAfter = Frame.Time - EventKeep.FindRef(Pair.Key);
		Pair.Value.RemoveAt(0, Pair.Value.Num() - AdaptiveFeatureMath::After(Pair.Value, KeepAfter).Num(), EAllowShrinking::No);
	}
	for (TPair<FName, TArray<FAdaptiveFeatureSample>>& Pair : ValueSamples)
	{
		const double KeepAfter = Frame.Time - ValueKeep.FindRef(Pair.Key);
		const int32 Expired = FMath::Min(Pair.Value.Num() - AdaptiveFeatureMath::After(Pair.Value, KeepAfter).Num(), Pair.Value.Num() - 1);
		Pair.Value.RemoveAt(0, FMath::Max(Expired, 0), EAllowShrinking::No);
	}
}

TOptional<double> UAdaptiveFeatureStage::GetRawValue(FName FeatureName) const
{
	for (int32 Index = 0; Index < Features.Num() && Index < States.Num(); ++Index)
	{
		if (States[Index].bValid && Features[Index].Name == FeatureName)
		{
			return States[Index].Raw;
		}
	}
	return {};
}

void UAdaptiveFeatureStage::LogFeature(const FAdaptiveFeatureDefinition& Definition, double Value, const TOptional<double>& Raw) const
{
	UAdaptiveLogSubsystem* Log = GetLog();
	if (!Log)
	{
		return;
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("name"), Definition.Name.ToString());
	Payload->SetNumberField(TEXT("value"), Value);
	if (Raw.IsSet())
	{
		Payload->SetNumberField(TEXT("raw"), RoundTo(*Raw, 0.001));
	}
	else
	{
		Payload->SetField(TEXT("raw"), MakeShared<FJsonValueNull>());
	}
	Log->LogRecord(AdaptiveLogRecordType::Feature, Payload);
}
