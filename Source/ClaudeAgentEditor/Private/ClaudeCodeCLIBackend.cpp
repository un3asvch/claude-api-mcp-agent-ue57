// Copyright Untry. All Rights Reserved.

#include "ClaudeCodeCLIBackend.h"
#include "ClaudeAgentEditor.h"
#include "ClaudeAgentSettings.h"
#include "ClaudeContextProvider.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/Runnable.h"

// -----------------------------------------------------------------------------
// Background thread that drains stdout from the CLI process
// -----------------------------------------------------------------------------

class FClaudeCLIReaderRunnable : public FRunnable
{
public:
	FClaudeCLIReaderRunnable(FClaudeCodeCLIBackend* InOwner, void* InProcHandle, void* InReadPipe)
		: Owner(InOwner), ProcHandlePtr(InProcHandle), ReadPipe(InReadPipe) {}

	virtual uint32 Run() override
	{
		FProcHandle Handle = *reinterpret_cast<FProcHandle*>(&ProcHandlePtr);

		while (!bStopRequested)
		{
			FString Chunk = FPlatformProcess::ReadPipe(ReadPipe);
			if (!Chunk.IsEmpty())
			{
				const FString ChunkCopy = Chunk;
				FClaudeCodeCLIBackend* OwnerCopy = Owner;
				AsyncTask(ENamedThreads::GameThread, [OwnerCopy, ChunkCopy]()
				{
					OwnerCopy->AppendStdoutChunk(ChunkCopy);
				});
			}
			else
			{
				FPlatformProcess::Sleep(0.05f);
				if (!FPlatformProcess::IsProcRunning(Handle))
				{
					// Drain final bytes
					FString Final = FPlatformProcess::ReadPipe(ReadPipe);
					if (!Final.IsEmpty())
					{
						FClaudeCodeCLIBackend* OwnerCopy = Owner;
						const FString FinalCopy = Final;
						AsyncTask(ENamedThreads::GameThread, [OwnerCopy, FinalCopy]()
						{
							OwnerCopy->AppendStdoutChunk(FinalCopy);
							OwnerCopy->FlushRemainingBuffer();
						});
					}
					break;
				}
			}
		}

		// Reader exits when CLI process dies. By this point the JSON parser
		// has already fired OnTurnComplete on the "result" message — this is
		// just a safety net for the case where the process dies WITHOUT sending
		// a final result line (e.g. it crashed or was killed externally).
		FClaudeCodeCLIBackend* OwnerCopy = Owner;
		AsyncTask(ENamedThreads::GameThread, [OwnerCopy]()
		{
			if (OwnerCopy->bIsBusy)
			{
				// Process died before sending "result" — recover so UI doesn't
				// stay stuck.
				OwnerCopy->bIsBusy = false;
				OwnerCopy->FinalizeTrackedAssistantMessage();
				OwnerCopy->OnTurnComplete.Broadcast();
			}
		});
		return 0;
	}

	virtual void Stop() override { bStopRequested = true; }

private:
	FClaudeCodeCLIBackend* Owner = nullptr;
	void* ProcHandlePtr = nullptr;
	void* ReadPipe = nullptr;
	FThreadSafeBool bStopRequested;
};

// -----------------------------------------------------------------------------
// Backend
// -----------------------------------------------------------------------------

FClaudeCodeCLIBackend::FClaudeCodeCLIBackend() {}

FClaudeCodeCLIBackend::~FClaudeCodeCLIBackend()
{
	Cancel();
}

void FClaudeCodeCLIBackend::AppendStdoutChunk(const FString& Chunk)
{
	PartialLineBuffer += Chunk;

	int32 NewlineIdx;
	while (PartialLineBuffer.FindChar(TEXT('\n'), NewlineIdx))
	{
		FString Line = PartialLineBuffer.Left(NewlineIdx);
		PartialLineBuffer = PartialLineBuffer.Mid(NewlineIdx + 1);
		Line.TrimStartAndEndInline();
		if (!Line.IsEmpty())
		{
			ParseStreamJsonLine(Line);
		}
	}
}

