#include "Replay/AdaptiveReplayEvaluation.h"
#include "Dom/JsonValue.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Misc/FileHelper.h"
#include "Pipeline/AdaptiveNeedTypes.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	double Round3(double Value)
	{
		return FMath::RoundToDouble(Value * 1000.0) / 1000.0;
	}

	TSharedPtr<FJsonValue> OptionalNumber(const TOptional<double>& Value)
	{
		return Value.IsSet() ? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNumber>(Round3(*Value))) : StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>());
	}

	FString FormatOptional(const TOptional<double>& Value)
	{
		return Value.IsSet() ? FString::Printf(TEXT("%.2f"), *Value) : FString(TEXT("–"));
	}

	TArray<FName> NamesFromJson(const FJsonObject& Object, const TCHAR* Field)
	{
		TArray<FName> Names;
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Object.TryGetArrayField(Field, Array))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Array)
			{
				Names.Add(FName(*Value->AsString()));
			}
		}
		return Names;
	}

	bool IsElevated(const FAdaptiveNeedState& Need)
	{
		return Need.bActive && Need.Level != EAdaptiveNeedLevel::Normal;
	}
}

// Labels

bool FAdaptiveReplayLabels::LoadFromString(const FString& Json, FAdaptiveReplayLabels& OutLabels, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root)
	{
		OutError = TEXT("not valid JSON");
		return false;
	}

	OutLabels = FAdaptiveReplayLabels();
	Root->TryGetNumberField(TEXT("tolerance"), OutLabels.Tolerance);
	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (!Root->TryGetArrayField(TEXT("labels"), Array))
	{
		OutError = TEXT("no \"labels\" array");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Array)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value->TryGetObject(Object))
		{
			continue;
		}
		FAdaptiveReplayLabel& Label = OutLabels.Labels.AddDefaulted_GetRef();
		if (!(*Object)->TryGetNumberField(TEXT("from"), Label.From) || !(*Object)->TryGetNumberField(TEXT("to"), Label.To) || Label.To <= Label.From)
		{
			OutError = FString::Printf(TEXT("label %d needs \"from\" < \"to\""), OutLabels.Labels.Num() - 1);
			return false;
		}
		Label.Needs = NamesFromJson(**Object, TEXT("needs"));
		Label.Actions = NamesFromJson(**Object, TEXT("actions"));
		(*Object)->TryGetStringField(TEXT("note"), Label.Note);
	}
	return true;
}

bool FAdaptiveReplayLabels::LoadFromFile(const FString& Path, FAdaptiveReplayLabels& OutLabels, FString& OutError)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("can't read %s"), *Path);
		return false;
	}
	return LoadFromString(Json, OutLabels, OutError);
}

bool FAdaptiveReplayLabels::GetExpected(double Time, TSet<FName>& OutNeeds) const
{
	OutNeeds.Reset();
	for (const FAdaptiveReplayLabel& Label : Labels)
	{
		const bool bAfterFrom = Time >= Label.From && Time < Label.From + Tolerance;
		const bool bAfterTo = Time >= Label.To && Time < Label.To + Tolerance;
		if (bAfterFrom || bAfterTo)
		{
			return false;
		}
		if (Time >= Label.From && Time < Label.To)
		{
			OutNeeds.Append(Label.Needs);
		}
	}
	return true;
}

// Confusion

TOptional<double> FAdaptiveNeedConfusion::Precision() const
{
	const int32 Predicted = TruePositives + FalsePositives;
	return Predicted > 0 ? TOptional<double>(double(TruePositives) / Predicted) : TOptional<double>();
}

TOptional<double> FAdaptiveNeedConfusion::Recall() const
{
	const int32 Expected = TruePositives + FalseNegatives;
	return Expected > 0 ? TOptional<double>(double(TruePositives) / Expected) : TOptional<double>();
}

TOptional<double> FAdaptiveNeedConfusion::F1() const
{
	const TOptional<double> P = Precision();
	const TOptional<double> R = Recall();
	if (!P.IsSet() || !R.IsSet())
	{
		return TOptional<double>();
	}
	return *P + *R > 0.0 ? TOptional<double>(2.0 * *P * *R / (*P + *R)) : TOptional<double>(0.0);
}

// Evaluation

