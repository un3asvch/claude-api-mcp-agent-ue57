// Copyright Untry. All Rights Reserved.

#include "ClaudeMCPServer.h"
#include "ClaudeToolRegistry.h"
#include "ClaudeAgentEditor.h"
#include "ClaudeAgentSettings.h"

#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Async/Async.h"
#include "Modules/ModuleManager.h"

namespace
{
	// MCP protocol version we implement
	const FString MCP_PROTOCOL_VERSION = TEXT("2024-11-05");
	const FString MCP_PATH = TEXT("/mcp");

	// JSON-RPC error codes (standard + MCP-specific)
	constexpr int32 RPC_PARSE_ERROR      = -32700;
	constexpr int32 RPC_INVALID_REQUEST  = -32600;
	constexpr int32 RPC_METHOD_NOT_FOUND = -32601;
	constexpr int32 RPC_INVALID_PARAMS   = -32602;
	constexpr int32 RPC_INTERNAL_ERROR   = -32603;
}

FClaudeMCPServer::FClaudeMCPServer() {}
FClaudeMCPServer::~FClaudeMCPServer() { Stop(); }

FString FClaudeMCPServer::SerializeJson(const TSharedRef<FJsonObject>& Obj)
{
	FString Out;
	auto Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Obj, Writer);
	return Out;
}

TSharedRef<FJsonObject> FClaudeMCPServer::MakeErrorResponse(int64 RequestId, int32 Code, const FString& Message)
{
	TSharedRef<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Resp->SetNumberField(TEXT("id"), RequestId);

	auto Err = MakeShared<FJsonObject>();
	Err->SetNumberField(TEXT("code"), Code);
	Err->SetStringField(TEXT("message"), Message);
	Resp->SetObjectField(TEXT("error"), Err);
	return Resp;
}

// -----------------------------------------------------------------------------
// Server lifecycle
// -----------------------------------------------------------------------------

bool FClaudeMCPServer::Start(int32 Port)
{
	if (bIsRunning && ListenPort == Port)
	{
		return true;
	}
	if (bIsRunning)
	{
		Stop();
	}

	ListenPort = Port;
	FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
	Router = HttpServerModule.GetHttpRouter(Port);
	if (!Router.IsValid())
	{
		UE_LOG(LogClaudeAgent, Error, TEXT("MCP: could not get HTTP router for port %d"), Port);
		return false;
	}

	RouteHandle = Router->BindRoute(
		FHttpPath(MCP_PATH),
		EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Req, const FHttpResultCallback& Cb)
		{
			return HandleMCPRequest(Req, Cb);
		}));

	HttpServerModule.StartAllListeners();
	bIsRunning = true;

	UE_LOG(LogClaudeAgent, Log, TEXT("MCP server listening on http://127.0.0.1:%d%s"), Port, *MCP_PATH);
	return true;
}

void FClaudeMCPServer::Stop()
{
	if (Router.IsValid() && RouteHandle.IsValid())
	{
		Router->UnbindRoute(RouteHandle);
		RouteHandle.Reset();
	}
	Router.Reset();
	bIsRunning = false;
}

// -----------------------------------------------------------------------------
// Request dispatch
// -----------------------------------------------------------------------------

