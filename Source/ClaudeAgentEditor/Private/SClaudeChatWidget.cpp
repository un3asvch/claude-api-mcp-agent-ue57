// Copyright Untry. All Rights Reserved.

#include "SClaudeChatWidget.h"
#include "IClaudeBackend.h"
#include "AnthropicAPIBackend.h"
#include "ClaudeCodeCLIBackend.h"
#include "ClaudeCLIRegistration.h"
#include "ClaudeConversationHistory.h"
#include "ClaudeAgentStyle.h"
#include "ClaudeAgentEditor.h"
#include "ClaudeAgentSettings.h"

#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/SBoxPanel.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "AssetRegistry/AssetData.h"
#include "Styling/AppStyle.h"
#include "Settings/EditorExperimentalSettings.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformProcess.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

#define LOCTEXT_NAMESPACE "ClaudeAgent"

void SClaudeChatWidget::Construct(const FArguments& InArgs)
{
	RebuildBackend();

	// Chat column — same as before but now it's the RIGHT half of a splitter
	TSharedRef<SVerticalBox> ChatColumn = SNew(SVerticalBox);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FSlateColor(ClaudeAgentStyle::CanvasBg()))
		.Padding(0)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			// ============ LEFT: history sidebar ============
			+ SSplitter::Slot()
			.Value(0.22f)
			.MinSize(160.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FSlateColor(ClaudeAgentStyle::SurfaceBg()))
				.Padding(8)
				[
					SNew(SVerticalBox)

					// Sidebar header
					+ SVerticalBox::Slot().AutoHeight().Padding(2, 2, 2, 6)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("HistoryLabel", "История"))
							.Font(ClaudeAgentStyle::TitleFont())
							.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextPrimary()))
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ToolTipText(LOCTEXT("OpenFolder", "Открыть папку с JSON-файлами"))
							.ContentPadding(FMargin(6, 2))
							.OnClicked(this, &SClaudeChatWidget::OnOpenConversationsFolderClicked)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("📁")))
								.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextSecondary()))
							]
						]
					]

					// New chat button — branded accent
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.ContentPadding(FMargin(10, 6))
						.ButtonColorAndOpacity(FSlateColor(ClaudeAgentStyle::BrandAccent()))
						.ForegroundColor(FSlateColor(FLinearColor::White))
						.ToolTipText(LOCTEXT("NewChatTip", "Начать новую беседу"))
						.OnClicked(this, &SClaudeChatWidget::OnNewChatClicked)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NewChat", "+  Новый чат"))
							.Font(ClaudeAgentStyle::HeaderFont())
							.ColorAndOpacity(FSlateColor(FLinearColor::White))
						]
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 6)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FSlateColor(ClaudeAgentStyle::DividerColor()))
						.Padding(FMargin(0, 1, 0, 0))
					]

					// Conversation list
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(HistoryList, SVerticalBox)
						]
					]
				]
			]

			// ============ RIGHT: chat column ============
			+ SSplitter::Slot()
			.Value(0.78f)
			[
				ChatColumn
			]
		]
	];

	// Fill the chat column with the existing layout
	// Chat column wraps in a branded background
	ChatColumn->AddSlot().AutoHeight()
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FSlateColor(ClaudeAgentStyle::SurfaceBg()))
		.Padding(FMargin(12, 10))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				// Small brand dot + title
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					SNew(SBox).WidthOverride(8.0f).HeightOverride(8.0f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("WhiteBrush"))
						.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::BrandAccent()))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "Claude Agent"))
					.Font(ClaudeAgentStyle::TitleFont())
					.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextPrimary()))
				]
			]

			+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNullWidget::NullWidget ]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
			[
				SAssignNew(BusyThrobber, SCircularThrobber)
				.Radius(8.0f)
				.NumPieces(6)
				.Period(0.8f)
				.Visibility(EVisibility::Hidden)
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)
			[
				SAssignNew(StatusText, STextBlock)
				.Text(LOCTEXT("Idle", "Готов"))
				.Font(ClaudeAgentStyle::SmallFont())
				.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextSecondary()))
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(8, 4))
				.ToolTipText(LOCTEXT("MCPTip", "Проверить/перерегистрировать MCP сервер в Claude Code CLI"))
				.OnClicked_Lambda([this]() -> FReply
				{
					const UClaudeAgentSettings* SS = UClaudeAgentSettings::Get();
					if (!SS->bEnableMCPServer)
					{
						HandleError(TEXT("MCP Server отключён в настройках плагина."));
						return FReply::Handled();
					}

					// Check first
					auto Status = FClaudeCLIRegistration::CheckStatus(SS->MCPRegistrationName);
					if (Status.bSuccess)
					{
						AddMessageBubble(TEXT("🔌 MCP"),
							FString::Printf(TEXT("Уже подключён:\n%s"), *Status.Output),
							FLinearColor(0.55f, 1.0f, 0.55f));
						return FReply::Handled();
					}

					// Register
					const FString URL = FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), SS->MCPServerPort);
					auto Reg = FClaudeCLIRegistration::Register(SS->MCPRegistrationName, URL, SS->MCPRegistrationScope);

					if (Reg.bSuccess)
					{
						AddMessageBubble(TEXT("🔌 MCP"),
							FString::Printf(TEXT("Зарегистрирован как '%s' (scope=%s) на %s.\n\nТеперь запусти `claude` в терминале проекта — он подключится автоматически."),
								*SS->MCPRegistrationName, *SS->MCPRegistrationScope, *URL),
							FLinearColor(0.55f, 1.0f, 0.55f));
					}
					else
					{
						AddMessageBubble(TEXT("⚠ MCP registration failed"),
							FString::Printf(TEXT("%s\n\nПроверь что `claude` CLI установлен и доступен в PATH, либо укажи полный путь в Project Settings → Plugins → Claude Agent → Claude Code Binary Path."),
								*Reg.Output),
							FLinearColor(1.0f, 0.4f, 0.4f));
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MCP", "MCP"))
					.Font(ClaudeAgentStyle::SmallFont())
					.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextSecondary()))
				]
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(8, 4))
				.ToolTipText(LOCTEXT("ReloadTip", "Перезагрузить backend (после смены в Settings)"))
				.OnClicked_Lambda([this]() -> FReply
				{
					RebuildBackend();
					if (StatusText.IsValid())
					{
						const FString Name = Backend.IsValid() ? Backend->GetBackendName() : TEXT("?");
						StatusText->SetText(FText::FromString(FString::Printf(TEXT("Backend: %s"), *Name)));
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("↻")))
					.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextSecondary()))
				]
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(8, 4))
				.ToolTipText(LOCTEXT("SettingsTip", "Настройки плагина"))
				.OnClicked(this, &SClaudeChatWidget::OnSettingsClicked)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("⚙")))
					.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextSecondary()))
				]
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(8, 4))
				.ToolTipText(LOCTEXT("SaveTip", "Принудительно сохранить текущий чат"))
				.OnClicked(this, &SClaudeChatWidget::OnSaveClicked)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("💾")))
					.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextSecondary()))
				]
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(8, 4))
				.ToolTipText(LOCTEXT("ClearTip", "Очистить текущий чат (не удаляет из истории)"))
				.OnClicked(this, &SClaudeChatWidget::OnClearClicked)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Clear", "Clear"))
					.Font(ClaudeAgentStyle::SmallFont())
					.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextSecondary()))
				]
			]
		]
	];


	// Chat scroll area
	ChatColumn->AddSlot().FillHeight(1.0f)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FSlateColor(ClaudeAgentStyle::CanvasBg()))
		.Padding(FMargin(6, 6))
		[
			SAssignNew(ChatScroll, SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(ChatBox, SVerticalBox)
			]
		]
	];

	// Attachments
	ChatColumn->AddSlot().AutoHeight()
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FSlateColor(ClaudeAgentStyle::CanvasBg()))
		.Padding(FMargin(12, 4))
		[
			SAssignNew(AttachmentsBox, SVerticalBox)
		]
	];

	ChatColumn->AddSlot().AutoHeight()
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FSlateColor(ClaudeAgentStyle::DividerColor()))
		.Padding(FMargin(0, 1, 0, 0))
	];

	// Input area
	ChatColumn->AddSlot().AutoHeight()
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FSlateColor(ClaudeAgentStyle::SurfaceBg()))
		.Padding(FMargin(12, 10))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).MinDesiredHeight(80).MaxDesiredHeight(200)
				[
					SAssignNew(InputBox, SMultiLineEditableTextBox)
					.HintText(LOCTEXT("Hint", "Спроси Claude что-нибудь… (Ctrl+Enter — отправить)"))
					.AutoWrapText(true)
					.AlwaysShowScrollbars(false)
					.Font(ClaudeAgentStyle::BodyFont())
					.ForegroundColor(FSlateColor(ClaudeAgentStyle::TextPrimary()))
					.BackgroundColor(FSlateColor(ClaudeAgentStyle::CanvasBg()))
					.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& Key) -> FReply
					{
						if (Key.GetKey() == EKeys::Enter && (Key.IsControlDown() || Key.IsCommandDown()))
						{
							OnSendClicked();
							return FReply::Handled();
						}
						return FReply::Unhandled();
					})
				]
			]

			// Send / stop buttons row
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 6, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ContentPadding(FMargin(14, 8))
					.ButtonColorAndOpacity(FSlateColor(ClaudeAgentStyle::BrandAccent()))
					.ForegroundColor(FSlateColor(FLinearColor::White))
					.ToolTipText(LOCTEXT("SendTip", "Отправить сообщение (Ctrl+Enter)"))
					.OnClicked(this, &SClaudeChatWidget::OnSendClicked)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Send", "Отправить  ⌃↵"))
						.Font(ClaudeAgentStyle::HeaderFont())
						.ColorAndOpacity(FSlateColor(FLinearColor::White))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(14, 8))
					.ToolTipText(LOCTEXT("StopTip", "Прервать текущий запрос"))
					.OnClicked(this, &SClaudeChatWidget::OnCancelClicked)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CancelBtn", "Stop"))
						.Font(ClaudeAgentStyle::SmallFont())
						.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextSecondary()))
					]
				]
			]
		]
	];

	// Welcome message
	AddMessageBubble(TEXT("Claude Agent"),
		TEXT("Готов помочь с твоим UE-проектом. Могу анализировать Blueprint'ы, "
		     "редактировать графы, управлять ассетами и акторами — всё с твоего подтверждения.\n\n"
		     "Кидай ассеты из Content Browser прямо сюда, или жми ПКМ по ассету → «Ask Claude»."),
		FLinearColor::White);

	// Populate the sidebar with any existing conversations
	RebuildHistoryList();
}


