// Copyright Untry. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ClaudeAgentTypes.h"
#include "ClaudeAgentSettings.generated.h"

UENUM()
enum class EClaudeCodePermissionMode : uint8
{
	/** Default — CLI will hang on first permission prompt (Bash/Write/Edit) because no TTY. Avoid. */
	Default         UMETA(DisplayName = "Default (interactive — will hang without TTY)"),
	/** Auto-accept file edits, still prompt on Bash. Safe-ish middle ground. */
	AcceptEdits     UMETA(DisplayName = "Auto-accept edits (Bash still prompts — will hang on Bash)"),
	/** Bypass ALL permissions. Unsafe — CLI can run any bash/write without asking. */
	BypassPermissions UMETA(DisplayName = "Bypass ALL permissions (DANGEROUS — no confirmations)"),
	/** Plan mode — CLI only plans changes, doesn't execute them. Safe for exploration. */
	Plan            UMETA(DisplayName = "Plan only (read-only reasoning, no edits)"),
};

UENUM()
enum class EClaudeBackendType : uint8
{
	AnthropicAPI    UMETA(DisplayName = "Anthropic API (pay-per-token, API key required)"),
	ClaudeCodeCLI   UMETA(DisplayName = "Claude Code CLI (uses your Pro/Max subscription)"),
};

UENUM()
enum class EClaudeModel : uint8
{
	Opus_4_7     UMETA(DisplayName = "claude-opus-4-7 (most capable — recommended)"),
	Opus_4_6     UMETA(DisplayName = "claude-opus-4-6 (previous top)"),
	Sonnet_4_6   UMETA(DisplayName = "claude-sonnet-4-6 (balanced)"),
	Haiku_4_5    UMETA(DisplayName = "claude-haiku-4-5 (fast, cheap)"),
};

/**
 * Settings for the Claude Agent plugin. Stored per-user, per-project so the
 * API key never gets committed to source control.
 */