bool FClaudeMCPServer::HandleMCPRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// GET on /mcp — we return a tiny health-check; MCP clients never GET unless doing SSE,
	// which we don't implement for notifications (we only reply to POSTs).
	if (Request.Verb == EHttpServerRequestVerbs::VERB_GET)
	{
		auto Resp = FHttpServerResponse::Create(TEXT("{\"status\":\"ok\",\"server\":\"ClaudeAgent-UE\"}"), TEXT("application/json"));
		OnComplete(MoveTemp(Resp));
		return true;
	}

	// Parse JSON-RPC body
	FString BodyStr = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Request.Body.GetData())));
	if (Request.Body.Num() < BodyStr.Len())
	{
		// Defensive: if the UTF8 conversion overshot, trim
		BodyStr.LeftInline(Request.Body.Num());
	}

	TSharedPtr<FJsonObject> RpcReq;
	auto Reader = TJsonReaderFactory<>::Create(BodyStr);
	if (!FJsonSerializer::Deserialize(Reader, RpcReq) || !RpcReq.IsValid())
	{
		auto Err = MakeErrorResponse(0, RPC_PARSE_ERROR, TEXT("JSON parse error"));
		auto Resp = FHttpServerResponse::Create(SerializeJson(Err), TEXT("application/json"));
		OnComplete(MoveTemp(Resp));
		return true;
	}

	// Extract id + method
	int64 RpcId = 0;
	RpcReq->TryGetNumberField(TEXT("id"), RpcId);

	FString Method;
	RpcReq->TryGetStringField(TEXT("method"), Method);

	const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
	RpcReq->TryGetObjectField(TEXT("params"), ParamsPtr);
	TSharedPtr<FJsonObject> Params = ParamsPtr ? *ParamsPtr : MakeShared<FJsonObject>();

	UE_LOG(LogClaudeAgent, Verbose, TEXT("MCP RPC: method=%s id=%lld"), *Method, RpcId);

	// Notifications have no id — we respond 202 Accepted with no body
	const bool bIsNotification = !RpcReq->HasField(TEXT("id"));

	// Dispatch
	if (Method == TEXT("initialize"))
	{
		auto Resp = BuildInitializeResponse(RpcId);
		auto HttpResp = FHttpServerResponse::Create(SerializeJson(Resp.ToSharedRef()), TEXT("application/json"));
		OnComplete(MoveTemp(HttpResp));
		return true;
	}
	else if (Method == TEXT("notifications/initialized") || Method.StartsWith(TEXT("notifications/")))
	{
		// Notification — no response body expected
		auto HttpResp = FHttpServerResponse::Create(TEXT(""), TEXT("application/json"));
		HttpResp->Code = EHttpServerResponseCodes::NoContent;
		OnComplete(MoveTemp(HttpResp));
		return true;
	}
	else if (Method == TEXT("tools/list"))
	{
		auto Resp = BuildToolsListResponse(RpcId);
		auto HttpResp = FHttpServerResponse::Create(SerializeJson(Resp.ToSharedRef()), TEXT("application/json"));
		OnComplete(MoveTemp(HttpResp));
		return true;
	}
	else if (Method == TEXT("tools/call"))
	{
		// Tool execution must happen on Game Thread. Hop there, run, then complete.
		// We need to capture the callback by value (safe — it's a TFunction wrapper).
		FHttpResultCallback CompleteCopy = OnComplete;
		HandleToolCall(Params, RpcId,
			[CompleteCopy](TSharedPtr<FJsonObject> Resp)
			{
				if (!Resp.IsValid())
				{
					Resp = MakeErrorResponse(0, RPC_INTERNAL_ERROR, TEXT("Tool returned null"));
				}
				auto HttpResp = FHttpServerResponse::Create(SerializeJson(Resp.ToSharedRef()), TEXT("application/json"));
				CompleteCopy(MoveTemp(HttpResp));
			});
		return true;
	}
	else if (Method == TEXT("ping"))
	{
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		R->SetNumberField(TEXT("id"), RpcId);
		R->SetObjectField(TEXT("result"), MakeShared<FJsonObject>());
		auto HttpResp = FHttpServerResponse::Create(SerializeJson(R), TEXT("application/json"));
		OnComplete(MoveTemp(HttpResp));
		return true;
	}
	else
	{
		auto Err = MakeErrorResponse(RpcId, RPC_METHOD_NOT_FOUND, FString::Printf(TEXT("Method not found: %s"), *Method));
		auto HttpResp = FHttpServerResponse::Create(SerializeJson(Err), TEXT("application/json"));
		OnComplete(MoveTemp(HttpResp));
		return true;
	}
}

// -----------------------------------------------------------------------------
// initialize
// -----------------------------------------------------------------------------

TSharedPtr<FJsonObject> FClaudeMCPServer::BuildInitializeResponse(int64 RequestId)
{
	TSharedRef<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Resp->SetNumberField(TEXT("id"), RequestId);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("protocolVersion"), MCP_PROTOCOL_VERSION);

	// Capabilities we support — just tools
	TSharedRef<FJsonObject> Caps = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> ToolsCap = MakeShared<FJsonObject>();
	ToolsCap->SetBoolField(TEXT("listChanged"), false);
	Caps->SetObjectField(TEXT("tools"), ToolsCap);
	Result->SetObjectField(TEXT("capabilities"), Caps);

	TSharedRef<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
	ServerInfo->SetStringField(TEXT("name"), TEXT("ClaudeAgent-UnrealEngine"));
	ServerInfo->SetStringField(TEXT("version"), TEXT("1.0.0"));
	Result->SetObjectField(TEXT("serverInfo"), ServerInfo);

	Resp->SetObjectField(TEXT("result"), Result);
	return Resp;
}