void FClaudeCodeCLIBackend::FlushRemainingBuffer()
{
	if (!PartialLineBuffer.IsEmpty())
	{
		FString Tail = PartialLineBuffer;
		PartialLineBuffer.Empty();
		Tail.TrimStartAndEndInline();
		if (!Tail.IsEmpty())
		{
			ParseStreamJsonLine(Tail);
		}
	}
}

void FClaudeCodeCLIBackend::ClearConversation()
{
	CachedSessionId.Empty();
	TrackedMessages.Empty();
}

void FClaudeCodeCLIBackend::Cancel()
{
	bCancelled = true;

	if (Reader.IsValid()) Reader->Stop();
	if (ReaderThread)
	{
		ReaderThread->WaitForCompletion();
		delete ReaderThread;
		ReaderThread = nullptr;
	}
	Reader.Reset();

	if (ProcHandle_Read)
	{
		FProcHandle Handle = *reinterpret_cast<FProcHandle*>(&ProcHandle_Read);
		if (FPlatformProcess::IsProcRunning(Handle))
		{
			FPlatformProcess::TerminateProc(Handle, true);
		}
		FPlatformProcess::CloseProc(Handle);
		ProcHandle_Read = nullptr;
	}

	if (StdOutReadPipe || StdOutWritePipe)
	{
		FPlatformProcess::ClosePipe(StdOutReadPipe, StdOutWritePipe);
		StdOutReadPipe = nullptr;
		StdOutWritePipe = nullptr;
	}
	if (StdInReadPipe || StdInWritePipe)
	{
		FPlatformProcess::ClosePipe(StdInReadPipe, StdInWritePipe);
		StdInReadPipe = nullptr;
		StdInWritePipe = nullptr;
	}

	bIsBusy = false;
}

FString FClaudeCodeCLIBackend::GetScratchDir() const
{
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("ClaudeAgent") / TEXT("Scratch");
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.DirectoryExists(*Dir)) PF.CreateDirectoryTree(*Dir);
	return Dir;
}

FString FClaudeCodeCLIBackend::ResolveClaudeBinaryPath() const
{
	const UClaudeAgentSettings* S = UClaudeAgentSettings::Get();
	if (!S->ClaudeCodeBinaryPath.IsEmpty()) return S->ClaudeCodeBinaryPath;

#if PLATFORM_WINDOWS
	// On Windows, claude is usually a .cmd shim from npm-global
	return TEXT("claude.cmd");
#else
	return TEXT("claude");
#endif
}

