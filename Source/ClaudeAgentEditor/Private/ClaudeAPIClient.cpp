// Copyright Untry. All Rights Reserved.

#include "ClaudeAPIClient.h"
#include "ClaudeAgentEditor.h"
#include "ClaudeAgentSettings.h"
#include "ClaudeToolRegistry.h"
#include "ClaudeContextProvider.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/MessageDialog.h"
#include "Async/Async.h"

namespace
{
	const FString API_URL = TEXT("https://api.anthropic.com/v1/messages");
	const FString API_VERSION = TEXT("2023-06-01");

	FString RoleToString(EClaudeRole R)
	{
		switch (R)
		{
			case EClaudeRole::User:      return TEXT("user");
			case EClaudeRole::Assistant: return TEXT("assistant");
			default:                     return TEXT("user");
		}
	}

	TSharedPtr<FJsonObject> ParseJson(const FString& Str)
	{
		TSharedPtr<FJsonObject> Obj;
		auto Reader = TJsonReaderFactory<>::Create(Str);
		FJsonSerializer::Deserialize(Reader, Obj);
		return Obj;
	}

	FString ToJsonString(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		auto Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}
}

FClaudeAPIClient::FClaudeAPIClient()
{
	// Prefer the shared registry owned by the module so the API backend and MCP server
	// expose the exact same tools. Falls back to a fresh registry if module not yet ready.
	if (FClaudeAgentEditorModule* Mod = FModuleManager::GetModulePtr<FClaudeAgentEditorModule>("ClaudeAgentEditor"))
	{
		ToolRegistry = Mod->GetSharedToolRegistry();
	}
	if (!ToolRegistry.IsValid())
	{
		ToolRegistry = MakeShared<FClaudeToolRegistry>();
	}
}

void FClaudeAPIClient::ClearConversation()
{
	Messages.Empty();
	IterationCount = 0;
}

void FClaudeAPIClient::Cancel()
{
	bCancelled = true;
	if (CurrentRequest.IsValid())
	{
		CurrentRequest->CancelRequest();
		CurrentRequest.Reset();
	}
	bIsBusy = false;
}

void FClaudeAPIClient::SendUserMessage(const FString& UserText, const TArray<FString>& AttachedAssetPaths)
{
	if (bIsBusy)
	{
		OnError.Broadcast(TEXT("Already processing a message. Cancel first."));
		return;
	}

	const UClaudeAgentSettings* S = UClaudeAgentSettings::Get();
	if (!S || S->ApiKey.IsEmpty())
	{
		OnError.Broadcast(TEXT("API key is not set. Open Project Settings -> Plugins -> Claude Agent and paste your key."));
		return;
	}

	bCancelled = false;
	IterationCount = 0;

	// Compose user content: optional attached-asset summaries + user text
	FString FullText;
	if (AttachedAssetPaths.Num() > 0)
	{
		FullText += TEXT("[Attached assets — full property dumps]\n\n");
		for (const FString& Path : AttachedAssetPaths)
		{
			FullText += FString::Printf(TEXT("=== %s ===\n"), *Path);
			FullText += FClaudeContextProvider::GetObjectProperties(Path, 1);
			FullText += TEXT("\n\n");
		}
		FullText += TEXT("[End attachments]\n\n");
	}
	FullText += UserText;

	// Guard against completely empty input (API will 400 on empty text blocks).
	if (FullText.TrimStartAndEnd().IsEmpty())
	{
		OnError.Broadcast(TEXT("Нельзя отправить пустое сообщение."));
		return;
	}

	Messages.Add(FClaudeMessage::MakeUserText(FullText));
	bIsBusy = true;
	DispatchRequest();
}