SClaudeChatWidget::~SClaudeChatWidget()
{
	if (Backend.IsValid()) Backend->Cancel();
}

// -----------------------------------------------------------------------------
// Drag and drop
// -----------------------------------------------------------------------------

void SClaudeChatWidget::OnDragEnter(const FGeometry&, const FDragDropEvent& DragDropEvent)
{
	auto Op = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	if (Op.IsValid())
	{
		bIsHovering = true;
	}
}

void SClaudeChatWidget::OnDragLeave(const FDragDropEvent&)
{
	bIsHovering = false;
}

FReply SClaudeChatWidget::OnDrop(const FGeometry&, const FDragDropEvent& DragDropEvent)
{
	bIsHovering = false;
	auto Op = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	if (!Op.IsValid()) return FReply::Unhandled();

	AttachAssets(Op->GetAssets());
	return FReply::Handled();
}

void SClaudeChatWidget::AttachAssets(const TArray<FAssetData>& Assets)
{
	for (const FAssetData& A : Assets)
	{
		const FString Path = A.GetObjectPathString();
		AttachedAssetPaths.AddUnique(Path);
	}
	RebuildAttachmentBar();
}

void SClaudeChatWidget::RebuildAttachmentBar()
{
	if (!AttachmentsBox.IsValid()) return;
	AttachmentsBox->ClearChildren();

	if (AttachedAssetPaths.Num() == 0) return;

	AttachmentsBox->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[
		SNew(STextBlock)
		.Text(FText::Format(LOCTEXT("AttachedFmt", "Прикреплено  ·  {0}"), FText::AsNumber(AttachedAssetPaths.Num())))
		.Font(ClaudeAgentStyle::SmallFont())
		.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextMuted()))
	];

	for (int32 i = 0; i < AttachedAssetPaths.Num(); ++i)
	{
		const FString Path = AttachedAssetPaths[i];
		const int32 IndexCopy = i;

		AttachmentsBox->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FSlateColor(ClaudeAgentStyle::SurfaceBg()))
			.Padding(FMargin(8, 4))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(SBox).WidthOverride(4.0f).HeightOverride(4.0f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("WhiteBrush"))
						.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::BrandAccent()))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FPaths::GetBaseFilename(Path)))
					.ToolTipText(FText::FromString(Path))
					.Font(ClaudeAgentStyle::BodyFont())
					.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextPrimary()))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(4, 0))
					.OnClicked_Lambda([this, IndexCopy]() -> FReply
					{
						if (AttachedAssetPaths.IsValidIndex(IndexCopy))
						{
							AttachedAssetPaths.RemoveAt(IndexCopy);
							RebuildAttachmentBar();
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("✕")))
						.Font(ClaudeAgentStyle::SmallFont())
						.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextMuted()))
					]
				]
			]
		];
	}
}

