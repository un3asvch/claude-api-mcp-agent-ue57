// Copyright Untry. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "ClaudeAgentTypes.h"

class IClaudeBackend;
class SScrollBox;
class SMultiLineEditableTextBox;
class SVerticalBox;

class SClaudeChatWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SClaudeChatWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SClaudeChatWidget();

	/** Drag-drop entry point */
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;

	/** Pre-fill the chat with attached assets (called from Content Browser ext.) */
	void AttachAssets(const TArray<struct FAssetData>& Assets);

private:
	FReply OnSendClicked();
	FReply OnCancelClicked();
	FReply OnClearClicked();
	FReply OnNewChatClicked();
	FReply OnSettingsClicked();
	FReply OnSaveClicked();
	FReply OnRemoveAttachmentClicked(int32 Index);
	FReply OnOpenConversationsFolderClicked();

	void HandleStreamText(const FString& Delta);
	void HandleThinking(const FString& Thought);
	void HandleToolCall(const FString& ToolName, const FString& InputJson);
	void HandleToolResult(const FString& Status, const FString& ResultBody);
	void HandleError(const FString& Err);
	void HandleStatus(const FString& StatusLine);
	void HandleTurnComplete();

	void RebuildBackend();
	void RebuildAttachmentBar();
	void RebuildHistoryList();
	void RenderMessagesInChat(const TArray<FClaudeMessage>& Messages);
	void AutoSaveConversation();
	void LoadConversationFromFile(const FString& FilePath);
	void AddMessageBubble(const FString& Header, const FString& Body, const FLinearColor& HeaderColor);
	void AppendToLastBubble(const FString& Delta);
	void ScrollToBottom();

	TSharedPtr<IClaudeBackend> Backend;

	TSharedPtr<SVerticalBox>              HistoryList;
	TSharedPtr<SScrollBox>                ChatScroll;
	TSharedPtr<SVerticalBox>              ChatBox;
	TSharedPtr<SMultiLineEditableTextBox> InputBox;
	TSharedPtr<SVerticalBox>              AttachmentsBox;
	TSharedPtr<class STextBlock>          StatusText;
	TSharedPtr<class SCircularThrobber>   BusyThrobber;
	TSharedPtr<class SMultiLineEditableText> ActiveAssistantText;

	/** File path of currently-loaded conversation. Empty = new unsaved chat. */
	FString CurrentConversationPath;

	TArray<FString> AttachedAssetPaths;
	bool bIsHovering = false;
};
