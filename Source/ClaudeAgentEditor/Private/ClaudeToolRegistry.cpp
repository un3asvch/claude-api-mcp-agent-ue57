// Copyright Untry. All Rights Reserved.

#include "ClaudeToolRegistry.h"
#include "ClaudeContextProvider.h"
#include "ClaudeAgentEditor.h"
#include "ClaudeAgentSettings.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/MessageDialog.h"

namespace
{
	/** Helper: build a property schema entry. */
	TSharedRef<FJsonObject> Prop(const FString& Type, const FString& Description)
	{
		auto P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), Type);
		P->SetStringField(TEXT("description"), Description);
		return P;
	}

	TSharedRef<FJsonObject> IntProp(const FString& Description, int32 Default)
	{
		auto P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), TEXT("integer"));
		P->SetStringField(TEXT("description"), Description);
		P->SetNumberField(TEXT("default"), Default);
		return P;
	}

	TSharedRef<FJsonObject> BoolProp(const FString& Description, bool Default)
	{
		auto P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), TEXT("boolean"));
		P->SetStringField(TEXT("description"), Description);
		P->SetBoolField(TEXT("default"), Default);
		return P;
	}

	TSharedRef<FJsonObject> MakeSchema(const TMap<FString, TSharedRef<FJsonObject>>& Props, const TArray<FString>& Required)
	{
		auto Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));

		auto PropertiesObj = MakeShared<FJsonObject>();
		for (const auto& Pair : Props)
		{
			PropertiesObj->SetObjectField(Pair.Key, Pair.Value);
		}
		Schema->SetObjectField(TEXT("properties"), PropertiesObj);

		TArray<TSharedPtr<FJsonValue>> Req;
		for (const FString& R : Required) Req.Add(MakeShared<FJsonValueString>(R));
		Schema->SetArrayField(TEXT("required"), Req);

		return Schema;
	}

	FString GetStr(const TSharedPtr<FJsonObject>& Input, const FString& Key, const FString& Default = TEXT(""))
	{
		if (!Input.IsValid()) return Default;
		FString V;
		if (Input->TryGetStringField(Key, V)) return V;
		return Default;
	}

	int32 GetInt(const TSharedPtr<FJsonObject>& Input, const FString& Key, int32 Default = 0)
	{
		if (!Input.IsValid()) return Default;
		int32 V;
		if (Input->TryGetNumberField(Key, V)) return V;
		return Default;
	}

	bool GetBool(const TSharedPtr<FJsonObject>& Input, const FString& Key, bool Default = false)
	{
		if (!Input.IsValid()) return Default;
		bool V;
		if (Input->TryGetBoolField(Key, V)) return V;
		return Default;
	}
}

FClaudeToolRegistry::FClaudeToolRegistry()
{
	RegisterDefaultTools();
}

void FClaudeToolRegistry::RegisterTool(const FClaudeToolDefinition& Tool)
{
	Tools.Add(Tool.Name, Tool);
}

const FClaudeToolDefinition* FClaudeToolRegistry::FindTool(const FString& Name) const
{
	return Tools.Find(Name);
}

TArray<TSharedPtr<FJsonValue>> FClaudeToolRegistry::BuildToolsJson() const
{
	const UClaudeAgentSettings* S = UClaudeAgentSettings::Get();
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const auto& Pair : Tools)
	{
		const FClaudeToolDefinition& T = Pair.Value;
		// Skip tools whose category is disabled in settings
		if (S && !S->IsCategoryEnabled(T.Category))
		{
			continue;
		}
		auto ToolObj = MakeShared<FJsonObject>();
		ToolObj->SetStringField(TEXT("name"), T.Name);
		ToolObj->SetStringField(TEXT("description"), T.Description);
		if (T.InputSchema.IsValid())
		{
			ToolObj->SetObjectField(TEXT("input_schema"), T.InputSchema);
		}
		Out.Add(MakeShared<FJsonValueObject>(ToolObj));
	}
	return Out;
}

FClaudeToolResult FClaudeToolRegistry::Execute(const FString& Name, const TSharedPtr<FJsonObject>& Input) const
{
	const FClaudeToolDefinition* T = FindTool(Name);
	if (!T)
	{
		FClaudeToolResult R;
		R.bIsError = true;
		R.Content = FString::Printf(TEXT("Unknown tool: %s"), *Name);
		return R;
	}

	UE_LOG(LogClaudeAgent, Log, TEXT("Executing tool: %s (read_only=%d)"), *Name, T->bIsReadOnly ? 1 : 0);
	return T->Handler.Execute(Input);
}

// -----------------------------------------------------------------------------
// Default tool registration
// -----------------------------------------------------------------------------