FReply SClaudeChatWidget::OnRemoveAttachmentClicked(int32 Index)
{
	if (AttachedAssetPaths.IsValidIndex(Index))
	{
		AttachedAssetPaths.RemoveAt(Index);
		RebuildAttachmentBar();
	}
	return FReply::Handled();
}

// -----------------------------------------------------------------------------
// Buttons
// -----------------------------------------------------------------------------

FReply SClaudeChatWidget::OnSendClicked()
{
	if (!Backend.IsValid()) return FReply::Handled();
	if (Backend->IsBusy())
	{
		StatusText->SetText(LOCTEXT("Busy", "Уже выполняется запрос..."));
		return FReply::Handled();
	}

	const FString Text = InputBox->GetText().ToString().TrimStartAndEnd();
	if (Text.IsEmpty() && AttachedAssetPaths.Num() == 0) return FReply::Handled();

	// Render user bubble
	FString DisplayText = Text;
	if (AttachedAssetPaths.Num() > 0)
	{
		DisplayText = FString::Printf(TEXT("[%d прикреплённых ассетов]\n%s"), AttachedAssetPaths.Num(), *Text);
	}
	AddMessageBubble(TEXT("You"), DisplayText, FLinearColor(0.85f, 0.85f, 0.85f));

	// Prepare empty assistant bubble for streaming
	AddMessageBubble(TEXT("Claude"), TEXT(""), FLinearColor(0.6f, 0.85f, 1.0f));

	StatusText->SetText(LOCTEXT("Thinking", "Думаю..."));

	const TArray<FString> AttachedSnapshot = AttachedAssetPaths;
	AttachedAssetPaths.Empty();
	RebuildAttachmentBar();

	Backend->SendUserMessage(Text, AttachedSnapshot);
	InputBox->SetText(FText::GetEmpty());

	// Live busy indicator
	if (BusyThrobber.IsValid()) BusyThrobber->SetVisibility(EVisibility::Visible);
	if (StatusText.IsValid()) StatusText->SetText(LOCTEXT("Thinking", "Думаю..."));

	// Save immediately — user message is now in history. If UE crashes before
	// we get a response, at least the question isn't lost.
	AutoSaveConversation();

	return FReply::Handled();
}

