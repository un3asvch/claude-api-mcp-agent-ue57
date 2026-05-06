// Copyright Untry. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IClaudeBackend.h"
#include "ClaudeAPIClient.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

/**
 * Wraps the existing FClaudeAPIClient (which has its own delegate set)
 * into the IClaudeBackend interface, forwarding events.
 */
class FAnthropicAPIBackend : public IClaudeBackend
{
public:
	FAnthropicAPIBackend()
	{
		Client = MakeShared<FClaudeAPIClient>();

		Client->OnStreamText.AddLambda([this](const FString& Delta)
		{
			OnStreamText.Broadcast(Delta);
		});
		Client->OnThinkingText.AddLambda([this](const FString& Thought)
		{
			OnThinking.Broadcast(Thought);
		});
		Client->OnError.AddLambda([this](const FString& Err)
		{
			OnError.Broadcast(Err);
		});
		Client->OnAgentTurnComplete.AddLambda([this]()
		{
			OnTurnComplete.Broadcast();
		});
		Client->OnToolUse.AddLambda([this](const FClaudeContentBlock& B)
		{
			FString InputStr;
			if (B.ToolInput.IsValid())
			{
				auto W = TJsonWriterFactory<>::Create(&InputStr);
				FJsonSerializer::Serialize(B.ToolInput.ToSharedRef(), W);
			}
			OnToolCall.Broadcast(B.ToolName, InputStr);
		});
		Client->OnToolResult.AddLambda([this](const FString& Id, const FClaudeToolResult& R)
		{
			OnToolResult.Broadcast(R.bIsError ? TEXT("error") : TEXT("ok"), R.Content);
		});
	}

	virtual void SendUserMessage(const FString& UserText, const TArray<FString>& AttachedAssetPaths) override
	{
		Client->SendUserMessage(UserText, AttachedAssetPaths);
	}
	virtual void Cancel() override { Client->Cancel(); }
	virtual void ClearConversation() override { Client->ClearConversation(); }
	virtual bool IsBusy() const override { return Client->IsBusy(); }
	virtual FString GetBackendName() const override { return TEXT("Anthropic API"); }

	virtual void LoadMessages(const TArray<FClaudeMessage>& Messages) override
	{
		Client->LoadConversation(Messages);
	}
	virtual TArray<FClaudeMessage> GetMessages() const override
	{
		return Client->GetConversation();
	}

	TSharedPtr<FClaudeAPIClient> GetClient() const { return Client; }

private:
	TSharedPtr<FClaudeAPIClient> Client;
};
