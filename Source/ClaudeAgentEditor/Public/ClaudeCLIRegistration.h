// Copyright Untry. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Runs `claude mcp add` / `claude mcp list` / `claude mcp remove` as child processes
 * and parses the output. All calls are synchronous — they're quick (<1s) and run
 * on the Game Thread.
 */
class FClaudeCLIRegistration
{
public:
	struct FResult
	{
		bool bSuccess = false;
		FString Output;   // stdout + stderr
		int32 ExitCode = -1;
	};

	/** Register (or update) this MCP server with Claude Code CLI. */
	static FResult Register(const FString& Name, const FString& URL, const FString& Scope);

	/** Remove registration. */
	static FResult Unregister(const FString& Name, const FString& Scope);

	/** Check whether a server with the given name is currently registered AND reachable per `claude mcp list`. */
	static FResult CheckStatus(const FString& Name);

	/** Is `claude` binary resolvable at all? */
	static bool IsCLIAvailable(FString& OutResolvedPath);

private:
	/** Run a `claude ...` command, capturing stdout+stderr. */
	static FResult RunCLI(const FString& Args);
	static FString ResolveClaudePath();
};