UCLASS(Config = EditorPerProjectUserSettings, DefaultConfig, meta = (DisplayName = "Claude Agent"))
class UClaudeAgentSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UClaudeAgentSettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** Which backend to use to talk to Claude. */
	UPROPERTY(Config, EditAnywhere, Category = "Backend")
	EClaudeBackendType Backend = EClaudeBackendType::AnthropicAPI;

	/** Anthropic API key. Get one at https://console.anthropic.com/. Stored locally only. Used only with Anthropic API backend. */
	UPROPERTY(Config, EditAnywhere, Category = "Backend|Anthropic API", meta = (DisplayName = "API Key", PasswordField = true))
	FString ApiKey;

	/** Path to the `claude` CLI executable. Leave empty to search in PATH. Used only with Claude Code CLI backend. */
	UPROPERTY(Config, EditAnywhere, Category = "Backend|Claude Code CLI", meta = (DisplayName = "Claude Code Binary Path"))
	FString ClaudeCodeBinaryPath;

	/** How the CLI handles Bash/Write/Edit permission prompts. Without a TTY, Default will HANG on the first prompt. */
	UPROPERTY(Config, EditAnywhere, Category = "Backend|Claude Code CLI")
	EClaudeCodePermissionMode CLIPermissionMode = EClaudeCodePermissionMode::AcceptEdits;

	/** Extra CLI args appended verbatim. Power-user escape hatch (e.g. `--model opus`, `--allowedTools Read,Write`). */
	UPROPERTY(Config, EditAnywhere, Category = "Backend|Claude Code CLI", meta = (DisplayName = "Extra CLI Args"))
	FString ExtraCLIArgs;

	/** Start a local MCP (Model Context Protocol) server that exposes plugin tools to external Claude Code sessions. Makes all UE tools available to `claude` CLI. */
	UPROPERTY(Config, EditAnywhere, Category = "MCP Server")
	bool bEnableMCPServer = true;

	/** Port the MCP server listens on (localhost only). Register this in Claude Code with `claude mcp add`. */
	UPROPERTY(Config, EditAnywhere, Category = "MCP Server", meta = (ClampMin = 1024, ClampMax = 65535, EditCondition = "bEnableMCPServer"))
	int32 MCPServerPort = 17812;

	/** Name under which the MCP server registers with Claude Code. Tools will be prefixed `mcp__<name>__...`. */
	UPROPERTY(Config, EditAnywhere, Category = "MCP Server", meta = (EditCondition = "bEnableMCPServer"))
	FString MCPRegistrationName = TEXT("unreal");

	/** Automatically register this server with Claude Code on editor startup (runs `claude mcp add`). */
	UPROPERTY(Config, EditAnywhere, Category = "MCP Server", meta = (EditCondition = "bEnableMCPServer"))
	bool bAutoRegisterWithCLI = true;

	/** Scope of the CLI registration. `user` = works everywhere, `project` = only in this UE project folder, `local` = only in current dir. */
	UPROPERTY(Config, EditAnywhere, Category = "MCP Server", meta = (EditCondition = "bAutoRegisterWithCLI"))
	FString MCPRegistrationScope = TEXT("user");

	/** Which model to use by default. Opus 4.7 = best reasoning, Sonnet = balanced, Haiku = fast/cheap. */
	UPROPERTY(Config, EditAnywhere, Category = "Model")
	EClaudeModel DefaultModel = EClaudeModel::Sonnet_4_6;

	/** Maximum tokens in a single response. */
	UPROPERTY(Config, EditAnywhere, Category = "Model", meta = (ClampMin = 1024, ClampMax = 64000))
	int32 MaxTokens = 16000;

	/** Temperature (0.0 = deterministic, 1.0 = creative). */
	UPROPERTY(Config, EditAnywhere, Category = "Model", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float Temperature = 1.0f;

	/** Enable streaming responses (recommended). */
	UPROPERTY(Config, EditAnywhere, Category = "Model")
	bool bStreamResponses = true;

	/** Enable extended thinking — Claude shows its reasoning in dedicated `thinking` blocks. Works with Opus/Sonnet 4.x. API backend only. */
	UPROPERTY(Config, EditAnywhere, Category = "Model")
	bool bEnableExtendedThinking = true;

	/** Budget of tokens Claude can spend on internal reasoning before answering. Higher = deeper analysis, slower and pricier. */
	UPROPERTY(Config, EditAnywhere, Category = "Model", meta = (ClampMin = 1024, ClampMax = 32000, EditCondition = "bEnableExtendedThinking"))
	int32 ThinkingBudgetTokens = 4000;

	/** Custom system prompt prepended to every conversation. Project context goes here. */
	UPROPERTY(Config, EditAnywhere, Category = "System Prompt", meta = (MultiLine = true))
	FString SystemPrompt;

	/** Maximum agent iterations (tool-use loops) per single user message. Hard safety stop. */
	UPROPERTY(Config, EditAnywhere, Category = "Agent", meta = (ClampMin = 1, ClampMax = 50))
	int32 MaxAgentIterations = 25;

	/** Always require a confirmation dialog before any write/modify tool runs. */
	UPROPERTY(Config, EditAnywhere, Category = "Agent")
	bool bConfirmBeforeWrites = true;

	/** Auto-approve read-only tools (no confirmation popup). */
	UPROPERTY(Config, EditAnywhere, Category = "Agent")
	bool bAutoApproveReadOnlyTools = true;

	/** Save conversation history to Saved/ClaudeAgent/Conversations/. */
	UPROPERTY(Config, EditAnywhere, Category = "History")
	bool bSaveConversations = true;

	// ==================== Tool Filtering ====================
	// All categories enabled by default. Disable categories you don't use to:
	//   - Reduce noise in tool selection (Claude picks the right tool faster)
	//   - Reduce token cost on tool definitions
	//   - Disable tools for systems your project doesn't use (e.g. GAS if on GASP)

	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableGeneral = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableBlueprint = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableLevel = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableAnimation = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableMaterial = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableBehaviorTree = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableStateTree = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableDataAsset = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableControlRig = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableVisual = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnablePIE = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableDebug = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableSpline = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableEnvironment = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableNiagara = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableSequencer = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableGAS = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnablePCG = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableInput = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableWidget = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnablePoseSearch = true;
	UPROPERTY(Config, EditAnywhere, Category = "Tool Filtering")
	bool bEnableSettings = true;

	/** Returns true if a given tool category is currently enabled. */
	bool IsCategoryEnabled(EClaudeToolCategory Cat) const;

	/** Returns the model string to send to the API. */
	FString GetModelString() const;

	/** True if the currently-selected model is Opus 4.7, which has different
	 *  API rules (no temperature/top_p/top_k, adaptive thinking instead of budget_tokens). */
	bool IsOpus47() const;

	/** Default project-aware system prompt. */
	static FString GetDefaultSystemPrompt();

	static const UClaudeAgentSettings* Get() { return GetDefault<UClaudeAgentSettings>(); }
	static UClaudeAgentSettings* GetMutable() { return GetMutableDefault<UClaudeAgentSettings>(); }
};
