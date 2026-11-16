#include "Pipeline/AdaptiveContextStage.h"
#include "AdaptiveCompanionFramework.h"
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

	EvaluateActions(Frame);
}

void UAdaptiveContextStage::RegisterActions(UObject* Handler, const TArray<UAdaptiveCompanionAction*>& Actions)
{
	if (!Handler || !Handler->GetClass()->ImplementsInterface(UAdaptiveCompanionActionHandler::StaticClass()))
	{
		UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("RegisterActions: '%s' doesn't implement IAdaptiveCompanionActionHandler, its actions are ignored."), *GetNameSafe(Handler));
		return;
	}

	UnregisterActions(Handler);

	TArray<TSharedPtr<FJsonValue>> Definitions;
	for (const UAdaptiveCompanionAction* Action : Actions)
	{
		const TCHAR* Problem = nullptr;
		if (!Action)
		{
			Problem = TEXT("is empty");
		}
		else if (Action->ActionName.IsNone())
		{
			Problem = TEXT("has no action name");
		}
		else if (Registered.ContainsByPredicate([Action](const FAdaptiveRegisteredAction& Entry) { return Entry.Action->ActionName == Action->ActionName; }))
		{
			Problem = TEXT("has the name of an action that is already registered");
		}

		if (Problem)
		{
			UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("RegisterActions: action '%s' of '%s' %s and is skipped."),
				*GetNameSafe(Action), *Handler->GetName(), Problem);
			continue;
		}

		Registered.Add({ Action, Handler });

		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Action->ActionName.ToString());
		Object->SetStringField(TEXT("asset"), Action->GetPathName());
		TSharedRef<FJsonObject> Relevance = MakeShared<FJsonObject>();
		for (const TPair<FName, float>& Pair : Action->Relevance)
		{
			Relevance->SetNumberField(Pair.Key.ToString(), Pair.Value);
		}
		Object->SetObjectField(TEXT("relevance"), Relevance);
		TArray<TSharedPtr<FJsonValue>> Required;
		for (const FName Context : Action->RequiredContexts) { Required.Add(MakeShared<FJsonValueString>(Context.ToString())); }
		Object->SetArrayField(TEXT("required_contexts"), Required);
		TArray<TSharedPtr<FJsonValue>> Blocked;
		for (const FName Context : Action->BlockedContexts) { Blocked.Add(MakeShared<FJsonValueString>(Context.ToString())); }
		Object->SetArrayField(TEXT("blocked_contexts"), Blocked);
		TSharedRef<FJsonObject> Suitability = MakeShared<FJsonObject>();
		for (const TPair<FName, float>& Pair : Action->Suitability)
		{
			Suitability->SetNumberField(Pair.Key.ToString(), Pair.Value);
		}
		Object->SetObjectField(TEXT("suitability"), Suitability);
		Object->SetNumberField(TEXT("base_cost"), Action->BaseCost);
		Object->SetNumberField(TEXT("intrusiveness"), Action->Intrusiveness);
		Object->SetNumberField(TEXT("cooldown"), Action->Cooldown);
		Definitions.Add(MakeShared<FJsonValueObject>(Object));
	}

	if (UAdaptiveLogSubsystem* Log = GetLog())
	{
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("handler"), Handler->GetClass()->GetName());
		Payload->SetArrayField(TEXT("actions"), Definitions);
		Log->LogRecord(AdaptiveLogRecordType::ActionRegistered, Payload);
	}
}

void UAdaptiveContextStage::UnregisterActions(UObject* Handler)
{
	for (int32 Index = Registered.Num() - 1; Index >= 0; --Index)
	{
		if (Registered[Index].Handler.Get() == Handler)
		{
			const FName Name = Registered[Index].Action->ActionName;
			Registered.RemoveAt(Index);
			LogUnregistered(Name, TEXT("unregistered"));
		}
	}
}

void UAdaptiveContextStage::LogUnregistered(FName Action, const FString& Reason)
{
	LoggedAvailability.Remove(Action);

	if (UAdaptiveLogSubsystem* Log = GetLog())
	{
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("name"), Action.ToString());
		Payload->SetStringField(TEXT("reason"), Reason);
		Log->LogRecord(AdaptiveLogRecordType::ActionUnregistered, Payload);
	}

	if (UAdaptiveDebugSubsystem* Debug = GetDebug())
	{
		Debug->RemoveValue(EAdaptiveDebugSection::Context, Action.ToString());
	}
}

void UAdaptiveContextStage::EvaluateActions(FAdaptivePipelineFrame& Frame)
{
	// a handler that was destroyed without unregistering, e.g. on a level change
	for (int32 Index = Registered.Num() - 1; Index >= 0; --Index)
	{
		if (!Registered[Index].Handler.IsValid() || !Registered[Index].Action)
		{
			const FName Name = Registered[Index].Action ? Registered[Index].Action->ActionName : NAME_None;
			Registered.RemoveAt(Index);
			LogUnregistered(Name, TEXT("handler destroyed"));
		}
	}

	UAdaptiveLogSubsystem* Log = GetLog();
	UAdaptiveDebugSubsystem* Debug = GetDebug();

	for (const FAdaptiveRegisteredAction& Entry : Registered)
	{
		const UAdaptiveCompanionAction* Action = Entry.Action;
		UObject* Handler = Entry.Handler.Get();

		FAdaptiveActionState& State = Frame.Actions.AddDefaulted_GetRef();
		State.Name = Action->ActionName;
		State.Action = Action;
		State.Handler = Handler;
		State.Suitability = Action->GetSuitability(Frame.Contexts);
		State.Cost = Action->BaseCost;
		State.bContextAllowed = Action->AllowsContexts(Frame.Contexts, State.Reason);

		if (State.bContextAllowed)
		{
			const FAdaptiveActionCheck Check = IAdaptiveCompanionActionHandler::Execute_CheckAction(Handler, Action);
			State.bAvailable = Check.bPossible;
			State.Reason = Check.bPossible ? FString() : Check.Reason;
			if (Check.Cost >= 0.f)
			{
				State.Cost = FMath::Clamp(double(Check.Cost), 0.0, 1.0);
			}
		}

		FLoggedAvailability* Logged = LoggedAvailability.Find(State.Name);
		if (!Logged || Logged->bAvailable != State.bAvailable || Logged->Reason != State.Reason)
		{
			LoggedAvailability.Add(State.Name, { State.bAvailable, State.Reason });

			if (Log)
			{
				TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
				Payload->SetStringField(TEXT("name"), State.Name.ToString());
				Payload->SetBoolField(TEXT("available"), State.bAvailable);
				Payload->SetStringField(TEXT("reason"), State.Reason);
				Payload->SetNumberField(TEXT("cost"), FMath::RoundToDouble(State.Cost * 100.0) / 100.0);
				Payload->SetNumberField(TEXT("suitability"), FMath::RoundToDouble(State.Suitability * 100.0) / 100.0);
				Log->LogRecord(AdaptiveLogRecordType::ActionAvailability, Payload);
			}
		}

		if (Debug)
		{
			Debug->SetText(EAdaptiveDebugSection::Context, State.Name.ToString(), State.bAvailable
				? FString::Printf(TEXT("available, cost %.2f, suitability %.2f"), State.Cost, State.Suitability)
				: FString::Printf(TEXT("unavailable: %s"), State.Reason.IsEmpty() ? TEXT("no reason given") : *State.Reason));
		}
	}
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