FReply SClaudeChatWidget::OnCancelClicked()
{
	if (Backend.IsValid()) Backend->Cancel();
	if (StatusText.IsValid()) StatusText->SetText(LOCTEXT("Cancelled", "Отменено"));
	if (BusyThrobber.IsValid()) BusyThrobber->SetVisibility(EVisibility::Hidden);
	return FReply::Handled();
}

FReply SClaudeChatWidget::OnClearClicked()
{
	if (Backend.IsValid()) Backend->ClearConversation();
	if (ChatBox.IsValid()) ChatBox->ClearChildren();
	AttachedAssetPaths.Empty();
	RebuildAttachmentBar();
	StatusText->SetText(LOCTEXT("Cleared", "История очищена"));
	return FReply::Handled();
}

FReply SClaudeChatWidget::OnSettingsClicked()
{
	if (auto* Settings = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		Settings->ShowViewer("Project", "Plugins", "Claude Agent");
	}
	return FReply::Handled();
}

FReply SClaudeChatWidget::OnSaveClicked()
{
	// Manual save: force a write regardless of whether there's "anything new".
	// Also temporarily enable bSaveConversations if the user happened to disable
	// it — explicit click expresses intent.
	const FString PathBefore = CurrentConversationPath;
	AutoSaveConversation();

	const UClaudeAgentSettings* S = UClaudeAgentSettings::Get();
	if (!S->bSaveConversations)
	{
		StatusText->SetText(LOCTEXT("SaveDisabled",
			"⚠ Сохранение отключено в настройках (Project Settings → Plugins → Claude Agent → History)"));
		return FReply::Handled();
	}

	if (!CurrentConversationPath.IsEmpty())
	{
		const FString ShortPath = FPaths::GetCleanFilename(CurrentConversationPath);
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("✓ Сохранено: %s"), *ShortPath)));
		UE_LOG(LogClaudeAgent, Log, TEXT("Manual save → %s"), *CurrentConversationPath);
	}
	else
	{
		StatusText->SetText(LOCTEXT("SaveNothing", "Нечего сохранять (чат пуст)"));
	}
	return FReply::Handled();
}

// -----------------------------------------------------------------------------
// Bubble rendering
// -----------------------------------------------------------------------------

