#include "Pipeline/AdaptiveContextStage.h"
#include "Pipeline/AdaptivePipelineSubsystem.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Logging/AdaptiveLogSubsystem.h"

void UAdaptiveContextStage::SetContext(FName Name, bool bActive)
{
	if (Name.IsNone() || Active.Contains(Name) == bActive)
	{
		return;
	}

	if (bActive)
	{
		Active.Add(Name);
	}
	else
	{
		Active.Remove(Name);
	}

	// logged right away like events, so the record has the time of the change, not of the next update
	LogChange(Name, bActive);
	ShowInOverlay(Name, bActive);
}

void UAdaptiveContextStage::SetObjective(FName Name, const FVector& Location)
{
	// a moving objective only counts as changed once it moved at least 1 cm
	if (Name.IsNone() || (Name == Objective && Location.Equals(ObjectiveLocation, 1.0)))
	{
		return;
	}

	Objective = Name;
	ObjectiveLocation = Location;
	LogObjective(Name, true);

	if (UAdaptiveDebugSubsystem* Debug = GetDebug())
	{
		Debug->SetText(EAdaptiveDebugSection::Context, TEXT("Objective"), FString::Printf(TEXT("%s at (%.1f, %.1f) m"), *Name.ToString(), Location.X / 100.0, Location.Y / 100.0));
	}
}

void UAdaptiveContextStage::ClearObjective()
{
	if (Objective.IsNone())
	{
		return;
	}

	const FName Cleared = Objective;
	Objective = NAME_None;
	LogObjective(Cleared, false);

	if (UAdaptiveDebugSubsystem* Debug = GetDebug())
	{
		Debug->SetText(EAdaptiveDebugSection::Context, TEXT("Objective"), TEXT("none"));
	}
}

void UAdaptiveContextStage::Update(FAdaptivePipelineFrame& Frame)
{
	Frame.Contexts.Append(Active);
	Frame.Objective = Objective;
	Frame.ObjectiveLocation = ObjectiveLocation;

	// compared as sets: a context that was switched off and on again between two updates is no change
	Frame.bContextChanged = PreviousActive.Num() != Active.Num()
		|| PreviousActive.ContainsByPredicate([this](FName Name) { return !Active.Contains(Name); });
	PreviousActive = Active;
}

void UAdaptiveContextStage::LogChange(FName Name, bool bActive) const
{
	if (UAdaptiveLogSubsystem* Log = GetLog())
	{
		TArray<TSharedPtr<FJsonValue>> ActiveNames;
		for (FName ActiveName : Active)
		{
			ActiveNames.Add(MakeShared<FJsonValueString>(ActiveName.ToString()));
		}

		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("name"), Name.ToString());
		Payload->SetBoolField(TEXT("active"), bActive);
		Payload->SetArrayField(TEXT("contexts"), ActiveNames);
		Log->LogRecord(AdaptiveLogRecordType::Context, Payload);
	}
}

void UAdaptiveContextStage::LogObjective(FName Name, bool bActive) const
{
	if (UAdaptiveLogSubsystem* Log = GetLog())
	{
		// in m like the navigation signals, rounded to cm
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("name"), Name.ToString());
		Payload->SetBoolField(TEXT("active"), bActive);
		Payload->SetNumberField(TEXT("x"), FMath::RoundToDouble(ObjectiveLocation.X) / 100.0);
		Payload->SetNumberField(TEXT("y"), FMath::RoundToDouble(ObjectiveLocation.Y) / 100.0);
		Payload->SetNumberField(TEXT("z"), FMath::RoundToDouble(ObjectiveLocation.Z) / 100.0);
		Log->LogRecord(AdaptiveLogRecordType::Objective, Payload);
	}
}

void UAdaptiveContextStage::ShowInOverlay(FName Name, bool bActive) const
{
	if (UAdaptiveDebugSubsystem* Debug = GetDebug())
	{
		const UAdaptivePipelineSubsystem* Pipeline = GetPipeline();
		Debug->SetText(EAdaptiveDebugSection::Context, Name.ToString(), FString::Printf(TEXT("%s since %.1f s"), bActive ? TEXT("active") : TEXT("inactive"), Pipeline ? Pipeline->GetTime() : 0.0));
	}
}
