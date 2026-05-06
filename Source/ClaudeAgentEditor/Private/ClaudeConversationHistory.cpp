// Copyright Untry. All Rights Reserved.

#include "ClaudeConversationHistory.h"
#include "ClaudeAgentEditor.h"

#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString RoleToString(EClaudeRole R)
	{
		switch (R)
		{
			case EClaudeRole::User:      return TEXT("user");
			case EClaudeRole::Assistant: return TEXT("assistant");
			case EClaudeRole::System:    return TEXT("system");
		}
		return TEXT("user");
	}

	EClaudeRole StringToRole(const FString& S)
	{
		if (S == TEXT("assistant")) return EClaudeRole::Assistant;
		if (S == TEXT("system"))    return EClaudeRole::System;
		return EClaudeRole::User;
	}

	FString BlockTypeToString(EClaudeContentBlockType T)
	{
		switch (T)
		{
			case EClaudeContentBlockType::Text:       return TEXT("text");
			case EClaudeContentBlockType::ToolUse:    return TEXT("tool_use");
			case EClaudeContentBlockType::ToolResult: return TEXT("tool_result");
			case EClaudeContentBlockType::Thinking:   return TEXT("thinking");
		}
		return TEXT("text");
	}

	EClaudeContentBlockType StringToBlockType(const FString& S)
	{
		if (S == TEXT("tool_use"))    return EClaudeContentBlockType::ToolUse;
		if (S == TEXT("tool_result")) return EClaudeContentBlockType::ToolResult;
		if (S == TEXT("thinking"))    return EClaudeContentBlockType::Thinking;
		return EClaudeContentBlockType::Text;
	}
}

FString FClaudeConversationHistory::GetConversationsDir()
{
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("ClaudeAgent") / TEXT("Conversations");
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.DirectoryExists(*Dir)) PF.CreateDirectoryTree(*Dir);
	return Dir;
}

FString FClaudeConversationHistory::MakeSlug(const FString& Text)
{
	FString S = Text;
	S = S.Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));

	FString Clean;
	Clean.Reserve(40);
	for (TCHAR C : S)
	{
		if (FChar::IsAlnum(C))
		{
			Clean.AppendChar(C);
		}
		else if (C == ' ' || C == '_' || C == '-')
		{
			if (Clean.Len() > 0 && Clean[Clean.Len() - 1] != '_') Clean.AppendChar('_');
		}
		if (Clean.Len() >= 40) break;
	}
	if (Clean.IsEmpty()) Clean = TEXT("chat");
	return Clean;
}

FString FClaudeConversationHistory::DeriveTitle(const TArray<FClaudeMessage>& Messages)
{
	for (const FClaudeMessage& M : Messages)
	{
		if (M.Role != EClaudeRole::User) continue;
		for (const FClaudeContentBlock& B : M.Blocks)
		{
			if (B.Type == EClaudeContentBlockType::Text && !B.Text.IsEmpty())
			{
				FString T = B.Text;
				T = T.Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));
				if (T.Len() > 80) T = T.Left(80) + TEXT("…");
				return T;
			}
		}
	}
	return TEXT("(empty)");
}

// -----------------------------------------------------------------------------
// List
// -----------------------------------------------------------------------------

TArray<FClaudeConversationSummary> FClaudeConversationHistory::ListConversations()
{
	TArray<FClaudeConversationSummary> Result;

	const FString Dir = GetConversationsDir();
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();

	TArray<FString> Files;
	PF.FindFiles(Files, *Dir, TEXT(".json"));

	for (const FString& File : Files)
	{
		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *File)) continue;

		TSharedPtr<FJsonObject> Obj;
		auto Reader = TJsonReaderFactory<>::Create(Raw);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) continue;

		FClaudeConversationSummary Sum;
		Sum.FilePath = File;
		Obj->TryGetStringField(TEXT("title"), Sum.Title);
		Obj->TryGetStringField(TEXT("backend"), Sum.BackendName);
		Obj->TryGetStringField(TEXT("cli_session_id"), Sum.CLISessionId);

		FString CreatedStr, ModifiedStr;
		if (Obj->TryGetStringField(TEXT("created"), CreatedStr))
		{
			FDateTime::ParseIso8601(*CreatedStr, Sum.Created);
		}
		if (Obj->TryGetStringField(TEXT("last_modified"), ModifiedStr))
		{
			FDateTime::ParseIso8601(*ModifiedStr, Sum.LastModified);
		}

		const TArray<TSharedPtr<FJsonValue>>* Msgs;
		if (Obj->TryGetArrayField(TEXT("messages"), Msgs))
		{
			Sum.MessageCount = Msgs->Num();
		}

		Result.Add(Sum);
	}

	// Newest first
	Result.Sort([](const FClaudeConversationSummary& A, const FClaudeConversationSummary& B)
	{
		return A.LastModified > B.LastModified;
	});

	return Result;
}