// -----------------------------------------------------------------------------
// tools/list
// -----------------------------------------------------------------------------

TSharedPtr<FJsonObject> FClaudeMCPServer::BuildToolsListResponse(int64 RequestId)
{
	TSharedRef<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Resp->SetNumberField(TEXT("id"), RequestId);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ToolsArr;

	if (ToolRegistry.IsValid())
	{
		const UClaudeAgentSettings* S = UClaudeAgentSettings::Get();
		for (const auto& Pair : ToolRegistry->GetAllTools())
		{
			const FClaudeToolDefinition& Def = Pair.Value;
			// Skip tools whose category is disabled in settings
			if (S && !S->IsCategoryEnabled(Def.Category))
			{
				continue;
			}
			TSharedRef<FJsonObject> ToolObj = MakeShared<FJsonObject>();
			ToolObj->SetStringField(TEXT("name"), Def.Name);
			ToolObj->SetStringField(TEXT("description"), Def.Description);
			if (Def.InputSchema.IsValid())
			{
				ToolObj->SetObjectField(TEXT("inputSchema"), Def.InputSchema);
			}
			ToolsArr.Add(MakeShared<FJsonValueObject>(ToolObj));
		}
	}

	Result->SetArrayField(TEXT("tools"), ToolsArr);
	Resp->SetObjectField(TEXT("result"), Result);
	return Resp;
}

// -----------------------------------------------------------------------------
// tools/call
// -----------------------------------------------------------------------------

void FClaudeMCPServer::HandleToolCall(
	const TSharedPtr<FJsonObject>& Params,
	int64 RequestId,
	TFunction<void(TSharedPtr<FJsonObject>)> ReplyCallback)
{
	FString ToolName;
	Params->TryGetStringField(TEXT("name"), ToolName);

	const TSharedPtr<FJsonObject>* ArgsPtr = nullptr;
	Params->TryGetObjectField(TEXT("arguments"), ArgsPtr);
	TSharedPtr<FJsonObject> Arguments = ArgsPtr ? *ArgsPtr : MakeShared<FJsonObject>();

	if (!ToolRegistry.IsValid())
	{
		ReplyCallback(MakeErrorResponse(RequestId, RPC_INTERNAL_ERROR, TEXT("Tool registry not available")));
		return;
	}
	if (!ToolRegistry->FindTool(ToolName))
	{
		ReplyCallback(MakeErrorResponse(RequestId, RPC_METHOD_NOT_FOUND, FString::Printf(TEXT("Unknown tool: %s"), *ToolName)));
		return;
	}

	// Hop to Game Thread — tools touch UObjects
	TSharedPtr<FClaudeToolRegistry> RegistryCopy = ToolRegistry;
	FString ToolNameCopy = ToolName;

	AsyncTask(ENamedThreads::GameThread, [RegistryCopy, ToolNameCopy, Arguments, RequestId, ReplyCallback]()
	{
		FClaudeToolResult Result = RegistryCopy->Execute(ToolNameCopy, Arguments);

		// Build MCP tools/call response
		TSharedRef<FJsonObject> Resp = MakeShared<FJsonObject>();
		Resp->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		Resp->SetNumberField(TEXT("id"), RequestId);

		TSharedRef<FJsonObject> ResultObj = MakeShared<FJsonObject>();

		// MCP content format: array of {type:"text", text:"..."}
		TArray<TSharedPtr<FJsonValue>> Content;
		TSharedRef<FJsonObject> TextBlock = MakeShared<FJsonObject>();
		TextBlock->SetStringField(TEXT("type"), TEXT("text"));
		TextBlock->SetStringField(TEXT("text"), Result.Content);
		Content.Add(MakeShared<FJsonValueObject>(TextBlock));

		ResultObj->SetArrayField(TEXT("content"), Content);
		if (Result.bIsError)
		{
			ResultObj->SetBoolField(TEXT("isError"), true);
		}

		Resp->SetObjectField(TEXT("result"), ResultObj);

		// Reply on the networking thread — HTTP callback should be safe from any thread
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [ReplyCallback, Resp]()
		{
			ReplyCallback(Resp);
		});
	});
}
