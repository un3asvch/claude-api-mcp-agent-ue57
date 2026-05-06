// Copyright Untry. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ClaudeAgentTypes.h"

class FJsonObject;

/**
 * Holds all tools the agent can call. Each tool has a JSON schema
 * (sent to the API) and a handler (invoked when Claude requests it).
 */
class FClaudeToolRegistry
{
public:
	FClaudeToolRegistry();

	void RegisterTool(const FClaudeToolDefinition& Tool);
	const FClaudeToolDefinition* FindTool(const FString& Name) const;

	/** Build the JSON array of tool schemas for the API request body. */
	TArray<TSharedPtr<FJsonValue>> BuildToolsJson() const;

	/** Execute a tool by name. */
	FClaudeToolResult Execute(const FString& Name, const TSharedPtr<FJsonObject>& Input) const;

	const TMap<FString, FClaudeToolDefinition>& GetAllTools() const { return Tools; }

private:
	void RegisterDefaultTools();

	TMap<FString, FClaudeToolDefinition> Tools;
};
