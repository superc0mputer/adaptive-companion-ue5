#include "Measurements/AdaptiveMeasurementTables.h"
#include "Logging/AdaptiveLogRecordTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Pipeline/AdaptivePerfMonitor.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/** The spans of a latency record, in the order of the table */
	const TCHAR* const LatencySpans[] = {
		TEXT("smoothing"), TEXT("selection_wait"), TEXT("pipeline"), TEXT("handoff"), TEXT("render"), TEXT("reaction"), TEXT("response"), TEXT("total"),
	};

	const TCHAR* const LatencyTimes[] = {
		TEXT("threshold"), TEXT("level_change"), TEXT("selected"), TEXT("dispatched"), TEXT("started"), TEXT("presented"),
	};

	/** A number field of Object, or of its sub-object Path ("ms.response"); unset if missing or null */
	TOptional<double> GetNumber(const FJsonObject& Object, const FString& Path)
	{
		FString Head, Tail;
		if (Path.Split(TEXT("."), &Head, &Tail))
		{
			const TSharedPtr<FJsonObject>* Child = nullptr;
			return Object.TryGetObjectField(Head, Child) && Child->IsValid() ? GetNumber(**Child, Tail) : TOptional<double>();
		}
		double Value = 0.0;
		return Object.TryGetNumberField(Path, Value) ? TOptional<double>(Value) : TOptional<double>();
	}

	FString Cell(const FJsonObject& Object, const FString& Path, int32 Decimals)
	{
		const TOptional<double> Value = GetNumber(Object, Path);
		return Value.IsSet() ? FAdaptiveTable::Number(*Value, Decimals) : FString();
	}

	FString CsvEscape(const FString& Text)
	{
		if (!Text.Contains(TEXT(",")) && !Text.Contains(TEXT("\"")) && !Text.Contains(TEXT("\n")))
		{
			return Text;
		}
		return TEXT("\"") + Text.Replace(TEXT("\""), TEXT("\"\"")) + TEXT("\"");
	}
}

void FAdaptiveTable::AddRow(const TArray<TPair<FString, FString>>& Cells)
{
	TArray<FString>& Row = Rows.AddDefaulted_GetRef();
	Row.SetNum(Columns.Num());
	for (const TPair<FString, FString>& Pair : Cells)
	{
		int32 Index = Columns.IndexOfByKey(Pair.Key);
		if (Index == INDEX_NONE)
		{
			Index = Columns.Add(Pair.Key);
		}
		if (Row.Num() <= Index)
		{
			Row.SetNum(Index + 1);
		}
		Row[Index] = Pair.Value;
	}
}

FString FAdaptiveTable::ToCsv() const
{
	TArray<FString> Lines;
	TArray<FString> Escaped;
	for (const FString& Column : Columns)
	{
		Escaped.Add(CsvEscape(Column));
	}
	Lines.Add(FString::Join(Escaped, TEXT(",")));
	for (const TArray<FString>& Row : Rows)
	{
		Escaped.Reset();
		for (int32 Index = 0; Index < Columns.Num(); ++Index)
		{
			Escaped.Add(Row.IsValidIndex(Index) ? CsvEscape(Row[Index]) : FString());
		}
		Lines.Add(FString::Join(Escaped, TEXT(",")));
	}
	return FString::Join(Lines, TEXT("\n")) + TEXT("\n");
}

FString FAdaptiveTable::ToMarkdown() const
{
	FString Text = TEXT("| ") + FString::Join(Columns, TEXT(" | ")) + TEXT(" |\n|");
	for (int32 Index = 0; Index < Columns.Num(); ++Index)
	{
		Text += TEXT("---|");
	}
	Text += TEXT("\n");
	for (const TArray<FString>& Row : Rows)
	{
		TArray<FString> Cells;
		for (int32 Index = 0; Index < Columns.Num(); ++Index)
		{
			Cells.Add(Row.IsValidIndex(Index) && !Row[Index].IsEmpty() ? Row[Index] : FString(TEXT("–")));
		}
		Text += TEXT("| ") + FString::Join(Cells, TEXT(" | ")) + TEXT(" |\n");
	}
	return Text;
}