FAdaptiveReplayEvaluation FAdaptiveReplayEvaluation::Evaluate(const TArray<FAdaptiveReplayStep>& Steps, const FAdaptiveReplayLabels& Labels)
{
	FAdaptiveReplayEvaluation Result;

	TSet<FName> AllNeeds;
	for (const FAdaptiveReplayStep& Step : Steps)
	{
		for (const TPair<FName, FAdaptiveNeedState>& Need : Step.Needs)
		{
			AllNeeds.Add(Need.Key);
		}
	}
	for (const FAdaptiveReplayLabel& Label : Labels.Labels)
	{
		AllNeeds.Append(Label.Needs);
	}
	for (const FName Need : AllNeeds)
	{
		Result.Needs.Add(Need);
	}

	TSet<FName> Expected;
	for (const FAdaptiveReplayStep& Step : Steps)
	{
		if (!Labels.GetExpected(Step.Time, Expected))
		{
			++Result.SkippedUpdates;
			continue;
		}
		++Result.CountedUpdates;
		for (TPair<FName, FAdaptiveNeedConfusion>& Need : Result.Needs)
		{
			const FAdaptiveNeedState* State = Step.Needs.Find(Need.Key);
			const bool bElevated = State && IsElevated(*State);
			const bool bExpected = Expected.Contains(Need.Key);
			(bElevated ? (bExpected ? Need.Value.TruePositives : Need.Value.FalsePositives) : (bExpected ? Need.Value.FalseNegatives : Need.Value.TrueNegatives))++;
		}
	}

	for (const FAdaptiveReplayLabel& Label : Labels.Labels)
	{
		FAdaptiveLabelResult& LabelResult = Result.Labels.AddDefaulted_GetRef();
		LabelResult.Label = Label;
		for (const FName Need : Label.Needs)
		{
			LabelResult.Delays.Add(Need);
		}

		for (const FAdaptiveReplayStep& Step : Steps)
		{
			if (Step.Time < Label.From || Step.Time >= Label.To)
			{
				continue;
			}
			for (TPair<FName, TOptional<double>>& Delay : LabelResult.Delays)
			{
				const FAdaptiveNeedState* State = Step.Needs.Find(Delay.Key);
				if (!Delay.Value.IsSet() && State && IsElevated(*State))
				{
					Delay.Value = Step.Time - Label.From;
				}
			}
			if (!Step.Intervention.IsNone())
			{
				LabelResult.Selected.Add(Step.Intervention);
			}
		}

		if (!Label.Actions.IsEmpty())
		{
			LabelResult.bFittingAction = LabelResult.Selected.ContainsByPredicate([&Label](FName Selected) { return Label.Actions.Contains(Selected); });
		}
	}

	Result.Needs.KeySort(FNameLexicalLess());
	return Result;
}

TSharedRef<FJsonObject> FAdaptiveReplayEvaluation::ToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("counted_updates"), CountedUpdates);
	Root->SetNumberField(TEXT("skipped_updates"), SkippedUpdates);

	TSharedRef<FJsonObject> NeedsObject = MakeShared<FJsonObject>();
	for (const TPair<FName, FAdaptiveNeedConfusion>& Need : Needs)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("tp"), Need.Value.TruePositives);
		Object->SetNumberField(TEXT("fp"), Need.Value.FalsePositives);
		Object->SetNumberField(TEXT("fn"), Need.Value.FalseNegatives);
		Object->SetNumberField(TEXT("tn"), Need.Value.TrueNegatives);
		Object->SetField(TEXT("precision"), OptionalNumber(Need.Value.Precision()));
		Object->SetField(TEXT("recall"), OptionalNumber(Need.Value.Recall()));
		Object->SetField(TEXT("f1"), OptionalNumber(Need.Value.F1()));
		NeedsObject->SetObjectField(Need.Key.ToString(), Object);
	}
	Root->SetObjectField(TEXT("needs"), NeedsObject);

	TArray<TSharedPtr<FJsonValue>> LabelArray;
	for (const FAdaptiveLabelResult& Label : Labels)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("from"), Label.Label.From);
		Object->SetNumberField(TEXT("to"), Label.Label.To);
		Object->SetStringField(TEXT("note"), Label.Label.Note);
		TSharedRef<FJsonObject> Delays = MakeShared<FJsonObject>();
		for (const TPair<FName, TOptional<double>>& Delay : Label.Delays)
		{
			Delays->SetField(Delay.Key.ToString(), OptionalNumber(Delay.Value));
		}
		Object->SetObjectField(TEXT("delays"), Delays);
		TArray<TSharedPtr<FJsonValue>> Selected;
		for (const FName Name : Label.Selected)
		{
			Selected.Add(MakeShared<FJsonValueString>(Name.ToString()));
		}
		Object->SetArrayField(TEXT("selected"), Selected);
		if (Label.bFittingAction.IsSet())
		{
			Object->SetBoolField(TEXT("fitting_action"), *Label.bFittingAction);
		}
		LabelArray.Add(MakeShared<FJsonValueObject>(Object));
	}
	Root->SetArrayField(TEXT("labels"), LabelArray);
	return Root;
}