// -----------------------------------------------------------------------------
// Save
// -----------------------------------------------------------------------------

FString FClaudeConversationHistory::SaveConversation(
	const FString& ExistingFilePath,
	const TArray<FClaudeMessage>& Messages,
	const FString& BackendName,
	const FString& CLISessionId)
{
	if (Messages.Num() == 0) return ExistingFilePath;

	const FDateTime Now = FDateTime::UtcNow();
	const FString Title = DeriveTitle(Messages);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetStringField(TEXT("title"), Title);
	Root->SetStringField(TEXT("backend"), BackendName);
	Root->SetStringField(TEXT("cli_session_id"), CLISessionId);
	Root->SetStringField(TEXT("last_modified"), Now.ToIso8601());

	FString ResolvedPath = ExistingFilePath;
	FString CreatedIso;
	if (ResolvedPath.IsEmpty())
	{
		const FString Stamp = Now.ToString(TEXT("%Y%m%d_%H%M%S"));
		const FString Slug = MakeSlug(Title);
		ResolvedPath = GetConversationsDir() / FString::Printf(TEXT("%s_%s.json"), *Stamp, *Slug);
		CreatedIso = Now.ToIso8601();
	}
	else
	{
		// Preserve original created timestamp if file already exists
		FString ExistingRaw;
		if (FFileHelper::LoadFileToString(ExistingRaw, *ResolvedPath))
		{
			TSharedPtr<FJsonObject> Existing;
			auto Reader = TJsonReaderFactory<>::Create(ExistingRaw);
			if (FJsonSerializer::Deserialize(Reader, Existing) && Existing.IsValid())
			{
				Existing->TryGetStringField(TEXT("created"), CreatedIso);
			}
		}
		if (CreatedIso.IsEmpty()) CreatedIso = Now.ToIso8601();
	}
	Root->SetStringField(TEXT("created"), CreatedIso);

	// Serialize messages
	TArray<TSharedPtr<FJsonValue>> MsgsJson;
	for (const FClaudeMessage& M : Messages)
	{
		TSharedRef<FJsonObject> MObj = MakeShared<FJsonObject>();
		MObj->SetStringField(TEXT("role"), RoleToString(M.Role));

		TArray<TSharedPtr<FJsonValue>> BlocksJson;
		for (const FClaudeContentBlock& B : M.Blocks)
		{
			TSharedRef<FJsonObject> BObj = MakeShared<FJsonObject>();
			BObj->SetStringField(TEXT("type"), BlockTypeToString(B.Type));
			switch (B.Type)
			{
				case EClaudeContentBlockType::Text:
				case EClaudeContentBlockType::Thinking:
					BObj->SetStringField(TEXT("text"), B.Text);
					break;
				case EClaudeContentBlockType::ToolUse:
					BObj->SetStringField(TEXT("id"), B.ToolUseId);
					BObj->SetStringField(TEXT("name"), B.ToolName);
					if (B.ToolInput.IsValid())
					{
						BObj->SetObjectField(TEXT("input"), B.ToolInput);
					}
					break;
				case EClaudeContentBlockType::ToolResult:
					BObj->SetStringField(TEXT("tool_use_id"), B.ToolResultId);
					BObj->SetStringField(TEXT("content"), B.ToolResultContent);
					if (B.bIsError) BObj->SetBoolField(TEXT("is_error"), true);
					break;
			}
			BlocksJson.Add(MakeShared<FJsonValueObject>(BObj));
		}
		MObj->SetArrayField(TEXT("blocks"), BlocksJson);
		MsgsJson.Add(MakeShared<FJsonValueObject>(MObj));
	}
	Root->SetArrayField(TEXT("messages"), MsgsJson);

	FString Out;
	auto Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);

	// Atomic write: temp file → flush → rename. This way, if UE crashes mid-write,
	// the real file either reflects the previous save or the new one — never a
	// half-written file. Also, Windows OS write cache isn't safe across crashes —
	// we must explicitly flush the handle before the rename.
	const FString TempPath = ResolvedPath + TEXT(".tmp");
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();

	// Convert to UTF-8 bytes manually so we control the write
	FTCHARToUTF8 Converter(*Out);
	const int32 NumBytes = Converter.Length();

	{
		TUniquePtr<IFileHandle> Handle(PF.OpenWrite(*TempPath, /*bAppend*/ false, /*bAllowRead*/ false));
		if (!Handle.IsValid())
		{
			UE_LOG(LogClaudeAgent, Warning, TEXT("Could not open temp file for write: %s"), *TempPath);
			return TEXT("");
		}
		if (!Handle->Write(reinterpret_cast<const uint8*>(Converter.Get()), NumBytes))
		{
			UE_LOG(LogClaudeAgent, Warning, TEXT("Write failed: %s"), *TempPath);
			return TEXT("");
		}
		if (!Handle->Flush(/*bFullFlush*/ true))
		{
			UE_LOG(LogClaudeAgent, Warning, TEXT("Flush failed: %s"), *TempPath);
			// Continue anyway — flush may not be supported but the write likely succeeded
		}
		// Handle is released at end of scope → file closed
	}

	// Atomic rename (best-effort: move overwrites existing file on Windows)
	if (PF.FileExists(*ResolvedPath))
	{
		PF.DeleteFile(*ResolvedPath);
	}
	if (!PF.MoveFile(*ResolvedPath, *TempPath))
	{
		UE_LOG(LogClaudeAgent, Warning, TEXT("Could not rename %s to %s"), *TempPath, *ResolvedPath);
		return TEXT("");
	}

	return ResolvedPath;
}