FString FAdaptiveTable::Number(double Value, int32 Decimals)
{
	const double Factor = FMath::Pow(10.0, Decimals);
	const double Rounded = FMath::RoundToDouble(Value * Factor) / Factor;
	return FString::SanitizeFloat(Rounded, 0);
}

bool FAdaptiveMeasurementTables::LoadSessionLog(const FString& Path, FString& OutError)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		OutError = TEXT("can't read the file");
		return false;
	}

	const FString Session = FPaths::GetBaseFilename(Path);
	Sessions.AddUnique(Session);
	for (const FString& Line : Lines)
	{
		// most lines are other records; only parse the ones that can be measurements
		if (!Line.Contains(TEXT("\"type\":\"latency\"")) && !Line.Contains(TEXT("\"type\":\"perf")))
		{
			continue;
		}
		TSharedPtr<FJsonObject> Record;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), Record) || !Record.IsValid())
		{
			continue;
		}
		const TSharedPtr<FJsonObject>* Payload = nullptr;
		if (Record->TryGetObjectField(TEXT("payload"), Payload))
		{
			AddRecord(Session, Record->GetStringField(TEXT("type")), Record->GetNumberField(TEXT("t")), *Payload);
		}
	}
	return true;
}

void FAdaptiveMeasurementTables::AddRecord(const FString& Session, const FString& Type, double Time, const TSharedPtr<FJsonObject>& Payload)
{
	if (!Payload.IsValid())
	{
		return;
	}
	Sessions.AddUnique(Session);
	if (Type == AdaptiveLogRecordType::Latency)
	{
		Latencies.Add({ Session, Time, Payload });
	}
	else if (Type == AdaptiveLogRecordType::Perf)
	{
		PerfWindows.Add({ Session, Time, Payload });
	}
	else if (Type == AdaptiveLogRecordType::PerfSummary)
	{
		PerfSummaries.Add({ Session, Time, Payload });
	}
}

FAdaptiveTable FAdaptiveMeasurementTables::GetLatencyTable() const
{
	FAdaptiveTable Table;
	for (const FRecord& Record : Latencies)
	{
		const FJsonObject& Payload = *Record.Payload;
		TArray<TPair<FString, FString>> Cells;
		Cells.Add({ TEXT("session"), Record.Session });
		Cells.Add({ TEXT("id"), Cell(Payload, TEXT("id"), 0) });
		Cells.Add({ TEXT("action"), Payload.GetStringField(TEXT("name")) });
		Cells.Add({ TEXT("need"), Payload.GetStringField(TEXT("need")) });
		bool bRepeat = false;
		Payload.TryGetBoolField(TEXT("repeat"), bRepeat);
		Cells.Add({ TEXT("repeat"), bRepeat ? TEXT("1") : TEXT("0") });
		for (const TCHAR* Time : LatencyTimes)
		{
			Cells.Add({ FString(Time) + TEXT("_s"), Cell(Payload, FString(TEXT("times.")) + Time, 4) });
		}
		for (const TCHAR* Span : LatencySpans)
		{
			Cells.Add({ FString(Span) + TEXT("_ms"), Cell(Payload, FString(TEXT("ms.")) + Span, 2) });
		}
		Cells.Add({ TEXT("handoff_frames"), Cell(Payload, TEXT("frames.handoff"), 0) });
		Cells.Add({ TEXT("render_frames"), Cell(Payload, TEXT("frames.render"), 0) });
		FString Incomplete;
		Payload.TryGetStringField(TEXT("incomplete"), Incomplete);
		Cells.Add({ TEXT("incomplete"), Incomplete });
		Table.AddRow(Cells);
	}
	return Table;
}