TSharedRef<FJsonObject> FClaudeAPIClient::BuildRequestBody() const
{
	const UClaudeAgentSettings* S = UClaudeAgentSettings::Get();
	const bool bOpus47 = S->IsOpus47();

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), S->GetModelString());
	Body->SetNumberField(TEXT("max_tokens"), S->MaxTokens);

	// Opus 4.7 returns 400 if temperature/top_p/top_k are specified at all.
	// We omit them entirely and use the server's default (which is 1.0).
	// For older models we still pass user's configured temperature.
	if (!bOpus47)
	{
		Body->SetNumberField(TEXT("temperature"), S->Temperature);
	}

	if (!S->SystemPrompt.IsEmpty())
	{
		Body->SetStringField(TEXT("system"), S->SystemPrompt);
	}

	if (S->bStreamResponses)
	{
		Body->SetBoolField(TEXT("stream"), true);
	}

	// Thinking — two different API shapes:
	//   4.6 and older: thinking = { type: "enabled", budget_tokens: N }, needs temperature=1
	//   4.7:           thinking = { type: "adaptive" },
	//                  output_config = { effort: "high", display: "summarized" }
	if (S->bEnableExtendedThinking)
	{
		if (bOpus47)
		{
			// New adaptive thinking — model decides budget dynamically.
			auto Thinking = MakeShared<FJsonObject>();
			Thinking->SetStringField(TEXT("type"), TEXT("adaptive"));
			Body->SetObjectField(TEXT("thinking"), Thinking);

			// output_config controls effort level and whether thinking is shown.
			// "display":"summarized" brings reasoning back into the stream —
			// without this, Opus 4.7 hides thinking content by default,
			// which would break our thinking bubbles in the UI.
			auto OutCfg = MakeShared<FJsonObject>();
			OutCfg->SetStringField(TEXT("effort"), TEXT("high"));
			OutCfg->SetStringField(TEXT("display"), TEXT("summarized"));
			Body->SetObjectField(TEXT("output_config"), OutCfg);
		}
		else
		{
			// Legacy extended thinking (4.6 and older).
			auto Thinking = MakeShared<FJsonObject>();
			Thinking->SetStringField(TEXT("type"), TEXT("enabled"));
			Thinking->SetNumberField(TEXT("budget_tokens"), S->ThinkingBudgetTokens);
			Body->SetObjectField(TEXT("thinking"), Thinking);
			// API requires temperature=1 when extended thinking is on
			Body->SetNumberField(TEXT("temperature"), 1.0);
		}
	}

	// Tools
	if (ToolRegistry.IsValid())
	{
		TArray<TSharedPtr<FJsonValue>> ToolsArr = ToolRegistry->BuildToolsJson();
		if (ToolsArr.Num() > 0)
		{
			Body->SetArrayField(TEXT("tools"), ToolsArr);
		}
	}

	// Messages — convert in-memory conversation to API shape
	TArray<TSharedPtr<FJsonValue>> Msgs;
	for (const FClaudeMessage& M : Messages)
	{
		TSharedRef<FJsonObject> MObj = MakeShared<FJsonObject>();
		MObj->SetStringField(TEXT("role"), RoleToString(M.Role));

		TArray<TSharedPtr<FJsonValue>> ContentArr;
		for (const FClaudeContentBlock& B : M.Blocks)
		{
			TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
			switch (B.Type)
			{
				case EClaudeContentBlockType::Text:
					// Anthropic API rejects empty text blocks (400 invalid_request_error).
					// Skip them — they can slip in from tool-use-only assistant turns.
					if (B.Text.IsEmpty()) continue;
					Block->SetStringField(TEXT("type"), TEXT("text"));
					Block->SetStringField(TEXT("text"), B.Text);
					break;

				case EClaudeContentBlockType::ToolUse:
					Block->SetStringField(TEXT("type"), TEXT("tool_use"));
					Block->SetStringField(TEXT("id"), B.ToolUseId);
					Block->SetStringField(TEXT("name"), B.ToolName);
					Block->SetObjectField(TEXT("input"), B.ToolInput.IsValid() ? B.ToolInput.ToSharedRef() : MakeShared<FJsonObject>());
					break;

				case EClaudeContentBlockType::ToolResult:
					Block->SetStringField(TEXT("type"), TEXT("tool_result"));
					Block->SetStringField(TEXT("tool_use_id"), B.ToolResultId);
					if (!B.ImageBase64.IsEmpty())
					{
						// Tool result with an image: content is an array of text + image blocks.
						// This shape is what the Anthropic API expects when a tool returns
						// visual output (e.g. a screenshot).
						TArray<TSharedPtr<FJsonValue>> ContentBlocks;

						// Text part — can be empty, but let's include a short label
						const FString TextLabel = B.ToolResultContent.IsEmpty()
							? TEXT("(image attached)") : B.ToolResultContent;
						TSharedRef<FJsonObject> TextBlock = MakeShared<FJsonObject>();
						TextBlock->SetStringField(TEXT("type"), TEXT("text"));
						TextBlock->SetStringField(TEXT("text"), TextLabel);
						ContentBlocks.Add(MakeShared<FJsonValueObject>(TextBlock));

						// Image part — base64-encoded bytes with media type
						TSharedRef<FJsonObject> ImageBlock = MakeShared<FJsonObject>();
						ImageBlock->SetStringField(TEXT("type"), TEXT("image"));
						TSharedRef<FJsonObject> Source = MakeShared<FJsonObject>();
						Source->SetStringField(TEXT("type"), TEXT("base64"));
						Source->SetStringField(TEXT("media_type"),
							B.ImageMediaType.IsEmpty() ? TEXT("image/png") : B.ImageMediaType);
						Source->SetStringField(TEXT("data"), B.ImageBase64);
						ImageBlock->SetObjectField(TEXT("source"), Source);
						ContentBlocks.Add(MakeShared<FJsonValueObject>(ImageBlock));

						Block->SetArrayField(TEXT("content"), ContentBlocks);
					}
					else
					{
						// Plain text tool result — legacy path. Tool results can't be empty
						// strings; replace with a placeholder.
						Block->SetStringField(TEXT("content"),
							B.ToolResultContent.IsEmpty() ? TEXT("(empty result)") : B.ToolResultContent);
					}
					if (B.bIsError) Block->SetBoolField(TEXT("is_error"), true);
					break;

				case EClaudeContentBlockType::Thinking:
					// We don't replay thinking back to the API in standard mode
					continue;
			}
			ContentArr.Add(MakeShared<FJsonValueObject>(Block));
		}

		// Skip messages that end up with zero valid content blocks — API will 400.
		if (ContentArr.Num() == 0) continue;

		MObj->SetArrayField(TEXT("content"), ContentArr);
		Msgs.Add(MakeShared<FJsonValueObject>(MObj));
	}
	Body->SetArrayField(TEXT("messages"), Msgs);

	return Body;
}

