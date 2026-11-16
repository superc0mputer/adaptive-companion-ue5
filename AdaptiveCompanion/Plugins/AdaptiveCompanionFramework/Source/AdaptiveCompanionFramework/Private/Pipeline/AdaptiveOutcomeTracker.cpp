#include "Pipeline/AdaptiveOutcomeTracker.h"

TOptional<double> FAdaptiveOutcomeMeasure::GetChange() const
{
	return Baseline.IsSet() && After.IsSet() ? TOptional<double>(*After - *Baseline) : TOptional<double>();
}

TOptional<double> FAdaptiveOutcomeMeasure::GetEndChange() const
{
	return Baseline.IsSet() && End.IsSet() ? TOptional<double>(*End - *Baseline) : TOptional<double>();
}

const FAdaptiveOutcomeMeasure* FAdaptiveOutcome::FindMeasure(FName Name) const
{
	return Measures.FindByPredicate([Name](const FAdaptiveOutcomeMeasure& Measure) { return Measure.Name == Name; });
}

TOptional<double> FAdaptiveOutcomeTracker::ValueOf(const FSample& Sample, const FAdaptiveOutcomeMeasure& Measure)
{
	const double* Value = Measure.bNeed ? Sample.Needs.Find(Measure.Name) : Sample.Features.Find(Measure.Name);
	return Value ? TOptional<double>(*Value) : TOptional<double>();
}

TArray<FAdaptiveOutcome> FAdaptiveOutcomeTracker::Update(const FAdaptivePipelineFrame& Frame)
{
	FSample& Sample = Latest.Emplace();
	Sample.Time = Frame.Time;
	for (const TPair<FName, FAdaptiveNeedState>& Need : Frame.Needs)
	{
		Sample.Needs.Add(Need.Key, Need.Value.Score);
		Sample.Levels.Add(Need.Key, Need.Value.Level);
	}
	Sample.Features = Frame.Features;
	Sample.Contexts = Frame.Contexts;

	TArray<FAdaptiveOutcome> Finished;
	for (int32 Index = 0; Index < Open.Num(); ++Index)
	{
		FAdaptiveOutcome& Outcome = Open[Index];
		if (!Outcome.HasEnded() || Sample.Time <= Outcome.EndTime)
		{
			continue;
		}

		// what follows no longer shows this intervention's effect alone
		FString CutShort;
		if (!Frame.Intervention.IsNone())
		{
			CutShort = TEXT("next intervention");
		}
		else if (!(Sample.Contexts.Num() == Outcome.EndContexts.Num() && Sample.Contexts.Includes(Outcome.EndContexts)))
		{
			CutShort = TEXT("context changed");
		}

		if (CutShort.IsEmpty())
		{
			for (int32 Measure = 0; Measure < Outcome.Measures.Num(); ++Measure)
			{
				if (const TOptional<double> Value = ValueOf(Sample, Outcome.Measures[Measure]))
				{
					Outcome.AfterSums[Measure] += *Value;
					++Outcome.AfterCounts[Measure];
				}
			}
			if (const EAdaptiveNeedLevel* Level = Sample.Levels.Find(Outcome.Need))
			{
				Outcome.AfterLevel = *Level;
			}
			++Outcome.AfterUpdates;
			Outcome.AfterDuration = Sample.Time - Outcome.EndTime;
		}

		if (!CutShort.IsEmpty() || Outcome.AfterDuration >= AfterWindow - UE_KINDA_SMALL_NUMBER)
		{
			Finish(Outcome, CutShort);
			Finished.Add(MoveTemp(Outcome));
			Open.RemoveAt(Index--);
		}
	}
	return Finished;
}

void FAdaptiveOutcomeTracker::Start(int32 Id, FName Action, FName Need, const TArray<FName>& Features, double Time)
{
	FAdaptiveOutcome& Outcome = Open.AddDefaulted_GetRef();
	Outcome.Id = Id;
	Outcome.Action = Action;
	Outcome.Need = Need;
	Outcome.SelectedTime = Time;

	if (!Need.IsNone())
	{
		Outcome.Measures.Add({ Need, true });
	}
	for (const FName Feature : Features)
	{
		if (!Feature.IsNone() && !Outcome.FindMeasure(Feature))
		{
			Outcome.Measures.Add({ Feature, false });
		}
	}
	Outcome.AfterSums.Init(0.0, Outcome.Measures.Num());
	Outcome.AfterCounts.Init(0, Outcome.Measures.Num());

	if (Latest.IsSet())
	{
		for (FAdaptiveOutcomeMeasure& Measure : Outcome.Measures)
		{
			Measure.Baseline = ValueOf(*Latest, Measure);
		}
		if (const EAdaptiveNeedLevel* Level = Latest->Levels.Find(Need))
		{
			Outcome.BaselineLevel = *Level;
		}
	}
	Outcome.EndLevel = Outcome.AfterLevel = Outcome.BaselineLevel;
}

bool FAdaptiveOutcomeTracker::End(FName Action, FName Phase, const FString& Detail, double Time)
{
	// the latest one of this action that is still running; only one intervention runs at a time
	for (int32 Index = Open.Num() - 1; Index >= 0; --Index)
	{
		FAdaptiveOutcome& Outcome = Open[Index];
		if (Outcome.Action != Action || Outcome.HasEnded())
		{
			continue;
		}

		Outcome.Phase = Phase;
		Outcome.Detail = Detail;
		Outcome.EndTime = Time;
		if (Latest.IsSet())
		{
			for (FAdaptiveOutcomeMeasure& Measure : Outcome.Measures)
			{
				Measure.End = ValueOf(*Latest, Measure);
			}
			if (const EAdaptiveNeedLevel* Level = Latest->Levels.Find(Outcome.Need))
			{
				Outcome.EndLevel = *Level;
			}
			Outcome.EndContexts = Latest->Contexts;
		}
		Outcome.AfterLevel = Outcome.EndLevel;
		return true;
	}
	return false;
}

TArray<FAdaptiveOutcome> FAdaptiveOutcomeTracker::Flush(const FString& Reason)
{
	TArray<FAdaptiveOutcome> Finished = MoveTemp(Open);
	Open.Reset();
	for (FAdaptiveOutcome& Outcome : Finished)
	{
		Finish(Outcome, Reason);
	}
	return Finished;
}

void FAdaptiveOutcomeTracker::Finish(FAdaptiveOutcome& Outcome, const FString& CutShort)
{
	Outcome.CutShort = CutShort;
	for (int32 Measure = 0; Measure < Outcome.Measures.Num(); ++Measure)
	{
		if (Outcome.AfterCounts.IsValidIndex(Measure) && Outcome.AfterCounts[Measure] > 0)
		{
			Outcome.Measures[Measure].After = Outcome.AfterSums[Measure] / Outcome.AfterCounts[Measure];
		}
	}
}
