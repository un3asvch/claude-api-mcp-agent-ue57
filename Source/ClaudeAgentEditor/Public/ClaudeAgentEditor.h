// Copyright Untry. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogClaudeAgent, Log, All);

class FToolBarBuilder;
class FMenuBuilder;
class FSpawnTabArgs;
class SDockTab;
class FExtender;
class FUICommandList;
class FClaudeToolRegistry;
class FClaudeMCPServer;

class FClaudeAgentEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FClaudeAgentEditorModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FClaudeAgentEditorModule>("ClaudeAgentEditor");
	}

	void OpenChatTab();

	/** Shared tool registry — used by both API backend and MCP server. */
	TSharedPtr<FClaudeToolRegistry> GetSharedToolRegistry() const { return SharedRegistry; }

	/** Restart MCP server (e.g. after port change in settings). */
	void RestartMCPServer();

private:
	void RegisterMenus();
	void RegisterContentBrowserExtensions();
	void UnregisterContentBrowserExtensions();

	TSharedRef<SDockTab> OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs);

	TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<struct FAssetData>& SelectedAssets);
	void AddAskClaudeMenuEntry(FMenuBuilder& MenuBuilder, TArray<struct FAssetData> SelectedAssets);
	void OnAskClaudeAboutAssets(TArray<struct FAssetData> SelectedAssets);

	TSharedPtr<FUICommandList> PluginCommands;
	FDelegateHandle ContentBrowserExtenderDelegateHandle;

	TSharedPtr<FClaudeToolRegistry> SharedRegistry;
	TSharedPtr<FClaudeMCPServer> MCPServer;
};
