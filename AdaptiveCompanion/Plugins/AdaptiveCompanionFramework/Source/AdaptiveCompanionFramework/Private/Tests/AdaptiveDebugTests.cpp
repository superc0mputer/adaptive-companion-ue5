#include "Debug/AdaptiveDebugData.h"
#include "Debug/AdaptiveDebugSubsystem.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveDebugDataTest, "AdaptiveCompanion.Debug.Data",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FAdaptiveDebugDataTest::RunTest(const FString& Parameters)
{
	FAdaptiveDebugData Data;

	for (int32 Index = 0; Index < static_cast<int32>(EAdaptiveDebugSection::Count); ++Index)
	{
		TestTrue(TEXT("Sections start empty"), Data.GetEntries(static_cast<EAdaptiveDebugSection>(Index)).IsEmpty());
	}

	Data.SetBar(EAdaptiveDebugSection::Need, TEXT("Need.Combat"), 0.4);
	Data.SetBar(EAdaptiveDebugSection::Need, TEXT("Need.Threat"), 1.7);
	Data.SetBar(EAdaptiveDebugSection::Need, TEXT("Need.Navigation"), -0.2);
	Data.SetText(EAdaptiveDebugSection::Need, TEXT("Level"), TEXT("Normal"));

	const TArray<FAdaptiveDebugEntry>& Need = Data.GetEntries(EAdaptiveDebugSection::Need);
	TestEqual(TEXT("Four entries"), Need.Num(), 4);
	TestEqual(TEXT("Bar value"), Need[0].Value, 0.4);
	TestEqual(TEXT("Bar above 1 is clamped"), Need[1].Value, 1.0);
	TestEqual(TEXT("Bar below 0 is clamped"), Need[2].Value, 0.0);
	TestTrue(TEXT("Bar kind"), Need[0].Kind == EAdaptiveDebugValueKind::Bar);
	TestTrue(TEXT("Text kind"), Need[3].Kind == EAdaptiveDebugValueKind::Text);
	TestTrue(TEXT("Other sections stay empty"), Data.GetEntries(EAdaptiveDebugSection::Features).IsEmpty());

	Data.SetBar(EAdaptiveDebugSection::Need, TEXT("Need.Combat"), std::numeric_limits<double>::quiet_NaN());
	TestTrue(TEXT("NaN stays visible as NaN"), FMath::IsNaN(Need[0].Value));

	// updating keeps the row in place, also when the kind changes
	Data.SetNumber(EAdaptiveDebugSection::Need, TEXT("Need.Combat"), 3.5);
	TestEqual(TEXT("Update doesn't add a row"), Need.Num(), 4);
	TestEqual(TEXT("Updated row keeps its position"), Need[0].Name, FString(TEXT("Need.Combat")));
	TestTrue(TEXT("Kind changed"), Need[0].Kind == EAdaptiveDebugValueKind::Number);
	TestEqual(TEXT("Numbers are not clamped"), Need[0].Value, 3.5);

	Data.Remove(EAdaptiveDebugSection::Need, TEXT("Need.Threat"));
	TestEqual(TEXT("Removed one"), Need.Num(), 3);
	TestEqual(TEXT("Order kept after remove"), Need[1].Name, FString(TEXT("Need.Navigation")));

	Data.SetNumber(EAdaptiveDebugSection::Telemetry, TEXT("Signal"), 2.0);
	Data.ClearSection(EAdaptiveDebugSection::Need);
	TestTrue(TEXT("Section cleared"), Need.IsEmpty());
	TestEqual(TEXT("Other section kept"), Data.GetEntries(EAdaptiveDebugSection::Telemetry).Num(), 1);

	Data.ClearAll();
	TestTrue(TEXT("All cleared"), Data.GetEntries(EAdaptiveDebugSection::Telemetry).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveDebugParseSectionTest, "AdaptiveCompanion.Debug.ParseSection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FAdaptiveDebugParseSectionTest::RunTest(const FString& Parameters)
{
	EAdaptiveDebugSection Section = EAdaptiveDebugSection::Telemetry;

	TestTrue(TEXT("Name"), UAdaptiveDebugSubsystem::ParseSection(TEXT("Features"), Section) && Section == EAdaptiveDebugSection::Features);
	TestTrue(TEXT("Name ignores case"), UAdaptiveDebugSubsystem::ParseSection(TEXT("need"), Section) && Section == EAdaptiveDebugSection::Need);
	TestTrue(TEXT("Number"), UAdaptiveDebugSubsystem::ParseSection(TEXT("6"), Section) && Section == EAdaptiveDebugSection::Behavior);
	TestFalse(TEXT("Number 0"), UAdaptiveDebugSubsystem::ParseSection(TEXT("0"), Section));
	TestFalse(TEXT("Number 7"), UAdaptiveDebugSubsystem::ParseSection(TEXT("7"), Section));
	TestFalse(TEXT("Count is not a section"), UAdaptiveDebugSubsystem::ParseSection(TEXT("Count"), Section));
	TestFalse(TEXT("Unknown"), UAdaptiveDebugSubsystem::ParseSection(TEXT("Foo"), Section));

	TestEqual(TEXT("Title"), FString(UAdaptiveDebugSubsystem::GetSectionTitle(EAdaptiveDebugSection::Selection)), FString(TEXT("5 Intervention Selection")));

	return true;
}

#endif
