#pragma once

#include "CoreMinimal.h"
#include "AdaptiveDebugData.generated.h"

/** One section of the debug overlay per pipeline stage, in pipeline order */
UENUM(BlueprintType)
enum class EAdaptiveDebugSection : uint8
{
	Telemetry	UMETA(DisplayName = "1 Telemetry"),
	Features	UMETA(DisplayName = "2 Features"),
	Need		UMETA(DisplayName = "3 Assistance Need"),
	Context		UMETA(DisplayName = "4 Context"),
	Selection	UMETA(DisplayName = "5 Intervention Selection"),
	Behavior	UMETA(DisplayName = "6 Behavior"),

	Count		UMETA(Hidden)
};

enum class EAdaptiveDebugValueKind : uint8
{
	/** Value in [0, 1], drawn as a bar */
	Bar,
	/** Any number, drawn as text */
	Number,
	Text
};

struct FAdaptiveDebugEntry
{
	FString Name;
	EAdaptiveDebugValueKind Kind = EAdaptiveDebugValueKind::Number;
	double Value = 0.0;
	FString Text;
};

/**
 *  The values the debug overlay shows, grouped by pipeline section. Entries keep the order in
 *  which they were first set, so rows don't jump around while values change.
 */
class ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveDebugData
{
public:

	/** Sets a value in [0, 1] (clamped), shown as a bar */
	void SetBar(EAdaptiveDebugSection Section, const FString& Name, double Value);
	void SetNumber(EAdaptiveDebugSection Section, const FString& Name, double Value);
	void SetText(EAdaptiveDebugSection Section, const FString& Name, const FString& Text);

	void Remove(EAdaptiveDebugSection Section, const FString& Name);
	void ClearSection(EAdaptiveDebugSection Section);
	void ClearAll();

	const TArray<FAdaptiveDebugEntry>& GetEntries(EAdaptiveDebugSection Section) const;

private:

	FAdaptiveDebugEntry& FindOrAdd(EAdaptiveDebugSection Section, const FString& Name);

	TArray<FAdaptiveDebugEntry> Sections[static_cast<int32>(EAdaptiveDebugSection::Count)];
};
