#include "Replay/AdaptiveInputRecording.h"
#include "Dom/JsonValue.h"
#include "Pipeline/AdaptiveCompanionAction.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> PhasesToJson(const TArray<FAdaptiveRecordedPhase>& Phases)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		for (const FAdaptiveRecordedPhase& Phase : Phases)
		{
			Array.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
				MakeShared<FJsonValueString>(Phase.Intervention.ToString()),
				MakeShared<FJsonValueString>(Phase.Phase.ToString()),
				MakeShared<FJsonValueString>(Phase.Detail),
				MakeShared<FJsonValueNumber>(Phase.Time) }));
		}
		return Array;
	}

	void PhasesFromJson(const FJsonObject& Payload, const TCHAR* Field, TArray<FAdaptiveRecordedPhase>& OutPhases)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Payload.TryGetArrayField(Field, Array))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Entry = Value->AsArray();
			if (Entry.Num() >= 4)
			{
				OutPhases.Add({ FName(*Entry[0]->AsString()), FName(*Entry[1]->AsString()), Entry[2]->AsString(), Entry[3]->AsNumber() });
			}
		}
	}

	bool SameAnswer(const FAdaptiveRecordedAction& A, const FAdaptiveRecordedAction& B)
	{
		return A.bPossible == B.bPossible && A.Reason == B.Reason && A.Cost == B.Cost;
	}
}

TSharedRef<FJsonObject> AdaptiveRecording::ToJson(const FAdaptiveRecordedUpdate& Update)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("u"), double(Update.Index));
	Payload->SetNumberField(TEXT("time"), Update.Time);

	if (!Update.Events.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> Events;
		for (const FAdaptiveTelemetryEvent& Event : Update.Events)
		{
			Events.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
				MakeShared<FJsonValueString>(Event.Name.ToString()),
				MakeShared<FJsonValueNumber>(Event.Value),
				MakeShared<FJsonValueNumber>(Event.Time) }));
		}
		Payload->SetArrayField(TEXT("events"), Events);
	}

	if (!Update.Values.IsEmpty())
	{
		TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
		for (const TPair<FName, double>& Value : Update.Values)
		{
			Values->SetNumberField(Value.Key.ToString(), Value.Value);
		}
		Payload->SetObjectField(TEXT("values"), Values);
	}

	if (Update.Contexts.IsSet())
	{
		TArray<TSharedPtr<FJsonValue>> Contexts;
		for (const FName Context : *Update.Contexts)
		{
			Contexts.Add(MakeShared<FJsonValueString>(Context.ToString()));
		}
		Payload->SetArrayField(TEXT("contexts"), Contexts);
	}

	if (Update.bObjectiveChanged)
	{
		TSharedRef<FJsonObject> Objective = MakeShared<FJsonObject>();
		Objective->SetStringField(TEXT("name"), Update.Objective.IsNone() ? FString() : Update.Objective.ToString());
		Objective->SetNumberField(TEXT("x"), Update.ObjectiveLocation.X);
		Objective->SetNumberField(TEXT("y"), Update.ObjectiveLocation.Y);
		Objective->SetNumberField(TEXT("z"), Update.ObjectiveLocation.Z);
		Payload->SetObjectField(TEXT("objective"), Objective);
	}

	if (Update.Registered.IsSet())
	{
		TArray<TSharedPtr<FJsonValue>> Actions;
		for (const FAdaptiveRecordedAction& Action : *Update.Registered)
		{
			Actions.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
				MakeShared<FJsonValueString>(Action.Name.ToString()),
				MakeShared<FJsonValueString>(Action.Asset) }));
		}
		Payload->SetArrayField(TEXT("actions"), Actions);
	}

	if (!Update.Answers.IsEmpty())
	{
		TSharedRef<FJsonObject> Answers = MakeShared<FJsonObject>();
		for (const FAdaptiveRecordedAction& Answer : Update.Answers)
		{
			Answers->SetArrayField(Answer.Name.ToString(), {
				MakeShared<FJsonValueBoolean>(Answer.bPossible),
				MakeShared<FJsonValueString>(Answer.Reason),
				MakeShared<FJsonValueNumber>(Answer.Cost) });
		}
		Payload->SetObjectField(TEXT("answers"), Answers);
	}

	if (!Update.PhasesBefore.IsEmpty())
	{
		Payload->SetArrayField(TEXT("before"), PhasesToJson(Update.PhasesBefore));
	}
	if (!Update.PhasesDuring.IsEmpty())
	{
		Payload->SetArrayField(TEXT("during"), PhasesToJson(Update.PhasesDuring));
	}
	if (Update.bRejected)
	{
		Payload->SetBoolField(TEXT("rejected"), true);
	}
	return Payload;
}

