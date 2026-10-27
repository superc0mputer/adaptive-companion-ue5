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

void UAdaptiveContextStage::Update(FAdaptivePipelineFrame& Frame)
{
	Frame.Contexts.Append(Active);

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

void UAdaptiveContextStage::ShowInOverlay(FName Name, bool bActive) const
{
	if (UAdaptiveDebugSubsystem* Debug = GetDebug())
	{
		const UAdaptivePipelineSubsystem* Pipeline = GetPipeline();
		Debug->SetText(EAdaptiveDebugSection::Context, Name.ToString(), FString::Printf(TEXT("%s since %.1f s"), bActive ? TEXT("active") : TEXT("inactive"), Pipeline ? Pipeline->GetTime() : 0.0));
	}
}