void FClaudeAPIClient::DispatchRequest()
{
	const UClaudeAgentSettings* S = UClaudeAgentSettings::Get();

	IterationCount++;
	if (IterationCount > S->MaxAgentIterations)
	{
		OnError.Broadcast(FString::Printf(TEXT("Hit max agent iterations (%d). Stopping."), S->MaxAgentIterations));
		bIsBusy = false;
		OnAgentTurnComplete.Broadcast();
		return;
	}

	StreamBuffer.Empty();
	CurrentStreamText.Empty();
	StreamingAssistantMessage = FClaudeMessage();
	StreamingAssistantMessage.Role = EClaudeRole::Assistant;

	const FString RequestBody = ToJsonString(BuildRequestBody());

	UE_LOG(LogClaudeAgent, Verbose, TEXT("Request body: %s"), *RequestBody);

	auto Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(API_URL);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("x-api-key"), S->ApiKey);
	Request->SetHeader(TEXT("anthropic-version"), API_VERSION);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(120.0f);

	if (S->bStreamResponses)
	{
		// Streaming: use OnRequestProgress to read chunks as they arrive.
		// Final assembly happens in OnProcessRequestComplete.
		Request->OnRequestProgress64().BindLambda(
			[WeakThis = TWeakPtr<FClaudeAPIClient>(AsShared())](FHttpRequestPtr Req, uint64 BytesSent, uint64 BytesReceived)
			{
				auto Strong = WeakThis.Pin();
				if (!Strong.IsValid()) return;
				if (!Req.IsValid() || !Req->GetResponse().IsValid()) return;

				const FString FullSoFar = Req->GetResponse()->GetContentAsString();
				if (FullSoFar.Len() > Strong->StreamBuffer.Len())
				{
					const FString Delta = FullSoFar.RightChop(Strong->StreamBuffer.Len());
					Strong->StreamBuffer = FullSoFar;
					AsyncTask(ENamedThreads::GameThread, [WeakThis, Delta]()
					{
						auto Pinned = WeakThis.Pin();
						if (Pinned.IsValid()) Pinned->HandleStreamChunk(Delta);
					});
				}
			});
	}

	Request->OnProcessRequestComplete().BindSP(this, &FClaudeAPIClient::HandleResponse);
	Request->ProcessRequest();
	CurrentRequest = Request;
}