bool AdaptiveRecording::FromJson(const FJsonObject& Payload, FAdaptiveRecordedUpdate& OutUpdate)
{
	double Index = 0.0;
	if (!Payload.TryGetNumberField(TEXT("u"), Index) || !Payload.TryGetNumberField(TEXT("time"), OutUpdate.Time))
	{
		return false;
	}
	OutUpdate.Index = int64(Index);

	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (Payload.TryGetArrayField(TEXT("events"), Array))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Entry = Value->AsArray();
			if (Entry.Num() >= 3)
			{
				OutUpdate.Events.Add({ FName(*Entry[0]->AsString()), Entry[1]->AsNumber(), Entry[2]->AsNumber() });
			}
		}
	}

	const TSharedPtr<FJsonObject>* Object = nullptr;
	if (Payload.TryGetObjectField(TEXT("values"), Object))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Value : (*Object)->Values)
		{
			OutUpdate.Values.Add(FName(*Value.Key), Value.Value->AsNumber());
		}
	}

	if (Payload.TryGetArrayField(TEXT("contexts"), Array))
	{
		TArray<FName>& Contexts = OutUpdate.Contexts.Emplace();
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			Contexts.Add(FName(*Value->AsString()));
		}
	}

	if (Payload.TryGetObjectField(TEXT("objective"), Object))
	{
		OutUpdate.bObjectiveChanged = true;
		const FString Name = (*Object)->GetStringField(TEXT("name"));
		OutUpdate.Objective = Name.IsEmpty() ? NAME_None : FName(*Name);
		OutUpdate.ObjectiveLocation = FVector((*Object)->GetNumberField(TEXT("x")), (*Object)->GetNumberField(TEXT("y")), (*Object)->GetNumberField(TEXT("z")));
	}

	if (Payload.TryGetArrayField(TEXT("actions"), Array))
	{
		TArray<FAdaptiveRecordedAction>& Registered = OutUpdate.Registered.Emplace();
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Entry = Value->AsArray();
			if (Entry.Num() >= 2)
			{
				FAdaptiveRecordedAction& Action = Registered.AddDefaulted_GetRef();
				Action.Name = FName(*Entry[0]->AsString());
				Action.Asset = Entry[1]->AsString();
			}
		}
	}

	if (Payload.TryGetObjectField(TEXT("answers"), Object))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Value : (*Object)->Values)
		{
			const TArray<TSharedPtr<FJsonValue>>& Entry = Value.Value->AsArray();
			if (Entry.Num() >= 3)
			{
				FAdaptiveRecordedAction& Answer = OutUpdate.Answers.AddDefaulted_GetRef();
				Answer.Name = FName(*Value.Key);
				Answer.bPossible = Entry[0]->AsBool();
				Answer.Reason = Entry[1]->AsString();
				Answer.Cost = Entry[2]->AsNumber();
			}
		}
	}

	PhasesFromJson(Payload, TEXT("before"), OutUpdate.PhasesBefore);
	PhasesFromJson(Payload, TEXT("during"), OutUpdate.PhasesDuring);
	Payload.TryGetBoolField(TEXT("rejected"), OutUpdate.bRejected);
	return true;
}

FAdaptiveRecordedUpdate FAdaptiveInputRecorder::Record(const FAdaptivePipelineFrame& Frame, TArray<FAdaptiveRecordedPhase> PhasesBefore, TArray<FAdaptiveRecordedPhase> PhasesDuring, bool bRejected)
{
	FAdaptiveRecordedUpdate Update;
	Update.Index = Frame.UpdateIndex;
	Update.Time = Frame.Time;
	Update.Events = Frame.Events;
	Update.PhasesBefore = MoveTemp(PhasesBefore);
	Update.PhasesDuring = MoveTemp(PhasesDuring);
	Update.bRejected = bRejected;

	for (const TPair<FName, double>& Value : Frame.Values)
	{
		const double* Last = LastValues.Find(Value.Key);
		if (!Last || *Last != Value.Value)
		{
			Update.Values.Add(Value.Key, Value.Value);
			LastValues.Add(Value.Key, Value.Value);
		}
	}

	const TArray<FName> Contexts = Frame.Contexts.Array();
	if (bFirst || Contexts != LastContexts)
	{
		Update.Contexts = Contexts;
		LastContexts = Contexts;
	}

	if (bFirst || Frame.Objective != LastObjective || !Frame.ObjectiveLocation.Equals(LastObjectiveLocation, 0.0))
	{
		Update.bObjectiveChanged = true;
		Update.Objective = Frame.Objective;
		Update.ObjectiveLocation = Frame.ObjectiveLocation;
		LastObjective = Frame.Objective;
		LastObjectiveLocation = Frame.ObjectiveLocation;
	}

	TArray<FName> Registered;
	for (const FAdaptiveActionState& State : Frame.Actions)
	{
		Registered.Add(State.Name);
	}
	if (bFirst || Registered != LastRegistered)
	{
		TArray<FAdaptiveRecordedAction>& List = Update.Registered.Emplace();
		for (const FAdaptiveActionState& State : Frame.Actions)
		{
			FAdaptiveRecordedAction& Action = List.AddDefaulted_GetRef();
			Action.Name = State.Name;
			Action.Asset = State.Action ? State.Action->GetPathName() : FString();
		}
		LastRegistered = Registered;
	}

	// only what the game was asked: actions the contexts allowed
	for (const FAdaptiveActionState& State : Frame.Actions)
	{
		if (!State.bContextAllowed)
		{
			continue;
		}
		FAdaptiveRecordedAction Answer;
		Answer.Name = State.Name;
		Answer.bPossible = State.bAvailable;
		Answer.Reason = State.Reason;
		Answer.Cost = State.Cost;
		const FAdaptiveRecordedAction* Last = LastAnswers.Find(State.Name);
		if (!Last || !SameAnswer(*Last, Answer))
		{
			Update.Answers.Add(Answer);
			LastAnswers.Add(State.Name, Answer);
		}
	}

	bFirst = false;
	return Update;
}