void SClaudeChatWidget::AddMessageBubble(const FString& Header, const FString& Body, const FLinearColor& /*HeaderColor — legacy, now computed from style*/)
{
	if (!ChatBox.IsValid()) return;

	TSharedPtr<SMultiLineEditableText> BodyTextRef;

	// Classify role from the header text
	const bool bIsUser       = (Header == TEXT("You"));
	const bool bIsClaude     = (Header == TEXT("Claude") || Header == TEXT("Claude Agent"));
	const bool bIsThinking   = Header.StartsWith(TEXT("💭"));
	const bool bIsToolCall   = Header.StartsWith(TEXT("🔧"));
	const bool bIsToolResult = Header.StartsWith(TEXT("✓")) || Header.StartsWith(TEXT("✗"));
	const bool bIsError      = Header.StartsWith(TEXT("⚠"));
	const bool bIsMCP        = Header.StartsWith(TEXT("🔌"));

	using namespace ClaudeAgentStyle;
	FLinearColor BubbleBg  = SurfaceBg();
	FLinearColor Accent    = TextSecondary();
	FLinearColor BodyColor = TextPrimary();

	if (bIsUser)            { BubbleBg = UserBg();     Accent = UserAccent(); }
	else if (bIsClaude)     { BubbleBg = ClaudeBg();   Accent = ClaudeAccent(); }
	else if (bIsThinking)   { BubbleBg = ThinkingBg(); Accent = ThinkingAccent(); BodyColor = TextSecondary(); }
	else if (bIsToolCall)   { BubbleBg = ToolCallBg(); Accent = ToolCallAccent(); }
	else if (bIsToolResult || bIsMCP) { BubbleBg = ToolOkBg();  Accent = ToolOkAccent(); }
	else if (bIsError)      { BubbleBg = ErrorBg();    Accent = ErrorAccent(); }

	// JSON-looking content gets monospace
	const bool bMonoBody = bIsToolCall || bIsToolResult;
	FSlateFontInfo BodyFontStyle = bMonoBody ? MonoFont() : BodyFont();

	ChatBox->AddSlot().AutoHeight().Padding(10, 5)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FSlateColor(BubbleBg))
		.Padding(FMargin(14, 10, 14, 12))
		[
			SNew(SVerticalBox)

			// Header row: small colored dot + role name, secondary-text color
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(SBox).WidthOverride(6.0f).HeightOverride(6.0f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("WhiteBrush"))
						.ColorAndOpacity(FSlateColor(Accent))
					]
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Header))
					.ColorAndOpacity(FSlateColor(Accent))
					.Font(HeaderFont())
				]

				+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNullWidget::NullWidget ]

				// Copy button — one click to copy the whole bubble body
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(4, 2))
					.ToolTipText(LOCTEXT("CopyBubble", "Скопировать текст"))
					.OnClicked_Lambda([BodyCopy = Body]() -> FReply
					{
						FPlatformApplicationMisc::ClipboardCopy(*BodyCopy);
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("⧉")))
						.Font(SmallFont())
						.ColorAndOpacity(FSlateColor(TextMuted()))
					]
				]
			]

			// Body — SMultiLineEditableText in read-only mode gives us native
			// selection + Ctrl+C + right-click context menu, while keeping
			// SetText/GetText compatible with our streaming append logic.
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(BodyTextRef, SMultiLineEditableText)
				.Text(FText::FromString(Body))
				.IsReadOnly(true)
				.AllowMultiLine(true)
				.AutoWrapText(true)
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
				.Font(BodyFontStyle)
				// SelectAllTextWhenFocused is false — we want click-drag to select a range
				.SelectAllTextWhenFocused(false)
				// Don't consume Enter in read-only mode (user might hit Enter after clicking)
				.ClearTextSelectionOnFocusLoss(false)
			]
		]
	];

	// Fresh empty "Claude" bubble → streaming target
	if (bIsClaude && Header == TEXT("Claude") && Body.IsEmpty())
	{
		ActiveAssistantText = BodyTextRef;
	}

	ScrollToBottom();
}


void SClaudeChatWidget::AppendToLastBubble(const FString& Delta)
{
	if (!ActiveAssistantText.IsValid()) return;
	const FString Current = ActiveAssistantText->GetText().ToString();
	ActiveAssistantText->SetText(FText::FromString(Current + Delta));
	ScrollToBottom();
}

void SClaudeChatWidget::ScrollToBottom()
{
	if (ChatScroll.IsValid()) ChatScroll->ScrollToEnd();
}

// -----------------------------------------------------------------------------
// Backend lifecycle
// -----------------------------------------------------------------------------

