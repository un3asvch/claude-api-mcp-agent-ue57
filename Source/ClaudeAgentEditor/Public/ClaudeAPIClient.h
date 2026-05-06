// Copyright Untry. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ClaudeAgentTypes.h"

class FClaudeToolRegistry;
class IHttpRequest;
class IHttpResponse;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnClaudeStreamText, const FString& /*DeltaText*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnClaudeThinkingText, const FString& /*ThinkingDelta*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnClaudeMessageComplete, const FClaudeMessage& /*AssistantMessage*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnClaudeError, const FString& /*Error*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnClaudeToolUse, const FClaudeContentBlock& /*ToolUseBlock*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnClaudeToolResult, const FString& /*ToolUseId*/, const FClaudeToolResult& /*Result*/);
DECLARE_MULTICAST_DELEGATE(FOnClaudeAgentTurnComplete);

/**
 * The agent. Owns the conversation state and runs the
 * tool-use loop: send -> if response includes tool_use, execute tools, send back, repeat.
 */
class FClaudeAPIClient : public TSharedFromThis<FClaudeAPIClient>
{
public:
	FClaudeAPIClient();

	/** Send a fresh user message; agent loop runs until model stops requesting tools. */
	void SendUserMessage(const FString& UserText, const TArray<FString>& AttachedAssetPaths = {});

	/** Cancel any in-flight request and stop the loop. */
	void Cancel();

	void ClearConversation();
	void LoadConversation(const TArray<FClaudeMessage>& InMessages) { Messages = InMessages; }
	const TArray<FClaudeMessage>& GetConversation() const { return Messages; }
	bool IsBusy() const { return bIsBusy; }

	// Delegates
	FOnClaudeStreamText        OnStreamText;          // streaming chunk arrived
	FOnClaudeThinkingText      OnThinkingText;        // extended thinking delta
	FOnClaudeMessageComplete   OnMessageComplete;     // full assistant message done
	FOnClaudeError             OnError;
	FOnClaudeToolUse           OnToolUse;             // model requested a tool
	FOnClaudeToolResult        OnToolResult;          // we ran it
	FOnClaudeAgentTurnComplete OnAgentTurnComplete;   // entire user-turn (incl. tool loops) done

	TSharedPtr<FClaudeToolRegistry> ToolRegistry;

private:
	void DispatchRequest();
	void HandleResponse(TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> Request, TSharedPtr<class IHttpResponse, ESPMode::ThreadSafe> Response, bool bSuccess);
	void HandleStreamChunk(const FString& Chunk);

	TSharedRef<class FJsonObject> BuildRequestBody() const;
	void AppendAssistantMessageFromResponse(const TSharedPtr<class FJsonObject>& ResponseObj);
	void RunToolUseLoop();

	TArray<FClaudeMessage> Messages;
	int32 IterationCount = 0;
	bool bIsBusy = false;
	bool bCancelled = false;

	FString StreamBuffer;
	FClaudeMessage StreamingAssistantMessage;
	FString CurrentStreamText;

	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> CurrentRequest;
};