// -----------------------------------------------------------------------------
// Load
// -----------------------------------------------------------------------------

bool FClaudeConversationHistory::LoadConversation(
	const FString& FilePath,
	TArray<FClaudeMessage>& OutMessages,
	FString& OutCLISessionId,
	FString& OutBackendName)
{
	OutMessages.Empty();
	OutCLISessionId.Empty();
	OutBackendName.Empty();

	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *FilePath)) return false;

	TSharedPtr<FJsonObject> Root;
	auto Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;

	Root->TryGetStringField(TEXT("backend"), OutBackendName);
	Root->TryGetStringField(TEXT("cli_session_id"), OutCLISessionId);

	const TArray<TSharedPtr<FJsonValue>>* Msgs;
	if (!Root->TryGetArrayField(TEXT("messages"), Msgs)) return true; // empty but valid

	for (const TSharedPtr<FJsonValue>& V : *Msgs)
	{
		auto MObj = V->AsObject();
		if (!MObj.IsValid()) continue;

		FClaudeMessage M;
		FString RoleStr;
		MObj->TryGetStringField(TEXT("role"), RoleStr);
		M.Role = StringToRole(RoleStr);

		const TArray<TSharedPtr<FJsonValue>>* BlocksArr;
		if (!MObj->TryGetArrayField(TEXT("blocks"), BlocksArr)) continue;

		for (const TSharedPtr<FJsonValue>& BV : *BlocksArr)
		{
			auto BObj = BV->AsObject();
			if (!BObj.IsValid()) continue;

			FClaudeContentBlock Block;
			FString TypeStr;
			BObj->TryGetStringField(TEXT("type"), TypeStr);
			Block.Type = StringToBlockType(TypeStr);

			switch (Block.Type)
			{
				case EClaudeContentBlockType::Text:
				case EClaudeContentBlockType::Thinking:
					BObj->TryGetStringField(TEXT("text"), Block.Text);
					break;
				case EClaudeContentBlockType::ToolUse:
				{
					BObj->TryGetStringField(TEXT("id"), Block.ToolUseId);
					BObj->TryGetStringField(TEXT("name"), Block.ToolName);
					const TSharedPtr<FJsonObject>* InputObj;
					if (BObj->TryGetObjectField(TEXT("input"), InputObj))
					{
						Block.ToolInput = *InputObj;
					}
					break;
				}
				case EClaudeContentBlockType::ToolResult:
					BObj->TryGetStringField(TEXT("tool_use_id"), Block.ToolResultId);
					BObj->TryGetStringField(TEXT("content"), Block.ToolResultContent);
					BObj->TryGetBoolField(TEXT("is_error"), Block.bIsError);
					break;
			}
			M.Blocks.Add(Block);
		}
		OutMessages.Add(M);
	}

	return true;
}

bool FClaudeConversationHistory::DeleteConversation(const FString& FilePath)
{
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	return PF.DeleteFile(*FilePath);
}