void SClaudeChatWidget::RebuildBackend()
{
	if (Backend.IsValid()) Backend->Cancel();

	const UClaudeAgentSettings* S = UClaudeAgentSettings::Get();
	if (S->Backend == EClaudeBackendType::ClaudeCodeCLI)
	{
		Backend = MakeShared<FClaudeCodeCLIBackend>();
	}
	else
	{
		Backend = MakeShared<FAnthropicAPIBackend>();
	}

	Backend->OnStreamText.AddSP(this, &SClaudeChatWidget::HandleStreamText);
	Backend->OnThinking.AddSP(this, &SClaudeChatWidget::HandleThinking);
	Backend->OnToolCall.AddSP(this, &SClaudeChatWidget::HandleToolCall);
	Backend->OnToolResult.AddSP(this, &SClaudeChatWidget::HandleToolResult);
	Backend->OnError.AddSP(this, &SClaudeChatWidget::HandleError);
	Backend->OnStatus.AddSP(this, &SClaudeChatWidget::HandleStatus);
	Backend->OnTurnComplete.AddSP(this, &SClaudeChatWidget::HandleTurnComplete);
}

// -----------------------------------------------------------------------------
// Backend callbacks
// -----------------------------------------------------------------------------

void SClaudeChatWidget::HandleStreamText(const FString& Delta)
{
	// First text delta = transition from "Думаю..." to "Пишет ответ..."
	if (StatusText.IsValid())
	{
		const FString Current = StatusText->GetText().ToString();
		if (Current == TEXT("Думаю...") || Current == TEXT("Рассуждает..."))
		{
			StatusText->SetText(LOCTEXT("Writing", "Пишет ответ..."));
		}
	}
	AppendToLastBubble(Delta);
}

void SClaudeChatWidget::HandleThinking(const FString& Thought)
{
	// Update status so the user sees reasoning is happening (distinct from waiting on network)
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(TEXT("Рассуждает...")));
	}
	// Extended thinking — distinct purple bubble
	AddMessageBubble(TEXT("💭 Thinking"), Thought, FLinearColor(0.75f, 0.65f, 1.0f));
	// Next assistant text goes into a fresh bubble
	AddMessageBubble(TEXT("Claude"), TEXT(""), FLinearColor(0.6f, 0.85f, 1.0f));
}

void SClaudeChatWidget::HandleToolCall(const FString& ToolName, const FString& InputJson)
{
	// Pretty-print JSON input for readability
	FString Pretty = InputJson;
	TSharedPtr<FJsonObject> Parsed;
	auto R = TJsonReaderFactory<>::Create(InputJson);
	if (FJsonSerializer::Deserialize(R, Parsed) && Parsed.IsValid())
	{
		auto W = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Pretty);
		FJsonSerializer::Serialize(Parsed.ToSharedRef(), W);
	}

	AddMessageBubble(FString::Printf(TEXT("🔧 %s"), *ToolName), Pretty, FLinearColor(1.0f, 0.75f, 0.3f));
	// Prepare fresh assistant bubble for the next streamed turn
	AddMessageBubble(TEXT("Claude"), TEXT(""), FLinearColor(0.6f, 0.85f, 1.0f));

	// Tool call registered in history — save NOW, before we execute the tool.
	// If UE crashes in the tool execution (e.g. during BT edit), we still have
	// a record of the call in the conversation.
	AutoSaveConversation();
}

void SClaudeChatWidget::HandleToolResult(const FString& Status, const FString& ResultBody)
{
	const bool bErr = (Status == TEXT("error"));
	const FString Clipped = ResultBody.Len() > 2000
		? ResultBody.Left(2000) + TEXT("\n\n[...обрезано для UI, модель получила всё...]")
		: ResultBody;
	AddMessageBubble(bErr ? TEXT("✗ Result (error)") : TEXT("✓ Result"), Clipped,
		bErr ? FLinearColor(1.0f, 0.4f, 0.4f) : FLinearColor(0.55f, 1.0f, 0.55f));

	// Save after tool result too — we now have a complete tool_use/tool_result pair
	AutoSaveConversation();
}

void SClaudeChatWidget::HandleError(const FString& Err)
{
	AddMessageBubble(TEXT("⚠ Error"), Err, FLinearColor(1.0f, 0.4f, 0.4f));
	if (StatusText.IsValid()) StatusText->SetText(LOCTEXT("ErrorStatus", "Ошибка"));
	if (BusyThrobber.IsValid()) BusyThrobber->SetVisibility(EVisibility::Hidden);

	// Save on errors — otherwise the whole conversation that led to the error is lost
	AutoSaveConversation();
}

void SClaudeChatWidget::HandleStatus(const FString& StatusLine)
{
	if (StatusText.IsValid()) StatusText->SetText(FText::FromString(StatusLine));
}