FString FAdaptiveReplayEvaluation::ToText() const
{
	FString Text = FString::Printf(TEXT("Labels: %d updates counted, %d within the tolerance skipped\n"), CountedUpdates, SkippedUpdates);
	for (const TPair<FName, FAdaptiveNeedConfusion>& Need : Needs)
	{
		const FAdaptiveNeedConfusion& C = Need.Value;
		Text += FString::Printf(TEXT("  %-16s TP %4d  FP %4d  FN %4d  TN %4d  precision %s  recall %s  F1 %s\n"), *Need.Key.ToString(),
			C.TruePositives, C.FalsePositives, C.FalseNegatives, C.TrueNegatives, *FormatOptional(C.Precision()), *FormatOptional(C.Recall()), *FormatOptional(C.F1()));
	}
	for (const FAdaptiveLabelResult& Label : Labels)
	{
		TArray<FString> Delays;
		for (const TPair<FName, TOptional<double>>& Delay : Label.Delays)
		{
			Delays.Add(FString::Printf(TEXT("%s %s"), *Delay.Key.ToString(), Delay.Value.IsSet() ? *FString::Printf(TEXT("after %.1f s"), *Delay.Value) : TEXT("never")));
		}
		TArray<FString> Selected;
		for (const FName Name : Label.Selected)
		{
			Selected.Add(Name.ToString());
		}
		Text += FString::Printf(TEXT("  %.1f–%.1f s %s: %s; selected %s%s\n"), Label.Label.From, Label.Label.To, *Label.Label.Note,
			Delays.IsEmpty() ? TEXT("no need expected") : *FString::Join(Delays, TEXT(", ")),
			Selected.IsEmpty() ? TEXT("nothing") : *FString::Join(Selected, TEXT(", ")),
			Label.bFittingAction.IsSet() ? (*Label.bFittingAction ? TEXT(" (fits)") : TEXT(" (no fitting action)")) : TEXT(""));
	}
	return Text;
}

// Session log

bool FAdaptiveSessionRecording::LoadSessionLog(const FString& Path, FAdaptiveSessionRecording& OutRecording, FString& OutError)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		OutError = FString::Printf(TEXT("can't read %s"), *Path);
		return false;
	}

	OutRecording = FAdaptiveSessionRecording();
	for (const FString& Line : Lines)
	{
		TSharedPtr<FJsonObject> Record;
		if (Line.IsEmpty() || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), Record) || !Record)
		{
			continue;
		}

		const FString Type = Record->GetStringField(TEXT("type"));
		const double Time = Record->GetNumberField(TEXT("t"));
		const TSharedPtr<FJsonObject>* Payload = nullptr;
		if (!Record->TryGetObjectField(TEXT("payload"), Payload))
		{
			continue;
		}

		if (Type == AdaptiveLogRecordType::Input)
		{
			FAdaptiveRecordedUpdate Update;
			if (AdaptiveRecording::FromJson(**Payload, Update))
			{
				OutRecording.Updates.Add(MoveTemp(Update));
			}
		}
		else if (Type == AdaptiveLogRecordType::State)
		{
			OutRecording.LiveLevelChanges.Add({ Time, FName(*(*Payload)->GetStringField(TEXT("need"))), (*Payload)->GetStringField(TEXT("level")) });
		}
		else if (Type == AdaptiveLogRecordType::Intervention && (*Payload)->GetStringField(TEXT("phase")) == TEXT("selected"))
		{
			OutRecording.LiveSelections.Add({ Time, FName(*(*Payload)->GetStringField(TEXT("name"))), FString() });
		}
		else if (Type == AdaptiveLogRecordType::SessionStart)
		{
			OutRecording.SessionId = Record->GetStringField(TEXT("session_id"));
		}
		else if (Type == AdaptiveLogRecordType::PipelineStart)
		{
			OutRecording.Config = (*Payload)->GetStringField(TEXT("config"));
		}
	}

	if (OutRecording.Updates.IsEmpty())
	{
		OutError = TEXT("no input records: the session was logged without Record Inputs, or before #24");
		return false;
	}
	return true;
}