void FClaudeAPIClient::HandleStreamChunk(const FString& Chunk)
{
	// Anthropic SSE: event lines like "event: content_block_delta\n" and "data: {...}\n\n"
	// We just want to extract incremental text deltas for live UI.
	TArray<FString> Lines;
	Chunk.ParseIntoArrayLines(Lines, false);

	for (const FString& Line : Lines)
	{
		if (!Line.StartsWith(TEXT("data: "))) continue;
		const FString Payload = Line.RightChop(6).TrimStartAndEnd();
		if (Payload.IsEmpty() || Payload == TEXT("[DONE]")) continue;

		auto Obj = ParseJson(Payload);
		if (!Obj.IsValid()) continue;

		FString EventType;
		Obj->TryGetStringField(TEXT("type"), EventType);

		if (EventType == TEXT("content_block_delta"))
		{
			const TSharedPtr<FJsonObject>* DeltaObj;
			if (Obj->TryGetObjectField(TEXT("delta"), DeltaObj))
			{
				FString DeltaType;
				(*DeltaObj)->TryGetStringField(TEXT("type"), DeltaType);
				if (DeltaType == TEXT("text_delta"))
				{
					FString Text;
					if ((*DeltaObj)->TryGetStringField(TEXT("text"), Text))
					{
						CurrentStreamText += Text;
						OnStreamText.Broadcast(Text);
					}
				}
				else if (DeltaType == TEXT("thinking_delta"))
				{
					FString Thought;
					if ((*DeltaObj)->TryGetStringField(TEXT("thinking"), Thought))
					{
						OnThinkingText.Broadcast(Thought);
					}
				}
			}
		}
	}
}