FAdaptiveTable FAdaptiveMeasurementTables::GetLatencySummary() const
{
	// per action in the order they first appear, then all together
	TArray<FString> Groups;
	for (const FRecord& Record : Latencies)
	{
		Groups.AddUnique(Record.Payload->GetStringField(TEXT("name")));
	}
	Groups.Add(TEXT("all"));

	FAdaptiveTable Table;
	for (const FString& Group : Groups)
	{
		int32 Count = 0;
		int32 Presented = 0;
		int32 Repeats = 0;
		TMap<FString, TArray<float>> Samples;
		TArray<float> Frames;
		for (const FRecord& Record : Latencies)
		{
			const FJsonObject& Payload = *Record.Payload;
			if (Group != TEXT("all") && Payload.GetStringField(TEXT("name")) != Group)
			{
				continue;
			}
			++Count;
			if (GetNumber(Payload, TEXT("times.presented")).IsSet())
			{
				++Presented;
			}
			// a repeat's threshold and level change are the first intervention's, so only its own spans count
			bool bRepeat = false;
			Payload.TryGetBoolField(TEXT("repeat"), bRepeat);
			Repeats += bRepeat ? 1 : 0;
			for (const TCHAR* Span : LatencySpans)
			{
				const bool bFromEpisode = FCString::Strcmp(Span, TEXT("selection_wait")) == 0 || FCString::Strcmp(Span, TEXT("response")) == 0
					|| FCString::Strcmp(Span, TEXT("total")) == 0 || FCString::Strcmp(Span, TEXT("smoothing")) == 0;
				if (bRepeat && bFromEpisode)
				{
					continue;
				}
				if (const TOptional<double> Value = GetNumber(Payload, FString(TEXT("ms.")) + Span))
				{
					Samples.FindOrAdd(Span).Add(float(*Value));
				}
			}
			const TOptional<double> Handoff = GetNumber(Payload, TEXT("frames.handoff"));
			const TOptional<double> Render = GetNumber(Payload, TEXT("frames.render"));
			if (Handoff.IsSet() && Render.IsSet())
			{
				Frames.Add(float(*Handoff + *Render));
			}
		}

		TArray<TPair<FString, FString>> Cells;
		Cells.Add({ TEXT("action"), Group });
		Cells.Add({ TEXT("interventions"), FString::FromInt(Count) });
		Cells.Add({ TEXT("on screen"), FString::FromInt(Presented) });
		Cells.Add({ TEXT("repeats"), FString::FromInt(Repeats) });
		auto AddStats = [&Cells, &Samples](const TCHAR* Span, const TCHAR* Label, bool bP95)
		{
			const FAdaptiveStats Stats = FAdaptiveStats::Of(Samples.FindRef(Span));
			const bool bAny = Stats.Count > 0;
			Cells.Add({ FString(Label) + TEXT(" median ms"), bAny ? FAdaptiveTable::Number(Stats.P50, 1) : FString() });
			if (bP95)
			{
				Cells.Add({ FString(Label) + TEXT(" p95 ms"), bAny ? FAdaptiveTable::Number(Stats.P95, 1) : FString() });
			}
			Cells.Add({ FString(Label) + TEXT(" max ms"), bAny ? FAdaptiveTable::Number(Stats.Max, 1) : FString() });
		};
		AddStats(TEXT("pipeline"), TEXT("pipeline"), false);
		AddStats(TEXT("handoff"), TEXT("handoff"), false);
		AddStats(TEXT("render"), TEXT("render"), false);
		AddStats(TEXT("reaction"), TEXT("selection → screen"), true);
		AddStats(TEXT("selection_wait"), TEXT("level change → selection"), false);
		AddStats(TEXT("response"), TEXT("level change → screen"), true);
		AddStats(TEXT("smoothing"), TEXT("smoothing"), false);
		AddStats(TEXT("total"), TEXT("threshold → screen"), true);
		const FAdaptiveStats FrameStats = FAdaptiveStats::Of(Frames);
		Cells.Add({ TEXT("frames median"), FrameStats.Count > 0 ? FAdaptiveTable::Number(FrameStats.P50, 0) : FString() });
		Table.AddRow(Cells);
	}
	return Table;
}

