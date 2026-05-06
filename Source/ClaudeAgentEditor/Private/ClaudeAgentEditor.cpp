// Copyright Untry. All Rights Reserved.

#include "ClaudeAgentEditor.h"
#include "ClaudeAgentSettings.h"
#include "ClaudeToolRegistry.h"
#include "ClaudeMCPServer.h"
#include "ClaudeCLIRegistration.h"
#include "SClaudeChatWidget.h"

#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Styling/AppStyle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserDelegates.h"
#include "AssetRegistry/AssetData.h"
#include "ToolMenus.h"
#include "ISettingsModule.h"
#include "LevelEditor.h"

DEFINE_LOG_CATEGORY(LogClaudeAgent);

#define LOCTEXT_NAMESPACE "ClaudeAgent"

static const FName ClaudeAgentTabName("ClaudeAgentTab");

void FClaudeAgentEditorModule::StartupModule()
{
	UE_LOG(LogClaudeAgent, Log, TEXT("Claude Agent Editor module starting"));

	// Shared tool registry — used by API backend and MCP server
	SharedRegistry = MakeShared<FClaudeToolRegistry>();

	// Start MCP server if enabled in settings
	const UClaudeAgentSettings* S = UClaudeAgentSettings::Get();
	if (S && S->bEnableMCPServer)
	{
		MCPServer = MakeShared<FClaudeMCPServer>();
		MCPServer->SetToolRegistry(SharedRegistry);
		if (!MCPServer->Start(S->MCPServerPort))
		{
			UE_LOG(LogClaudeAgent, Error, TEXT("Failed to start MCP server on port %d"), S->MCPServerPort);
		}
		else if (S->bAutoRegisterWithCLI)
		{
			// Auto-register with Claude Code CLI so `claude` picks us up on its next launch.
			// Runs synchronously but quickly (<1s usually). If CLI is not installed, this
			// is a harmless no-op with a logged warning.
			const FString URL = FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), S->MCPServerPort);
			auto RegResult = FClaudeCLIRegistration::Register(S->MCPRegistrationName, URL, S->MCPRegistrationScope);
			if (RegResult.bSuccess)
			{
				UE_LOG(LogClaudeAgent, Log, TEXT("MCP auto-registered with Claude Code as '%s' (%s scope)"),
					*S->MCPRegistrationName, *S->MCPRegistrationScope);
			}
			else
			{
				UE_LOG(LogClaudeAgent, Warning, TEXT("MCP auto-registration with CLI failed (is `claude` installed?). Output: %s"),
					*RegResult.Output.Left(500));
			}
		}
	}

	// Register settings
	if (auto* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "Claude Agent",
			LOCTEXT("SettingsName", "Claude Agent"),
			LOCTEXT("SettingsDesc", "Configure the Claude AI assistant plugin (API key, model, system prompt)."),
			GetMutableDefault<UClaudeAgentSettings>());
	}

	// Register tab spawner
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ClaudeAgentTabName,
		FOnSpawnTab::CreateRaw(this, &FClaudeAgentEditorModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Claude Agent"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Open the Claude AI assistant panel"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"));

	// Top-level menu entry under "Tools"
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([this]()
	{
		FToolMenuOwnerScoped MenuOwner(this);
		if (UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools"))
		{
			FToolMenuSection& Sec = ToolsMenu->FindOrAddSection("ClaudeAgent");
			Sec.AddMenuEntry(
				"OpenClaudeAgent",
				LOCTEXT("OpenClaude", "Claude Agent"),
				LOCTEXT("OpenClaudeTip", "Open the Claude AI assistant"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"),
				FUIAction(FExecuteAction::CreateRaw(this, &FClaudeAgentEditorModule::OpenChatTab)));
		}
	}));

	RegisterContentBrowserExtensions();
}

void FClaudeAgentEditorModule::ShutdownModule()
{
	if (MCPServer.IsValid())
	{
		MCPServer->Stop();
		MCPServer.Reset();
	}
	SharedRegistry.Reset();

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ClaudeAgentTabName);
	}

	if (auto* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "Claude Agent");
	}

	UnregisterContentBrowserExtensions();
}

void FClaudeAgentEditorModule::RestartMCPServer()
{
	const UClaudeAgentSettings* S = UClaudeAgentSettings::Get();
	if (MCPServer.IsValid())
	{
		MCPServer->Stop();
	}
	if (S && S->bEnableMCPServer)
	{
		if (!MCPServer.IsValid())
		{
			MCPServer = MakeShared<FClaudeMCPServer>();
			MCPServer->SetToolRegistry(SharedRegistry);
		}
		MCPServer->Start(S->MCPServerPort);
	}
}

void FClaudeAgentEditorModule::OpenChatTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(ClaudeAgentTabName);
}

TSharedRef<SDockTab> FClaudeAgentEditorModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("TabTitle", "Claude Agent"))
		[
			SNew(SClaudeChatWidget)
		];
}

// -----------------------------------------------------------------------------
// Content Browser extension: right-click "Ask Claude about this asset"
// -----------------------------------------------------------------------------

void FClaudeAgentEditorModule::RegisterContentBrowserExtensions()
{
	FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	auto& Extenders = CB.GetAllAssetViewContextMenuExtenders();
	Extenders.Add(FContentBrowserMenuExtender_SelectedAssets::CreateRaw(this, &FClaudeAgentEditorModule::OnExtendContentBrowserAssetSelectionMenu));
	ContentBrowserExtenderDelegateHandle = Extenders.Last().GetHandle();
}

void FClaudeAgentEditorModule::UnregisterContentBrowserExtensions()
{
	if (auto* CB = FModuleManager::GetModulePtr<FContentBrowserModule>(TEXT("ContentBrowser")))
	{
		CB->GetAllAssetViewContextMenuExtenders().RemoveAll(
			[Handle = ContentBrowserExtenderDelegateHandle](const FContentBrowserMenuExtender_SelectedAssets& Delegate)
			{
				return Delegate.GetHandle() == Handle;
			});
	}
}

TSharedRef<FExtender> FClaudeAgentEditorModule::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender(new FExtender());
	Extender->AddMenuExtension(
		"GetAssetActions",
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateRaw(this, &FClaudeAgentEditorModule::AddAskClaudeMenuEntry, SelectedAssets));
	return Extender;
}

void FClaudeAgentEditorModule::AddAskClaudeMenuEntry(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AskClaude", "Ask Claude about this asset"),
		LOCTEXT("AskClaudeTip", "Open the Claude Agent panel with this asset attached"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"),
		FUIAction(FExecuteAction::CreateRaw(this, &FClaudeAgentEditorModule::OnAskClaudeAboutAssets, SelectedAssets)));
}

void FClaudeAgentEditorModule::OnAskClaudeAboutAssets(TArray<FAssetData> SelectedAssets)
{
	OpenChatTab();

	// The tab content is rebuilt every time it's spawned, so we can't easily reach
	// the existing widget instance. Simplest robust path: post-show, find the active
	// SClaudeChatWidget and inject attachments into it.
	TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(ClaudeAgentTabName);
	if (Tab.IsValid())
	{
		TSharedRef<SWidget> Content = Tab->GetContent();
		// Cheap RTTI walk: SDockTab content root IS our widget
		if (TSharedPtr<SClaudeChatWidget> Chat = StaticCastSharedRef<SClaudeChatWidget>(Content).ToSharedPtr())
		{
			Chat->AttachAssets(SelectedAssets);
		}
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FClaudeAgentEditorModule, ClaudeAgentEditor)
