#include "Logging/AdaptiveLogSubsystem.h"
#include "Logging/AdaptiveSessionLogWriter.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveLogRecordLineTest, "AdaptiveCompanion.Logging.RecordLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FAdaptiveLogRecordLineTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("value"), 0.5);
	Payload->SetNumberField(TEXT("nan"), std::numeric_limits<double>::quiet_NaN());
	Payload->SetStringField(TEXT("text"), TEXT("quote \" and\nnewline"));

	const FString Line = UAdaptiveLogSubsystem::MakeRecordLine(7, 1.25, TOptional<double>(), FDateTime(2026, 10, 20, 13, 30, 12, 345),
		TEXT("session"), TEXT("P01"), TEXT("signal"), Payload);

	TestFalse(TEXT("Record is a single line"), Line.Contains(TEXT("\n")));

	TSharedPtr<FJsonObject> Record;
	TestTrue(TEXT("Record is valid JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), Record) && Record.IsValid());
	if (!Record.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("seq"), Record->GetIntegerField(TEXT("seq")), 7);
	TestEqual(TEXT("t"), Record->GetNumberField(TEXT("t")), 1.25);
	TestTrue(TEXT("game_time is null without a world"), Record->GetField(TEXT("game_time"), EJson::Null).IsValid());
	TestEqual(TEXT("wall"), Record->GetStringField(TEXT("wall")), TEXT("2026-10-20T13:30:12.345Z"));
	TestEqual(TEXT("session_id"), Record->GetStringField(TEXT("session_id")), TEXT("session"));
	TestEqual(TEXT("participant_id"), Record->GetStringField(TEXT("participant_id")), TEXT("P01"));
	TestEqual(TEXT("type"), Record->GetStringField(TEXT("type")), TEXT("signal"));

	const TSharedPtr<FJsonObject> ReadPayload = Record->GetObjectField(TEXT("payload"));
	TestEqual(TEXT("payload value"), ReadPayload->GetNumberField(TEXT("value")), 0.5);
	TestTrue(TEXT("NaN is written as null"), ReadPayload->GetField(TEXT("nan"), EJson::Null).IsValid());
	TestEqual(TEXT("payload text"), ReadPayload->GetStringField(TEXT("text")), TEXT("quote \" and\nnewline"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveLogWriterTest, "AdaptiveCompanion.Logging.Writer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FAdaptiveLogWriterTest::RunTest(const FString& Parameters)
{
	const FString FilePath = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("AdaptiveLogs"), TEXT("WriterTest.jsonl"));
	constexpr int32 LineCount = 5000;

	{
		FAdaptiveSessionLogWriter Writer;
		TestTrue(TEXT("Open"), Writer.Open(FilePath));

		for (int32 Index = 0; Index < LineCount; ++Index)
		{
			Writer.AppendLine(FString::Printf(TEXT("{\"i\":%d,\"s\":\"äöü\"}"), Index));

			// many small flushes, so several write tasks are in flight at once
			if (Index % 100 == 0)
			{
				Writer.Flush();
			}
		}

		Writer.Close();
	}

	TArray<FString> Lines;
	TestTrue(TEXT("Read back"), FFileHelper::LoadFileToStringArray(Lines, *FilePath));
	TestEqual(TEXT("Line count"), Lines.Num(), LineCount);

	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FString Expected = FString::Printf(TEXT("{\"i\":%d,\"s\":\"äöü\"}"), Index);
		if (Lines[Index] != Expected)
		{
			AddError(FString::Printf(TEXT("Line %d is '%s', expected '%s'"), Index, *Lines[Index], *Expected));
			break;
		}
	}

	IFileManager::Get().Delete(*FilePath);
	return true;
}

#endif