void FClaudeAPIClient::HandleResponse(TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
                                      TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
                                      bool bSuccess)
{
	CurrentRequest.Reset();

	if (bCancelled)
	{
		bIsBusy = false;
		return;
	}

	if (!bSuccess || !Response.IsValid())
	{
		OnError.Broadcast(TEXT("HTTP request failed (no response)."));
		bIsBusy = false;
		OnAgentTurnComplete.Broadcast();
		return;
	}

	const int32 Code = Response->GetResponseCode();
	const FString Body = Response->GetContentAsString();

	if (Code < 200 || Code >= 300)
	{
		OnError.Broadcast(FString::Printf(TEXT("API error %d: %s"), Code, *Body));
		bIsBusy = false;
		OnAgentTurnComplete.Broadcast();
		return;
	}

	const UClaudeAgentSettings* S = UClaudeAgentSettings::Get();
	TSharedPtr<FJsonObject> ResponseObj;

	if (S->bStreamResponses)
	{
		// In streaming mode, reconstruct the final message from the SSE event log we cached in StreamBuffer.
		// We parse the message_start / content_block_start / content_block_delta / content_block_stop events
		// to build the assistant message exactly.

		FClaudeMessage Assistant;
		Assistant.Role = EClaudeRole::Assistant;

		FString StopReason;

		// Per-block accumulators keyed by index
		TMap<int32, FClaudeContentBlock> BlocksByIndex;
		TMap<int32, FString> ToolJsonByIndex;

		TArray<FString> Lines;
		StreamBuffer.ParseIntoArrayLines(Lines, false);

		for (const FString& Line : Lines)
		{
			if (!Line.StartsWith(TEXT("data: "))) continue;
			const FString Payload = Line.RightChop(6).TrimStartAndEnd();
			if (Payload.IsEmpty() || Payload == TEXT("[DONE]")) continue;

			auto Obj = ParseJson(Payload);
			if (!Obj.IsValid()) continue;

			FString Type;
			Obj->TryGetStringField(TEXT("type"), Type);

			if (Type == TEXT("content_block_start"))
			{
				int32 Index = 0;
				Obj->TryGetNumberField(TEXT("index"), Index);
				const TSharedPtr<FJsonObject>* CB;
				if (Obj->TryGetObjectField(TEXT("content_block"), CB))
				{
					FString BType;
					(*CB)->TryGetStringField(TEXT("type"), BType);

					FClaudeContentBlock Block;
					if (BType == TEXT("text"))
					{
						Block.Type = EClaudeContentBlockType::Text;
					}
					else if (BType == TEXT("tool_use"))
					{
						Block.Type = EClaudeContentBlockType::ToolUse;
						(*CB)->TryGetStringField(TEXT("id"), Block.ToolUseId);
						(*CB)->TryGetStringField(TEXT("name"), Block.ToolName);
						ToolJsonByIndex.Add(Index, TEXT(""));
					}
					BlocksByIndex.Add(Index, Block);
				}
			}
			else if (Type == TEXT("content_block_delta"))
			{
				int32 Index = 0;
				Obj->TryGetNumberField(TEXT("index"), Index);
				const TSharedPtr<FJsonObject>* DeltaObj;
				if (Obj->TryGetObjectField(TEXT("delta"), DeltaObj))
				{
					FString DType;
					(*DeltaObj)->TryGetStringField(TEXT("type"), DType);
					FClaudeContentBlock* Block = BlocksByIndex.Find(Index);
					if (!Block) continue;

					if (DType == TEXT("text_delta"))
					{
						FString T;
						(*DeltaObj)->TryGetStringField(TEXT("text"), T);
						Block->Text += T;
					}
					else if (DType == TEXT("input_json_delta"))
					{
						FString PJ;
						(*DeltaObj)->TryGetStringField(TEXT("partial_json"), PJ);
						if (FString* Acc = ToolJsonByIndex.Find(Index)) *Acc += PJ;
					}
				}
			}
			else if (Type == TEXT("message_delta"))
			{
				const TSharedPtr<FJsonObject>* DeltaObj;
				if (Obj->TryGetObjectField(TEXT("delta"), DeltaObj))
				{
					(*DeltaObj)->TryGetStringField(TEXT("stop_reason"), StopReason);
				}
			}
		}

		// Finalize tool_use blocks: parse accumulated JSON
		for (auto& Pair : ToolJsonByIndex)
		{
			FClaudeContentBlock* Block = BlocksByIndex.Find(Pair.Key);
			if (!Block) continue;
			if (!Pair.Value.IsEmpty())
			{
				Block->ToolInput = ParseJson(Pair.Value);
			}
			if (!Block->ToolInput.IsValid())
			{
				Block->ToolInput = MakeShared<FJsonObject>();
			}
		}

		// Append in index order, filtering out blocks that ended up empty
		// (e.g. text block was started but model went straight to tool_use without any text_delta)
		TArray<int32> SortedKeys;
		BlocksByIndex.GetKeys(SortedKeys);
		SortedKeys.Sort();
		for (int32 K : SortedKeys)
		{
			const FClaudeContentBlock& B = BlocksByIndex[K];
			const bool bIsEmptyText = (B.Type == EClaudeContentBlockType::Text && B.Text.IsEmpty());
			const bool bIsEmptyThinking = (B.Type == EClaudeContentBlockType::Thinking && B.Text.IsEmpty());
			if (bIsEmptyText || bIsEmptyThinking) continue;
			Assistant.Blocks.Add(B);
		}

		// If the model produced literally nothing (shouldn't happen but defensive), skip the turn.
		if (Assistant.Blocks.Num() == 0)
		{
			UE_LOG(LogClaudeAgent, Warning, TEXT("Assistant message had zero content blocks — skipping."));
			bIsBusy = false;
			OnAgentTurnComplete.Broadcast();
			return;
		}

		Messages.Add(Assistant);
		OnMessageComplete.Broadcast(Assistant);

		// Continue agent loop?
		if (StopReason == TEXT("tool_use"))
		{
			RunToolUseLoop();
		}
		else
		{
			bIsBusy = false;
			OnAgentTurnComplete.Broadcast();
		}
	}
	else
	{
		// Non-streaming: parse the full response object
		ResponseObj = ParseJson(Body);
		if (!ResponseObj.IsValid())
		{
			OnError.Broadcast(TEXT("Could not parse API response JSON."));
			bIsBusy = false;
			OnAgentTurnComplete.Broadcast();
			return;
		}

		AppendAssistantMessageFromResponse(ResponseObj);

		FString StopReason;
		ResponseObj->TryGetStringField(TEXT("stop_reason"), StopReason);
		if (StopReason == TEXT("tool_use"))
		{
			RunToolUseLoop();
		}
		else
		{
			bIsBusy = false;
			OnAgentTurnComplete.Broadcast();
		}
	}
}

