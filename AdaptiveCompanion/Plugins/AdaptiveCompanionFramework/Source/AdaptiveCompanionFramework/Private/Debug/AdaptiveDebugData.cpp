#include "Debug/AdaptiveDebugData.h"

void FAdaptiveDebugData::SetBar(EAdaptiveDebugSection Section, const FString& Name, double Value)
{
	FAdaptiveDebugEntry& Entry = FindOrAdd(Section, Name);
	Entry.Kind = EAdaptiveDebugValueKind::Bar;
	// NaN is kept so the overlay can show it instead of an empty bar
	Entry.Value = FMath::IsNaN(Value) ? Value : FMath::Clamp(Value, 0.0, 1.0);
	Entry.Text.Reset();
}

void FAdaptiveDebugData::SetNumber(EAdaptiveDebugSection Section, const FString& Name, double Value)
{
	FAdaptiveDebugEntry& Entry = FindOrAdd(Section, Name);
	Entry.Kind = EAdaptiveDebugValueKind::Number;
	Entry.Value = Value;
	Entry.Text.Reset();
}

void FAdaptiveDebugData::SetText(EAdaptiveDebugSection Section, const FString& Name, const FString& Text)
{
	FAdaptiveDebugEntry& Entry = FindOrAdd(Section, Name);
	Entry.Kind = EAdaptiveDebugValueKind::Text;
	Entry.Value = 0.0;
	Entry.Text = Text;
}

void FAdaptiveDebugData::Remove(EAdaptiveDebugSection Section, const FString& Name)
{
	if (Section < EAdaptiveDebugSection::Count)
	{
		Sections[static_cast<int32>(Section)].RemoveAll([&Name](const FAdaptiveDebugEntry& Entry) { return Entry.Name == Name; });
	}
}

void FAdaptiveDebugData::ClearSection(EAdaptiveDebugSection Section)
{
	if (Section < EAdaptiveDebugSection::Count)
	{
		Sections[static_cast<int32>(Section)].Reset();
	}
}

void FAdaptiveDebugData::ClearAll()
{
	for (TArray<FAdaptiveDebugEntry>& Entries : Sections)
	{
		Entries.Reset();
	}
}

const TArray<FAdaptiveDebugEntry>& FAdaptiveDebugData::GetEntries(EAdaptiveDebugSection Section) const
{
	check(Section < EAdaptiveDebugSection::Count);
	return Sections[static_cast<int32>(Section)];
}

FAdaptiveDebugEntry& FAdaptiveDebugData::FindOrAdd(EAdaptiveDebugSection Section, const FString& Name)
{
	check(Section < EAdaptiveDebugSection::Count);
	TArray<FAdaptiveDebugEntry>& Entries = Sections[static_cast<int32>(Section)];

	// sections hold a handful of rows, a linear search is cheaper than a map here
	for (FAdaptiveDebugEntry& Entry : Entries)
	{
		if (Entry.Name == Name)
		{
			return Entry;
		}
	}

	FAdaptiveDebugEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Name = Name;
	return Entry;
}