void FAdaptiveMeasurementTables::AddPerfCells(const FJsonObject& Payload, TArray<TPair<FString, FString>>& Cells, bool bFull)
{
	Cells.Add({ TEXT("duration_s"), Cell(Payload, TEXT("duration"), 1) });
	Cells.Add({ TEXT("updates"), Cell(Payload, TEXT("updates"), 0) });
	Cells.Add({ TEXT("frames"), Cell(Payload, TEXT("frames"), 0) });
	for (const TCHAR* Stat : { TEXT("mean"), TEXT("p50"), TEXT("p95"), TEXT("p99"), TEXT("max") })
	{
		if (bFull || FCString::Strcmp(Stat, TEXT("p50")) != 0)
		{
			Cells.Add({ FString::Printf(TEXT("update_%s_us"), Stat), Cell(Payload, FString(TEXT("update_us.")) + Stat, 1) });
		}
	}
	const TSharedPtr<FJsonObject>* Parts = nullptr;
	if (Payload.TryGetObjectField(TEXT("parts_us"), Parts))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Part : (*Parts)->Values)
		{
			Cells.Add({ Part.Key + TEXT("_mean_us"), Cell(Payload, FString::Printf(TEXT("parts_us.%s.mean"), *Part.Key), 1) });
			if (bFull)
			{
				Cells.Add({ Part.Key + TEXT("_max_us"), Cell(Payload, FString::Printf(TEXT("parts_us.%s.max"), *Part.Key), 1) });
			}
		}
	}
	Cells.Add({ TEXT("api_mean_us_per_frame"), Cell(Payload, TEXT("api_us_per_frame.mean"), 2) });
	Cells.Add({ TEXT("api_max_us_per_frame"), Cell(Payload, TEXT("api_us_per_frame.max"), 1) });
	Cells.Add({ TEXT("api_calls_per_frame"), Cell(Payload, TEXT("api_calls_per_frame"), 2) });
	Cells.Add({ TEXT("input_wait_mean_ms"), Cell(Payload, TEXT("input_wait_ms.mean"), 1) });
	Cells.Add({ TEXT("input_wait_max_ms"), Cell(Payload, TEXT("input_wait_ms.max"), 1) });
	Cells.Add({ TEXT("events_per_update"), Cell(Payload, TEXT("load_per_update.events"), 2) });
	Cells.Add({ TEXT("values_per_update"), Cell(Payload, TEXT("load_per_update.values"), 1) });
	Cells.Add({ TEXT("features"), Cell(Payload, TEXT("load_per_update.features"), 1) });
	Cells.Add({ TEXT("needs"), Cell(Payload, TEXT("load_per_update.needs"), 1) });
	Cells.Add({ TEXT("actions"), Cell(Payload, TEXT("load_per_update.actions"), 1) });
	Cells.Add({ TEXT("frame_mean_ms"), Cell(Payload, TEXT("frame_ms.mean"), 2) });
	Cells.Add({ TEXT("frame_p95_ms"), Cell(Payload, TEXT("frame_ms.p95"), 2) });
	Cells.Add({ TEXT("frame_max_ms"), Cell(Payload, TEXT("frame_ms.max"), 2) });
	Cells.Add({ TEXT("game_thread_ms"), Cell(Payload, TEXT("game_thread_ms"), 2) });
	Cells.Add({ TEXT("render_thread_ms"), Cell(Payload, TEXT("render_thread_ms"), 2) });
	Cells.Add({ TEXT("gpu_ms"), Cell(Payload, TEXT("gpu_ms"), 2) });
	Cells.Add({ TEXT("pipeline_us_per_frame"), Cell(Payload, TEXT("pipeline_us_per_frame"), 2) });
	Cells.Add({ TEXT("game_thread_share_pct"), Cell(Payload, TEXT("game_thread_share_pct"), 3) });
}

FAdaptiveTable FAdaptiveMeasurementTables::GetPerfWindowTable() const
{
	FAdaptiveTable Table;
	for (const FRecord& Record : PerfWindows)
	{
		TArray<TPair<FString, FString>> Cells;
		Cells.Add({ TEXT("session"), Record.Session });
		Cells.Add({ TEXT("t_s"), FAdaptiveTable::Number(Record.Time, 1) });
		AddPerfCells(*Record.Payload, Cells, false);
		Table.AddRow(Cells);
	}
	return Table;
}

FAdaptiveTable FAdaptiveMeasurementTables::GetPerfSummaryTable() const
{
	FAdaptiveTable Table;
	for (const FRecord& Record : PerfSummaries)
	{
		TArray<TPair<FString, FString>> Cells;
		Cells.Add({ TEXT("session"), Record.Session });
		AddPerfCells(*Record.Payload, Cells, true);
		Table.AddRow(Cells);
	}
	return Table;
}
