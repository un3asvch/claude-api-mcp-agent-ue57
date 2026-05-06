// Copyright Untry. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ClaudeAgentTypes.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnBackendStreamText, const FString& /*Delta*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBackendThinking, const FString& /*ThinkingDelta*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnBackendToolCall, const FString& /*ToolName*/, const FString& /*InputJson*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnBackendToolResult, const FString& /*ToolName*/, const FString& /*ResultBody*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBackendError, const FString& /*Error*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBackendStatus, const FString& /*StatusLine*/);
DECLARE_MULTICAST_DELEGATE(FOnBackendTurnComplete);

/**
 * Common interface for any source of Claude responses.
 * Two implementations:
 *   - FAnthropicAPIBackend  — direct HTTPS to api.anthropic.com (pay-per-token)
 *   - FClaudeCodeCLIBackend — spawns the `claude` CLI as a subprocess (uses Pro/Max subscription)
 */
class IClaudeBackend
{
public:
	virtual ~IClaudeBackend() = default;

	virtual void SendUserMessage(const FString& UserText, const TArray<FString>& AttachedAssetPaths) = 0;
	virtual void Cancel() = 0;
	virtual void ClearConversation() = 0;
	virtual bool IsBusy() const = 0;
	virtual FString GetBackendName() const = 0;

	/** Replace the in-memory conversation with a loaded one. No-op for CLI (session_id is restored separately). */
	virtual void LoadMessages(const TArray<FClaudeMessage>& Messages) = 0;
	/** Snapshot current conversation for saving. For CLI backend, typically empty — we rely on CLI's own session store. */
	virtual TArray<FClaudeMessage> GetMessages() const = 0;
	/** CLI session ID (empty string for API backend). */
	virtual FString GetCLISessionId() const { return TEXT(""); }
	/** Restore CLI session from disk (no-op for API backend). */
	virtual void SetCLISessionId(const FString& /*SessionId*/) {}

	FOnBackendStreamText    OnStreamText;
	FOnBackendThinking      OnThinking;      // extended-thinking deltas (API only)
	FOnBackendToolCall      OnToolCall;      // model requested a tool
	FOnBackendToolResult    OnToolResult;    // tool finished
	FOnBackendError         OnError;
	FOnBackendStatus        OnStatus;
	FOnBackendTurnComplete  OnTurnComplete;
};