// Live comparison

bool FAdaptiveLiveComparison::Matches() const
{
	return MatchedLevelChanges == LiveLevelChanges && MatchedLevelChanges == ReplayLevelChanges
		&& MatchedSelections == LiveSelections && MatchedSelections == ReplaySelections;
}

FAdaptiveLiveComparison FAdaptiveLiveComparison::Compare(const TArray<FAdaptiveReplayStep>& Steps, const FAdaptiveSessionRecording& Recording, double MaxOffset)
{
	FAdaptiveLiveComparison Result;

	// the replay's level changes, as the need stage logs them: every need starts Normal
	TArray<FAdaptiveLiveRecord> ReplayChanges;
	TArray<FAdaptiveLiveRecord> ReplaySelections;
	TMap<FName, EAdaptiveNeedLevel> Levels;
	for (const FAdaptiveReplayStep& Step : Steps)
	{
		for (const TPair<FName, FAdaptiveNeedState>& Need : Step.Needs)
		{
			EAdaptiveNeedLevel& Level = Levels.FindOrAdd(Need.Key, EAdaptiveNeedLevel::Normal);
			if (Need.Value.Level != Level)
			{
				Level = Need.Value.Level;
				ReplayChanges.Add({ Step.Time, Need.Key, AdaptiveNeedMath::LevelName(Level) });
			}
		}
		if (!Step.Intervention.IsNone())
		{
			ReplaySelections.Add({ Step.Time, Step.Intervention, FString() });
		}
	}

	auto Match = [&Result, MaxOffset](const TArray<FAdaptiveLiveRecord>& Live, const TArray<FAdaptiveLiveRecord>& Replay, const TCHAR* What, int32& OutMatched)
	{
		TArray<bool> Used;
		Used.Init(false, Replay.Num());
		for (const FAdaptiveLiveRecord& Record : Live)
		{
			const int32 Found = Replay.IndexOfByPredicate([&](const FAdaptiveLiveRecord& Candidate)
			{
				const int32 Index = &Candidate - Replay.GetData();
				return !Used[Index] && Candidate.Name == Record.Name && Candidate.Value == Record.Value && FMath::Abs(Candidate.Time - Record.Time) <= MaxOffset;
			});
			if (Found != INDEX_NONE)
			{
				Used[Found] = true;
				++OutMatched;
			}
			else if (Result.Differences.Num() < 10)
			{
				Result.Differences.Add(FString::Printf(TEXT("%s %s %s at %.2f s live, not in the replay"), What, *Record.Name.ToString(), *Record.Value, Record.Time));
			}
		}
		for (int32 Index = 0; Index < Replay.Num(); ++Index)
		{
			if (!Used[Index] && Result.Differences.Num() < 10)
			{
				Result.Differences.Add(FString::Printf(TEXT("%s %s %s at %.2f s in the replay, not live"), What, *Replay[Index].Name.ToString(), *Replay[Index].Value, Replay[Index].Time));
			}
		}
	};

	Result.LiveLevelChanges = Recording.LiveLevelChanges.Num();
	Result.ReplayLevelChanges = ReplayChanges.Num();
	Match(Recording.LiveLevelChanges, ReplayChanges, TEXT("level"), Result.MatchedLevelChanges);
	Result.LiveSelections = Recording.LiveSelections.Num();
	Result.ReplaySelections = ReplaySelections.Num();
	Match(Recording.LiveSelections, ReplaySelections, TEXT("selection"), Result.MatchedSelections);
	return Result;
}

TSharedRef<FJsonObject> FAdaptiveLiveComparison::ToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("matches"), Matches());
	Root->SetNumberField(TEXT("live_level_changes"), LiveLevelChanges);
	Root->SetNumberField(TEXT("replay_level_changes"), ReplayLevelChanges);
	Root->SetNumberField(TEXT("matched_level_changes"), MatchedLevelChanges);
	Root->SetNumberField(TEXT("live_selections"), LiveSelections);
	Root->SetNumberField(TEXT("replay_selections"), ReplaySelections);
	Root->SetNumberField(TEXT("matched_selections"), MatchedSelections);
	TArray<TSharedPtr<FJsonValue>> Array;
	for (const FString& Difference : Differences)
	{
		Array.Add(MakeShared<FJsonValueString>(Difference));
	}
	Root->SetArrayField(TEXT("differences"), Array);
	return Root;
}
