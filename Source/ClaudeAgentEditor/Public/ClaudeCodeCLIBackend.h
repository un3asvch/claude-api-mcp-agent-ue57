// Copyright Untry. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IClaudeBackend.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h"

class FRunnableThread;

/**
 * Backend that runs the official `claude` CLI as a subprocess.
 * Uses the user's Claude Pro/Max subscription — no API key needed.
 *
 * How it works:
 *   - Each turn spawns `claude -p "<prompt>" --output-format stream-json --verbose`
 *   - First turn: no flags. Second+ turns: `--resume <session_id>` to continue.
 *   - We capture session_id from the first stream-json `system` event.
 *   - stdout is read on a background thread; JSON lines are parsed and
 *     text deltas are forwarded to the UI on the game thread.
 *
 * Note: Claude Code's own tools (Read/Write/Bash/Edit) are used instead of
 * the plugin's UE-tools. We pre-write attached asset dumps to a scratch
 * directory and reference them in the prompt so Claude Code can Read() them.
 */
class FClaudeCodeCLIBackend : public IClaudeBackend
{
	friend class FClaudeCLIReaderRunnable;

public:
	FClaudeCodeCLIBackend();
	virtual ~FClaudeCodeCLIBackend();

	// IClaudeBackend
	virtual void SendUserMessage(const FString& UserText, const TArray<FString>& AttachedAssetPaths) override;
	virtual void Cancel() override;
	virtual void ClearConversation() override;
	virtual bool IsBusy() const override { return bIsBusy; }
	virtual FString GetBackendName() const override { return TEXT("Claude Code CLI"); }

	// CLI has no in-memory message array — its own session store on disk is the source of truth.
	virtual void LoadMessages(const TArray<FClaudeMessage>& In) override { TrackedMessages = In; }
	virtual TArray<FClaudeMessage> GetMessages() const override { return TrackedMessages; }
	virtual FString GetCLISessionId() const override { return CachedSessionId; }
	virtual void SetCLISessionId(const FString& SessionId) override { CachedSessionId = SessionId; }

	/** Called from reader thread (via AsyncTask game-thread hop) with a chunk of stdout bytes. */
	void AppendStdoutChunk(const FString& Chunk);
	/** Called when the reader thread detects process exit. */
	void FlushRemainingBuffer();

private:
	void RunReaderLoop();
	void ParseStreamJsonLine(const FString& Line);
	FString WriteAttachmentsToScratch(const TArray<FString>& AssetPaths);
	FString ResolveClaudeBinaryPath() const;
	FString GetScratchDir() const;
	void EmitError(const FString& Err);

	void* ProcHandle_Read = nullptr;   // void* avoids leaking FProcHandle into header
	void* StdOutReadPipe = nullptr;
	void* StdOutWritePipe = nullptr;
	void* StdInReadPipe = nullptr;
	void* StdInWritePipe = nullptr;

	FString PartialLineBuffer;
	FString CachedSessionId;
	FThreadSafeBool bIsBusy;
	FThreadSafeBool bCancelled;
	TUniquePtr<class FClaudeCLIReaderRunnable> Reader;
	FRunnableThread* ReaderThread = nullptr;

	/**
	 * Mirror of the conversation so the persistence layer can save a readable
	 * transcript. The CLI itself stores the authoritative session on disk; this
	 * is purely for our .json export / history list UI.
	 */
	TArray<FClaudeMessage> TrackedMessages;

public:
	/** Append a message chunk to TrackedMessages (or append to existing assistant block). */
	void AppendTrackedAssistantText(const FString& Delta);
	void PushTrackedUserMessage(const FString& Text);
	void FinalizeTrackedAssistantMessage();
};