void SClaudeChatWidget::HandleTurnComplete()
{
	if (StatusText.IsValid()) StatusText->SetText(LOCTEXT("Done", "Готово"));
	if (BusyThrobber.IsValid()) BusyThrobber->SetVisibility(EVisibility::Hidden);
	ActiveAssistantText.Reset();

	// Auto-save after every agent turn
	AutoSaveConversation();
}

// -----------------------------------------------------------------------------
// Conversation persistence
// -----------------------------------------------------------------------------

void SClaudeChatWidget::AutoSaveConversation()
{
	const UClaudeAgentSettings* S = UClaudeAgentSettings::Get();
	if (!S->bSaveConversations) return;
	if (!Backend.IsValid()) return;

	const TArray<FClaudeMessage> Msgs = Backend->GetMessages();
	const FString SessionId = Backend->GetCLISessionId();
	const FString BackendName = Backend->GetBackendName();

	// Only skip save if literally nothing has happened yet — no messages AND
	// no CLI session. Either condition alone is enough reason to persist.
	if (Msgs.Num() == 0 && SessionId.IsEmpty() && CurrentConversationPath.IsEmpty())
	{
		UE_LOG(LogClaudeAgent, Verbose, TEXT("AutoSave skipped — empty conversation"));
		return;
	}

	const FString NewPath = FClaudeConversationHistory::SaveConversation(
		CurrentConversationPath, Msgs, BackendName, SessionId);

	if (!NewPath.IsEmpty())
	{
		const bool bFirstSave = CurrentConversationPath.IsEmpty();
		CurrentConversationPath = NewPath;
		// Log on first save (creating new file) so the user can see where it went.
		// Subsequent saves stay quiet to avoid log spam.
		if (bFirstSave)
		{
			UE_LOG(LogClaudeAgent, Log, TEXT("Conversation saved: %s"), *NewPath);
		}
	}
	else
	{
		UE_LOG(LogClaudeAgent, Warning, TEXT("AutoSaveConversation: SaveConversation returned empty path (existing path=%s)"), *CurrentConversationPath);
	}

	// Refresh sidebar (cheap — just re-lists files)
	RebuildHistoryList();
}

void SClaudeChatWidget::RebuildHistoryList()
{
	if (!HistoryList.IsValid()) return;
	HistoryList->ClearChildren();

	const TArray<FClaudeConversationSummary> List = FClaudeConversationHistory::ListConversations();

	if (List.Num() == 0)
	{
		HistoryList->AddSlot().AutoHeight().Padding(4, 8)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoHistory", "Пока пусто"))
			.Font(ClaudeAgentStyle::SmallFont())
			.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextMuted()))
		];
		return;
	}

	for (const FClaudeConversationSummary& Sum : List)
	{
		const FString Path = Sum.FilePath;
		const bool bIsActive = (Path == CurrentConversationPath);

		const FString Subtitle = FString::Printf(TEXT("%s · %s"),
			*Sum.LastModified.ToString(TEXT("%d.%m %H:%M")),
			*Sum.BackendName);

		HistoryList->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FSlateColor(bIsActive
				? ClaudeAgentStyle::SurfaceHover()
				: ClaudeAgentStyle::SurfaceBg()))
			.Padding(FMargin(8, 6))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill).Padding(0, 0, 8, 0)
				[
					// Tiny accent bar on active item
					SNew(SBox).WidthOverride(2.0f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("WhiteBrush"))
						.ColorAndOpacity(FSlateColor(bIsActive
							? ClaudeAgentStyle::BrandAccent()
							: FLinearColor(0,0,0,0)))
					]
				]

				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(0))
					.HAlign(HAlign_Fill)
					.ToolTipText(FText::FromString(Sum.Title))
					.OnClicked_Lambda([this, Path]() -> FReply
					{
						LoadConversationFromFile(Path);
						return FReply::Handled();
					})
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(Sum.Title.Left(60)))
							.Font(ClaudeAgentStyle::BodyFont())
							.ColorAndOpacity(FSlateColor(bIsActive
								? ClaudeAgentStyle::TextPrimary()
								: ClaudeAgentStyle::TextPrimary()))
							.AutoWrapText(false)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
						[
							SNew(STextBlock)
							.Text(FText::FromString(Subtitle))
							.Font(ClaudeAgentStyle::SmallFont())
							.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextMuted()))
						]
					]
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(4, 2))
					.ToolTipText(LOCTEXT("DeleteTip", "Удалить эту беседу"))
					.OnClicked_Lambda([this, Path]() -> FReply
					{
						FClaudeConversationHistory::DeleteConversation(Path);
						if (Path == CurrentConversationPath)
						{
							CurrentConversationPath.Empty();
						}
						RebuildHistoryList();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("✕")))
						.Font(ClaudeAgentStyle::SmallFont())
						.ColorAndOpacity(FSlateColor(ClaudeAgentStyle::TextMuted()))
					]
				]
			]
		];
	}
}

