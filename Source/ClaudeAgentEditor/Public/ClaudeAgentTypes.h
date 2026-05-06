// Copyright Untry. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "ClaudeAgentTypes.generated.h"

UENUM()
enum class EClaudeRole : uint8
{
	User,
	Assistant,
	System,
};

UENUM()
enum class EClaudeContentBlockType : uint8
{
	Text,
	ToolUse,
	ToolResult,
	Thinking,
};

/**
 * One block inside a message. A message can contain a sequence of blocks
 * (e.g. text + tool_use + text). This mirrors Anthropic's API shape.
 */
struct FClaudeContentBlock
{
	EClaudeContentBlockType Type = EClaudeContentBlockType::Text;

	// Text / Thinking
	FString Text;

	// ToolUse
	FString ToolUseId;
	FString ToolName;
	TSharedPtr<FJsonObject> ToolInput;

	// ToolResult
	FString ToolResultId;       // matches ToolUseId
	FString ToolResultContent;  // string content
	bool bIsError = false;

	// Image attachment — if non-empty, a tool_result will be serialized as
	// a JSON array with both the text content AND this image. Only valid on
	// ToolResult blocks. Format is base64-encoded PNG/JPEG raw bytes.
	FString ImageBase64;
	FString ImageMediaType;     // "image/png" or "image/jpeg"

	static FClaudeContentBlock MakeText(const FString& InText)
	{
		FClaudeContentBlock B;
		B.Type = EClaudeContentBlockType::Text;
		B.Text = InText;
		return B;
	}

	static FClaudeContentBlock MakeToolResult(const FString& InId, const FString& InContent, bool bInError = false)
	{
		FClaudeContentBlock B;
		B.Type = EClaudeContentBlockType::ToolResult;
		B.ToolResultId = InId;
		B.ToolResultContent = InContent;
		B.bIsError = bInError;
		return B;
	}
};

struct FClaudeMessage
{
	EClaudeRole Role = EClaudeRole::User;
	TArray<FClaudeContentBlock> Blocks;

	static FClaudeMessage MakeUserText(const FString& InText)
	{
		FClaudeMessage M;
		M.Role = EClaudeRole::User;
		M.Blocks.Add(FClaudeContentBlock::MakeText(InText));
		return M;
	}
};

/** Result returned from a tool execution back into the conversation. */
struct FClaudeToolResult
{
	FString Content;
	bool bIsError = false;
	bool bRequiresUserConfirmation = false;
	FString ConfirmationPrompt;

	// Optional image attachment. If set, the tool_result block sent back
	// to the model will be a JSON array containing both the text content
	// and this image.
	FString ImageBase64;
	FString ImageMediaType;  // "image/png" or "image/jpeg"
};

/** Function signature for a tool handler. */
DECLARE_DELEGATE_RetVal_OneParam(FClaudeToolResult, FClaudeToolHandler, const TSharedPtr<FJsonObject>& /*Input*/);

/** Tool category — used for filtering which tools are exposed to the agent.
 *  All categories enabled by default; user can toggle off in Project Settings. */
UENUM()
enum class EClaudeToolCategory : uint8
{
	General         UMETA(DisplayName = "General (read, search, project info)"),
	Blueprint       UMETA(DisplayName = "Blueprint (create, edit, graph nodes)"),
	Level           UMETA(DisplayName = "Level / Actor (spawn, transform, level design)"),
	Animation       UMETA(DisplayName = "Animation (Sequence, Montage, AnimBP)"),
	Material        UMETA(DisplayName = "Material (instances, parameters)"),
	BehaviorTree    UMETA(DisplayName = "Behavior Tree / Blackboard"),
	StateTree       UMETA(DisplayName = "StateTree"),
	DataAsset       UMETA(DisplayName = "Data Asset / Data Table"),
	ControlRig      UMETA(DisplayName = "Control Rig / IK Rig / Retargeter"),
	Visual          UMETA(DisplayName = "Visual (screenshots, thumbnails)"),
	PIE             UMETA(DisplayName = "PIE (runtime debugging in Play mode)"),
	Debug           UMETA(DisplayName = "Debug (compile errors, refs, orphans)"),
	Spline          UMETA(DisplayName = "Splines"),
	Environment     UMETA(DisplayName = "Environment (lighting, fog, post-process)"),
	Niagara         UMETA(DisplayName = "Niagara (VFX)"),
	Sequencer       UMETA(DisplayName = "Sequencer (cinematics)"),
	GAS             UMETA(DisplayName = "Gameplay Ability System"),
	PCG             UMETA(DisplayName = "PCG (procedural content)"),
	Input           UMETA(DisplayName = "Enhanced Input"),
	Widget          UMETA(DisplayName = "Widget / UMG"),
	PoseSearch      UMETA(DisplayName = "Pose Search / Motion Matching"),
	Settings        UMETA(DisplayName = "World / Project Settings"),
};

/** Tool definition exposed to the API. */
struct FClaudeToolDefinition
{
	FString Name;
	FString Description;
	TSharedPtr<FJsonObject> InputSchema; // JSON schema
	FClaudeToolHandler Handler;
	bool bIsReadOnly = true;
	EClaudeToolCategory Category = EClaudeToolCategory::General;
};