void FClaudeToolRegistry::RegisterDefaultTools()
{
	// list_assets ---------------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("list_assets");
		T.Description = TEXT("List assets in the project. Filter by class name (e.g. 'Blueprint', 'AnimBlueprint', 'Material', 'StaticMesh', 'SkeletalMesh', 'DataAsset') and/or content path (e.g. '/Game/Characters'). Returns asset names, paths, and classes.");
		T.InputSchema = MakeSchema({
			{ TEXT("class_name"), Prop(TEXT("string"), TEXT("Optional class name filter, e.g. 'Blueprint'")) },
			{ TEXT("path_filter"), Prop(TEXT("string"), TEXT("Optional content path, e.g. '/Game/Characters'")) },
			{ TEXT("max_results"), IntProp(TEXT("Cap on results returned (default 50)"), 50) },
		}, {});
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::General;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::ListAssets(GetStr(In, TEXT("class_name")), GetStr(In, TEXT("path_filter")), GetInt(In, TEXT("max_results"), 50));
			return R;
		});
		RegisterTool(T);
	}

	// get_asset_references ------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_asset_references");
		T.Description = TEXT("Get references for an asset. Set 'reverse' to true to find what REFERENCES this asset, false to find what this asset DEPENDS ON.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Full asset path, e.g. '/Game/Characters/CBP_Player.CBP_Player'")) },
			{ TEXT("reverse"), BoolProp(TEXT("True = find referencers; false = find dependencies"), true) },
		}, { TEXT("asset_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetAssetReferences(GetStr(In, TEXT("asset_path")), GetBool(In, TEXT("reverse"), true));
			return R;
		});
		RegisterTool(T);
	}

	// get_object_properties ----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_object_properties");
		T.Description = TEXT("Get all UPROPERTY values for an asset. For Blueprints, returns the CDO (default values) of the generated class. Useful for inspecting Data Assets, default actor values, settings.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Full asset path")) },
			{ TEXT("max_depth"), IntProp(TEXT("Recursion depth for nested objects (default 1)"), 1) },
		}, { TEXT("asset_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetObjectProperties(GetStr(In, TEXT("asset_path")), GetInt(In, TEXT("max_depth"), 1));
			return R;
		});
		RegisterTool(T);
	}

	// get_blueprint_graph ------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_blueprint_graph");
		T.Description = TEXT("Read the structure of a Blueprint graph: nodes, pins, connections. If graph_name is empty, returns ALL graphs (event graphs, functions, macros). Heavy — prefer naming a specific graph for big BPs.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Optional specific graph name (e.g. 'EventGraph', 'MyFunction')")) },
		}, { TEXT("asset_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetBlueprintGraph(GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("graph_name")));
			return R;
		});
		RegisterTool(T);
	}

	// list_blueprint_functions -------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("list_blueprint_functions");
		T.Description = TEXT("List names of all functions, macros, and event graphs in a Blueprint. Use this BEFORE calling get_blueprint_graph on a large BP to find the right graph_name.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
		}, { TEXT("asset_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::ListBlueprintFunctions(GetStr(In, TEXT("asset_path")));
			return R;
		});
		RegisterTool(T);
	}

	// get_anim_blueprint_info --------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_anim_blueprint_info");
		T.Description = TEXT("Get top-level info about an Animation Blueprint: target skeleton, parent class, function graphs.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Animation Blueprint asset path")) },
		}, { TEXT("asset_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetAnimBlueprintInfo(GetStr(In, TEXT("asset_path")));
			return R;
		});
		RegisterTool(T);
	}

	// get_editor_selection -----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_editor_selection");
		T.Description = TEXT("Returns whatever the user currently has selected in the level viewport.");
		T.InputSchema = MakeSchema({}, {});
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>&)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetEditorSelection();
			return R;
		});
		RegisterTool(T);
	}

	// read_output_log ----------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("read_output_log");
		T.Description = TEXT("Read the project's output log file. Optionally filter by substring (e.g. 'Warning', 'LogBlueprint'). Returns the LAST N matching lines.");
		T.InputSchema = MakeSchema({
			{ TEXT("filter"), Prop(TEXT("string"), TEXT("Optional substring filter")) },
			{ TEXT("last_n_lines"), IntProp(TEXT("How many lines to return from the end (default 100)"), 100) },
		}, {});
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::ReadOutputLog(GetStr(In, TEXT("filter")), GetInt(In, TEXT("last_n_lines"), 100));
			return R;
		});
		RegisterTool(T);
	}

	// get_project_info ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_project_info");
		T.Description = TEXT("Get basic project info: name, engine version, platform, project directory.");
		T.InputSchema = MakeSchema({}, {});
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::General;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>&)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetProjectInfo();
			return R;
		});
		RegisterTool(T);
	}

	// ===== WRITE TOOLS — require confirmation ===========================

	// set_object_property ------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_object_property");
		T.Description = TEXT("Set a UPROPERTY value on an asset (or its CDO if it's a Blueprint). Value must be a string in Unreal's text format (e.g. 'true', '60.0', '(X=1.0,Y=0.0,Z=0.0)'). REQUIRES USER CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Asset path")) },
			{ TEXT("property_name"), Prop(TEXT("string"), TEXT("Exact UPROPERTY name")) },
			{ TEXT("new_value"), Prop(TEXT("string"), TEXT("New value as Unreal-formatted text")) },
		}, { TEXT("asset_path"), TEXT("property_name"), TEXT("new_value") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			const FString AssetPath = GetStr(In, TEXT("asset_path"));
			const FString PropName  = GetStr(In, TEXT("property_name"));
			const FString NewValue  = GetStr(In, TEXT("new_value"));

			R.bRequiresUserConfirmation = true;
			R.ConfirmationPrompt = FString::Printf(TEXT("Claude wants to write:\n\n%s.%s = %s\n\nAllow?"), *AssetPath, *PropName, *NewValue);

			FString Err;
			const FString Msg = FClaudeContextProvider::SetObjectProperty(AssetPath, PropName, NewValue, Err);
			if (!Err.IsEmpty())
			{
				R.bIsError = true;
				R.Content = Err;
			}
			else
			{
				R.Content = Msg;
			}
			return R;
		});
		RegisterTool(T);
	}

	// compile_blueprint --------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("compile_blueprint");
		T.Description = TEXT("Compile a Blueprint. Returns error/warning counts and messages. REQUIRES USER CONFIRMATION (because it can dirty the asset and trigger re-instancing).");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
		}, { TEXT("asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::General;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.bRequiresUserConfirmation = true;
			R.ConfirmationPrompt = FString::Printf(TEXT("Compile Blueprint:\n%s ?"), *GetStr(In, TEXT("asset_path")));

			FString Err;
			R.Content = FClaudeContextProvider::CompileBlueprint(GetStr(In, TEXT("asset_path")), Err);
			if (!Err.IsEmpty())
			{
				R.bIsError = true;
				R.Content = Err;
			}
			return R;
		});
		RegisterTool(T);
	}

	// create_blueprint ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_blueprint");
		T.Description = TEXT("Create a new Blueprint asset from a parent class. ParentClass can be short name (Actor, Pawn, Character, ActorComponent) or full path. Returns path to the new BP. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("parent_class"), Prop(TEXT("string"), TEXT("Parent class, e.g. 'Actor' or '/Script/Engine.Character'")) },
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("Full path without .uasset, e.g. '/Game/Blueprints/BP_Thing'")) },
		}, { TEXT("parent_class"), TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			FString Err;
			R.Content = FClaudeContextProvider::CreateBlueprint(GetStr(In, TEXT("parent_class")), GetStr(In, TEXT("new_asset_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_blueprint_variable ---------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_blueprint_variable");
		T.Description = TEXT("Add a member variable to a Blueprint. Supports scalar types (bool/int/int64/float/double/string/name/text), structs (vector/rotator/transform/vector2d/color), or class references. Optional container_type (array/set/map), default_value, category, and visibility flags. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("var_name"), Prop(TEXT("string"), TEXT("Variable name")) },
			{ TEXT("var_type"), Prop(TEXT("string"), TEXT("Type: bool/int/int64/float/double/string/name/text/vector/rotator/transform/vector2d/color/<ClassName>")) },
			{ TEXT("container_type"), Prop(TEXT("string"), TEXT("Optional: empty/array/set/map")) },
			{ TEXT("default_value"), Prop(TEXT("string"), TEXT("Optional default value in Unreal text format")) },
			{ TEXT("category"), Prop(TEXT("string"), TEXT("Optional category label for the Details panel")) },
			{ TEXT("instance_editable"), BoolProp(TEXT("Allow editing per-instance in the level (default true)"), true) },
			{ TEXT("blueprint_read_only"), BoolProp(TEXT("Prevent SET nodes in graphs (default false)"), false) },
		}, { TEXT("asset_path"), TEXT("var_name"), TEXT("var_type") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			FString Err;
			R.Content = FClaudeContextProvider::AddBlueprintVariable(
				GetStr(In, TEXT("asset_path")),
				GetStr(In, TEXT("var_name")),
				GetStr(In, TEXT("var_type")),
				GetStr(In, TEXT("container_type")),
				GetStr(In, TEXT("default_value")),
				GetStr(In, TEXT("category")),
				GetBool(In, TEXT("instance_editable"), true),
				GetBool(In, TEXT("blueprint_read_only"), false),
				Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_component_to_blueprint -----------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_component_to_blueprint");
		T.Description = TEXT("Add a component to an Actor-based Blueprint via SCS. Optionally attach to an existing component by name (nested hierarchy). REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("component_class"), Prop(TEXT("string"), TEXT("Component class, e.g. 'StaticMeshComponent', 'PointLightComponent'")) },
			{ TEXT("component_name"), Prop(TEXT("string"), TEXT("Name for the new component")) },
			{ TEXT("attach_parent"), Prop(TEXT("string"), TEXT("Optional: name of existing component to attach under")) },
		}, { TEXT("asset_path"), TEXT("component_class"), TEXT("component_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			FString Err;
			R.Content = FClaudeContextProvider::AddComponentToBlueprint(
				GetStr(In, TEXT("asset_path")),
				GetStr(In, TEXT("component_class")),
				GetStr(In, TEXT("component_name")),
				GetStr(In, TEXT("attach_parent")),
				Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// set_component_property ---------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_component_property");
		T.Description = TEXT("Set a UPROPERTY on a component's template inside a Blueprint. E.g. set StaticMesh on a StaticMeshComponent: value = asset path like '/Engine/BasicShapes/Cube.Cube'. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("component_name"), Prop(TEXT("string"), TEXT("Component name inside the BP")) },
			{ TEXT("property_name"), Prop(TEXT("string"), TEXT("UPROPERTY name on the component")) },
			{ TEXT("new_value"), Prop(TEXT("string"), TEXT("Value in Unreal text format (for assets use full path, for vectors '(X=1,Y=2,Z=3)')")) },
		}, { TEXT("asset_path"), TEXT("component_name"), TEXT("property_name"), TEXT("new_value") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			FString Err;
			R.Content = FClaudeContextProvider::SetComponentProperty(
				GetStr(In, TEXT("asset_path")),
				GetStr(In, TEXT("component_name")),
				GetStr(In, TEXT("property_name")),
				GetStr(In, TEXT("new_value")),
				Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// save_asset ---------------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("save_asset");
		T.Description = TEXT("Save a dirty asset to disk. For Blueprints, also compiles before saving. Call this after making changes to persist them. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Asset path")) },
		}, { TEXT("asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::General;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			FString Err;
			R.Content = FClaudeContextProvider::SaveAsset(GetStr(In, TEXT("asset_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// spawn_actor --------------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("spawn_actor");
		T.Description = TEXT("Spawn an actor in the current editor level at a given location. ClassPath can be a BP (e.g. '/Game/Blueprints/BP_Thing.BP_Thing') or a native class. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("class_path"), Prop(TEXT("string"), TEXT("Actor class path")) },
			{ TEXT("x"), Prop(TEXT("number"), TEXT("World X")) },
			{ TEXT("y"), Prop(TEXT("number"), TEXT("World Y")) },
			{ TEXT("z"), Prop(TEXT("number"), TEXT("World Z")) },
		}, { TEXT("class_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			FString Err;
			double X = 0, Y = 0, Z = 0;
			In->TryGetNumberField(TEXT("x"), X);
			In->TryGetNumberField(TEXT("y"), Y);
			In->TryGetNumberField(TEXT("z"), Z);
			R.Content = FClaudeContextProvider::SpawnActor(
				GetStr(In, TEXT("class_path")),
				FVector(X, Y, Z),
				Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// get_actor_component_tree -------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_actor_component_tree");
		T.Description = TEXT("Read the component tree of a Blueprint (via SCS) or a live Actor. Returns component names, classes, and parent relationships.");
		T.InputSchema = MakeSchema({
			{ TEXT("path"), Prop(TEXT("string"), TEXT("Blueprint asset path or live actor path")) },
		}, { TEXT("path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Level;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetActorComponentTree(GetStr(In, TEXT("path")));
			return R;
		});
		RegisterTool(T);
	}

	// ===== NEW: deep graph reading ======================================

	// get_node_details ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_node_details");
		T.Description = TEXT("Deep inspection of a single Blueprint node by its GUID (which you get from get_blueprint_graph). Returns the node's UPROPERTY values, useful for inspecting K2Node settings, macro instances, cast nodes, etc.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Graph containing the node")) },
			{ TEXT("node_guid"), Prop(TEXT("string"), TEXT("Node GUID (from get_blueprint_graph output)")) },
		}, { TEXT("asset_path"), TEXT("graph_name"), TEXT("node_guid") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetNodeDetails(
				GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("graph_name")), GetStr(In, TEXT("node_guid")));
			return R;
		});
		RegisterTool(T);
	}

	// ===== NEW: graph editing ===========================================

	// add_function_node --------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_function_node");
		T.Description = TEXT("Add a K2Node_CallFunction node to a Blueprint graph. Returns the new NodeGuid which you can use in connect_pins. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Target graph, e.g. 'EventGraph'")) },
			{ TEXT("function_class"), Prop(TEXT("string"), TEXT("Class that owns the function, e.g. 'Actor', 'KismetSystemLibrary', '/Script/Engine.Actor'")) },
			{ TEXT("function_name"), Prop(TEXT("string"), TEXT("Exact UFUNCTION name, e.g. 'K2_SetActorLocation', 'PrintString'")) },
			{ TEXT("x"), IntProp(TEXT("X position in graph (default 0)"), 0) },
			{ TEXT("y"), IntProp(TEXT("Y position in graph (default 0)"), 0) },
		}, { TEXT("asset_path"), TEXT("graph_name"), TEXT("function_class"), TEXT("function_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			FString Err;
			R.Content = FClaudeContextProvider::AddFunctionCallNode(
				GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("graph_name")),
				GetStr(In, TEXT("function_class")), GetStr(In, TEXT("function_name")),
				GetInt(In, TEXT("x")), GetInt(In, TEXT("y")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// connect_pins -------------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("connect_pins");
		T.Description = TEXT("Wire two pins in a Blueprint graph together. Use node GUIDs from get_blueprint_graph and pin names. Schema validates type compatibility. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Graph name")) },
			{ TEXT("from_node_guid"), Prop(TEXT("string"), TEXT("Source node GUID")) },
			{ TEXT("from_pin_name"), Prop(TEXT("string"), TEXT("Source pin name (usually output pin)")) },
			{ TEXT("to_node_guid"), Prop(TEXT("string"), TEXT("Target node GUID")) },
			{ TEXT("to_pin_name"), Prop(TEXT("string"), TEXT("Target pin name (usually input pin)")) },
		}, { TEXT("asset_path"), TEXT("graph_name"), TEXT("from_node_guid"), TEXT("from_pin_name"), TEXT("to_node_guid"), TEXT("to_pin_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			FString Err;
			R.Content = FClaudeContextProvider::ConnectBlueprintPins(
				GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("graph_name")),
				GetStr(In, TEXT("from_node_guid")), GetStr(In, TEXT("from_pin_name")),
				GetStr(In, TEXT("to_node_guid")), GetStr(In, TEXT("to_pin_name")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// delete_node --------------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("delete_node");
		T.Description = TEXT("Delete a node from a Blueprint graph by GUID. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Graph name")) },
			{ TEXT("node_guid"), Prop(TEXT("string"), TEXT("Node GUID")) },
		}, { TEXT("asset_path"), TEXT("graph_name"), TEXT("node_guid") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			FString Err;
			R.Content = FClaudeContextProvider::DeleteBlueprintNode(
				GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("graph_name")),
				GetStr(In, TEXT("node_guid")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== NEW: delete parity ===========================================

	// remove_blueprint_variable ------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("remove_blueprint_variable");
		T.Description = TEXT("Remove a member variable from a Blueprint. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("var_name"), Prop(TEXT("string"), TEXT("Variable name")) },
		}, { TEXT("asset_path"), TEXT("var_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			FString Err;
			R.Content = FClaudeContextProvider::RemoveBlueprintVariable(
				GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("var_name")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// remove_component_from_blueprint ------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("remove_component_from_blueprint");
		T.Description = TEXT("Remove a component from a Blueprint by its component name. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("component_name"), Prop(TEXT("string"), TEXT("Component name to remove")) },
		}, { TEXT("asset_path"), TEXT("component_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			FString Err;
			R.Content = FClaudeContextProvider::RemoveComponentFromBlueprint(
				GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("component_name")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// destroy_actor ------------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("destroy_actor");
		T.Description = TEXT("Destroy an actor in the current editor level by its path or label. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_path"), Prop(TEXT("string"), TEXT("Actor path or label")) },
		}, { TEXT("actor_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			FString Err;
			R.Content = FClaudeContextProvider::DestroyActor(GetStr(In, TEXT("actor_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== NEW: live actor ops ==========================================

	// set_actor_transform ------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_actor_transform");
		T.Description = TEXT("Move/rotate/scale a live actor in the editor viewport. Omit a component (leave its fields absent) to leave it unchanged. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_path"), Prop(TEXT("string"), TEXT("Actor path or label")) },
			{ TEXT("loc_x"), Prop(TEXT("number"), TEXT("Location X (optional)")) },
			{ TEXT("loc_y"), Prop(TEXT("number"), TEXT("Location Y (optional)")) },
			{ TEXT("loc_z"), Prop(TEXT("number"), TEXT("Location Z (optional)")) },
			{ TEXT("rot_pitch"), Prop(TEXT("number"), TEXT("Rotation pitch (optional)")) },
			{ TEXT("rot_yaw"), Prop(TEXT("number"), TEXT("Rotation yaw (optional)")) },
			{ TEXT("rot_roll"), Prop(TEXT("number"), TEXT("Rotation roll (optional)")) },
			{ TEXT("scale_x"), Prop(TEXT("number"), TEXT("Scale X (optional)")) },
			{ TEXT("scale_y"), Prop(TEXT("number"), TEXT("Scale Y (optional)")) },
			{ TEXT("scale_z"), Prop(TEXT("number"), TEXT("Scale Z (optional)")) },
		}, { TEXT("actor_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Level;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			FString Err;

			auto HasField = [&](const FString& Key) { return In.IsValid() && In->HasField(Key); };

			const bool bSetLoc = HasField(TEXT("loc_x")) || HasField(TEXT("loc_y")) || HasField(TEXT("loc_z"));
			const bool bSetRot = HasField(TEXT("rot_pitch")) || HasField(TEXT("rot_yaw")) || HasField(TEXT("rot_roll"));
			const bool bSetScl = HasField(TEXT("scale_x")) || HasField(TEXT("scale_y")) || HasField(TEXT("scale_z"));

			double LX=0,LY=0,LZ=0,P=0,Y=0,Rl=0,SX=1,SY=1,SZ=1;
			if (In.IsValid())
			{
				In->TryGetNumberField(TEXT("loc_x"), LX);    In->TryGetNumberField(TEXT("loc_y"), LY);    In->TryGetNumberField(TEXT("loc_z"), LZ);
				In->TryGetNumberField(TEXT("rot_pitch"), P); In->TryGetNumberField(TEXT("rot_yaw"), Y);   In->TryGetNumberField(TEXT("rot_roll"), Rl);
				In->TryGetNumberField(TEXT("scale_x"), SX);  In->TryGetNumberField(TEXT("scale_y"), SY);  In->TryGetNumberField(TEXT("scale_z"), SZ);
			}

			R.Content = FClaudeContextProvider::SetActorTransform(
				GetStr(In, TEXT("actor_path")),
				bSetLoc, FVector(LX, LY, LZ),
				bSetRot, FRotator(P, Y, Rl),
				bSetScl, FVector(SX, SY, SZ),
				Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// set_actor_property -------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_actor_property");
		T.Description = TEXT("Set a UPROPERTY value on a live actor (not the Blueprint CDO — modifies the specific instance in the level). REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_path"), Prop(TEXT("string"), TEXT("Actor path or label")) },
			{ TEXT("property_name"), Prop(TEXT("string"), TEXT("UPROPERTY name")) },
			{ TEXT("new_value"), Prop(TEXT("string"), TEXT("New value in Unreal text format")) },
		}, { TEXT("actor_path"), TEXT("property_name"), TEXT("new_value") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Level;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			FString Err;
			R.Content = FClaudeContextProvider::SetActorProperty(
				GetStr(In, TEXT("actor_path")),
				GetStr(In, TEXT("property_name")),
				GetStr(In, TEXT("new_value")),
				Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// find_actors_in_level -----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("find_actors_in_level");
		T.Description = TEXT("List actors currently in the editor level, optionally filtered by class. Returns labels, classes, paths, and locations.");
		T.InputSchema = MakeSchema({
			{ TEXT("class_filter"), Prop(TEXT("string"), TEXT("Optional class name/path filter (e.g. 'StaticMeshActor', '/Script/Engine.Light')")) },
			{ TEXT("max_results"), IntProp(TEXT("Cap on results (default 50)"), 50) },
		}, {});
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Level;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::FindActorsInLevel(
				GetStr(In, TEXT("class_filter")),
				GetInt(In, TEXT("max_results"), 50));
			return R;
		});
		RegisterTool(T);
	}

	// ===== NEW: Blueprint types + interfaces ============================

	// create_widget_blueprint --------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_widget_blueprint");
		T.Description = TEXT("Create a UMG Widget Blueprint (WBP) at the given path. Returns path to new asset. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/UI/WBP_MainMenu'")) },
		}, { TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateWidgetBlueprint(GetStr(In, TEXT("new_asset_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// create_anim_blueprint ----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_anim_blueprint");
		T.Description = TEXT("Create an Animation Blueprint bound to a specific skeleton. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/Animations/ABP_Player'")) },
			{ TEXT("skeleton_path"), Prop(TEXT("string"), TEXT("Path to USkeleton, e.g. '/Game/Mannequin/SK_Mannequin_Skeleton.SK_Mannequin_Skeleton'")) },
		}, { TEXT("new_asset_path"), TEXT("skeleton_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateAnimBlueprint(GetStr(In, TEXT("new_asset_path")), GetStr(In, TEXT("skeleton_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// create_blueprint_interface -----------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_blueprint_interface");
		T.Description = TEXT("Create an empty Blueprint Interface asset. Add functions via Kismet editor or by adding entries. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/Interfaces/BPI_Interactable'")) },
		}, { TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateBlueprintInterface(GetStr(In, TEXT("new_asset_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// implement_interface ------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("implement_interface");
		T.Description = TEXT("Make an existing Blueprint implement a Blueprint Interface. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("blueprint_path"), Prop(TEXT("string"), TEXT("Blueprint that will implement the interface")) },
			{ TEXT("interface_path"), Prop(TEXT("string"), TEXT("Path to the Blueprint Interface asset")) },
		}, { TEXT("blueprint_path"), TEXT("interface_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::ImplementInterface(GetStr(In, TEXT("blueprint_path")), GetStr(In, TEXT("interface_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_blueprint_function ---------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_blueprint_function");
		T.Description = TEXT("Add a new empty function graph to a Blueprint. After creation, use get_blueprint_graph to see the Entry/Result nodes and add nodes to the function body. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("function_name"), Prop(TEXT("string"), TEXT("New function name")) },
		}, { TEXT("asset_path"), TEXT("function_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddBlueprintFunction(GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("function_name")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== NEW: Graph node creation — the core of BP logic ==============

	// add_variable_node --------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_variable_node");
		T.Description = TEXT("Add a Get or Set node for a member variable to a graph. Returns the new NodeGuid. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Target graph, e.g. 'EventGraph'")) },
			{ TEXT("var_name"), Prop(TEXT("string"), TEXT("Existing member variable name")) },
			{ TEXT("setter"), BoolProp(TEXT("true = Set node (writes), false = Get node (reads)"), false) },
			{ TEXT("x"), IntProp(TEXT("X position"), 0) },
			{ TEXT("y"), IntProp(TEXT("Y position"), 0) },
		}, { TEXT("asset_path"), TEXT("graph_name"), TEXT("var_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddVariableNode(
				GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("graph_name")),
				GetStr(In, TEXT("var_name")), GetBool(In, TEXT("setter"), false),
				GetInt(In, TEXT("x")), GetInt(In, TEXT("y")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_branch_node ----------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_branch_node");
		T.Description = TEXT("Add a Branch (if/else) node to a graph. Pins: Condition (bool in), True (exec out), False (exec out). REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Target graph")) },
			{ TEXT("x"), IntProp(TEXT("X position"), 0) },
			{ TEXT("y"), IntProp(TEXT("Y position"), 0) },
		}, { TEXT("asset_path"), TEXT("graph_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddBranchNode(
				GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("graph_name")),
				GetInt(In, TEXT("x")), GetInt(In, TEXT("y")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_sequence_node --------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_sequence_node");
		T.Description = TEXT("Add a Sequence node (fires outputs 0,1,2,... in order). REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Target graph")) },
			{ TEXT("num_outputs"), IntProp(TEXT("Number of output pins (default 2)"), 2) },
			{ TEXT("x"), IntProp(TEXT("X position"), 0) },
			{ TEXT("y"), IntProp(TEXT("Y position"), 0) },
		}, { TEXT("asset_path"), TEXT("graph_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddSequenceNode(
				GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("graph_name")),
				GetInt(In, TEXT("num_outputs"), 2),
				GetInt(In, TEXT("x")), GetInt(In, TEXT("y")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_custom_event ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_custom_event");
		T.Description = TEXT("Add a Custom Event node to the Event Graph. Call this event from other graphs with a CallFunction node of the same name. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Event graph name (usually 'EventGraph')")) },
			{ TEXT("event_name"), Prop(TEXT("string"), TEXT("Name for the custom event")) },
			{ TEXT("x"), IntProp(TEXT("X position"), 0) },
			{ TEXT("y"), IntProp(TEXT("Y position"), 0) },
		}, { TEXT("asset_path"), TEXT("graph_name"), TEXT("event_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddCustomEventNode(
				GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("graph_name")),
				GetStr(In, TEXT("event_name")),
				GetInt(In, TEXT("x")), GetInt(In, TEXT("y")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_make_break_struct ----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_make_break_struct");
		T.Description = TEXT("Add a Make<Struct> or Break<Struct> node. Common: vector, rotator, transform, vector2d, color. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Target graph")) },
			{ TEXT("struct_name"), Prop(TEXT("string"), TEXT("Struct name (vector/rotator/transform/...) or full path")) },
			{ TEXT("make"), BoolProp(TEXT("true = Make, false = Break"), true) },
			{ TEXT("x"), IntProp(TEXT("X position"), 0) },
			{ TEXT("y"), IntProp(TEXT("Y position"), 0) },
		}, { TEXT("asset_path"), TEXT("graph_name"), TEXT("struct_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddMakeBreakStructNode(
				GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("graph_name")),
				GetStr(In, TEXT("struct_name")), GetBool(In, TEXT("make"), true),
				GetInt(In, TEXT("x")), GetInt(In, TEXT("y")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_cast_node ------------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_cast_node");
		T.Description = TEXT("Add a Cast To<Class> node. Use pure_cast=false for normal exec cast (with Success/Failed pins), true for pure value cast. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Target graph")) },
			{ TEXT("target_class"), Prop(TEXT("string"), TEXT("Class to cast to (native class name, BP class, or full path)")) },
			{ TEXT("pure_cast"), BoolProp(TEXT("Pure cast (no exec pins)"), false) },
			{ TEXT("x"), IntProp(TEXT("X position"), 0) },
			{ TEXT("y"), IntProp(TEXT("Y position"), 0) },
		}, { TEXT("asset_path"), TEXT("graph_name"), TEXT("target_class") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddCastNode(
				GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("graph_name")),
				GetStr(In, TEXT("target_class")), GetBool(In, TEXT("pure_cast"), false),
				GetInt(In, TEXT("x")), GetInt(In, TEXT("y")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_self_node ------------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_self_node");
		T.Description = TEXT("Add a Self reference node. Outputs 'this' actor/BP. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Target graph")) },
			{ TEXT("x"), IntProp(TEXT("X position"), 0) },
			{ TEXT("y"), IntProp(TEXT("Y position"), 0) },
		}, { TEXT("asset_path"), TEXT("graph_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddSelfNode(
				GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("graph_name")),
				GetInt(In, TEXT("x")), GetInt(In, TEXT("y")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// set_pin_default_value ----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_pin_default_value");
		T.Description = TEXT("Set the default (literal) value of a pin that isn't connected. E.g. hardcode 100.0 into a float input pin. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Graph name")) },
			{ TEXT("node_guid"), Prop(TEXT("string"), TEXT("Target node GUID")) },
			{ TEXT("pin_name"), Prop(TEXT("string"), TEXT("Pin name")) },
			{ TEXT("new_value"), Prop(TEXT("string"), TEXT("Default value as Unreal-formatted text")) },
		}, { TEXT("asset_path"), TEXT("graph_name"), TEXT("node_guid"), TEXT("pin_name"), TEXT("new_value") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Blueprint;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SetPinDefaultValue(
				GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("graph_name")),
				GetStr(In, TEXT("node_guid")), GetStr(In, TEXT("pin_name")),
				GetStr(In, TEXT("new_value")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== NEW: Editor actions ==========================================

	// open_asset_editor --------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("open_asset_editor");
		T.Description = TEXT("Open the standard UE editor window for an asset (Blueprint, Behavior Tree, Blackboard, AnimBP, Material, Widget, Level, etc). Useful before editing BTs since the BT editor graph needs to be active.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Asset path")) },
		}, { TEXT("asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::General;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::OpenAssetEditor(GetStr(In, TEXT("asset_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== NEW: AI — Behavior Tree & Blackboard =========================

	// create_blackboard --------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_blackboard");
		T.Description = TEXT("Create a new Blackboard Data asset. Optionally chain to a parent blackboard to inherit keys. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/AI/BB_Enemy'")) },
			{ TEXT("parent_blackboard"), Prop(TEXT("string"), TEXT("Optional parent BB asset path")) },
		}, { TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::BehaviorTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateBlackboard(
				GetStr(In, TEXT("new_asset_path")),
				GetStr(In, TEXT("parent_blackboard")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_blackboard_key -------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_blackboard_key");
		T.Description = TEXT("Add a typed key to a Blackboard. Types: bool/int/float/string/name/vector/rotator/object/class/enum. For object/class/enum, specify key_type_object as the base class or enum path. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("blackboard_path"), Prop(TEXT("string"), TEXT("Blackboard asset path")) },
			{ TEXT("key_name"), Prop(TEXT("string"), TEXT("Key name, e.g. 'TargetActor'")) },
			{ TEXT("key_type"), Prop(TEXT("string"), TEXT("bool/int/float/string/name/vector/rotator/object/class/enum")) },
			{ TEXT("key_type_object"), Prop(TEXT("string"), TEXT("For object/class: base class path (e.g. 'Actor'). For enum: enum path.")) },
			{ TEXT("instance_synced"), BoolProp(TEXT("Sync across instances"), false) },
		}, { TEXT("blackboard_path"), TEXT("key_name"), TEXT("key_type") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::BehaviorTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddBlackboardKey(
				GetStr(In, TEXT("blackboard_path")),
				GetStr(In, TEXT("key_name")),
				GetStr(In, TEXT("key_type")),
				GetStr(In, TEXT("key_type_object")),
				GetBool(In, TEXT("instance_synced"), false),
				Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// remove_blackboard_key ----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("remove_blackboard_key");
		T.Description = TEXT("Remove a key from a Blackboard. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("blackboard_path"), Prop(TEXT("string"), TEXT("Blackboard asset path")) },
			{ TEXT("key_name"), Prop(TEXT("string"), TEXT("Key name")) },
		}, { TEXT("blackboard_path"), TEXT("key_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::BehaviorTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::RemoveBlackboardKey(
				GetStr(In, TEXT("blackboard_path")),
				GetStr(In, TEXT("key_name")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// get_blackboard_structure -------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_blackboard_structure");
		T.Description = TEXT("Read all keys on a Blackboard: names, types, instance-synced flag, parent reference.");
		T.InputSchema = MakeSchema({
			{ TEXT("blackboard_path"), Prop(TEXT("string"), TEXT("Blackboard asset path")) },
		}, { TEXT("blackboard_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::BehaviorTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetBlackboardStructure(GetStr(In, TEXT("blackboard_path")));
			return R;
		});
		RegisterTool(T);
	}

	// create_behavior_tree -----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_behavior_tree");
		T.Description = TEXT("Create a new Behavior Tree asset. Optionally bind a Blackboard asset to it. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/AI/BT_Enemy'")) },
			{ TEXT("blackboard_path"), Prop(TEXT("string"), TEXT("Optional BB to bind")) },
		}, { TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::BehaviorTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateBehaviorTree(
				GetStr(In, TEXT("new_asset_path")),
				GetStr(In, TEXT("blackboard_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// get_behavior_tree_structure ----------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_behavior_tree_structure");
		T.Description = TEXT("Read the full BT graph: nodes with GUIDs, positions, runtime classes, and parent-child relations. If the BT editor is not initialized for this asset yet, returns a fallback message — use open_asset_editor first.");
		T.InputSchema = MakeSchema({
			{ TEXT("bt_path"), Prop(TEXT("string"), TEXT("Behavior Tree asset path")) },
		}, { TEXT("bt_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::BehaviorTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetBehaviorTreeStructure(GetStr(In, TEXT("bt_path")));
			return R;
		});
		RegisterTool(T);
	}

	// add_bt_composite ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_bt_composite");
		T.Description = TEXT("Add a Composite node (selector/sequence/simpleparallel) to a Behavior Tree. Optionally auto-connects under parent_node_guid. If parent is empty, node is unparented (you'll need to connect it later). REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("bt_path"), Prop(TEXT("string"), TEXT("Behavior Tree asset path")) },
			{ TEXT("composite_type"), Prop(TEXT("string"), TEXT("selector / sequence / simpleparallel")) },
			{ TEXT("parent_node_guid"), Prop(TEXT("string"), TEXT("Optional GUID of parent node")) },
			{ TEXT("x"), IntProp(TEXT("X position"), 0) },
			{ TEXT("y"), IntProp(TEXT("Y position"), 0) },
		}, { TEXT("bt_path"), TEXT("composite_type") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::BehaviorTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddBTComposite(
				GetStr(In, TEXT("bt_path")),
				GetStr(In, TEXT("composite_type")),
				GetStr(In, TEXT("parent_node_guid")),
				GetInt(In, TEXT("x")), GetInt(In, TEXT("y")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_bt_task --------------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_bt_task");
		T.Description = TEXT("Add a Task node to a Behavior Tree. task_class accepts native class short name (e.g. 'BTTask_MoveTo'), full class path, or BP asset path like '/Game/AI/Tasks/BTT_ChasePlayer'. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("bt_path"), Prop(TEXT("string"), TEXT("Behavior Tree asset path")) },
			{ TEXT("task_class"), Prop(TEXT("string"), TEXT("Task class or BP path")) },
			{ TEXT("parent_node_guid"), Prop(TEXT("string"), TEXT("Optional GUID of parent composite")) },
			{ TEXT("x"), IntProp(TEXT("X position"), 0) },
			{ TEXT("y"), IntProp(TEXT("Y position"), 0) },
		}, { TEXT("bt_path"), TEXT("task_class") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::BehaviorTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddBTTask(
				GetStr(In, TEXT("bt_path")),
				GetStr(In, TEXT("task_class")),
				GetStr(In, TEXT("parent_node_guid")),
				GetInt(In, TEXT("x")), GetInt(In, TEXT("y")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_bt_decorator ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_bt_decorator");
		T.Description = TEXT("Attach a Decorator to a BT node (Composite or Task). Decorators gate execution (e.g. BTDecorator_Blackboard). REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("bt_path"), Prop(TEXT("string"), TEXT("Behavior Tree asset path")) },
			{ TEXT("decorator_class"), Prop(TEXT("string"), TEXT("Decorator class or BP path")) },
			{ TEXT("target_node_guid"), Prop(TEXT("string"), TEXT("Node to attach decorator to")) },
		}, { TEXT("bt_path"), TEXT("decorator_class"), TEXT("target_node_guid") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::BehaviorTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddBTDecorator(
				GetStr(In, TEXT("bt_path")),
				GetStr(In, TEXT("decorator_class")),
				GetStr(In, TEXT("target_node_guid")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_bt_service -----------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_bt_service");
		T.Description = TEXT("Attach a Service to a Composite BT node. Services tick while the composite subtree is active. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("bt_path"), Prop(TEXT("string"), TEXT("Behavior Tree asset path")) },
			{ TEXT("service_class"), Prop(TEXT("string"), TEXT("Service class or BP path")) },
			{ TEXT("target_node_guid"), Prop(TEXT("string"), TEXT("Composite node to attach service to")) },
		}, { TEXT("bt_path"), TEXT("service_class"), TEXT("target_node_guid") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::BehaviorTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddBTService(
				GetStr(In, TEXT("bt_path")),
				GetStr(In, TEXT("service_class")),
				GetStr(In, TEXT("target_node_guid")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// connect_bt_nodes ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("connect_bt_nodes");
		T.Description = TEXT("Connect a BT parent (Root or Composite) to a child node. Schema validates the connection. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("bt_path"), Prop(TEXT("string"), TEXT("Behavior Tree asset path")) },
			{ TEXT("parent_guid"), Prop(TEXT("string"), TEXT("Parent node GUID")) },
			{ TEXT("child_guid"), Prop(TEXT("string"), TEXT("Child node GUID")) },
		}, { TEXT("bt_path"), TEXT("parent_guid"), TEXT("child_guid") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::BehaviorTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::ConnectBTNodes(
				GetStr(In, TEXT("bt_path")),
				GetStr(In, TEXT("parent_guid")),
				GetStr(In, TEXT("child_guid")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// delete_bt_node -----------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("delete_bt_node");
		T.Description = TEXT("Delete a node from a Behavior Tree graph by GUID. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("bt_path"), Prop(TEXT("string"), TEXT("Behavior Tree asset path")) },
			{ TEXT("node_guid"), Prop(TEXT("string"), TEXT("Node GUID")) },
		}, { TEXT("bt_path"), TEXT("node_guid") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::BehaviorTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::DeleteBTNode(
				GetStr(In, TEXT("bt_path")),
				GetStr(In, TEXT("node_guid")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== NEW: Animation ===============================================

	// get_anim_sequence_info ---------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_anim_sequence_info");
		T.Description = TEXT("Read metadata of an Anim Sequence/Montage: skeleton, length, framerate, rate scale, notify count.");
		T.InputSchema = MakeSchema({
			{ TEXT("sequence_path"), Prop(TEXT("string"), TEXT("Anim sequence/montage asset path")) },
		}, { TEXT("sequence_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Animation;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetAnimSequenceInfo(GetStr(In, TEXT("sequence_path")));
			return R;
		});
		RegisterTool(T);
	}

	// get_anim_notifies --------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_anim_notifies");
		T.Description = TEXT("List all Notifies and Notify States on an anim sequence/montage with times, durations, and classes.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Anim sequence/montage asset path")) },
		}, { TEXT("asset_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Animation;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetAnimNotifies(GetStr(In, TEXT("asset_path")));
			return R;
		});
		RegisterTool(T);
	}

	// get_anim_montage_structure -----------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_anim_montage_structure");
		T.Description = TEXT("Read a Montage's slots, sections, branching points, length.");
		T.InputSchema = MakeSchema({
			{ TEXT("montage_path"), Prop(TEXT("string"), TEXT("AnimMontage asset path")) },
		}, { TEXT("montage_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Animation;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetAnimMontageStructure(GetStr(In, TEXT("montage_path")));
			return R;
		});
		RegisterTool(T);
	}

	// get_anim_curves ----------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_anim_curves");
		T.Description = TEXT("List float curves defined on an Anim Sequence.");
		T.InputSchema = MakeSchema({
			{ TEXT("sequence_path"), Prop(TEXT("string"), TEXT("Anim sequence asset path")) },
		}, { TEXT("sequence_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Animation;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetAnimCurves(GetStr(In, TEXT("sequence_path")));
			return R;
		});
		RegisterTool(T);
	}

	// add_anim_notify ----------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_anim_notify");
		T.Description = TEXT("Add a Notify to an anim sequence/montage at a specific time. notify_class is optional — empty = plain notify (name only); or pass an AnimNotify/AnimNotifyState class (e.g. 'AnimNotify_PlaySound') or a BP notify path. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Anim sequence/montage asset path")) },
			{ TEXT("notify_name"), Prop(TEXT("string"), TEXT("Notify name (shows on timeline)")) },
			{ TEXT("time"), Prop(TEXT("number"), TEXT("Time in seconds from start")) },
			{ TEXT("notify_class"), Prop(TEXT("string"), TEXT("Optional class path or BP asset")) },
		}, { TEXT("asset_path"), TEXT("notify_name"), TEXT("time") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Animation;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			double TimeVal = 0.0;
			In->TryGetNumberField(TEXT("time"), TimeVal);
			R.Content = FClaudeContextProvider::AddAnimNotify(
				GetStr(In, TEXT("asset_path")),
				GetStr(In, TEXT("notify_name")),
				(float)TimeVal,
				GetStr(In, TEXT("notify_class")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// remove_anim_notify -------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("remove_anim_notify");
		T.Description = TEXT("Remove Notify(s) by name from an anim asset. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Anim sequence/montage asset path")) },
			{ TEXT("notify_name"), Prop(TEXT("string"), TEXT("Notify name to remove")) },
		}, { TEXT("asset_path"), TEXT("notify_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Animation;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::RemoveAnimNotify(
				GetStr(In, TEXT("asset_path")),
				GetStr(In, TEXT("notify_name")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_anim_curve -----------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_anim_curve");
		T.Description = TEXT("Add an empty float curve with the given name to an Anim Sequence. Keys can be set later via sequencer editor. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("sequence_path"), Prop(TEXT("string"), TEXT("Anim sequence asset path")) },
			{ TEXT("curve_name"), Prop(TEXT("string"), TEXT("Curve name")) },
		}, { TEXT("sequence_path"), TEXT("curve_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Animation;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddAnimCurve(
				GetStr(In, TEXT("sequence_path")),
				GetStr(In, TEXT("curve_name")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// create_anim_montage ------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_anim_montage");
		T.Description = TEXT("Create a new Anim Montage bound to a skeleton, optionally seeded from a source AnimSequence. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/Anims/AM_Attack'")) },
			{ TEXT("skeleton_path"), Prop(TEXT("string"), TEXT("Target USkeleton path")) },
			{ TEXT("source_sequence"), Prop(TEXT("string"), TEXT("Optional AnimSequence to populate the default segment from")) },
		}, { TEXT("new_asset_path"), TEXT("skeleton_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Animation;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateAnimMontage(
				GetStr(In, TEXT("new_asset_path")),
				GetStr(In, TEXT("skeleton_path")),
				GetStr(In, TEXT("source_sequence")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_montage_section ------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_montage_section");
		T.Description = TEXT("Add a named Section to a montage at a given start time. Sections can be jumped to by name at runtime. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("montage_path"), Prop(TEXT("string"), TEXT("AnimMontage asset path")) },
			{ TEXT("section_name"), Prop(TEXT("string"), TEXT("Section name")) },
			{ TEXT("start_time"), Prop(TEXT("number"), TEXT("Start time in seconds")) },
		}, { TEXT("montage_path"), TEXT("section_name"), TEXT("start_time") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Animation;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			double StartTime = 0.0;
			In->TryGetNumberField(TEXT("start_time"), StartTime);
			R.Content = FClaudeContextProvider::AddMontageSection(
				GetStr(In, TEXT("montage_path")),
				GetStr(In, TEXT("section_name")),
				(float)StartTime, Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_montage_slot ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_montage_slot");
		T.Description = TEXT("Add a Slot to a montage. Slot names are referenced by AnimBP Slot nodes. Default slot is 'DefaultSlot'. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("montage_path"), Prop(TEXT("string"), TEXT("AnimMontage asset path")) },
			{ TEXT("slot_name"), Prop(TEXT("string"), TEXT("Slot name")) },
		}, { TEXT("montage_path"), TEXT("slot_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Animation;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddMontageSlot(
				GetStr(In, TEXT("montage_path")),
				GetStr(In, TEXT("slot_name")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_anim_sequence_player_node --------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_anim_sequence_player_node");
		T.Description = TEXT("Add an Anim Sequence Player node to an AnimBP graph. graph_name='AnimGraph' for the main graph. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("anim_bp_path"), Prop(TEXT("string"), TEXT("Animation Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Target graph (usually 'AnimGraph')")) },
			{ TEXT("sequence_path"), Prop(TEXT("string"), TEXT("Anim Sequence asset to play")) },
			{ TEXT("x"), IntProp(TEXT("X position"), 0) },
			{ TEXT("y"), IntProp(TEXT("Y position"), 0) },
		}, { TEXT("anim_bp_path"), TEXT("graph_name"), TEXT("sequence_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Animation;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddAnimSequencePlayerNode(
				GetStr(In, TEXT("anim_bp_path")),
				GetStr(In, TEXT("graph_name")),
				GetStr(In, TEXT("sequence_path")),
				GetInt(In, TEXT("x")), GetInt(In, TEXT("y")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_anim_state_machine_node ----------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_anim_state_machine_node");
		T.Description = TEXT("Add an empty State Machine node to an AnimBP graph. States inside must be added manually via the AnimBP editor — this tool creates the container. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("anim_bp_path"), Prop(TEXT("string"), TEXT("Animation Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Target graph")) },
			{ TEXT("machine_name"), Prop(TEXT("string"), TEXT("State machine name")) },
			{ TEXT("x"), IntProp(TEXT("X position"), 0) },
			{ TEXT("y"), IntProp(TEXT("Y position"), 0) },
		}, { TEXT("anim_bp_path"), TEXT("graph_name"), TEXT("machine_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Animation;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddAnimStateMachineNode(
				GetStr(In, TEXT("anim_bp_path")),
				GetStr(In, TEXT("graph_name")),
				GetStr(In, TEXT("machine_name")),
				GetInt(In, TEXT("x")), GetInt(In, TEXT("y")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_anim_blend_space_player_node -----------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_anim_blend_space_player_node");
		T.Description = TEXT("Add a BlendSpace Player node to an AnimBP graph. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("anim_bp_path"), Prop(TEXT("string"), TEXT("Animation Blueprint asset path")) },
			{ TEXT("graph_name"), Prop(TEXT("string"), TEXT("Target graph")) },
			{ TEXT("blend_space_path"), Prop(TEXT("string"), TEXT("BlendSpace asset to play")) },
			{ TEXT("x"), IntProp(TEXT("X position"), 0) },
			{ TEXT("y"), IntProp(TEXT("Y position"), 0) },
		}, { TEXT("anim_bp_path"), TEXT("graph_name"), TEXT("blend_space_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Animation;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddAnimBlendSpacePlayerNode(
				GetStr(In, TEXT("anim_bp_path")),
				GetStr(In, TEXT("graph_name")),
				GetStr(In, TEXT("blend_space_path")),
				GetInt(In, TEXT("x")), GetInt(In, TEXT("y")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== NEW: Materials ===============================================

	// create_material ----------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_material");
		T.Description = TEXT("Create an empty Material asset. The graph is blank — use open_asset_editor to edit it visually, or add parameters via other tools. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/Materials/M_Ground'")) },
		}, { TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Material;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateMaterial(GetStr(In, TEXT("new_asset_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// create_material_instance -------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_material_instance");
		T.Description = TEXT("Create a Material Instance Constant (MIC) asset based on a parent material. Use MICs to override parameters without touching the master material. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/Materials/MI_RedGround'")) },
			{ TEXT("parent_material"), Prop(TEXT("string"), TEXT("Path to parent Material or MaterialInstance")) },
		}, { TEXT("new_asset_path"), TEXT("parent_material") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Material;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateMaterialInstance(
				GetStr(In, TEXT("new_asset_path")),
				GetStr(In, TEXT("parent_material")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// get_material_info --------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_material_info");
		T.Description = TEXT("Read material (or material instance) metadata: blend mode, domain, shading models, all exposed parameters (scalar/vector/texture/static switches).");
		T.InputSchema = MakeSchema({
			{ TEXT("material_path"), Prop(TEXT("string"), TEXT("Material or MaterialInstance asset path")) },
		}, { TEXT("material_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Material;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetMaterialInfo(GetStr(In, TEXT("material_path")));
			return R;
		});
		RegisterTool(T);
	}

	// get_material_instance_params ---------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_material_instance_params");
		T.Description = TEXT("Read a Material Instance Constant's explicit parameter overrides (scalars, vectors, textures, static switches).");
		T.InputSchema = MakeSchema({
			{ TEXT("mic_path"), Prop(TEXT("string"), TEXT("MaterialInstanceConstant asset path")) },
		}, { TEXT("mic_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Material;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetMaterialInstanceParams(GetStr(In, TEXT("mic_path")));
			return R;
		});
		RegisterTool(T);
	}

	// set_material_scalar_param ------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_material_scalar_param");
		T.Description = TEXT("Set a scalar parameter value on a Material Instance. Fast, no shader recompile. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("mic_path"), Prop(TEXT("string"), TEXT("MIC asset path")) },
			{ TEXT("param_name"), Prop(TEXT("string"), TEXT("Scalar parameter name")) },
			{ TEXT("value"), Prop(TEXT("number"), TEXT("Scalar value")) },
		}, { TEXT("mic_path"), TEXT("param_name"), TEXT("value") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Material;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			double V = 0.0;
			In->TryGetNumberField(TEXT("value"), V);
			R.Content = FClaudeContextProvider::SetMaterialScalarParam(
				GetStr(In, TEXT("mic_path")),
				GetStr(In, TEXT("param_name")),
				(float)V, Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// set_material_vector_param ------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_material_vector_param");
		T.Description = TEXT("Set a vector (LinearColor) parameter on a MIC. Values are 0-1 floats for RGBA. Fast, no shader recompile. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("mic_path"), Prop(TEXT("string"), TEXT("MIC asset path")) },
			{ TEXT("param_name"), Prop(TEXT("string"), TEXT("Vector parameter name")) },
			{ TEXT("r"), Prop(TEXT("number"), TEXT("Red (0-1)")) },
			{ TEXT("g"), Prop(TEXT("number"), TEXT("Green (0-1)")) },
			{ TEXT("b"), Prop(TEXT("number"), TEXT("Blue (0-1)")) },
			{ TEXT("a"), Prop(TEXT("number"), TEXT("Alpha (0-1)")) },
		}, { TEXT("mic_path"), TEXT("param_name"), TEXT("r"), TEXT("g"), TEXT("b") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Material;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			double Rv=0, Gv=0, Bv=0, Av=1;
			In->TryGetNumberField(TEXT("r"), Rv);
			In->TryGetNumberField(TEXT("g"), Gv);
			In->TryGetNumberField(TEXT("b"), Bv);
			In->TryGetNumberField(TEXT("a"), Av);
			R.Content = FClaudeContextProvider::SetMaterialVectorParam(
				GetStr(In, TEXT("mic_path")),
				GetStr(In, TEXT("param_name")),
				FLinearColor((float)Rv, (float)Gv, (float)Bv, (float)Av), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// set_material_texture_param -----------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_material_texture_param");
		T.Description = TEXT("Set a texture parameter on a MIC to a texture asset. Fast, no shader recompile. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("mic_path"), Prop(TEXT("string"), TEXT("MIC asset path")) },
			{ TEXT("param_name"), Prop(TEXT("string"), TEXT("Texture parameter name")) },
			{ TEXT("texture_path"), Prop(TEXT("string"), TEXT("Texture2D asset path")) },
		}, { TEXT("mic_path"), TEXT("param_name"), TEXT("texture_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Material;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SetMaterialTextureParam(
				GetStr(In, TEXT("mic_path")),
				GetStr(In, TEXT("param_name")),
				GetStr(In, TEXT("texture_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// set_material_static_switch -----------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_material_static_switch");
		T.Description = TEXT("Set a static switch parameter on a MIC. WARNING: this triggers a shader recompile (1-5 seconds, editor will pause). REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("mic_path"), Prop(TEXT("string"), TEXT("MIC asset path")) },
			{ TEXT("param_name"), Prop(TEXT("string"), TEXT("Static switch parameter name")) },
			{ TEXT("value"), BoolProp(TEXT("Switch value"), false) },
		}, { TEXT("mic_path"), TEXT("param_name"), TEXT("value") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Material;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SetMaterialStaticSwitch(
				GetStr(In, TEXT("mic_path")),
				GetStr(In, TEXT("param_name")),
				GetBool(In, TEXT("value"), false), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== NEW: Level Design ============================================

	// Shared helper for parsing vector from JSON object
	// Inline lambda because we can't add free helper below RegisterDefaultTools easily

	// spawn_static_mesh_actor --------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("spawn_static_mesh_actor");
		T.Description = TEXT("Spawn a StaticMeshActor with a given mesh, location, rotation, scale. Fastest way to place individual whitebox props. Optional material override and label. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("mesh_path"), Prop(TEXT("string"), TEXT("StaticMesh asset path, e.g. '/Engine/BasicShapes/Cube.Cube'")) },
			{ TEXT("x"), Prop(TEXT("number"), TEXT("Location X")) },
			{ TEXT("y"), Prop(TEXT("number"), TEXT("Location Y")) },
			{ TEXT("z"), Prop(TEXT("number"), TEXT("Location Z")) },
			{ TEXT("pitch"), Prop(TEXT("number"), TEXT("Rotation pitch (optional)")) },
			{ TEXT("yaw"),   Prop(TEXT("number"), TEXT("Rotation yaw (optional)")) },
			{ TEXT("roll"),  Prop(TEXT("number"), TEXT("Rotation roll (optional)")) },
			{ TEXT("scale_x"), Prop(TEXT("number"), TEXT("Scale X (default 1)")) },
			{ TEXT("scale_y"), Prop(TEXT("number"), TEXT("Scale Y (default 1)")) },
			{ TEXT("scale_z"), Prop(TEXT("number"), TEXT("Scale Z (default 1)")) },
			{ TEXT("material_override"), Prop(TEXT("string"), TEXT("Optional Material/MIC path for slot 0")) },
			{ TEXT("label"), Prop(TEXT("string"), TEXT("Optional World Outliner label")) },
		}, { TEXT("mesh_path"), TEXT("x"), TEXT("y"), TEXT("z") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Level;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			double X=0, Y=0, Z=0, P=0, Yaw=0, Rl=0, SX=1, SY=1, SZ=1;
			In->TryGetNumberField(TEXT("x"), X);
			In->TryGetNumberField(TEXT("y"), Y);
			In->TryGetNumberField(TEXT("z"), Z);
			In->TryGetNumberField(TEXT("pitch"), P);
			In->TryGetNumberField(TEXT("yaw"), Yaw);
			In->TryGetNumberField(TEXT("roll"), Rl);
			if (!In->TryGetNumberField(TEXT("scale_x"), SX)) SX = 1;
			if (!In->TryGetNumberField(TEXT("scale_y"), SY)) SY = 1;
			if (!In->TryGetNumberField(TEXT("scale_z"), SZ)) SZ = 1;
			R.Content = FClaudeContextProvider::SpawnStaticMeshActor(
				GetStr(In, TEXT("mesh_path")),
				FVector(X, Y, Z),
				FRotator(P, Yaw, Rl),
				FVector(SX, SY, SZ),
				GetStr(In, TEXT("material_override")),
				GetStr(In, TEXT("label")),
				Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// spawn_actors_grid --------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("spawn_actors_grid");
		T.Description = TEXT("Spawn a 3D grid of actors in one batch (single undo step). Specify either class_path (BP/native class) OR mesh_path (spawns StaticMeshActors with that mesh). Cap: 2000 actors total. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("class_path"), Prop(TEXT("string"), TEXT("Actor class or BP path (optional if mesh_path given)")) },
			{ TEXT("mesh_path"), Prop(TEXT("string"), TEXT("StaticMesh path (optional; if set, spawns StaticMeshActors)")) },
			{ TEXT("origin_x"), Prop(TEXT("number"), TEXT("Grid origin X")) },
			{ TEXT("origin_y"), Prop(TEXT("number"), TEXT("Grid origin Y")) },
			{ TEXT("origin_z"), Prop(TEXT("number"), TEXT("Grid origin Z")) },
			{ TEXT("count_x"), IntProp(TEXT("Actors along X (1-200)"), 1) },
			{ TEXT("count_y"), IntProp(TEXT("Actors along Y (1-200)"), 1) },
			{ TEXT("count_z"), IntProp(TEXT("Actors along Z (1-200)"), 1) },
			{ TEXT("spacing_x"), Prop(TEXT("number"), TEXT("Spacing along X (units)")) },
			{ TEXT("spacing_y"), Prop(TEXT("number"), TEXT("Spacing along Y (units)")) },
			{ TEXT("spacing_z"), Prop(TEXT("number"), TEXT("Spacing along Z (units)")) },
			{ TEXT("folder"), Prop(TEXT("string"), TEXT("Optional World Outliner folder for grouping")) },
		}, { TEXT("origin_x"), TEXT("origin_y"), TEXT("origin_z") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Level;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			double OX=0,OY=0,OZ=0,SX=0,SY=0,SZ=0;
			In->TryGetNumberField(TEXT("origin_x"), OX);
			In->TryGetNumberField(TEXT("origin_y"), OY);
			In->TryGetNumberField(TEXT("origin_z"), OZ);
			In->TryGetNumberField(TEXT("spacing_x"), SX);
			In->TryGetNumberField(TEXT("spacing_y"), SY);
			In->TryGetNumberField(TEXT("spacing_z"), SZ);
			R.Content = FClaudeContextProvider::SpawnActorsGrid(
				GetStr(In, TEXT("class_path")),
				GetStr(In, TEXT("mesh_path")),
				FVector(OX, OY, OZ),
				GetInt(In, TEXT("count_x"), 1),
				GetInt(In, TEXT("count_y"), 1),
				GetInt(In, TEXT("count_z"), 1),
				FVector(SX, SY, SZ),
				GetStr(In, TEXT("folder")),
				Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// spawn_actors_line --------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("spawn_actors_line");
		T.Description = TEXT("Spawn N actors evenly distributed along a line between start and end points. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("class_path"), Prop(TEXT("string"), TEXT("Actor class (optional if mesh_path given)")) },
			{ TEXT("mesh_path"), Prop(TEXT("string"), TEXT("StaticMesh path (optional)")) },
			{ TEXT("start_x"), Prop(TEXT("number"), TEXT("Start X")) },
			{ TEXT("start_y"), Prop(TEXT("number"), TEXT("Start Y")) },
			{ TEXT("start_z"), Prop(TEXT("number"), TEXT("Start Z")) },
			{ TEXT("end_x"), Prop(TEXT("number"), TEXT("End X")) },
			{ TEXT("end_y"), Prop(TEXT("number"), TEXT("End Y")) },
			{ TEXT("end_z"), Prop(TEXT("number"), TEXT("End Z")) },
			{ TEXT("count"), IntProp(TEXT("Number of actors (1-500)"), 2) },
			{ TEXT("folder"), Prop(TEXT("string"), TEXT("Optional folder")) },
		}, { TEXT("start_x"), TEXT("start_y"), TEXT("start_z"), TEXT("end_x"), TEXT("end_y"), TEXT("end_z"), TEXT("count") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Level;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			double S0=0,S1=0,S2=0,E0=0,E1=0,E2=0;
			In->TryGetNumberField(TEXT("start_x"), S0); In->TryGetNumberField(TEXT("start_y"), S1); In->TryGetNumberField(TEXT("start_z"), S2);
			In->TryGetNumberField(TEXT("end_x"), E0);   In->TryGetNumberField(TEXT("end_y"), E1);   In->TryGetNumberField(TEXT("end_z"), E2);
			R.Content = FClaudeContextProvider::SpawnActorsLine(
				GetStr(In, TEXT("class_path")),
				GetStr(In, TEXT("mesh_path")),
				FVector(S0, S1, S2),
				FVector(E0, E1, E2),
				GetInt(In, TEXT("count"), 2),
				GetStr(In, TEXT("folder")),
				Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// duplicate_actors ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("duplicate_actors");
		T.Description = TEXT("Duplicate one or more existing actors with a location offset. Can make multiple copies — each copy is offset by (i * offset) from the source. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_paths"), Prop(TEXT("array"), TEXT("Array of actor paths or labels to duplicate")) },
			{ TEXT("offset_x"), Prop(TEXT("number"), TEXT("Offset X per copy")) },
			{ TEXT("offset_y"), Prop(TEXT("number"), TEXT("Offset Y per copy")) },
			{ TEXT("offset_z"), Prop(TEXT("number"), TEXT("Offset Z per copy")) },
			{ TEXT("copy_count"), IntProp(TEXT("Number of copies per source (1-100)"), 1) },
		}, { TEXT("actor_paths") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Level;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			TArray<FString> Paths;
			const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
			if (In->TryGetArrayField(TEXT("actor_paths"), ArrPtr))
			{
				for (const TSharedPtr<FJsonValue>& V : *ArrPtr) Paths.Add(V->AsString());
			}
			double OX=0, OY=0, OZ=0;
			In->TryGetNumberField(TEXT("offset_x"), OX);
			In->TryGetNumberField(TEXT("offset_y"), OY);
			In->TryGetNumberField(TEXT("offset_z"), OZ);
			R.Content = FClaudeContextProvider::DuplicateActors(
				Paths, FVector(OX, OY, OZ),
				GetInt(In, TEXT("copy_count"), 1), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// set_actor_folder ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_actor_folder");
		T.Description = TEXT("Move an actor into a World Outliner folder. Use slashes for nested folders: 'Props/Furniture'. Empty path removes from folder. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_path"), Prop(TEXT("string"), TEXT("Actor path or label")) },
			{ TEXT("folder_path"), Prop(TEXT("string"), TEXT("Folder path (e.g. 'Environment/Walls')")) },
		}, { TEXT("actor_path"), TEXT("folder_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Level;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SetActorFolder(
				GetStr(In, TEXT("actor_path")),
				GetStr(In, TEXT("folder_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// get_actor_bounds ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_actor_bounds");
		T.Description = TEXT("Read actor's world-space bounding box: origin, extent, size, min/max corners. Useful for alignment and snapping.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_path"), Prop(TEXT("string"), TEXT("Actor path or label")) },
		}, { TEXT("actor_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Level;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetActorBounds(GetStr(In, TEXT("actor_path")));
			return R;
		});
		RegisterTool(T);
	}

	// save_current_level -------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("save_current_level");
		T.Description = TEXT("Save the currently open level (.umap) to disk. Use after batch operations. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({}, {});
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::General;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SaveCurrentLevel(Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// batch_set_actor_property -------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("batch_set_actor_property");
		T.Description = TEXT("Set a property on all actors matching filters (by class, folder, and/or name substring). Filters are combined with AND; empty filter means 'no filter on that dimension'. Single undo covers all. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("class_filter"), Prop(TEXT("string"), TEXT("Optional: only actors of this class")) },
			{ TEXT("folder_filter"), Prop(TEXT("string"), TEXT("Optional: only actors in this World Outliner folder")) },
			{ TEXT("name_contains"), Prop(TEXT("string"), TEXT("Optional: actor label substring")) },
			{ TEXT("property_name"), Prop(TEXT("string"), TEXT("UPROPERTY name to set")) },
			{ TEXT("new_value"), Prop(TEXT("string"), TEXT("New value in Unreal text format")) },
		}, { TEXT("property_name"), TEXT("new_value") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Level;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::BatchSetActorProperty(
				GetStr(In, TEXT("class_filter")),
				GetStr(In, TEXT("folder_filter")),
				GetStr(In, TEXT("name_contains")),
				GetStr(In, TEXT("property_name")),
				GetStr(In, TEXT("new_value")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// align_actors -------------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("align_actors");
		T.Description = TEXT("Align 2+ actors on an axis to a reference value: min/max/center/first. E.g. align walls' Z to 0 to snap them all to ground level. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_paths"), Prop(TEXT("array"), TEXT("Array of actor paths/labels")) },
			{ TEXT("axis"), Prop(TEXT("string"), TEXT("Axis: x, y, or z")) },
			{ TEXT("mode"), Prop(TEXT("string"), TEXT("Reference: min, max, center, or first")) },
		}, { TEXT("actor_paths"), TEXT("axis"), TEXT("mode") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Level;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			TArray<FString> Paths;
			const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
			if (In->TryGetArrayField(TEXT("actor_paths"), ArrPtr))
			{
				for (const TSharedPtr<FJsonValue>& V : *ArrPtr) Paths.Add(V->AsString());
			}
			R.Content = FClaudeContextProvider::AlignActors(
				Paths, GetStr(In, TEXT("axis")), GetStr(In, TEXT("mode")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// place_on_surface ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("place_on_surface");
		T.Description = TEXT("Raycast down from actor's location until hitting world geometry, then snap the actor's bottom to that surface. Useful for dropping props onto floors. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_path"), Prop(TEXT("string"), TEXT("Actor to place")) },
			{ TEXT("max_distance"), Prop(TEXT("number"), TEXT("Max raycast distance (default 100000)")) },
		}, { TEXT("actor_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Level;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			double MaxDist = 0.0;
			In->TryGetNumberField(TEXT("max_distance"), MaxDist);
			R.Content = FClaudeContextProvider::PlaceOnSurface(
				GetStr(In, TEXT("actor_path")), (float)MaxDist, Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== NEW: StateTree ===============================================

	// get_state_tree_structure -------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_state_tree_structure");
		T.Description = TEXT("Read StateTree hierarchy: subtrees, states with IDs and names, their types, task/transition counts, nested children. Use this first before editing.");
		T.InputSchema = MakeSchema({
			{ TEXT("state_tree_path"), Prop(TEXT("string"), TEXT("StateTree asset path")) },
		}, { TEXT("state_tree_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::StateTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetStateTreeStructure(GetStr(In, TEXT("state_tree_path")));
			return R;
		});
		RegisterTool(T);
	}

	// get_state_tree_schema ----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_state_tree_schema");
		T.Description = TEXT("Read which Schema a StateTree uses (e.g. StateTreeAIComponentSchema requires AAIController+AActor context). Lists schema fields.");
		T.InputSchema = MakeSchema({
			{ TEXT("state_tree_path"), Prop(TEXT("string"), TEXT("StateTree asset path")) },
		}, { TEXT("state_tree_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::StateTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetStateTreeSchema(GetStr(In, TEXT("state_tree_path")));
			return R;
		});
		RegisterTool(T);
	}

	// list_state_tree_tasks ----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("list_state_tree_tasks");
		T.Description = TEXT("Flat list of all tasks across all states in a StateTree. Each entry shows which state it belongs to and its index within state.Tasks.");
		T.InputSchema = MakeSchema({
			{ TEXT("state_tree_path"), Prop(TEXT("string"), TEXT("StateTree asset path")) },
		}, { TEXT("state_tree_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::StateTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::ListStateTreeTasks(GetStr(In, TEXT("state_tree_path")));
			return R;
		});
		RegisterTool(T);
	}

	// list_state_tree_transitions ----------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("list_state_tree_transitions");
		T.Description = TEXT("Flat list of all transitions in a StateTree with source state, index, and trigger type (OnTick/OnEvent/OnStateCompleted/etc).");
		T.InputSchema = MakeSchema({
			{ TEXT("state_tree_path"), Prop(TEXT("string"), TEXT("StateTree asset path")) },
		}, { TEXT("state_tree_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::StateTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::ListStateTreeTransitions(GetStr(In, TEXT("state_tree_path")));
			return R;
		});
		RegisterTool(T);
	}

	// get_state_tree_bindings --------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_state_tree_bindings");
		T.Description = TEXT("Read property binding count for a StateTree. Bindings wire context values (from Schema) into task/condition properties.");
		T.InputSchema = MakeSchema({
			{ TEXT("state_tree_path"), Prop(TEXT("string"), TEXT("StateTree asset path")) },
		}, { TEXT("state_tree_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::StateTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetStateTreeBindings(GetStr(In, TEXT("state_tree_path")));
			return R;
		});
		RegisterTool(T);
	}

	// create_state_tree --------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_state_tree");
		T.Description = TEXT("Create a new StateTree asset. Optional schema_class lets you preset context expectations, e.g. 'StateTreeAIComponentSchema' for NPC trees or 'StateTreeComponentSchema' for generic actor trees. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/AI/ST_Enemy'")) },
			{ TEXT("schema_class"), Prop(TEXT("string"), TEXT("Optional: schema class name")) },
		}, { TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::StateTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateStateTree(
				GetStr(In, TEXT("new_asset_path")),
				GetStr(In, TEXT("schema_class")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// set_state_tree_schema ----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_state_tree_schema");
		T.Description = TEXT("Change an existing StateTree's schema. WARNING: may invalidate existing states/tasks that reference context from the old schema. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("state_tree_path"), Prop(TEXT("string"), TEXT("StateTree asset path")) },
			{ TEXT("schema_class"), Prop(TEXT("string"), TEXT("Schema class name or full path")) },
		}, { TEXT("state_tree_path"), TEXT("schema_class") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::StateTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SetStateTreeSchema(
				GetStr(In, TEXT("state_tree_path")),
				GetStr(In, TEXT("schema_class")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_state ----------------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_state");
		T.Description = TEXT("Add a new state to a StateTree. Empty parent_state_id creates a root subtree; otherwise adds as child of the given state. state_type options: 'State', 'Group', 'Linked', 'SubTree'. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("state_tree_path"), Prop(TEXT("string"), TEXT("StateTree asset path")) },
			{ TEXT("parent_state_id"), Prop(TEXT("string"), TEXT("Optional parent state GUID")) },
			{ TEXT("state_name"), Prop(TEXT("string"), TEXT("Name for the new state")) },
			{ TEXT("state_type"), Prop(TEXT("string"), TEXT("Optional: State/Group/Linked/SubTree")) },
		}, { TEXT("state_tree_path"), TEXT("state_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::StateTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddState(
				GetStr(In, TEXT("state_tree_path")),
				GetStr(In, TEXT("parent_state_id")),
				GetStr(In, TEXT("state_name")),
				GetStr(In, TEXT("state_type")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// remove_state -------------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("remove_state");
		T.Description = TEXT("Remove a state (and all its children) from a StateTree. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("state_tree_path"), Prop(TEXT("string"), TEXT("StateTree asset path")) },
			{ TEXT("state_id"), Prop(TEXT("string"), TEXT("State GUID to remove")) },
		}, { TEXT("state_tree_path"), TEXT("state_id") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::StateTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::RemoveState(
				GetStr(In, TEXT("state_tree_path")),
				GetStr(In, TEXT("state_id")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_state_tree_task ------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_state_tree_task");
		T.Description = TEXT("Add a task slot to a state. task_class is a UScriptStruct name (e.g. 'StateTreeRunParallelStateTreeTask' or 'AITask_MoveToTask'). Task internals may need further configuration via the StateTree editor — this creates the slot. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("state_tree_path"), Prop(TEXT("string"), TEXT("StateTree asset path")) },
			{ TEXT("state_id"), Prop(TEXT("string"), TEXT("State GUID")) },
			{ TEXT("task_class"), Prop(TEXT("string"), TEXT("Task struct name")) },
		}, { TEXT("state_tree_path"), TEXT("state_id"), TEXT("task_class") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::StateTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddStateTreeTask(
				GetStr(In, TEXT("state_tree_path")),
				GetStr(In, TEXT("state_id")),
				GetStr(In, TEXT("task_class")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_state_tree_transition ------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_state_tree_transition");
		T.Description = TEXT("Add a transition from one state to another. trigger options: 'OnTick', 'OnStateCompleted', 'OnStateSucceeded', 'OnStateFailed', 'OnEvent'. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("state_tree_path"), Prop(TEXT("string"), TEXT("StateTree asset path")) },
			{ TEXT("from_state_id"), Prop(TEXT("string"), TEXT("Source state GUID")) },
			{ TEXT("to_state_id"), Prop(TEXT("string"), TEXT("Target state GUID")) },
			{ TEXT("trigger_type"), Prop(TEXT("string"), TEXT("Trigger enum value")) },
		}, { TEXT("state_tree_path"), TEXT("from_state_id"), TEXT("to_state_id"), TEXT("trigger_type") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::StateTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddStateTreeTransition(
				GetStr(In, TEXT("state_tree_path")),
				GetStr(In, TEXT("from_state_id")),
				GetStr(In, TEXT("to_state_id")),
				GetStr(In, TEXT("trigger_type")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_state_tree_condition -------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_state_tree_condition");
		T.Description = TEXT("Add a condition slot. If transition_index is empty, adds to the state's EnterConditions. Otherwise adds to that transition's Conditions array. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("state_tree_path"), Prop(TEXT("string"), TEXT("StateTree asset path")) },
			{ TEXT("state_id"), Prop(TEXT("string"), TEXT("State GUID")) },
			{ TEXT("transition_index"), Prop(TEXT("string"), TEXT("Optional: transition array index")) },
			{ TEXT("condition_class"), Prop(TEXT("string"), TEXT("Condition struct name")) },
		}, { TEXT("state_tree_path"), TEXT("state_id"), TEXT("condition_class") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::StateTree;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddStateTreeCondition(
				GetStr(In, TEXT("state_tree_path")),
				GetStr(In, TEXT("state_id")),
				GetStr(In, TEXT("transition_index")),
				GetStr(In, TEXT("condition_class")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== NEW: Data Asset / Data Table =================================

	// create_data_asset --------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_data_asset");
		T.Description = TEXT("Create a new Data Asset of the given class. Class can be a native UDataAsset subclass name (e.g. 'DataTable'), a full /Script/ path, or a BP-generated DA class path like '/Game/Items/DA_ItemDefinition_Base'. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/Items/DA_Pistol'")) },
			{ TEXT("asset_class"), Prop(TEXT("string"), TEXT("Data asset class name or path")) },
		}, { TEXT("new_asset_path"), TEXT("asset_class") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::DataAsset;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateDataAsset(
				GetStr(In, TEXT("new_asset_path")),
				GetStr(In, TEXT("asset_class")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// duplicate_data_asset -----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("duplicate_data_asset");
		T.Description = TEXT("Duplicate an existing Data Asset to a new path. All properties carry over. Most common workflow: copy DA_Pistol to DA_Pistol_Silenced, then modify specific fields. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("source_path"), Prop(TEXT("string"), TEXT("Source Data Asset path")) },
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("Destination path")) },
		}, { TEXT("source_path"), TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::DataAsset;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::DuplicateDataAsset(
				GetStr(In, TEXT("source_path")),
				GetStr(In, TEXT("new_asset_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// list_data_assets_by_class ------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("list_data_assets_by_class");
		T.Description = TEXT("List all Data Assets in the project, optionally filtered by class and/or folder. Empty class_filter returns all UDataAsset subclasses.");
		T.InputSchema = MakeSchema({
			{ TEXT("class_filter"), Prop(TEXT("string"), TEXT("Optional: class name or path")) },
			{ TEXT("path_filter"), Prop(TEXT("string"), TEXT("Optional: package path like '/Game/Items'")) },
		}, {});
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::DataAsset;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::ListDataAssetsByClass(
				GetStr(In, TEXT("class_filter")),
				GetStr(In, TEXT("path_filter")));
			return R;
		});
		RegisterTool(T);
	}

	// set_data_asset_field -----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_data_asset_field");
		T.Description = TEXT("Set a property on a Data Asset using a dotted path. Supports nested structs and array indexing: 'BaseStats.Damage', 'Abilities[2].Cooldown', 'Montages.Attack'. Use Unreal text format for values: structs as '(X=1,Y=2)', object refs as '/Game/Path/Asset.Asset', enums by name. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Data Asset path")) },
			{ TEXT("property_path"), Prop(TEXT("string"), TEXT("Dotted path, e.g. 'BaseStats.Health' or 'Items[0].Name'")) },
			{ TEXT("new_value"), Prop(TEXT("string"), TEXT("New value in Unreal text format")) },
		}, { TEXT("asset_path"), TEXT("property_path"), TEXT("new_value") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::DataAsset;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SetDataAssetField(
				GetStr(In, TEXT("asset_path")),
				GetStr(In, TEXT("property_path")),
				GetStr(In, TEXT("new_value")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_data_asset_array_element ---------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_data_asset_array_element");
		T.Description = TEXT("Append an element to a TArray property on a Data Asset. array_path uses dotted notation (e.g. 'Inventory' or 'Stats.Modifiers'). new_element_value is optional — if given, element is initialized to that value in Unreal text format. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Data Asset path")) },
			{ TEXT("array_path"), Prop(TEXT("string"), TEXT("Path to TArray property")) },
			{ TEXT("new_element_value"), Prop(TEXT("string"), TEXT("Optional initial value for new element")) },
		}, { TEXT("asset_path"), TEXT("array_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::DataAsset;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddDataAssetArrayElement(
				GetStr(In, TEXT("asset_path")),
				GetStr(In, TEXT("array_path")),
				GetStr(In, TEXT("new_element_value")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// list_data_table_rows -----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("list_data_table_rows");
		T.Description = TEXT("Read all rows of a DataTable with their cells. Also returns the row struct definition (column names and types). Cell values longer than 200 chars are truncated.");
		T.InputSchema = MakeSchema({
			{ TEXT("data_table_path"), Prop(TEXT("string"), TEXT("DataTable asset path")) },
		}, { TEXT("data_table_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::DataAsset;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::ListDataTableRows(GetStr(In, TEXT("data_table_path")));
			return R;
		});
		RegisterTool(T);
	}

	// set_data_table_cell ------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_data_table_cell");
		T.Description = TEXT("Set a single cell in a DataTable by row name and column name. Value in Unreal text format. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("data_table_path"), Prop(TEXT("string"), TEXT("DataTable asset path")) },
			{ TEXT("row_name"), Prop(TEXT("string"), TEXT("Row name (FName)")) },
			{ TEXT("column_name"), Prop(TEXT("string"), TEXT("Column (property name on row struct)")) },
			{ TEXT("new_value"), Prop(TEXT("string"), TEXT("New value")) },
		}, { TEXT("data_table_path"), TEXT("row_name"), TEXT("column_name"), TEXT("new_value") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::DataAsset;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SetDataTableCell(
				GetStr(In, TEXT("data_table_path")),
				GetStr(In, TEXT("row_name")),
				GetStr(In, TEXT("column_name")),
				GetStr(In, TEXT("new_value")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== NEW: Control Rig / IK Rig / IK Retargeter ====================

	// get_control_rig_info -----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_control_rig_info");
		T.Description = TEXT("Read high-level info about a Control Rig asset: parent class, generated class, preview skeletal mesh, hierarchy element count, compile status.");
		T.InputSchema = MakeSchema({
			{ TEXT("control_rig_path"), Prop(TEXT("string"), TEXT("ControlRig BP asset path")) },
		}, { TEXT("control_rig_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::ControlRig;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetControlRigInfo(GetStr(In, TEXT("control_rig_path")));
			return R;
		});
		RegisterTool(T);
	}

	// get_control_rig_hierarchy ------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_control_rig_hierarchy");
		T.Description = TEXT("List all elements in a Control Rig's URigHierarchy: bones, controls, nulls, curves. Returns names grouped by element type.");
		T.InputSchema = MakeSchema({
			{ TEXT("control_rig_path"), Prop(TEXT("string"), TEXT("ControlRig BP asset path")) },
		}, { TEXT("control_rig_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::ControlRig;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetControlRigHierarchy(GetStr(In, TEXT("control_rig_path")));
			return R;
		});
		RegisterTool(T);
	}

	// list_control_rig_graphs --------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("list_control_rig_graphs");
		T.Description = TEXT("List all graphs in a Control Rig (uber graphs like Setup/Forward Solve/Backward Solve, plus user functions). Returns graph name, kind, and node count.");
		T.InputSchema = MakeSchema({
			{ TEXT("control_rig_path"), Prop(TEXT("string"), TEXT("ControlRig BP asset path")) },
		}, { TEXT("control_rig_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::ControlRig;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::ListControlRigGraphs(GetStr(In, TEXT("control_rig_path")));
			return R;
		});
		RegisterTool(T);
	}

	// get_ik_rig_info ----------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_ik_rig_info");
		T.Description = TEXT("Read IK Rig asset info: preview skeletal mesh, retarget root bone, retarget chains (name + start/end bones), solver stack.");
		T.InputSchema = MakeSchema({
			{ TEXT("ik_rig_path"), Prop(TEXT("string"), TEXT("IK Rig asset path (e.g. '/Game/Characters/IK_Mannequin')")) },
		}, { TEXT("ik_rig_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::ControlRig;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetIKRigInfo(GetStr(In, TEXT("ik_rig_path")));
			return R;
		});
		RegisterTool(T);
	}

	// get_ik_retargeter_info ---------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_ik_retargeter_info");
		T.Description = TEXT("Read IK Retargeter info: source/target IK Rig paths, retarget poses, chain mapping count, current retarget pose.");
		T.InputSchema = MakeSchema({
			{ TEXT("retargeter_path"), Prop(TEXT("string"), TEXT("IK Retargeter asset path (e.g. '/Game/Characters/RTG_MannequinToMetaHuman')")) },
		}, { TEXT("retargeter_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::ControlRig;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetIKRetargeterInfo(GetStr(In, TEXT("retargeter_path")));
			return R;
		});
		RegisterTool(T);
	}

	// list_retarget_chains -----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("list_retarget_chains");
		T.Description = TEXT("List all chain mappings in an IK Retargeter: target chain name and mapped source chain name. Empty source_chain means that chain is unmapped.");
		T.InputSchema = MakeSchema({
			{ TEXT("retargeter_path"), Prop(TEXT("string"), TEXT("IK Retargeter asset path")) },
		}, { TEXT("retargeter_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::ControlRig;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::ListRetargetChains(GetStr(In, TEXT("retargeter_path")));
			return R;
		});
		RegisterTool(T);
	}

	// create_ik_retargeter -----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_ik_retargeter");
		T.Description = TEXT("Create a new IK Retargeter asset. Optionally pre-assign source and target IK Rigs — typically this is how you set up retargeting between UE Mannequin and MetaHuman, or between different characters. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/Characters/RTG_MannequinToMetaHuman'")) },
			{ TEXT("source_ik_rig"), Prop(TEXT("string"), TEXT("Optional source IK Rig path")) },
			{ TEXT("target_ik_rig"), Prop(TEXT("string"), TEXT("Optional target IK Rig path")) },
		}, { TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::ControlRig;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateIKRetargeter(
				GetStr(In, TEXT("new_asset_path")),
				GetStr(In, TEXT("source_ik_rig")),
				GetStr(In, TEXT("target_ik_rig")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// set_retarget_chain_mapping -----------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_retarget_chain_mapping");
		T.Description = TEXT("Assign a source chain to a specific target chain in an IK Retargeter. target_chain_name must match one of the target IK rig's retarget chains. Use list_retarget_chains to see valid names. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("retargeter_path"), Prop(TEXT("string"), TEXT("IK Retargeter asset path")) },
			{ TEXT("source_chain_name"), Prop(TEXT("string"), TEXT("Source chain name")) },
			{ TEXT("target_chain_name"), Prop(TEXT("string"), TEXT("Target chain name")) },
		}, { TEXT("retargeter_path"), TEXT("source_chain_name"), TEXT("target_chain_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::ControlRig;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SetRetargetChainMapping(
				GetStr(In, TEXT("retargeter_path")),
				GetStr(In, TEXT("source_chain_name")),
				GetStr(In, TEXT("target_chain_name")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// auto_map_retarget_chains -------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("auto_map_retarget_chains");
		T.Description = TEXT("Automatically map source → target chains in a retargeter by name matching (exact case-insensitive, then substring). Like the 'Auto Map' button in the IK Retargeter editor. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("retargeter_path"), Prop(TEXT("string"), TEXT("IK Retargeter asset path")) },
		}, { TEXT("retargeter_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::ControlRig;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AutoMapRetargetChains(
				GetStr(In, TEXT("retargeter_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== NEW: Visual capture ==========================================
	//
	// These tools attach base64-encoded PNG data to the tool result, so the
	// model can actually see the screenshot in the next turn. Expect each
	// screenshot to cost ~1500-2500 input tokens when the result is read.

	// capture_viewport ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("capture_viewport");
		T.Description = TEXT("Capture a screenshot of the active editor viewport (what the user is currently looking at). Returns a PNG image attached to the tool result — you will see it in the next turn. Default 1024x768. Warning: each screenshot costs ~2000 input tokens when returned.");
		T.InputSchema = MakeSchema({
			{ TEXT("width"), IntProp(TEXT("Width in pixels (256-2048)"), 1024) },
			{ TEXT("height"), IntProp(TEXT("Height in pixels (256-2048)"), 768) },
			{ TEXT("show_ui"), BoolProp(TEXT("Include editor UI overlay"), false) },
		}, {});
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Visual;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err; FString Image;
			R.Content = FClaudeContextProvider::CaptureViewport(
				GetInt(In, TEXT("width"), 1024),
				GetInt(In, TEXT("height"), 768),
				GetBool(In, TEXT("show_ui"), false),
				Image, Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; return R; }
			R.ImageBase64 = Image;
			R.ImageMediaType = TEXT("image/png");
			return R;
		});
		RegisterTool(T);
	}

	// capture_from_camera ------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("capture_from_camera");
		T.Description = TEXT("Capture a screenshot from a specific camera position and rotation. Useful when you want to look at a specific actor or area from a chosen angle. Returns a PNG attached to the tool result.");
		T.InputSchema = MakeSchema({
			{ TEXT("x"), Prop(TEXT("number"), TEXT("Camera location X")) },
			{ TEXT("y"), Prop(TEXT("number"), TEXT("Camera location Y")) },
			{ TEXT("z"), Prop(TEXT("number"), TEXT("Camera location Z")) },
			{ TEXT("pitch"), Prop(TEXT("number"), TEXT("Camera pitch (degrees, negative = looking down)")) },
			{ TEXT("yaw"), Prop(TEXT("number"), TEXT("Camera yaw (degrees)")) },
			{ TEXT("roll"), Prop(TEXT("number"), TEXT("Camera roll (degrees)")) },
			{ TEXT("width"), IntProp(TEXT("Width in pixels (256-2048)"), 1024) },
			{ TEXT("height"), IntProp(TEXT("Height in pixels (256-2048)"), 768) },
			{ TEXT("fov"), Prop(TEXT("number"), TEXT("Field of view in degrees (default 90)")) },
		}, { TEXT("x"), TEXT("y"), TEXT("z") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Visual;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err; FString Image;
			double X=0, Y=0, Z=0, P=0, Yaw=0, Rl=0, FOV=0;
			In->TryGetNumberField(TEXT("x"), X);
			In->TryGetNumberField(TEXT("y"), Y);
			In->TryGetNumberField(TEXT("z"), Z);
			In->TryGetNumberField(TEXT("pitch"), P);
			In->TryGetNumberField(TEXT("yaw"), Yaw);
			In->TryGetNumberField(TEXT("roll"), Rl);
			In->TryGetNumberField(TEXT("fov"), FOV);
			R.Content = FClaudeContextProvider::CaptureFromCamera(
				FVector(X, Y, Z),
				FRotator(P, Yaw, Rl),
				GetInt(In, TEXT("width"), 1024),
				GetInt(In, TEXT("height"), 768),
				(float)FOV,
				Image, Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; return R; }
			R.ImageBase64 = Image;
			R.ImageMediaType = TEXT("image/png");
			return R;
		});
		RegisterTool(T);
	}

	// capture_asset_thumbnail --------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("capture_asset_thumbnail");
		T.Description = TEXT("Render the content browser thumbnail of any asset (StaticMesh, Material, Texture, Blueprint, Niagara, etc). Much cheaper than a full viewport screenshot — useful for quickly previewing what an asset looks like.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Asset path")) },
			{ TEXT("size"), IntProp(TEXT("Thumbnail size in pixels (128-1024)"), 256) },
		}, { TEXT("asset_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Visual;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err; FString Image;
			R.Content = FClaudeContextProvider::CaptureAssetThumbnail(
				GetStr(In, TEXT("asset_path")),
				GetInt(In, TEXT("size"), 256),
				Image, Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; return R; }
			R.ImageBase64 = Image;
			R.ImageMediaType = TEXT("image/png");
			return R;
		});
		RegisterTool(T);
	}

	// ===== STAGE 1: PIE — Play-In-Editor =================================

	// pie_spawn_actor ----------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("pie_spawn_actor");
		T.Description = TEXT("Spawn an actor in the running PIE world (requires Play started). Class can be a native class name or a Blueprint asset path. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("class_path"), Prop(TEXT("string"), TEXT("Class to spawn (e.g. '/Game/AI/BP_Enemy.BP_Enemy_C' or 'StaticMeshActor')")) },
			{ TEXT("x"), Prop(TEXT("number"), TEXT("Location X")) },
			{ TEXT("y"), Prop(TEXT("number"), TEXT("Location Y")) },
			{ TEXT("z"), Prop(TEXT("number"), TEXT("Location Z")) },
			{ TEXT("pitch"), Prop(TEXT("number"), TEXT("Rotation pitch")) },
			{ TEXT("yaw"), Prop(TEXT("number"), TEXT("Rotation yaw")) },
			{ TEXT("roll"), Prop(TEXT("number"), TEXT("Rotation roll")) },
		}, { TEXT("class_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::PIE;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			double X=0, Y=0, Z=0, P=0, Yw=0, Rl=0;
			In->TryGetNumberField(TEXT("x"), X);
			In->TryGetNumberField(TEXT("y"), Y);
			In->TryGetNumberField(TEXT("z"), Z);
			In->TryGetNumberField(TEXT("pitch"), P);
			In->TryGetNumberField(TEXT("yaw"), Yw);
			In->TryGetNumberField(TEXT("roll"), Rl);
			R.Content = FClaudeContextProvider::PIESpawnActor(
				GetStr(In, TEXT("class_path")),
				FVector(X, Y, Z), FRotator(P, Yw, Rl), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// pie_destroy_actor --------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("pie_destroy_actor");
		T.Description = TEXT("Destroy an actor in the running PIE world by name or label. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_name"), Prop(TEXT("string"), TEXT("Actor name or label")) },
		}, { TEXT("actor_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::PIE;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::PIEDestroyActor(GetStr(In, TEXT("actor_name")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// pie_teleport_actor -------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("pie_teleport_actor");
		T.Description = TEXT("Teleport an actor to a new location/rotation in PIE. Useful for putting AI/player in test positions. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_name"), Prop(TEXT("string"), TEXT("Target actor")) },
			{ TEXT("x"), Prop(TEXT("number"), TEXT("X")) },
			{ TEXT("y"), Prop(TEXT("number"), TEXT("Y")) },
			{ TEXT("z"), Prop(TEXT("number"), TEXT("Z")) },
			{ TEXT("pitch"), Prop(TEXT("number"), TEXT("Pitch")) },
			{ TEXT("yaw"), Prop(TEXT("number"), TEXT("Yaw")) },
			{ TEXT("roll"), Prop(TEXT("number"), TEXT("Roll")) },
		}, { TEXT("actor_name"), TEXT("x"), TEXT("y"), TEXT("z") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::PIE;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			double X=0, Y=0, Z=0, P=0, Yw=0, Rl=0;
			In->TryGetNumberField(TEXT("x"), X);
			In->TryGetNumberField(TEXT("y"), Y);
			In->TryGetNumberField(TEXT("z"), Z);
			In->TryGetNumberField(TEXT("pitch"), P);
			In->TryGetNumberField(TEXT("yaw"), Yw);
			In->TryGetNumberField(TEXT("roll"), Rl);
			R.Content = FClaudeContextProvider::PIETeleportActor(GetStr(In, TEXT("actor_name")), FVector(X,Y,Z), FRotator(P,Yw,Rl), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// pie_get_property ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("pie_get_property");
		T.Description = TEXT("Read a property on a PIE actor by dotted path (e.g. 'Health' or 'CharacterMovement.MaxWalkSpeed'). Returns current value.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_name"), Prop(TEXT("string"), TEXT("Actor name")) },
			{ TEXT("property_name"), Prop(TEXT("string"), TEXT("Property path")) },
		}, { TEXT("actor_name"), TEXT("property_name") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::PIE;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::PIEGetProperty(GetStr(In, TEXT("actor_name")), GetStr(In, TEXT("property_name")));
			return R;
		});
		RegisterTool(T);
	}

	// pie_set_property ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("pie_set_property");
		T.Description = TEXT("Set a property on a PIE actor at runtime. Useful for testing different values without stopping play. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_name"), Prop(TEXT("string"), TEXT("Actor name")) },
			{ TEXT("property_name"), Prop(TEXT("string"), TEXT("Property path")) },
			{ TEXT("new_value"), Prop(TEXT("string"), TEXT("New value (Unreal text format)")) },
		}, { TEXT("actor_name"), TEXT("property_name"), TEXT("new_value") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::PIE;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::PIESetProperty(GetStr(In, TEXT("actor_name")), GetStr(In, TEXT("property_name")), GetStr(In, TEXT("new_value")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// pie_get_blackboard_key ---------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("pie_get_blackboard_key");
		T.Description = TEXT("Read a Blackboard key value on a PIE AI controller. Pawn name resolves to its AIController automatically.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_name"), Prop(TEXT("string"), TEXT("Pawn or AIController name")) },
			{ TEXT("key_name"), Prop(TEXT("string"), TEXT("Blackboard key")) },
		}, { TEXT("actor_name"), TEXT("key_name") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::PIE;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::PIEGetBlackboardKey(GetStr(In, TEXT("actor_name")), GetStr(In, TEXT("key_name")));
			return R;
		});
		RegisterTool(T);
	}

	// pie_set_blackboard_key ---------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("pie_set_blackboard_key");
		T.Description = TEXT("Set a Blackboard key value at runtime. For Object keys, pass another PIE actor's name to set as reference. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_name"), Prop(TEXT("string"), TEXT("Pawn name")) },
			{ TEXT("key_name"), Prop(TEXT("string"), TEXT("Blackboard key")) },
			{ TEXT("new_value"), Prop(TEXT("string"), TEXT("Value as text — for vectors use 'X=0,Y=0,Z=0', for objects use actor name")) },
		}, { TEXT("actor_name"), TEXT("key_name"), TEXT("new_value") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::PIE;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::PIESetBlackboardKey(GetStr(In, TEXT("actor_name")), GetStr(In, TEXT("key_name")), GetStr(In, TEXT("new_value")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// pie_move_ai_to -----------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("pie_move_ai_to");
		T.Description = TEXT("Force a PIE AI controller to MoveTo a location. Uses navmesh pathfinding. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_name"), Prop(TEXT("string"), TEXT("Pawn or AIController name")) },
			{ TEXT("x"), Prop(TEXT("number"), TEXT("Destination X")) },
			{ TEXT("y"), Prop(TEXT("number"), TEXT("Destination Y")) },
			{ TEXT("z"), Prop(TEXT("number"), TEXT("Destination Z")) },
			{ TEXT("acceptance_radius"), Prop(TEXT("number"), TEXT("Stop within this distance (default 50)")) },
		}, { TEXT("actor_name"), TEXT("x"), TEXT("y"), TEXT("z") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::PIE;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			double X=0, Y=0, Z=0, Rad=-1;
			In->TryGetNumberField(TEXT("x"), X);
			In->TryGetNumberField(TEXT("y"), Y);
			In->TryGetNumberField(TEXT("z"), Z);
			In->TryGetNumberField(TEXT("acceptance_radius"), Rad);
			R.Content = FClaudeContextProvider::PIEMoveAITo(GetStr(In, TEXT("actor_name")), FVector(X,Y,Z), (float)Rad, Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// pie_stop_ai --------------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("pie_stop_ai");
		T.Description = TEXT("Stop AI movement and brain logic on a PIE pawn. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_name"), Prop(TEXT("string"), TEXT("Pawn name")) },
		}, { TEXT("actor_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::PIE;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::PIEStopAI(GetStr(In, TEXT("actor_name")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// pie_get_game_state -------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("pie_get_game_state");
		T.Description = TEXT("Get summary of the current PIE state: paused, time, actor count, player pawn location.");
		T.InputSchema = MakeSchema({}, {});
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::PIE;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::PIEGetGameState();
			return R;
		});
		RegisterTool(T);
	}

	// pie_list_actors ----------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("pie_list_actors");
		T.Description = TEXT("List actors in the running PIE world, optionally filtered by class and/or name substring. Capped at 200.");
		T.InputSchema = MakeSchema({
			{ TEXT("class_filter"), Prop(TEXT("string"), TEXT("Optional class name (e.g. 'Pawn', 'StaticMeshActor')")) },
			{ TEXT("name_contains"), Prop(TEXT("string"), TEXT("Optional name substring")) },
		}, {});
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::PIE;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::PIEListActors(GetStr(In, TEXT("class_filter")), GetStr(In, TEXT("name_contains")));
			return R;
		});
		RegisterTool(T);
	}

	// pie_console_command ------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("pie_console_command");
		T.Description = TEXT("Execute a console command in the PIE session. Examples: 'stat fps', 'slomo 0.1', 'show collision', 'GodMode'. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("command"), Prop(TEXT("string"), TEXT("Console command")) },
		}, { TEXT("command") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::PIE;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::PIEConsoleCommand(GetStr(In, TEXT("command")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== STAGE 1: Debug ===============================================

	// bp_get_compile_errors ----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("bp_get_compile_errors");
		T.Description = TEXT("Get compile status and any errors/warnings on a Blueprint. Walks all graphs and reports nodes with compiler messages.");
		T.InputSchema = MakeSchema({
			{ TEXT("blueprint_path"), Prop(TEXT("string"), TEXT("BP asset path")) },
		}, { TEXT("blueprint_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Debug;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::BPGetCompileErrors(GetStr(In, TEXT("blueprint_path")));
			return R;
		});
		RegisterTool(T);
	}

	// bp_refresh_all_nodes -----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("bp_refresh_all_nodes");
		T.Description = TEXT("Reconstruct all nodes in a Blueprint and recompile. Fixes 'Refresh All Nodes' editor button equivalent — useful after API changes or asset moves. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("blueprint_path"), Prop(TEXT("string"), TEXT("BP asset path")) },
		}, { TEXT("blueprint_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Debug;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::BPRefreshAllNodes(GetStr(In, TEXT("blueprint_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// bp_fix_broken_references -------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("bp_fix_broken_references");
		T.Description = TEXT("Detect pins with broken object/class references on a Blueprint. Detection only — manual fix required (open BP and rewire).");
		T.InputSchema = MakeSchema({
			{ TEXT("blueprint_path"), Prop(TEXT("string"), TEXT("BP asset path")) },
		}, { TEXT("blueprint_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Debug;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::BPFixBrokenReferences(GetStr(In, TEXT("blueprint_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// bp_fix_deprecated_nodes --------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("bp_fix_deprecated_nodes");
		T.Description = TEXT("Reconstruct nodes that call deprecated functions in a Blueprint. UE will try to map old → new where possible. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("blueprint_path"), Prop(TEXT("string"), TEXT("BP asset path")) },
		}, { TEXT("blueprint_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Debug;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::BPFixDeprecatedNodes(GetStr(In, TEXT("blueprint_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// bp_find_unconnected_pins -------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("bp_find_unconnected_pins");
		T.Description = TEXT("Find unconnected execution output pins (dead branches) in a Blueprint. Useful for finding incomplete logic.");
		T.InputSchema = MakeSchema({
			{ TEXT("blueprint_path"), Prop(TEXT("string"), TEXT("BP asset path")) },
		}, { TEXT("blueprint_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Debug;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::BPFindUnconnectedPins(GetStr(In, TEXT("blueprint_path")));
			return R;
		});
		RegisterTool(T);
	}

	// find_orphan_assets -------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("find_orphan_assets");
		T.Description = TEXT("Find assets in the project with no incoming references via the Asset Registry. Useful for cleanup. WARNING: 'orphan' may include assets only referenced via redirectors/soft-class-refs/at runtime — verify before deleting.");
		T.InputSchema = MakeSchema({
			{ TEXT("search_path"), Prop(TEXT("string"), TEXT("Path to scan (default '/Game')")) },
			{ TEXT("max_results"), IntProp(TEXT("Cap on results (default 50)"), 50) },
		}, {});
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Debug;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::FindOrphanAssets(GetStr(In, TEXT("search_path")), GetInt(In, TEXT("max_results"), 50));
			return R;
		});
		RegisterTool(T);
	}

	// find_circular_dependencies -----------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("find_circular_dependencies");
		T.Description = TEXT("Detect 2-cycles between assets (A↔B). Helps catch tightly-coupled BPs that may cause cook/load issues. Longer cycles are NOT detected for performance.");
		T.InputSchema = MakeSchema({
			{ TEXT("search_path"), Prop(TEXT("string"), TEXT("Path to scan (default '/Game')")) },
			{ TEXT("max_results"), IntProp(TEXT("Cap on results (default 50)"), 50) },
		}, {});
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Debug;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::FindCircularDependencies(GetStr(In, TEXT("search_path")), GetInt(In, TEXT("max_results"), 50));
			return R;
		});
		RegisterTool(T);
	}

	// get_dependency_tree ------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_dependency_tree");
		T.Description = TEXT("Get the asset dependency tree for a specific asset. Useful for understanding what an asset pulls in.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Asset path")) },
			{ TEXT("max_depth"), IntProp(TEXT("Max recursion depth (1-8, default 3)"), 3) },
		}, { TEXT("asset_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Debug;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetDependencyTree(GetStr(In, TEXT("asset_path")), GetInt(In, TEXT("max_depth"), 3));
			return R;
		});
		RegisterTool(T);
	}

	// ===== STAGE 1: Splines =============================================

	// create_spline_actor ------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_spline_actor");
		T.Description = TEXT("Spawn a new generic actor with a SplineComponent and 2 default points. Use add_spline_point and set_spline_point to extend. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("label"), Prop(TEXT("string"), TEXT("Actor label (e.g. 'Path_Patrol_North')")) },
			{ TEXT("x"), Prop(TEXT("number"), TEXT("Spawn X")) },
			{ TEXT("y"), Prop(TEXT("number"), TEXT("Spawn Y")) },
			{ TEXT("z"), Prop(TEXT("number"), TEXT("Spawn Z")) },
		}, {});
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Spline;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err; FString OutPath;
			double X=0, Y=0, Z=0;
			In->TryGetNumberField(TEXT("x"), X);
			In->TryGetNumberField(TEXT("y"), Y);
			In->TryGetNumberField(TEXT("z"), Z);
			R.Content = FClaudeContextProvider::CreateSplineActor(GetStr(In, TEXT("label")), FVector(X,Y,Z), OutPath, Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// add_spline_point ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_spline_point");
		T.Description = TEXT("Append a point to a spline actor at world position. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_name"), Prop(TEXT("string"), TEXT("Spline actor")) },
			{ TEXT("x"), Prop(TEXT("number"), TEXT("X")) },
			{ TEXT("y"), Prop(TEXT("number"), TEXT("Y")) },
			{ TEXT("z"), Prop(TEXT("number"), TEXT("Z")) },
		}, { TEXT("actor_name"), TEXT("x"), TEXT("y"), TEXT("z") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Spline;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			double X=0, Y=0, Z=0;
			In->TryGetNumberField(TEXT("x"), X);
			In->TryGetNumberField(TEXT("y"), Y);
			In->TryGetNumberField(TEXT("z"), Z);
			R.Content = FClaudeContextProvider::AddSplinePoint(GetStr(In, TEXT("actor_name")), FVector(X,Y,Z), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// set_spline_point ---------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_spline_point");
		T.Description = TEXT("Modify an existing spline point: position and optionally type (Linear / Curve / CurveClamped / Constant). REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_name"), Prop(TEXT("string"), TEXT("Spline actor")) },
			{ TEXT("point_index"), IntProp(TEXT("Point index"), 0) },
			{ TEXT("x"), Prop(TEXT("number"), TEXT("X")) },
			{ TEXT("y"), Prop(TEXT("number"), TEXT("Y")) },
			{ TEXT("z"), Prop(TEXT("number"), TEXT("Z")) },
			{ TEXT("point_type"), Prop(TEXT("string"), TEXT("Optional: Linear/Curve/CurveClamped/Constant")) },
		}, { TEXT("actor_name"), TEXT("point_index"), TEXT("x"), TEXT("y"), TEXT("z") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Spline;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			double X=0, Y=0, Z=0;
			In->TryGetNumberField(TEXT("x"), X);
			In->TryGetNumberField(TEXT("y"), Y);
			In->TryGetNumberField(TEXT("z"), Z);
			R.Content = FClaudeContextProvider::SetSplinePoint(GetStr(In, TEXT("actor_name")), GetInt(In, TEXT("point_index"), 0), FVector(X,Y,Z), GetStr(In, TEXT("point_type")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// remove_spline_point ------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("remove_spline_point");
		T.Description = TEXT("Remove a point from a spline by index. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_name"), Prop(TEXT("string"), TEXT("Spline actor")) },
			{ TEXT("point_index"), IntProp(TEXT("Point index"), 0) },
		}, { TEXT("actor_name"), TEXT("point_index") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Spline;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::RemoveSplinePoint(GetStr(In, TEXT("actor_name")), GetInt(In, TEXT("point_index"), 0), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// get_spline_info ----------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_spline_info");
		T.Description = TEXT("Read full spline info: closed loop, point count, length, and each point's location and type.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_name"), Prop(TEXT("string"), TEXT("Spline actor")) },
		}, { TEXT("actor_name") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Spline;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetSplineInfo(GetStr(In, TEXT("actor_name")));
			return R;
		});
		RegisterTool(T);
	}

	// set_spline_closed --------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_spline_closed");
		T.Description = TEXT("Set whether a spline forms a closed loop. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("actor_name"), Prop(TEXT("string"), TEXT("Spline actor")) },
			{ TEXT("closed"), BoolProp(TEXT("Closed loop?"), false) },
		}, { TEXT("actor_name"), TEXT("closed") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Spline;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SetSplineClosed(GetStr(In, TEXT("actor_name")), GetBool(In, TEXT("closed"), false), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== STAGE 1: Environment / Lighting / Atmospherics ===============

	// set_post_process_settings ------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_post_process_settings");
		T.Description = TEXT("Set Post Process Volume settings. Pass JSON with field/value pairs (auto-prefixed with 'Settings.' if needed). Examples: {\"VignetteIntensity\":0.7, \"BloomIntensity\":1.5}. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("volume_name"), Prop(TEXT("string"), TEXT("Post Process Volume actor name")) },
			{ TEXT("json_settings"), Prop(TEXT("string"), TEXT("JSON object of property:value pairs")) },
		}, { TEXT("volume_name"), TEXT("json_settings") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Environment;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SetPostProcessSettings(GetStr(In, TEXT("volume_name")), GetStr(In, TEXT("json_settings")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// set_fog_properties -------------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_fog_properties");
		T.Description = TEXT("Set ExponentialHeightFog properties. Examples: {\"FogDensity\":0.05, \"FogHeightFalloff\":0.2}. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("fog_name"), Prop(TEXT("string"), TEXT("Fog actor name")) },
			{ TEXT("json_props"), Prop(TEXT("string"), TEXT("JSON object")) },
		}, { TEXT("fog_name"), TEXT("json_props") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Environment;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SetFogProperties(GetStr(In, TEXT("fog_name")), GetStr(In, TEXT("json_props")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// set_sky_atmosphere_properties --------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_sky_atmosphere_properties");
		T.Description = TEXT("Set SkyAtmosphere actor properties. Examples: {\"MultiScatteringFactor\":1.0, \"MieAnisotropy\":0.8}. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("sky_name"), Prop(TEXT("string"), TEXT("Sky actor name")) },
			{ TEXT("json_props"), Prop(TEXT("string"), TEXT("JSON object")) },
		}, { TEXT("sky_name"), TEXT("json_props") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Environment;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SetSkyAtmosphereProperties(GetStr(In, TEXT("sky_name")), GetStr(In, TEXT("json_props")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// set_light_properties -----------------------------------------------
	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_light_properties");
		T.Description = TEXT("Set light actor properties. Works on DirectionalLight, PointLight, SpotLight, RectLight, SkyLight. Examples: {\"Intensity\":3.0, \"LightColor\":{\"R\":1,\"G\":0.8,\"B\":0.4,\"A\":1}, \"CastShadows\":true}. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("light_name"), Prop(TEXT("string"), TEXT("Light actor name")) },
			{ TEXT("json_props"), Prop(TEXT("string"), TEXT("JSON object")) },
		}, { TEXT("light_name"), TEXT("json_props") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Environment;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SetLightProperties(GetStr(In, TEXT("light_name")), GetStr(In, TEXT("json_props")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== STAGE 2: Niagara =============================================

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_niagara_system");
		T.Description = TEXT("Create a new empty Niagara System asset (.uasset). After creation, add emitters via add_niagara_emitter or open in editor. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/VFX/NS_Fire'")) },
		}, { TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Niagara;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateNiagaraSystem(GetStr(In, TEXT("new_asset_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("read_niagara_system");
		T.Description = TEXT("Read structure of a Niagara System: emitters with names and enabled flags, exposed parameter store info.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Niagara System path")) },
		}, { TEXT("asset_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Niagara;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::ReadNiagaraSystem(GetStr(In, TEXT("asset_path")));
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_niagara_parameter");
		T.Description = TEXT("Set a Niagara user parameter on the asset. NOTE: asset-level parameter editing has limited support — runtime instance setting is preferred via UNiagaraComponent. May return guidance instead of actually setting. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Niagara System path")) },
			{ TEXT("parameter_name"), Prop(TEXT("string"), TEXT("User.SomeParam style name")) },
			{ TEXT("new_value"), Prop(TEXT("string"), TEXT("Value as text")) },
		}, { TEXT("asset_path"), TEXT("parameter_name"), TEXT("new_value") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Niagara;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SetNiagaraParameter(GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("parameter_name")), GetStr(In, TEXT("new_value")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_niagara_emitter");
		T.Description = TEXT("Add a UNiagaraEmitter asset as an emitter handle in a UNiagaraSystem. May fail in some UE versions where the API is not BlueprintCallable. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("system_path"), Prop(TEXT("string"), TEXT("Target Niagara System")) },
			{ TEXT("emitter_asset_path"), Prop(TEXT("string"), TEXT("Source Niagara Emitter asset")) },
		}, { TEXT("system_path"), TEXT("emitter_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Niagara;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddNiagaraEmitter(GetStr(In, TEXT("system_path")), GetStr(In, TEXT("emitter_asset_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== STAGE 2: Sequencer ===========================================

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_level_sequence");
		T.Description = TEXT("Create a new empty Level Sequence asset (.uasset) for cinematics or scripted in-game cutscenes. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/Cinematics/LS_Intro'")) },
		}, { TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Sequencer;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateLevelSequence(GetStr(In, TEXT("new_asset_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("read_level_sequence");
		T.Description = TEXT("Read Level Sequence structure: playback range (in seconds), display rate (FPS), all bindings (possessables/spawnables), master tracks.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Level Sequence path")) },
		}, { TEXT("asset_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::Sequencer;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::ReadLevelSequence(GetStr(In, TEXT("asset_path")));
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_sequence_binding");
		T.Description = TEXT("Bind an actor in the level to a possessable in a Level Sequence. After this, you can add tracks for that binding. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Level Sequence path")) },
			{ TEXT("actor_name"), Prop(TEXT("string"), TEXT("Actor name or label in the editor world")) },
		}, { TEXT("asset_path"), TEXT("actor_name") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Sequencer;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddSequenceBinding(GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("actor_name")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_sequence_track");
		T.Description = TEXT("Add a track to an existing binding in a Level Sequence. Supported track types: Transform, Float, Visibility. Creates a default section spanning current playback range. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Level Sequence path")) },
			{ TEXT("binding_name"), Prop(TEXT("string"), TEXT("Name of the binding (actor's label)")) },
			{ TEXT("track_type"), Prop(TEXT("string"), TEXT("Transform / Float / Visibility")) },
		}, { TEXT("asset_path"), TEXT("binding_name"), TEXT("track_type") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Sequencer;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddSequenceTrack(GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("binding_name")), GetStr(In, TEXT("track_type")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_sequence_keyframe");
		T.Description = TEXT("Add a keyframe to a Float or Visibility track at the given time (seconds). Float value should be a number; Visibility value 'true'/'false'. Transform keyframing not yet supported. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Level Sequence path")) },
			{ TEXT("binding_name"), Prop(TEXT("string"), TEXT("Binding name")) },
			{ TEXT("track_type"), Prop(TEXT("string"), TEXT("Float or Visibility")) },
			{ TEXT("time_seconds"), Prop(TEXT("number"), TEXT("Time in seconds")) },
			{ TEXT("value"), Prop(TEXT("string"), TEXT("Value (number or true/false)")) },
		}, { TEXT("asset_path"), TEXT("binding_name"), TEXT("track_type"), TEXT("time_seconds"), TEXT("value") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Sequencer;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			double Time = 0;
			In->TryGetNumberField(TEXT("time_seconds"), Time);
			R.Content = FClaudeContextProvider::AddSequenceKeyframe(
				GetStr(In, TEXT("asset_path")),
				GetStr(In, TEXT("binding_name")),
				GetStr(In, TEXT("track_type")),
				(float)Time,
				GetStr(In, TEXT("value")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_sequence_range");
		T.Description = TEXT("Set the playback range of a Level Sequence in seconds. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Level Sequence path")) },
			{ TEXT("start_seconds"), Prop(TEXT("number"), TEXT("Start time")) },
			{ TEXT("end_seconds"), Prop(TEXT("number"), TEXT("End time")) },
		}, { TEXT("asset_path"), TEXT("start_seconds"), TEXT("end_seconds") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::Sequencer;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			double S = 0, E = 0;
			In->TryGetNumberField(TEXT("start_seconds"), S);
			In->TryGetNumberField(TEXT("end_seconds"), E);
			R.Content = FClaudeContextProvider::SetSequenceRange(GetStr(In, TEXT("asset_path")), (float)S, (float)E, Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== STAGE 2: Pose Search =========================================

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_pose_search_schema");
		T.Description = TEXT("Create a Pose Search Schema asset (defines features for motion matching). Optionally pre-set the target Skeleton. Configuration of bones/trajectory must be done in editor. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/Animation/PSS_Locomotion'")) },
			{ TEXT("skeleton_path"), Prop(TEXT("string"), TEXT("Optional skeleton asset")) },
		}, { TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::PoseSearch;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreatePoseSearchSchema(GetStr(In, TEXT("new_asset_path")), GetStr(In, TEXT("skeleton_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_pose_search_database");
		T.Description = TEXT("Create a Pose Search Database asset that uses the given schema. After creation, add animations via add_pose_search_animation (limited support). REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/Animation/PSDB_Locomotion'")) },
			{ TEXT("schema_path"), Prop(TEXT("string"), TEXT("Pose Search Schema asset")) },
		}, { TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::PoseSearch;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreatePoseSearchDatabase(GetStr(In, TEXT("new_asset_path")), GetStr(In, TEXT("schema_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("read_pose_search_database");
		T.Description = TEXT("Read Pose Search Database info: schema path, animation count.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("Pose Search Database path")) },
		}, { TEXT("asset_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::PoseSearch;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::ReadPoseSearchDatabase(GetStr(In, TEXT("asset_path")));
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("add_pose_search_animation");
		T.Description = TEXT("Add an AnimSequence to a Pose Search Database. NOTE: programmatic addition uses fragile reflection over FInstancedStruct — may return guidance to do it manually instead. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("database_path"), Prop(TEXT("string"), TEXT("Pose Search Database")) },
			{ TEXT("anim_sequence_path"), Prop(TEXT("string"), TEXT("AnimSequence to add")) },
		}, { TEXT("database_path"), TEXT("anim_sequence_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::PoseSearch;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::AddPoseSearchAnimation(GetStr(In, TEXT("database_path")), GetStr(In, TEXT("anim_sequence_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	// ===== STAGE 2: GAS =================================================

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_gameplay_ability");
		T.Description = TEXT("Create a Blueprint asset derived from UGameplayAbility (or a custom parent ability class). REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/Abilities/GA_Dash'")) },
			{ TEXT("parent_ability_class"), Prop(TEXT("string"), TEXT("Optional parent class")) },
		}, { TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::GAS;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateGameplayAbility(GetStr(In, TEXT("new_asset_path")), GetStr(In, TEXT("parent_ability_class")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_gameplay_effect");
		T.Description = TEXT("Create a Blueprint asset derived from UGameplayEffect. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/Effects/GE_Damage'")) },
		}, { TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::GAS;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateGameplayEffect(GetStr(In, TEXT("new_asset_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("create_attribute_set");
		T.Description = TEXT("Create a Blueprint asset derived from UAttributeSet. NOTE: AttributeSets are typically defined in C++ for full functionality. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("new_asset_path"), Prop(TEXT("string"), TEXT("e.g. '/Game/Attributes/AS_Combat'")) },
		}, { TEXT("new_asset_path") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::GAS;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::CreateAttributeSet(GetStr(In, TEXT("new_asset_path")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("list_gameplay_abilities");
		T.Description = TEXT("List all Blueprint assets derived from UGameplayAbility, optionally filtered by path.");
		T.InputSchema = MakeSchema({
			{ TEXT("path_filter"), Prop(TEXT("string"), TEXT("Optional path filter")) },
		}, {});
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::GAS;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::ListGameplayAbilities(GetStr(In, TEXT("path_filter")));
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("list_gameplay_effects");
		T.Description = TEXT("List all Blueprint assets derived from UGameplayEffect, optionally filtered by path.");
		T.InputSchema = MakeSchema({
			{ TEXT("path_filter"), Prop(TEXT("string"), TEXT("Optional path filter")) },
		}, {});
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::GAS;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::ListGameplayEffects(GetStr(In, TEXT("path_filter")));
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("list_attribute_sets");
		T.Description = TEXT("List all Blueprint assets derived from UAttributeSet, optionally filtered by path.");
		T.InputSchema = MakeSchema({
			{ TEXT("path_filter"), Prop(TEXT("string"), TEXT("Optional path filter")) },
		}, {});
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::GAS;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::ListAttributeSets(GetStr(In, TEXT("path_filter")));
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("get_gas_info");
		T.Description = TEXT("Read structure of a GAS asset (Ability/Effect/AttributeSet): type, key properties, and for AttributeSets the list of attributes.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("GAS BP asset path")) },
		}, { TEXT("asset_path") });
		T.bIsReadOnly = true;
		T.Category = EClaudeToolCategory::GAS;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R;
			R.Content = FClaudeContextProvider::GetGASInfo(GetStr(In, TEXT("asset_path")));
			return R;
		});
		RegisterTool(T);
	}

	{
		FClaudeToolDefinition T;
		T.Name = TEXT("set_gameplay_effect_property");
		T.Description = TEXT("Set a property on a GameplayEffect Blueprint's CDO using a dotted path. Supports nested struct/array indexing. Examples: 'DurationPolicy=Instant', 'Period=1.0'. REQUIRES CONFIRMATION.");
		T.InputSchema = MakeSchema({
			{ TEXT("asset_path"), Prop(TEXT("string"), TEXT("GameplayEffect BP path")) },
			{ TEXT("property_path"), Prop(TEXT("string"), TEXT("Dotted path")) },
			{ TEXT("new_value"), Prop(TEXT("string"), TEXT("Value (Unreal text)")) },
		}, { TEXT("asset_path"), TEXT("property_path"), TEXT("new_value") });
		T.bIsReadOnly = false;
		T.Category = EClaudeToolCategory::GAS;
		T.Handler = FClaudeToolHandler::CreateLambda([](const TSharedPtr<FJsonObject>& In)
		{
			FClaudeToolResult R; FString Err;
			R.Content = FClaudeContextProvider::SetGameplayEffectProperty(GetStr(In, TEXT("asset_path")), GetStr(In, TEXT("property_path")), GetStr(In, TEXT("new_value")), Err);
			if (!Err.IsEmpty()) { R.bIsError = true; R.Content = Err; }
			return R;
		});
		RegisterTool(T);
	}
}
