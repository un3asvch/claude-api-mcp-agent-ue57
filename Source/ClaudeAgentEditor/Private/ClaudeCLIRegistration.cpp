// Copyright Untry. All Rights Reserved.

#include "ClaudeCLIRegistration.h"
#include "ClaudeAgentEditor.h"
#include "ClaudeAgentSettings.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

FString FClaudeCLIRegistration::ResolveClaudePath()
{
	const UClaudeAgentSettings* S = UClaudeAgentSettings::Get();
	if (S && !S->ClaudeCodeBinaryPath.IsEmpty())
	{
		return S->ClaudeCodeBinaryPath;
	}
#if PLATFORM_WINDOWS
	return TEXT("claude.cmd");
#else
	return TEXT("claude");
#endif
}

bool FClaudeCLIRegistration::IsCLIAvailable(FString& OutResolvedPath)
{
	OutResolvedPath = ResolveClaudePath();

	// Quick probe: `claude --version`
	FResult R = RunCLI(TEXT("--version"));
	return R.bSuccess;
}

FClaudeCLIRegistration::FResult FClaudeCLIRegistration::RunCLI(const FString& Args)
{
	FResult Result;
	const FString BinaryPath = ResolveClaudePath();

	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	FPlatformProcess::CreatePipe(ReadPipe, WritePipe);

	uint32 Pid = 0;
	FProcHandle Proc = FPlatformProcess::CreateProc(
		*BinaryPath, *Args,
		/*bLaunchDetached*/ false,
		/*bLaunchHidden*/   true,
		/*bLaunchReallyHidden*/ true,
		&Pid, 0, nullptr,
		WritePipe);

	if (!Proc.IsValid())
	{
		Result.Output = FString::Printf(TEXT("Could not launch '%s'. Is claude CLI installed and on PATH?"), *BinaryPath);
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		return Result;
	}

	// Drain stdout while process runs (up to ~5s)
	const double StartTime = FPlatformTime::Seconds();
	while (FPlatformProcess::IsProcRunning(Proc))
	{
		FString Chunk = FPlatformProcess::ReadPipe(ReadPipe);
		if (!Chunk.IsEmpty()) Result.Output += Chunk;

		if (FPlatformTime::Seconds() - StartTime > 10.0)
		{
			FPlatformProcess::TerminateProc(Proc, true);
			Result.Output += TEXT("\n[timeout after 10s]");
			break;
		}
		FPlatformProcess::Sleep(0.02f);
	}

	// Final drain
	FString Tail = FPlatformProcess::ReadPipe(ReadPipe);
	if (!Tail.IsEmpty()) Result.Output += Tail;

	int32 RC = 0;
	FPlatformProcess::GetProcReturnCode(Proc, &RC);
	Result.ExitCode = RC;
	Result.bSuccess = (RC == 0);

	FPlatformProcess::CloseProc(Proc);
	FPlatformProcess::ClosePipe(ReadPipe, WritePipe);

	UE_LOG(LogClaudeAgent, Log, TEXT("claude %s → exit=%d, output=%s"), *Args, RC, *Result.Output.Left(300));
	return Result;
}

FClaudeCLIRegistration::FResult FClaudeCLIRegistration::Register(const FString& Name, const FString& URL, const FString& Scope)
{
	// Remove existing registration first to avoid the "already exists" error on re-registration.
	// We ignore the exit code — if it wasn't there, removal fails harmlessly.
	Unregister(Name, Scope);

	// `claude mcp add <name> --transport http <url> --scope <user|project|local>`
	const FString Args = FString::Printf(
		TEXT("mcp add %s --transport http %s --scope %s"),
		*Name, *URL, *Scope);

	return RunCLI(Args);
}

FClaudeCLIRegistration::FResult FClaudeCLIRegistration::Unregister(const FString& Name, const FString& Scope)
{
	const FString Args = FString::Printf(TEXT("mcp remove %s --scope %s"), *Name, *Scope);
	return RunCLI(Args);
}

FClaudeCLIRegistration::FResult FClaudeCLIRegistration::CheckStatus(const FString& Name)
{
	FResult R = RunCLI(TEXT("mcp list"));
	if (!R.bSuccess) return R;

	// Output looks like:
	//   Checking MCP server health...
	//   unreal: http://127.0.0.1:17812/mcp (HTTP) - ✓ Connected
	//   other: ...

	TArray<FString> Lines;
	R.Output.ParseIntoArrayLines(Lines, true);
	for (const FString& Line : Lines)
	{
		if (Line.StartsWith(Name + TEXT(":")))
		{
			R.Output = Line;
			R.bSuccess = Line.Contains(TEXT("Connected")) || Line.Contains(TEXT("✓"));
			return R;
		}
	}
	R.bSuccess = false;
	R.Output = FString::Printf(TEXT("'%s' not found in `claude mcp list` output"), *Name);
	return R;
}
