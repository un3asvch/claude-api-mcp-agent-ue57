// Copyright Untry. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ClaudeAgentTypes.h"

/** Metadata about a saved conversation, for the sidebar list. */
struct FClaudeConversationSummary
{
	FString FilePath;
	FString Title;          // first user line, truncated
	FDateTime Created;
	FDateTime LastModified;
	int32 MessageCount = 0;
	FString BackendName;    // "Anthropic API" / "Claude Code CLI"
	FString CLISessionId;   // for CLI resume
};

/**
 * Persists conversations to JSON files under Saved/ClaudeAgent/Conversations/.
 * Each conversation = one file: {timestamp}_{slug}.json
 *
 * File format (JSON):
 * {
 *   "version": 1,
 *   "title": "...",
 *   "created": "2026-04-16T10:00:00Z",
 *   "last_modified": "...",
 *   "backend": "Anthropic API",
 *   "cli_session_id": "",
 *   "messages": [
 *     { "role": "user", "blocks": [ { "type": "text", "text": "..." } ] },
 *     { "role": "assistant", "blocks": [
 *         { "type": "thinking", "text": "..." },
 *         { "type": "text", "text": "..." },
 *         { "type": "tool_use", "id": "...", "name": "...", "input": {...} }
 *       ]
 *     },
 *     ...
 *   ]
 * }
 */
class FClaudeConversationHistory
{
public:
	static FString GetConversationsDir();

	/** List all saved conversations, newest first. */
	static TArray<FClaudeConversationSummary> ListConversations();

	/** Save or update a conversation to disk. If FilePath is empty, a new file is created and its path returned. */
	static FString SaveConversation(
		const FString& ExistingFilePath,
		const TArray<FClaudeMessage>& Messages,
		const FString& BackendName,
		const FString& CLISessionId);

	/** Load a conversation from disk into messages + metadata. */
	static bool LoadConversation(
		const FString& FilePath,
		TArray<FClaudeMessage>& OutMessages,
		FString& OutCLISessionId,
		FString& OutBackendName);

	/** Delete a conversation file. */
	static bool DeleteConversation(const FString& FilePath);

private:
	static FString MakeSlug(const FString& Text);
	static FString DeriveTitle(const TArray<FClaudeMessage>& Messages);
};