FString FClaudeCodeCLIBackend::WriteAttachmentsToScratch(const TArray<FString>& AssetPaths)
{
	if (AssetPaths.Num() == 0) return TEXT("");

	const FString Dir = GetScratchDir();
	const FString Stamp = FGuid::NewGuid().ToString(EGuidFormats::DigitsLower).Left(8);

	FString ManifestText;
	ManifestText += FString::Printf(TEXT("Attached UE assets dumped at %s\n\n"), *FDateTime::Now().ToIso8601());

	for (int32 i = 0; i < AssetPaths.Num(); ++i)
	{
		const FString& Path = AssetPaths[i];
		const FString Dump = FClaudeContextProvider::GetObjectProperties(Path, 1);

		const FString FileName = FString::Printf(TEXT("attachment_%s_%d.json"), *Stamp, i);
		const FString FullPath = Dir / FileName;
		FFileHelper::SaveStringToFile(Dump, *FullPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

		ManifestText += FString::Printf(TEXT("- %s -> %s\n"), *Path, *FullPath);
	}
	return ManifestText;
}

void FClaudeCodeCLIBackend::SendUserMessage(const FString& UserText, const TArray<FString>& AttachedAssetPaths)
{
	if (bIsBusy)
	{
		EmitError(TEXT("Backend is busy."));
		return;
	}

	bCancelled = false;
	bIsBusy = true;
	PartialLineBuffer.Empty();

	// Record the user message now so it's captured even if the CLI process
	// dies or UE crashes before we get a response.
	PushTrackedUserMessage(UserText);

	const UClaudeAgentSettings* S = UClaudeAgentSettings::Get();

	// Build the user prompt with attachment manifest
	FString FullPrompt;
	if (!S->SystemPrompt.IsEmpty() && CachedSessionId.IsEmpty())
	{
		// Claude Code doesn't take a separate system prompt argument easily; prepend it on first turn.
		FullPrompt += TEXT("[System context — apply this for the whole session]\n");
		FullPrompt += S->SystemPrompt;
		FullPrompt += TEXT("\n\n");
	}
	if (AttachedAssetPaths.Num() > 0)
	{
		FullPrompt += TEXT("[Attached assets — read these JSON files with your Read tool if needed]\n");
		FullPrompt += WriteAttachmentsToScratch(AttachedAssetPaths);
		FullPrompt += TEXT("\n");
	}
	FullPrompt += UserText;

	// Build CLI args
	const FString BinaryPath = ResolveClaudeBinaryPath();

	// We pass the prompt via -p so we don't have to feed it through stdin,
	// which works around interactive-TTY assumptions. CLI quoting is handled
	// by the platform layer via FString args.
	FString Args;
	Args += TEXT(" -p");
	Args += TEXT(" --output-format stream-json --verbose");

	// Permission mode — without a TTY, CLI default mode will HANG on permission prompts.
	switch (S->CLIPermissionMode)
	{
		case EClaudeCodePermissionMode::AcceptEdits:
			Args += TEXT(" --permission-mode acceptEdits");
			break;
		case EClaudeCodePermissionMode::BypassPermissions:
			Args += TEXT(" --dangerously-skip-permissions");
			break;
		case EClaudeCodePermissionMode::Plan:
			Args += TEXT(" --permission-mode plan");
			break;
		case EClaudeCodePermissionMode::Default:
		default:
			// No flag — CLI will hang on first prompt. User has been warned in settings.
			break;
	}

	if (!S->ExtraCLIArgs.IsEmpty())
	{
		Args += TEXT(" ");
		Args += S->ExtraCLIArgs;
	}

	if (!CachedSessionId.IsEmpty())
	{
		Args += FString::Printf(TEXT(" --resume %s"), *CachedSessionId);
	}

	// Quote-escape: replace embedded quotes
	FString EscapedPrompt = FullPrompt.Replace(TEXT("\""), TEXT("\\\""));
	Args += FString::Printf(TEXT(" \"%s\""), *EscapedPrompt);

	UE_LOG(LogClaudeAgent, Log, TEXT("Spawning Claude Code: %s %s"), *BinaryPath, *Args.Left(200));

	// Working directory = project dir, so Claude Code can read/edit project files
	const FString WorkingDir = FPaths::ProjectDir();

	// Create pipes
	FPlatformProcess::CreatePipe(StdOutReadPipe, StdOutWritePipe);

	uint32 ProcessID = 0;
	FProcHandle Handle = FPlatformProcess::CreateProc(
		*BinaryPath,
		*Args,
		/*bLaunchDetached*/ false,
		/*bLaunchHidden*/   true,
		/*bLaunchReallyHidden*/ true,
		&ProcessID,
		/*PriorityModifier*/ 0,
		*WorkingDir,
		StdOutWritePipe,
		StdInReadPipe);

	if (!Handle.IsValid())
	{
		EmitError(FString::Printf(TEXT(
			"Не удалось запустить Claude Code CLI: '%s'\n"
			"Проверь что Claude Code установлен (npm i -g @anthropic-ai/claude-code) "
			"и что бинарник доступен в PATH, либо укажи полный путь в Project Settings → Plugins → Claude Agent → Claude Code Binary Path."
		), *BinaryPath));
		Cancel();
		return;
	}

	// Stash the handle (we use void* to keep FProcHandle out of the header)
	static_assert(sizeof(FProcHandle) <= sizeof(void*) * 2, "FProcHandle larger than expected");
	FMemory::Memcpy(&ProcHandle_Read, &Handle, sizeof(FProcHandle));

	OnStatus.Broadcast(FString::Printf(TEXT("Запущен Claude Code (pid %u)..."), ProcessID));

	// Spawn reader thread
	Reader = MakeUnique<FClaudeCLIReaderRunnable>(this, ProcHandle_Read, StdOutReadPipe);
	ReaderThread = FRunnableThread::Create(Reader.Get(), TEXT("ClaudeCLIReader"));
}

void FClaudeCodeCLIBackend::EmitError(const FString& Err)
{
	OnError.Broadcast(Err);
	bIsBusy = false;
	OnTurnComplete.Broadcast();
}

void FClaudeCodeCLIBackend::ParseStreamJsonLine(const FString& Line)
{
	// stream-json from `claude -p --output-format stream-json` emits one JSON object per line.
	// Schema (as of late 2025):
	//   { "type": "system",  "subtype": "init", "session_id": "...", ... }
	//   { "type": "assistant", "message": { "content": [ { "type": "text", "text": "..." }, ... ] } }
	//   { "type": "user", "message": { "content": [ { "type": "tool_result", ... } ] } }
	//   { "type": "result", "subtype": "success", "result": "<final text>", "session_id": "...", "is_error": false, ... }
	//
	// We surface text deltas via OnStreamText, capture session_id, and surface tool calls in status.

	TSharedPtr<FJsonObject> Obj;
	auto JsonReader = TJsonReaderFactory<>::Create(Line);
	if (!FJsonSerializer::Deserialize(JsonReader, Obj) || !Obj.IsValid())
	{
		// Not JSON — could be a stderr leak or banner; show as status
		OnStatus.Broadcast(Line);
		return;
	}

	FString Type;
	Obj->TryGetStringField(TEXT("type"), Type);

	if (Type == TEXT("system"))
	{
		FString Sub;
		Obj->TryGetStringField(TEXT("subtype"), Sub);
		if (Sub == TEXT("init"))
		{
			FString SessionId;
			if (Obj->TryGetStringField(TEXT("session_id"), SessionId) && !SessionId.IsEmpty())
			{
				CachedSessionId = SessionId;
				OnStatus.Broadcast(FString::Printf(TEXT("Session: %s"), *SessionId.Left(8)));
			}
		}
		return;
	}

	if (Type == TEXT("assistant"))
	{
		const TSharedPtr<FJsonObject>* MessageObj;
		if (!Obj->TryGetObjectField(TEXT("message"), MessageObj)) return;

		const TArray<TSharedPtr<FJsonValue>>* ContentArr;
		if (!(*MessageObj)->TryGetArrayField(TEXT("content"), ContentArr)) return;

		for (const TSharedPtr<FJsonValue>& V : *ContentArr)
		{
			auto BlockObj = V->AsObject();
			if (!BlockObj.IsValid()) continue;

			FString BType;
			BlockObj->TryGetStringField(TEXT("type"), BType);

			if (BType == TEXT("text"))
			{
				FString Text;
				if (BlockObj->TryGetStringField(TEXT("text"), Text) && !Text.IsEmpty())
				{
					OnStreamText.Broadcast(Text);
					AppendTrackedAssistantText(Text);
				}
			}
			else if (BType == TEXT("tool_use"))
			{
				FString ToolName;
				BlockObj->TryGetStringField(TEXT("name"), ToolName);

				FString InputStr;
				const TSharedPtr<FJsonObject>* InputObj;
				if (BlockObj->TryGetObjectField(TEXT("input"), InputObj))
				{
					auto W = TJsonWriterFactory<>::Create(&InputStr);
					FJsonSerializer::Serialize(InputObj->ToSharedRef(), W);
				}
				OnToolCall.Broadcast(ToolName, InputStr);
			}
			else if (BType == TEXT("thinking"))
			{
				FString Thought;
				if (BlockObj->TryGetStringField(TEXT("thinking"), Thought) && !Thought.IsEmpty())
				{
					OnThinking.Broadcast(Thought);
				}
			}
		}
		return;
	}

	if (Type == TEXT("result"))
	{
		bool bIsError = false;
		Obj->TryGetBoolField(TEXT("is_error"), bIsError);
		if (bIsError)
		{
			FString ErrMsg;
			Obj->TryGetStringField(TEXT("result"), ErrMsg);
			OnError.Broadcast(ErrMsg.IsEmpty() ? TEXT("Claude Code returned an error result.") : ErrMsg);
		}

		// Capture session id from result event too
		FString SessionId;
		if (Obj->TryGetStringField(TEXT("session_id"), SessionId) && !SessionId.IsEmpty())
		{
			CachedSessionId = SessionId;
		}

		bIsBusy = false;

		// Fire OnTurnComplete here, when we know the model finished its turn,
		// instead of waiting for the CLI process to exit. The CLI process may
		// stay alive between turns (multi-turn session), so relying on process
		// exit would leave the throbber spinning forever.
		FinalizeTrackedAssistantMessage();
		OnTurnComplete.Broadcast();
		return;
	}

	// "user" type messages contain tool_result blocks
	if (Type == TEXT("user"))
	{
		const TSharedPtr<FJsonObject>* MessageObj;
		if (!Obj->TryGetObjectField(TEXT("message"), MessageObj)) return;

		const TArray<TSharedPtr<FJsonValue>>* ContentArr;
		if (!(*MessageObj)->TryGetArrayField(TEXT("content"), ContentArr)) return;

		for (const TSharedPtr<FJsonValue>& V : *ContentArr)
		{
			auto BlockObj = V->AsObject();
			if (!BlockObj.IsValid()) continue;

			FString BType;
			BlockObj->TryGetStringField(TEXT("type"), BType);
			if (BType == TEXT("tool_result"))
			{
				FString ResultContent;
				// content can be string or array of {type:text,text:...}
				if (!BlockObj->TryGetStringField(TEXT("content"), ResultContent))
				{
					const TArray<TSharedPtr<FJsonValue>>* Inner;
					if (BlockObj->TryGetArrayField(TEXT("content"), Inner))
					{
						for (const TSharedPtr<FJsonValue>& IV : *Inner)
						{
							auto IO = IV->AsObject();
							if (IO.IsValid())
							{
								FString T;
								IO->TryGetStringField(TEXT("text"), T);
								ResultContent += T;
							}
						}
					}
				}
				bool bErr = false;
				BlockObj->TryGetBoolField(TEXT("is_error"), bErr);
				OnToolResult.Broadcast(bErr ? TEXT("error") : TEXT("ok"), ResultContent);
			}
		}
		return;
	}
}

// -----------------------------------------------------------------------------
// Tracked message bookkeeping — so .json export has real transcripts, not just
// a session_id handle. These must only be called on the game thread.
// -----------------------------------------------------------------------------

void FClaudeCodeCLIBackend::PushTrackedUserMessage(const FString& Text)
{
	FClaudeMessage Msg;
	Msg.Role = EClaudeRole::User;
	FClaudeContentBlock Block;
	Block.Type = EClaudeContentBlockType::Text;
	Block.Text = Text;
	Msg.Blocks.Add(Block);
	TrackedMessages.Add(Msg);
}

void FClaudeCodeCLIBackend::AppendTrackedAssistantText(const FString& Delta)
{
	// If the last message is an open assistant text block, append to it.
	// Otherwise start a fresh assistant message + text block.
	const int32 LastIdx = TrackedMessages.Num() - 1;
	if (LastIdx >= 0 &&
		TrackedMessages[LastIdx].Role == EClaudeRole::Assistant &&
		TrackedMessages[LastIdx].Blocks.Num() > 0 &&
		TrackedMessages[LastIdx].Blocks.Last().Type == EClaudeContentBlockType::Text)
	{
		TrackedMessages[LastIdx].Blocks.Last().Text += Delta;
		return;
	}

	FClaudeMessage Msg;
	Msg.Role = EClaudeRole::Assistant;
	FClaudeContentBlock Block;
	Block.Type = EClaudeContentBlockType::Text;
	Block.Text = Delta;
	Msg.Blocks.Add(Block);
	TrackedMessages.Add(Msg);
}

void FClaudeCodeCLIBackend::FinalizeTrackedAssistantMessage()
{
	// No-op right now — structure is already correct by the time the turn
	// completes. Kept as a hook so we can add finalization logic later
	// (e.g. flush a partial thinking block, trim trailing whitespace).
}