void SClaudeChatWidget::LoadConversationFromFile(const FString& FilePath)
{
	TArray<FClaudeMessage> Msgs;
	FString SessionId, BackendName;
	if (!FClaudeConversationHistory::LoadConversation(FilePath, Msgs, SessionId, BackendName))
	{
		HandleError(FString::Printf(TEXT("Не удалось загрузить %s"), *FilePath));
		return;
	}

	// Cancel any running request
	if (Backend.IsValid()) Backend->Cancel();

	CurrentConversationPath = FilePath;

	if (Backend.IsValid())
	{
		Backend->ClearConversation();
		Backend->LoadMessages(Msgs);
		if (!SessionId.IsEmpty()) Backend->SetCLISessionId(SessionId);
	}

	// Render into chat
	if (ChatBox.IsValid()) ChatBox->ClearChildren();
	ActiveAssistantText.Reset();

	AddMessageBubble(TEXT("Claude Agent"),
		FString::Printf(TEXT("Загружена беседа (backend: %s, %d сообщений)%s"),
			*BackendName, Msgs.Num(),
			SessionId.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("\nCLI session: %s"), *SessionId.Left(8))),
		FLinearColor(0.6f, 0.85f, 1.0f));

	RenderMessagesInChat(Msgs);
	RebuildHistoryList();

	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("Loaded", "Загружено"));
	}
}

void SClaudeChatWidget::RenderMessagesInChat(const TArray<FClaudeMessage>& Messages)
{
	for (const FClaudeMessage& M : Messages)
	{
		if (M.Role == EClaudeRole::User)
		{
			for (const FClaudeContentBlock& B : M.Blocks)
			{
				if (B.Type == EClaudeContentBlockType::Text)
				{
					AddMessageBubble(TEXT("You"), B.Text, FLinearColor(0.85f, 0.85f, 0.85f));
				}
				else if (B.Type == EClaudeContentBlockType::ToolResult)
				{
					const FString Clipped = B.ToolResultContent.Len() > 2000
						? B.ToolResultContent.Left(2000) + TEXT("\n\n[...]")
						: B.ToolResultContent;
					AddMessageBubble(
						B.bIsError ? TEXT("✗ Result (error)") : TEXT("✓ Result"),
						Clipped,
						B.bIsError ? FLinearColor(1.0f, 0.4f, 0.4f) : FLinearColor(0.55f, 1.0f, 0.55f));
				}
			}
		}
		else if (M.Role == EClaudeRole::Assistant)
		{
			for (const FClaudeContentBlock& B : M.Blocks)
			{
				switch (B.Type)
				{
					case EClaudeContentBlockType::Thinking:
						AddMessageBubble(TEXT("💭 Thinking"), B.Text, FLinearColor(0.75f, 0.65f, 1.0f));
						break;
					case EClaudeContentBlockType::Text:
						AddMessageBubble(TEXT("Claude"), B.Text, FLinearColor(0.6f, 0.85f, 1.0f));
						break;
					case EClaudeContentBlockType::ToolUse:
					{
						FString InputStr;
						if (B.ToolInput.IsValid())
						{
							auto W = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&InputStr);
							FJsonSerializer::Serialize(B.ToolInput.ToSharedRef(), W);
						}
						AddMessageBubble(FString::Printf(TEXT("🔧 %s"), *B.ToolName), InputStr, FLinearColor(1.0f, 0.75f, 0.3f));
						break;
					}
					default: break;
				}
			}
		}
	}
}

FReply SClaudeChatWidget::OnNewChatClicked()
{
	if (Backend.IsValid())
	{
		Backend->Cancel();
		Backend->ClearConversation();
	}
	if (ChatBox.IsValid()) ChatBox->ClearChildren();
	CurrentConversationPath.Empty();
	AttachedAssetPaths.Empty();
	RebuildAttachmentBar();
	ActiveAssistantText.Reset();

	AddMessageBubble(TEXT("Claude Agent"),
		TEXT("Новый чат. Задавай вопрос — я начну с чистого листа."),
		FLinearColor(0.6f, 0.85f, 1.0f));

	if (StatusText.IsValid()) StatusText->SetText(LOCTEXT("Ready", "Готов"));
	RebuildHistoryList();
	return FReply::Handled();
}

FReply SClaudeChatWidget::OnOpenConversationsFolderClicked()
{
	const FString Dir = FClaudeConversationHistory::GetConversationsDir();
	FPlatformProcess::ExploreFolder(*Dir);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
