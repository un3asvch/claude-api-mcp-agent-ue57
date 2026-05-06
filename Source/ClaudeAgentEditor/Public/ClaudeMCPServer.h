// Copyright Untry. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IHttpRouter.h"
#include "HttpResultCallback.h"

class FClaudeToolRegistry;
struct FHttpServerRequest;

/**
 * MCP (Model Context Protocol) server that exposes the plugin's UE tools
 * over HTTP (SSE transport) so external clients like Claude Code CLI
 * can call them as if they were native.
 *
 * MCP uses JSON-RPC 2.0. We implement the minimum required surface:
 *   - initialize           — capability handshake
 *   - tools/list           — enumerate available tools
 *   - tools/call           — invoke a tool by name
 *   - notifications/initialized — client ready
 *
 * Transport: streamable HTTP per MCP spec (2024-11-05). Single endpoint
 * accepts POSTs with JSON-RPC messages and optionally upgrades to SSE for
 * server->client notifications. We only do request/response here (no
 * sampling), so SSE is only used to wrap the single response.
 *
 * Tools are executed on the Game Thread via AsyncTask to avoid touching
 * UObjects from worker threads. The HTTP request callback is held until
 * tool execution completes.
 */
class FClaudeMCPServer
{
public:
	FClaudeMCPServer();
	~FClaudeMCPServer();

	/** Start listening on the configured port. Safe to call multiple times. */
	bool Start(int32 Port);

	/** Stop the server. */
	void Stop();

	bool IsRunning() const { return bIsRunning; }
	int32 GetPort() const { return ListenPort; }

	/** Tool registry is shared with the API backend so tools are defined once. */
	void SetToolRegistry(const TSharedPtr<FClaudeToolRegistry>& InRegistry) { ToolRegistry = InRegistry; }

private:
	bool HandleMCPRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	TSharedPtr<class FJsonObject> BuildInitializeResponse(int64 RequestId);
	TSharedPtr<class FJsonObject> BuildToolsListResponse(int64 RequestId);
	void HandleToolCall(
		const TSharedPtr<class FJsonObject>& Params,
		int64 RequestId,
		TFunction<void(TSharedPtr<class FJsonObject>)> ReplyCallback);

	static TSharedRef<class FJsonObject> MakeErrorResponse(int64 RequestId, int32 Code, const FString& Message);
	static FString SerializeJson(const TSharedRef<class FJsonObject>& Obj);

	TSharedPtr<class IHttpRouter> Router;
	FHttpRouteHandle RouteHandle;
	int32 ListenPort = 0;
	bool bIsRunning = false;

	TSharedPtr<FClaudeToolRegistry> ToolRegistry;
};
