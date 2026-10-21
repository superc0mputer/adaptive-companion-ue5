#include "Logging/AdaptiveSessionLogWriter.h"
#include "AdaptiveCompanionFramework.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

FAdaptiveSessionLogWriter::~FAdaptiveSessionLogWriter()
{
	Close();
}

bool FAdaptiveSessionLogWriter::Open(const FString& InFilePath)
{
	Close();

	IFileManager& FileManager = IFileManager::Get();
	FileManager.MakeDirectory(*FPaths::GetPath(InFilePath), true);

	FArchive* Writer = FileManager.CreateFileWriter(*InFilePath, FILEWRITE_AllowRead);
	if (!Writer)
	{
		UE_LOG(LogAdaptiveCompanionFramework, Error, TEXT("Could not open session log '%s'."), *InFilePath);
		return false;
	}

	File = MakeShareable(Writer);
	FilePath = InFilePath;
	return true;
}

void FAdaptiveSessionLogWriter::AppendLine(FStringView Line)
{
	if (!File.IsValid())
	{
		return;
	}

	const FTCHARToUTF8 Utf8(Line.GetData(), Line.Len());
	Pending.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	Pending.Add('\n');
}

void FAdaptiveSessionLogWriter::Flush()
{
	if (!File.IsValid() || Pending.IsEmpty())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FAdaptiveSessionLogWriter::Flush);

	const int32 Capacity = Pending.Max();
	auto WriteTask = [File = File, Data = MoveTemp(Pending)]() mutable
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FAdaptiveSessionLogWriter::Write);
		File->Serialize(Data.GetData(), Data.Num());
		File->Flush();
	};

	// keep the buffer's capacity so steady logging doesn't reallocate after every flush
	Pending.Reserve(Capacity);

	LastWrite = LastWrite.IsValid()
		? UE::Tasks::Launch(UE_SOURCE_LOCATION, MoveTemp(WriteTask), UE::Tasks::Prerequisites(LastWrite), UE::Tasks::ETaskPriority::BackgroundNormal)
		: UE::Tasks::Launch(UE_SOURCE_LOCATION, MoveTemp(WriteTask), UE::Tasks::ETaskPriority::BackgroundNormal);
}

void FAdaptiveSessionLogWriter::Close()
{
	if (!File.IsValid())
	{
		return;
	}

	Flush();
	if (LastWrite.IsValid())
	{
		LastWrite.Wait();
		LastWrite = UE::Tasks::FTask();
	}

	File->Close();
	File.Reset();
}
