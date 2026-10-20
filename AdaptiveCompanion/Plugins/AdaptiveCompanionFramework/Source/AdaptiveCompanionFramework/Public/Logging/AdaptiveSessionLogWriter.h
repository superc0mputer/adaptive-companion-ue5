#pragma once

#include "CoreMinimal.h"
#include "Tasks/Task.h"

/**
 *  Appends lines to a single file without blocking the game thread.
 *  Lines are collected in memory and written by a background task on Flush(). Writes run strictly
 *  in order, so the file always contains complete lines in the order they were appended.
 *  Not thread-safe: call everything from the same thread (the game thread).
 */
class ADAPTIVECOMPANIONFRAMEWORK_API FAdaptiveSessionLogWriter
{
public:

	FAdaptiveSessionLogWriter() = default;
	~FAdaptiveSessionLogWriter();

	FAdaptiveSessionLogWriter(const FAdaptiveSessionLogWriter&) = delete;
	FAdaptiveSessionLogWriter& operator=(const FAdaptiveSessionLogWriter&) = delete;

	/** Creates (or replaces) the file, including missing directories. Returns false if it can't be opened */
	bool Open(const FString& InFilePath);

	/** Adds one line (without line break) to the pending buffer. Nothing touches the disk until Flush() */
	void AppendLine(FStringView Line);

	/** Hands the pending buffer to a background task that writes it to the file */
	void Flush();

	/** Flushes, waits until everything is on disk and closes the file */
	void Close();

	bool IsOpen() const { return File.IsValid(); }
	const FString& GetFilePath() const { return FilePath; }

	/** Size of the buffer that hasn't been handed to the writer task yet */
	int32 GetPendingBytes() const { return Pending.Num(); }

private:

	FString FilePath;

	/** UTF-8 encoded lines waiting for the next Flush() */
	TArray<uint8> Pending;

	/** Only accessed by write tasks, or on the calling thread after LastWrite has finished */
	TSharedPtr<FArchive> File;

	/** The most recent write task; the next one waits for it so writes stay in order */
	UE::Tasks::FTask LastWrite;
};