void FClaudeAPIClient::AppendAssistantMessageFromResponse(const TSharedPtr<FJsonObject>& ResponseObj)
{
	FClaudeMessage Assistant;
	Assistant.Role = EClaudeRole::Assistant;

	const TArray<TSharedPtr<FJsonValue>>* ContentArr;
	if (ResponseObj->TryGetArrayField(TEXT("content"), ContentArr))
	{
		for (const TSharedPtr<FJsonValue>& V : *ContentArr)
		{
			auto BObj = V->AsObject();
			if (!BObj.IsValid()) continue;

			FString Type;
			BObj->TryGetStringField(TEXT("type"), Type);

			FClaudeContentBlock Block;
			if (Type == TEXT("text"))
			{
				Block.Type = EClaudeContentBlockType::Text;
				BObj->TryGetStringField(TEXT("text"), Block.Text);
			}
			else if (Type == TEXT("tool_use"))
			{
				Block.Type = EClaudeContentBlockType::ToolUse;
				BObj->TryGetStringField(TEXT("id"), Block.ToolUseId);
				BObj->TryGetStringField(TEXT("name"), Block.ToolName);
				const TSharedPtr<FJsonObject>* InputObj;
				if (BObj->TryGetObjectField(TEXT("input"), InputObj))
				{
					Block.ToolInput = *InputObj;
				}
				else
				{
					Block.ToolInput = MakeShared<FJsonObject>();
				}
			}
			else
			{
				continue;
			}

			// Filter out empty text/thinking blocks
			const bool bIsEmptyText = (Block.Type == EClaudeContentBlockType::Text && Block.Text.IsEmpty());
			const bool bIsEmptyThinking = (Block.Type == EClaudeContentBlockType::Thinking && Block.Text.IsEmpty());
			if (bIsEmptyText || bIsEmptyThinking) continue;

			Assistant.Blocks.Add(Block);
		}
	}

	if (Assistant.Blocks.Num() == 0)
	{
		UE_LOG(LogClaudeAgent, Warning, TEXT("Assistant message had zero content blocks — skipping."));
		return;
	}

	Messages.Add(Assistant);
	OnMessageComplete.Broadcast(Assistant);
}

void FClaudeAPIClient::RunToolUseLoop()
{
	// Find the most recent assistant message and execute every tool_use block in it.
	if (Messages.Num() == 0) { bIsBusy = false; OnAgentTurnComplete.Broadcast(); return; }
	const FClaudeMessage& Last = Messages.Last();
	if (Last.Role != EClaudeRole::Assistant) { bIsBusy = false; OnAgentTurnComplete.Broadcast(); return; }

	FClaudeMessage ToolResultMsg;
	ToolResultMsg.Role = EClaudeRole::User;

	const UClaudeAgentSettings* S = UClaudeAgentSettings::Get();

	for (const FClaudeContentBlock& B : Last.Blocks)
	{
		if (B.Type != EClaudeContentBlockType::ToolUse) continue;

		OnToolUse.Broadcast(B);

		const FClaudeToolDefinition* Def = ToolRegistry->FindTool(B.ToolName);
		const bool bRequiresConfirm = (Def && !Def->bIsReadOnly && S->bConfirmBeforeWrites);

		if (bRequiresConfirm)
		{
			FString InputStr;
			auto W = TJsonWriterFactory<>::Create(&InputStr);
			FJsonSerializer::Serialize(B.ToolInput.ToSharedRef(), W);
			const FText Prompt = FText::FromString(FString::Printf(
				TEXT("Claude wants to call tool:\n\n%s\n\nInput:\n%s\n\nAllow?"),
				*B.ToolName, *InputStr));

			const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::YesNo, Prompt);
			if (Choice != EAppReturnType::Yes)
			{
				FClaudeToolResult Denied;
				Denied.bIsError = true;
				Denied.Content = TEXT("User denied this tool call.");
				ToolResultMsg.Blocks.Add(FClaudeContentBlock::MakeToolResult(B.ToolUseId, Denied.Content, true));
				OnToolResult.Broadcast(B.ToolUseId, Denied);
				continue;
			}
		}

		FClaudeToolResult Result = ToolRegistry->Execute(B.ToolName, B.ToolInput);

		FClaudeContentBlock ResultBlock = FClaudeContentBlock::MakeToolResult(B.ToolUseId, Result.Content, Result.bIsError);
		// If the tool returned an image, attach it so the model can actually see
		// the visual output on the next turn.
		if (!Result.ImageBase64.IsEmpty())
		{
			ResultBlock.ImageBase64 = Result.ImageBase64;
			ResultBlock.ImageMediaType = Result.ImageMediaType;
		}
		ToolResultMsg.Blocks.Add(ResultBlock);
		OnToolResult.Broadcast(B.ToolUseId, Result);
	}

	if (ToolResultMsg.Blocks.Num() > 0)
	{
		Messages.Add(ToolResultMsg);
		// Continue the loop with the new tool_result message
		DispatchRequest();
	}
	else
	{
		bIsBusy = false;
		OnAgentTurnComplete.Broadcast();
	}
}
