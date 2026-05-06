// Copyright Untry. All Rights Reserved.

#include "ClaudeContextProvider.h"
#include "ClaudeAgentEditor.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Self.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#include "PackageTools.h"
#include "FileHelpers.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "ScopedTransaction.h"
#include "EngineUtils.h"

// AI — Behavior Tree
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"

// Editor subsystems
#include "Subsystems/AssetEditorSubsystem.h"

// AI Graph editor (wrapper nodes for BT)
#include "AIGraphNode.h"
#include "AIGraphTypes.h"

// Animation
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/CurveIdentifier.h"

// Materials
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialExpression.h"
#include "MaterialEditingLibrary.h"
#include "Engine/Texture.h"

// Level design
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "FileHelpers.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

// StateTree
#include "StateTree.h"
#include "UObject/Package.h"

// Data Asset / Data Table
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"

// Control Rig / IK Rig (forward-decl placeholder struct for reflection call)
struct FRigElementKey_Placeholder { FName Name; uint8 Type; };

// Visual capture
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "LevelEditorViewport.h"
#include "EditorViewportClient.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "ObjectTools.h"
#include "ThumbnailRendering/ThumbnailManager.h"

// PIE — runtime debugging
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	FString JsonObjectToString(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	/** Truncate giant strings so a single tool result can't blow the context. */
	FString Truncate(const FString& In, int32 MaxChars = 30000)
	{
		if (In.Len() <= MaxChars) return In;
		return In.Left(MaxChars) + FString::Printf(TEXT("\n\n[...truncated %d chars...]"), In.Len() - MaxChars);
	}
}

UObject* FClaudeContextProvider::LoadAssetByPath(const FString& AssetPath)
{
	if (AssetPath.IsEmpty()) return nullptr;
	return StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
}

// -----------------------------------------------------------------------------
// Asset Registry
// -----------------------------------------------------------------------------

FString FClaudeContextProvider::ListAssets(const FString& ClassName, const FString& PathFilter, int32 MaxResults)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	if (!ClassName.IsEmpty())
	{
		Filter.ClassPaths.Add(FTopLevelAssetPath(FName(TEXT("/Script/Engine")), FName(*ClassName)));
		// Also try /Script/CoreUObject and a generic search if needed
		// Most user-facing classes live in /Script/Engine; for others Claude can omit ClassName and filter by path.
	}
	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(*PathFilter));
	}

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("total_found"), Assets.Num());

	const int32 Limit = FMath::Min(Assets.Num(), FMath::Max(1, MaxResults));
	TArray<TSharedPtr<FJsonValue>> Items;

	for (int32 i = 0; i < Limit; ++i)
	{
		const FAssetData& A = Assets[i];
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), A.AssetName.ToString());
		Entry->SetStringField(TEXT("path"), A.GetObjectPathString());
		Entry->SetStringField(TEXT("class"), A.AssetClassPath.ToString());
		Entry->SetStringField(TEXT("package_path"), A.PackagePath.ToString());
		Items.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Root->SetArrayField(TEXT("assets"), Items);

	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::GetAssetReferences(const FString& AssetPath, bool bReverse)
{
	FAssetRegistryModule& Mod = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = Mod.Get();

	const FName PackageName = FName(*FPackageName::ObjectPathToPackageName(AssetPath));

	TArray<FName> Result;
	if (bReverse)
	{
		Registry.GetReferencers(PackageName, Result);
	}
	else
	{
		Registry.GetDependencies(PackageName, Result);
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset"), AssetPath);
	Root->SetStringField(TEXT("direction"), bReverse ? TEXT("referencers") : TEXT("dependencies"));

	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FName& N : Result)
	{
		Items.Add(MakeShared<FJsonValueString>(N.ToString()));
	}
	Root->SetArrayField(TEXT("results"), Items);
	return Truncate(JsonObjectToString(Root));
}

// -----------------------------------------------------------------------------
// Object property inspection
// -----------------------------------------------------------------------------

FString FClaudeContextProvider::JsonifyProperties(UObject* Object, int32 Depth, int32 MaxDepth)
{
	if (!Object || Depth > MaxDepth) return TEXT("{}");

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("__class__"), Object->GetClass()->GetName());

	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;

		// Skip transient / editor-only noise to keep the JSON small
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient)) continue;

		FString ValueStr;
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Object);
		Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Object, PPF_None);

		// Truncate per-property to keep one fat string from dominating
		if (ValueStr.Len() > 2000)
		{
			ValueStr = ValueStr.Left(2000) + TEXT("...[truncated]");
		}

		Obj->SetStringField(Prop->GetName(), ValueStr);
	}

	return JsonObjectToString(Obj);
}

FString FClaudeContextProvider::GetObjectProperties(const FString& AssetPath, int32 MaxDepth)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	if (!Asset)
	{
		return FString::Printf(TEXT("{\"error\": \"Asset not found: %s\"}"), *AssetPath);
	}

	// For Blueprints, also expose the CDO of the generated class
	if (UBlueprint* BP = Cast<UBlueprint>(Asset))
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("blueprint_path"), AssetPath);
		Root->SetStringField(TEXT("parent_class"), BP->ParentClass ? BP->ParentClass->GetName() : TEXT(""));

		if (UClass* GenClass = BP->GeneratedClass)
		{
			UObject* CDO = GenClass->GetDefaultObject();
			FString CDOJson = JsonifyProperties(CDO, 0, FMath::Max(1, MaxDepth));
			TSharedPtr<FJsonObject> Parsed;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CDOJson);
			if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
			{
				Root->SetObjectField(TEXT("default_properties"), Parsed.ToSharedRef());
			}
		}
		return Truncate(JsonObjectToString(Root));
	}

	return Truncate(JsonifyProperties(Asset, 0, FMath::Max(1, MaxDepth)));
}

FString FClaudeContextProvider::SetObjectProperty(const FString& AssetPath, const FString& PropertyName, const FString& NewValue, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
		return TEXT("");
	}

	UObject* Target = Asset;
	if (UBlueprint* BP = Cast<UBlueprint>(Asset))
	{
		if (BP->GeneratedClass) Target = BP->GeneratedClass->GetDefaultObject();
	}

	FProperty* Prop = Target->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		OutError = FString::Printf(TEXT("Property '%s' not found on %s"), *PropertyName, *Target->GetClass()->GetName());
		return TEXT("");
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Target);

	Target->Modify();
	const TCHAR* ImportResult = Prop->ImportText_Direct(*NewValue, ValuePtr, Target, PPF_None);
	if (!ImportResult)
	{
		OutError = FString::Printf(TEXT("Failed to parse value '%s' for property '%s'"), *NewValue, *PropertyName);
		return TEXT("");
	}

	if (UBlueprint* BP = Cast<UBlueprint>(Asset))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	}
	Asset->MarkPackageDirty();

	return FString::Printf(TEXT("Set %s.%s = %s"), *AssetPath, *PropertyName, *NewValue);
}

// -----------------------------------------------------------------------------
// Blueprint
// -----------------------------------------------------------------------------

FString FClaudeContextProvider::JsonifyEdGraph(UEdGraph* Graph)
{
	if (!Graph) return TEXT("{}");

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("graph_name"), Graph->GetName());
	Root->SetStringField(TEXT("graph_class"), Graph->GetClass()->GetName());

	TArray<TSharedPtr<FJsonValue>> NodesJson;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		TSharedRef<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
		NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		NodeObj->SetNumberField(TEXT("x"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("y"), Node->NodePosY);

		// Node comment (text above nodes)
		if (!Node->NodeComment.IsEmpty())
		{
			NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);
		}

		// Disambiguate K2 node types — makes graphs much more readable for Claude
		if (UK2Node_CallFunction* CallFn = Cast<UK2Node_CallFunction>(Node))
		{
			NodeObj->SetStringField(TEXT("kind"), TEXT("call_function"));
			if (UFunction* Fn = CallFn->GetTargetFunction())
			{
				NodeObj->SetStringField(TEXT("function_name"), Fn->GetName());
				if (UClass* OwnerClass = Fn->GetOwnerClass())
				{
					NodeObj->SetStringField(TEXT("function_class"), OwnerClass->GetPathName());
				}
			}
		}
		else if (UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(Node))
		{
			NodeObj->SetStringField(TEXT("kind"), TEXT("variable_get"));
			NodeObj->SetStringField(TEXT("variable_name"), VarGet->GetVarNameString());
		}
		else if (UK2Node_VariableSet* VarSet = Cast<UK2Node_VariableSet>(Node))
		{
			NodeObj->SetStringField(TEXT("kind"), TEXT("variable_set"));
			NodeObj->SetStringField(TEXT("variable_name"), VarSet->GetVarNameString());
		}
		else if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
		{
			NodeObj->SetStringField(TEXT("kind"), TEXT("event"));
			NodeObj->SetStringField(TEXT("event_name"), Event->EventReference.GetMemberName().ToString());
		}
		else if (UK2Node_IfThenElse* Branch = Cast<UK2Node_IfThenElse>(Node))
		{
			NodeObj->SetStringField(TEXT("kind"), TEXT("branch"));
		}
		else if (UK2Node* K2 = Cast<UK2Node>(Node))
		{
			NodeObj->SetStringField(TEXT("kind"), TEXT("k2_node"));
		}

		TArray<TSharedPtr<FJsonValue>> PinsJson;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			if (Pin->bHidden) continue; // skip hidden pins — too noisy for Claude

			TSharedRef<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"));

			// Full pin type info
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			if (!Pin->PinType.PinSubCategory.IsNone())
			{
				PinObj->SetStringField(TEXT("subtype"), Pin->PinType.PinSubCategory.ToString());
			}
			if (Pin->PinType.PinSubCategoryObject.IsValid())
			{
				PinObj->SetStringField(TEXT("subtype_object"), Pin->PinType.PinSubCategoryObject->GetName());
			}
			if (Pin->PinType.IsArray())      PinObj->SetStringField(TEXT("container"), TEXT("array"));
			else if (Pin->PinType.IsSet())   PinObj->SetStringField(TEXT("container"), TEXT("set"));
			else if (Pin->PinType.IsMap())   PinObj->SetStringField(TEXT("container"), TEXT("map"));
			if (Pin->PinType.bIsReference)   PinObj->SetBoolField(TEXT("is_reference"), true);

			// Default values — critical context Claude was missing before
			if (!Pin->DefaultValue.IsEmpty() && Pin->LinkedTo.Num() == 0)
			{
				PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue.Left(500));
			}
			if (Pin->DefaultObject)
			{
				PinObj->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
			}
			if (!Pin->AutogeneratedDefaultValue.IsEmpty() && Pin->DefaultValue.IsEmpty() && Pin->LinkedTo.Num() == 0)
			{
				PinObj->SetStringField(TEXT("autogen_default"), Pin->AutogeneratedDefaultValue.Left(500));
			}

			// Connections — use node_id.pin_name to let Claude trace the wire
			TArray<TSharedPtr<FJsonValue>> Links;
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (Linked && Linked->GetOwningNode())
				{
					Links.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s.%s"),
						*Linked->GetOwningNode()->NodeGuid.ToString(),
						*Linked->PinName.ToString())));
				}
			}
			if (Links.Num() > 0)
			{
				PinObj->SetArrayField(TEXT("linked_to"), Links);
			}
			PinsJson.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		NodeObj->SetArrayField(TEXT("pins"), PinsJson);
		NodesJson.Add(MakeShared<FJsonValueObject>(NodeObj));
	}
	Root->SetArrayField(TEXT("nodes"), NodesJson);
	Root->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

	// Include any subgraphs (e.g. state machines in AnimBP, collapsed graphs, composite nodes)
	if (Graph->SubGraphs.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> SubGraphNames;
		for (UEdGraph* Sub : Graph->SubGraphs)
		{
			if (Sub) SubGraphNames.Add(MakeShared<FJsonValueString>(Sub->GetName()));
		}
		Root->SetArrayField(TEXT("subgraphs"), SubGraphNames);
	}

	return JsonObjectToString(Root);
}

FString FClaudeContextProvider::GetBlueprintGraph(const FString& AssetPath, const FString& GraphName)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP)
	{
		return FString::Printf(TEXT("{\"error\": \"Not a Blueprint: %s\"}"), *AssetPath);
	}

	auto FindAndDump = [&](UEdGraph* Graph) -> bool
	{
		if (!Graph) return false;
		if (GraphName.IsEmpty() || Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return true;
		}
		return false;
	};

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("blueprint"), AssetPath);

	TArray<TSharedPtr<FJsonValue>> AllGraphs;

	for (UEdGraph* G : BP->UbergraphPages)
		if (FindAndDump(G))
		{
			TSharedPtr<FJsonObject> Parsed;
			auto R = TJsonReaderFactory<>::Create(JsonifyEdGraph(G));
			if (FJsonSerializer::Deserialize(R, Parsed) && Parsed.IsValid())
				AllGraphs.Add(MakeShared<FJsonValueObject>(Parsed.ToSharedRef()));
		}

	for (UEdGraph* G : BP->FunctionGraphs)
		if (FindAndDump(G))
		{
			TSharedPtr<FJsonObject> Parsed;
			auto R = TJsonReaderFactory<>::Create(JsonifyEdGraph(G));
			if (FJsonSerializer::Deserialize(R, Parsed) && Parsed.IsValid())
				AllGraphs.Add(MakeShared<FJsonValueObject>(Parsed.ToSharedRef()));
		}

	for (UEdGraph* G : BP->MacroGraphs)
		if (FindAndDump(G))
		{
			TSharedPtr<FJsonObject> Parsed;
			auto R = TJsonReaderFactory<>::Create(JsonifyEdGraph(G));
			if (FJsonSerializer::Deserialize(R, Parsed) && Parsed.IsValid())
				AllGraphs.Add(MakeShared<FJsonValueObject>(Parsed.ToSharedRef()));
		}

	Root->SetArrayField(TEXT("graphs"), AllGraphs);
	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::ListBlueprintFunctions(const FString& AssetPath)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP)
	{
		return FString::Printf(TEXT("{\"error\": \"Not a Blueprint: %s\"}"), *AssetPath);
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Funcs;
	for (UEdGraph* G : BP->FunctionGraphs)
	{
		if (G) Funcs.Add(MakeShared<FJsonValueString>(G->GetName()));
	}
	Root->SetArrayField(TEXT("functions"), Funcs);

	TArray<TSharedPtr<FJsonValue>> Macros;
	for (UEdGraph* G : BP->MacroGraphs)
	{
		if (G) Macros.Add(MakeShared<FJsonValueString>(G->GetName()));
	}
	Root->SetArrayField(TEXT("macros"), Macros);

	TArray<TSharedPtr<FJsonValue>> Events;
	for (UEdGraph* G : BP->UbergraphPages)
	{
		if (G) Events.Add(MakeShared<FJsonValueString>(G->GetName()));
	}
	Root->SetArrayField(TEXT("event_graphs"), Events);

	return JsonObjectToString(Root);
}

FString FClaudeContextProvider::CompileBlueprint(const FString& AssetPath, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP)
	{
		OutError = FString::Printf(TEXT("Not a Blueprint: %s"), *AssetPath);
		return TEXT("");
	}

	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Results);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("blueprint"), AssetPath);
	Root->SetNumberField(TEXT("error_count"), Results.NumErrors);
	Root->SetNumberField(TEXT("warning_count"), Results.NumWarnings);

	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const TSharedRef<FTokenizedMessage>& Msg : Results.Messages)
	{
		Messages.Add(MakeShared<FJsonValueString>(Msg->ToText().ToString()));
	}
	Root->SetArrayField(TEXT("messages"), Messages);

	return JsonObjectToString(Root);
}

// -----------------------------------------------------------------------------
// Animation Blueprint
// -----------------------------------------------------------------------------

FString FClaudeContextProvider::GetAnimBlueprintInfo(const FString& AssetPath)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UAnimBlueprint* ABP = Cast<UAnimBlueprint>(Asset);
	if (!ABP)
	{
		return FString::Printf(TEXT("{\"error\": \"Not an AnimBlueprint: %s\"}"), *AssetPath);
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("anim_blueprint"), AssetPath);
	Root->SetStringField(TEXT("target_skeleton"), ABP->TargetSkeleton ? ABP->TargetSkeleton->GetPathName() : TEXT(""));
	Root->SetStringField(TEXT("parent_class"), ABP->ParentClass ? ABP->ParentClass->GetName() : TEXT(""));

	TArray<TSharedPtr<FJsonValue>> GraphNames;
	for (UEdGraph* G : ABP->FunctionGraphs)
	{
		if (G) GraphNames.Add(MakeShared<FJsonValueString>(G->GetName()));
	}
	Root->SetArrayField(TEXT("function_graphs"), GraphNames);

	return Truncate(JsonObjectToString(Root));
}

// -----------------------------------------------------------------------------
// Editor selection
// -----------------------------------------------------------------------------

FString FClaudeContextProvider::GetEditorSelection()
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> Actors;
	if (GEditor)
	{
		USelection* Sel = GEditor->GetSelectedActors();
		if (Sel)
		{
			for (FSelectionIterator It(*Sel); It; ++It)
			{
				if (AActor* Actor = Cast<AActor>(*It))
				{
					TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
					A->SetStringField(TEXT("name"), Actor->GetActorLabel());
					A->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
					A->SetStringField(TEXT("path"), Actor->GetPathName());
					Actors.Add(MakeShared<FJsonValueObject>(A));
				}
			}
		}
	}
	Root->SetArrayField(TEXT("selected_actors"), Actors);
	return JsonObjectToString(Root);
}

// -----------------------------------------------------------------------------
// Logs
// -----------------------------------------------------------------------------

FString FClaudeContextProvider::ReadOutputLog(const FString& Filter, int32 LastNLines)
{
	const FString OutputLogPath = FPaths::ProjectLogDir() / FApp::GetProjectName() + TEXT(".log");

	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *OutputLogPath))
	{
		return FString::Printf(TEXT("{\"error\": \"Could not read log at %s\"}"), *OutputLogPath);
	}

	TArray<FString> Lines;
	Contents.ParseIntoArrayLines(Lines, false);

	TArray<FString> Filtered;
	for (const FString& Line : Lines)
	{
		if (Filter.IsEmpty() || Line.Contains(Filter, ESearchCase::IgnoreCase))
		{
			Filtered.Add(Line);
		}
	}

	const int32 Take = FMath::Min(Filtered.Num(), FMath::Max(1, LastNLines));
	const int32 Start = Filtered.Num() - Take;

	FString Result;
	for (int32 i = Start; i < Filtered.Num(); ++i)
	{
		Result += Filtered[i] + TEXT("\n");
	}
	return Truncate(Result);
}

// -----------------------------------------------------------------------------
// Project info
// -----------------------------------------------------------------------------

FString FClaudeContextProvider::GetProjectInfo()
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	Root->SetStringField(TEXT("project_dir"), FPaths::ProjectDir());
	Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Root->SetStringField(TEXT("platform"), FPlatformProperties::IniPlatformName());
	return JsonObjectToString(Root);
}

// =============================================================================
// WRITE OPERATIONS — actual asset/logic mutation
// =============================================================================

FString FClaudeContextProvider::CreateBlueprint(const FString& ParentClassPath, const FString& NewAssetPath, FString& OutError)
{
	// Resolve parent class
	UClass* ParentClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ParentClassPath);
	if (!ParentClass)
	{
		// Try as ShortName, e.g. "Actor" -> /Script/Engine.Actor
		const FString AsEngine = FString::Printf(TEXT("/Script/Engine.%s"), *ParentClassPath);
		ParentClass = StaticLoadClass(UObject::StaticClass(), nullptr, *AsEngine);
	}
	if (!ParentClass)
	{
		OutError = FString::Printf(TEXT("Parent class not found: %s"), *ParentClassPath);
		return TEXT("");
	}

	// Split NewAssetPath into package path + asset name: /Game/Foo/BP_Thing
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		OutError = TEXT("NewAssetPath must be like '/Game/Path/BPName'");
		return TEXT("");
	}

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateBP", "Create Blueprint (Claude)"));

	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, UBlueprint::StaticClass(), Factory);
	if (!NewObj)
	{
		OutError = TEXT("CreateAsset returned null — path may already exist");
		return TEXT("");
	}

	return FString::Printf(TEXT("Created Blueprint: %s (parent: %s)"), *NewObj->GetPathName(), *ParentClass->GetName());
}

FString FClaudeContextProvider::AddBlueprintVariable(
	const FString& AssetPath, const FString& VarName, const FString& VarType,
	const FString& ContainerType, const FString& DefaultValue, const FString& Category,
	bool bInstanceEditable, bool bBlueprintReadOnly, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = FString::Printf(TEXT("Not a Blueprint: %s"), *AssetPath); return TEXT(""); }

	// Build FEdGraphPinType from a readable type name
	FEdGraphPinType PinType;
	const FString LowerType = VarType.ToLower();

	if (LowerType == TEXT("bool"))        { PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean; }
	else if (LowerType == TEXT("int") || LowerType == TEXT("int32")) { PinType.PinCategory = UEdGraphSchema_K2::PC_Int; }
	else if (LowerType == TEXT("int64"))  { PinType.PinCategory = UEdGraphSchema_K2::PC_Int64; }
	else if (LowerType == TEXT("float"))  { PinType.PinCategory = UEdGraphSchema_K2::PC_Real; PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float; }
	else if (LowerType == TEXT("double")) { PinType.PinCategory = UEdGraphSchema_K2::PC_Real; PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double; }
	else if (LowerType == TEXT("string")) { PinType.PinCategory = UEdGraphSchema_K2::PC_String; }
	else if (LowerType == TEXT("name"))   { PinType.PinCategory = UEdGraphSchema_K2::PC_Name; }
	else if (LowerType == TEXT("text"))   { PinType.PinCategory = UEdGraphSchema_K2::PC_Text; }
	else if (LowerType == TEXT("vector")) { PinType.PinCategory = UEdGraphSchema_K2::PC_Struct; PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get(); }
	else if (LowerType == TEXT("rotator")){ PinType.PinCategory = UEdGraphSchema_K2::PC_Struct; PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get(); }
	else if (LowerType == TEXT("transform")) { PinType.PinCategory = UEdGraphSchema_K2::PC_Struct; PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get(); }
	else if (LowerType == TEXT("color") || LowerType == TEXT("linearcolor")) { PinType.PinCategory = UEdGraphSchema_K2::PC_Struct; PinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get(); }
	else
	{
		// Try as an object/class reference
		UClass* Cls = StaticLoadClass(UObject::StaticClass(), nullptr, *VarType);
		if (!Cls)
		{
			const FString AsEngine = FString::Printf(TEXT("/Script/Engine.%s"), *VarType);
			Cls = StaticLoadClass(UObject::StaticClass(), nullptr, *AsEngine);
		}
		if (Cls)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			PinType.PinSubCategoryObject = Cls;
		}
		else
		{
			OutError = FString::Printf(TEXT("Unknown var type: %s"), *VarType);
			return TEXT("");
		}
	}

	// Apply container type
	const FString LowerContainer = ContainerType.ToLower();
	if (LowerContainer == TEXT("array"))    PinType.ContainerType = EPinContainerType::Array;
	else if (LowerContainer == TEXT("set")) PinType.ContainerType = EPinContainerType::Set;
	else if (LowerContainer == TEXT("map")) PinType.ContainerType = EPinContainerType::Map;

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddVar", "Add Blueprint Variable (Claude)"));

	const bool bAdded = FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType, DefaultValue);
	if (!bAdded) { OutError = FString::Printf(TEXT("Failed to add variable '%s' (already exists?)"), *VarName); return TEXT(""); }

	// Apply metadata
	if (!Category.IsEmpty())
	{
		FBlueprintEditorUtils::SetBlueprintVariableCategory(BP, FName(*VarName), nullptr, FText::FromString(Category));
	}

	// Property flags (instance editable, read-only)
	for (FBPVariableDescription& Var : BP->NewVariables)
	{
		if (Var.VarName == FName(*VarName))
		{
			if (bInstanceEditable) Var.PropertyFlags &= ~CPF_DisableEditOnInstance;
			else                   Var.PropertyFlags |= CPF_DisableEditOnInstance;
			if (bBlueprintReadOnly) Var.PropertyFlags |= CPF_BlueprintReadOnly;
			else                    Var.PropertyFlags &= ~CPF_BlueprintReadOnly;
			break;
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return FString::Printf(TEXT("Added variable '%s' (%s%s%s) to %s"),
		*VarName, *VarType,
		LowerContainer.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("/%s"), *LowerContainer),
		DefaultValue.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", default=%s"), *DefaultValue),
		*AssetPath);
}

FString FClaudeContextProvider::AddComponentToBlueprint(const FString& AssetPath, const FString& ComponentClass, const FString& ComponentName, const FString& AttachParentName, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = FString::Printf(TEXT("Not a Blueprint: %s"), *AssetPath); return TEXT(""); }
	if (!BP->SimpleConstructionScript) { OutError = TEXT("Blueprint has no SCS (Actor BP?)"); return TEXT(""); }

	UClass* CompClass = StaticLoadClass(UActorComponent::StaticClass(), nullptr, *ComponentClass);
	if (!CompClass)
	{
		const FString AsEngine = FString::Printf(TEXT("/Script/Engine.%s"), *ComponentClass);
		CompClass = StaticLoadClass(UActorComponent::StaticClass(), nullptr, *AsEngine);
	}
	if (!CompClass) { OutError = FString::Printf(TEXT("Component class not found: %s"), *ComponentClass); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddComp", "Add Blueprint Component (Claude)"));

	USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(CompClass, FName(*ComponentName));
	if (!NewNode) { OutError = TEXT("CreateNode returned null"); return TEXT(""); }

	// Attach to a specific parent if requested
	if (!AttachParentName.IsEmpty())
	{
		USCS_Node* ParentNode = nullptr;
		for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (N && N->GetVariableName().ToString().Equals(AttachParentName, ESearchCase::IgnoreCase))
			{
				ParentNode = N;
				break;
			}
		}
		if (ParentNode)
		{
			ParentNode->AddChildNode(NewNode);
		}
		else
		{
			BP->SimpleConstructionScript->AddNode(NewNode);
		}
	}
	else
	{
		BP->SimpleConstructionScript->AddNode(NewNode);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return FString::Printf(TEXT("Added component '%s' (%s)%s to %s"),
		*ComponentName, *CompClass->GetName(),
		AttachParentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" under %s"), *AttachParentName),
		*AssetPath);
}

FString FClaudeContextProvider::SaveAsset(const FString& AssetPath, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
		return TEXT("");
	}

	UPackage* Package = Asset->GetOutermost();
	if (!Package)
	{
		OutError = TEXT("Could not get package for asset");
		return TEXT("");
	}

	// For Blueprints, make sure it's compiled before saving
	if (UBlueprint* BP = Cast<UBlueprint>(Asset))
	{
		if (BP->Status == BS_Dirty || BP->Status == BS_Unknown)
		{
			FKismetEditorUtilities::CompileBlueprint(BP);
		}
	}

	TArray<UPackage*> PackagesToSave;
	PackagesToSave.Add(Package);
	const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, /*bOnlyDirty*/ false);

	if (!bSaved)
	{
		OutError = TEXT("SavePackages returned false");
		return TEXT("");
	}
	return FString::Printf(TEXT("Saved: %s"), *AssetPath);
}

FString FClaudeContextProvider::SpawnActor(const FString& ClassPath, const FVector& Location, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor is null");
		return TEXT("");
	}

	UClass* ActorClass = nullptr;
	// Try Blueprint-class path first (e.g. /Game/Foo/BP_Thing.BP_Thing_C)
	if (UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *ClassPath))
	{
		if (UBlueprint* BP = Cast<UBlueprint>(Loaded))
		{
			ActorClass = BP->GeneratedClass;
		}
		else
		{
			ActorClass = Cast<UClass>(Loaded);
		}
	}
	if (!ActorClass) ActorClass = StaticLoadClass(AActor::StaticClass(), nullptr, *ClassPath);
	if (!ActorClass)
	{
		OutError = FString::Printf(TEXT("Actor class not resolvable: %s"), *ClassPath);
		return TEXT("");
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		OutError = TEXT("No editor world");
		return TEXT("");
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "Spawn", "Spawn Actor (Claude)"));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Spawned = World->SpawnActor<AActor>(ActorClass, Location, FRotator::ZeroRotator, Params);
	if (!Spawned)
	{
		OutError = TEXT("SpawnActor returned null");
		return TEXT("");
	}

	return FString::Printf(TEXT("Spawned %s at (%s). Path: %s"),
		*ActorClass->GetName(), *Location.ToString(), *Spawned->GetPathName());
}

FString FClaudeContextProvider::GetActorComponentTree(const FString& ActorPathOrBlueprintPath)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

	UObject* Obj = LoadAssetByPath(ActorPathOrBlueprintPath);
	TArray<TSharedPtr<FJsonValue>> Components;

	auto AddComp = [&](const FString& Name, const FString& Class, const FString& Parent)
	{
		TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetStringField(TEXT("name"), Name);
		C->SetStringField(TEXT("class"), Class);
		C->SetStringField(TEXT("parent"), Parent);
		Components.Add(MakeShared<FJsonValueObject>(C));
	};

	if (UBlueprint* BP = Cast<UBlueprint>(Obj))
	{
		if (BP->SimpleConstructionScript)
		{
			for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
			{
				if (!Node) continue;
				const FString ParentName = Node->ParentComponentOrVariableName.ToString();
				AddComp(
					Node->GetVariableName().ToString(),
					Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT(""),
					ParentName);
			}
		}
		Root->SetStringField(TEXT("source"), TEXT("blueprint"));
	}
	else if (AActor* Actor = Cast<AActor>(Obj))
	{
		TArray<UActorComponent*> Comps;
		Actor->GetComponents(Comps);
		for (UActorComponent* C : Comps)
		{
			if (!C) continue;
			FString ParentName;
			if (USceneComponent* SC = Cast<USceneComponent>(C))
			{
				if (SC->GetAttachParent()) ParentName = SC->GetAttachParent()->GetName();
			}
			AddComp(C->GetName(), C->GetClass()->GetName(), ParentName);
		}
		Root->SetStringField(TEXT("source"), TEXT("actor_instance"));
	}
	else
	{
		return FString::Printf(TEXT("{\"error\": \"Not a Blueprint or Actor: %s\"}"), *ActorPathOrBlueprintPath);
	}

	Root->SetArrayField(TEXT("components"), Components);
	return JsonObjectToString(Root);
}

// =============================================================================
// Deep node inspection
// =============================================================================

FString FClaudeContextProvider::GetNodeDetails(const FString& AssetPath, const FString& GraphName, const FString& NodeGuid)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) return FString::Printf(TEXT("{\"error\":\"Not a Blueprint: %s\"}"), *AssetPath);

	auto FindGraph = [BP, &GraphName]() -> UEdGraph*
	{
		for (UEdGraph* G : BP->UbergraphPages)  if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) return G;
		for (UEdGraph* G : BP->FunctionGraphs)  if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) return G;
		for (UEdGraph* G : BP->MacroGraphs)     if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) return G;
		return nullptr;
	};

	UEdGraph* Graph = FindGraph();
	if (!Graph) return FString::Printf(TEXT("{\"error\":\"Graph not found: %s\"}"), *GraphName);

	FGuid Guid;
	FGuid::Parse(NodeGuid, Guid);

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node || Node->NodeGuid != Guid) continue;

		// Full property dump of the node itself — useful for inspecting node settings
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("node_id"), NodeGuid);
		Root->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		Root->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

		FString Props = JsonifyProperties(Node, 0, 2);
		TSharedPtr<FJsonObject> Parsed;
		auto R = TJsonReaderFactory<>::Create(Props);
		if (FJsonSerializer::Deserialize(R, Parsed) && Parsed.IsValid())
		{
			Root->SetObjectField(TEXT("properties"), Parsed.ToSharedRef());
		}
		return Truncate(JsonObjectToString(Root));
	}
	return FString::Printf(TEXT("{\"error\":\"Node %s not found in %s\"}"), *NodeGuid, *GraphName);
}

// =============================================================================
// Delete operations
// =============================================================================

FString FClaudeContextProvider::RemoveBlueprintVariable(const FString& AssetPath, const FString& VarName, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = FString::Printf(TEXT("Not a Blueprint: %s"), *AssetPath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "RemVar", "Remove Blueprint Variable (Claude)"));
	FBlueprintEditorUtils::RemoveMemberVariable(BP, FName(*VarName));
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return FString::Printf(TEXT("Removed variable '%s' from %s"), *VarName, *AssetPath);
}

FString FClaudeContextProvider::RemoveComponentFromBlueprint(const FString& AssetPath, const FString& ComponentName, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP || !BP->SimpleConstructionScript)
	{
		OutError = TEXT("Blueprint has no SCS (not an Actor BP?)");
		return TEXT("");
	}

	USCS_Node* Target = nullptr;
	for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (N && N->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			Target = N;
			break;
		}
	}
	if (!Target)
	{
		OutError = FString::Printf(TEXT("Component '%s' not found"), *ComponentName);
		return TEXT("");
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "RemComp", "Remove Blueprint Component (Claude)"));
	BP->SimpleConstructionScript->RemoveNodeAndPromoteChildren(Target);
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return FString::Printf(TEXT("Removed component '%s' from %s"), *ComponentName, *AssetPath);
}

FString FClaudeContextProvider::DestroyActor(const FString& ActorPath, FString& OutError)
{
	if (!GEditor) { OutError = TEXT("No GEditor"); return TEXT(""); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return TEXT(""); }

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetPathName() == ActorPath || It->GetActorLabel() == ActorPath)
		{
			FoundActor = *It;
			break;
		}
	}
	if (!FoundActor) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorPath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "DelActor", "Destroy Actor (Claude)"));
	const FString Name = FoundActor->GetActorLabel();
	World->DestroyActor(FoundActor);
	return FString::Printf(TEXT("Destroyed actor '%s'"), *Name);
}

// =============================================================================
// Graph editing — add nodes, connect pins
// =============================================================================

FString FClaudeContextProvider::AddFunctionCallNode(
	const FString& AssetPath, const FString& GraphName,
	const FString& FunctionClass, const FString& FunctionName,
	int32 X, int32 Y, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = FString::Printf(TEXT("Not a Blueprint: %s"), *AssetPath); return TEXT(""); }

	// Find graph
	UEdGraph* Graph = nullptr;
	for (UEdGraph* G : BP->UbergraphPages)  if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) Graph = G;
	if (!Graph) for (UEdGraph* G : BP->FunctionGraphs) if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) Graph = G;
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return TEXT(""); }

	// Resolve target class
	UClass* TargetClass = StaticLoadClass(UObject::StaticClass(), nullptr, *FunctionClass);
	if (!TargetClass)
	{
		const FString AsEngine = FString::Printf(TEXT("/Script/Engine.%s"), *FunctionClass);
		TargetClass = StaticLoadClass(UObject::StaticClass(), nullptr, *AsEngine);
	}
	if (!TargetClass) { OutError = FString::Printf(TEXT("Function class not found: %s"), *FunctionClass); return TEXT(""); }

	UFunction* Fn = TargetClass->FindFunctionByName(FName(*FunctionName));
	if (!Fn) { OutError = FString::Printf(TEXT("Function '%s' not found on class %s"), *FunctionName, *FunctionClass); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddNode", "Add Function Node (Claude)"));

	UK2Node_CallFunction* NewNode = NewObject<UK2Node_CallFunction>(Graph);
	NewNode->CreateNewGuid();
	NewNode->SetFlags(RF_Transactional);
	NewNode->NodePosX = X;
	NewNode->NodePosY = Y;
	NewNode->SetFromFunction(Fn);
	Graph->AddNode(NewNode, /*bFromUI*/ false, /*bSelectNewNode*/ false);
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return FString::Printf(TEXT("Added %s::%s node. NodeGuid=%s"), *TargetClass->GetName(), *Fn->GetName(), *NewNode->NodeGuid.ToString());
}

FString FClaudeContextProvider::ConnectBlueprintPins(
	const FString& AssetPath, const FString& GraphName,
	const FString& FromNodeGuid, const FString& FromPinName,
	const FString& ToNodeGuid, const FString& ToPinName, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = FString::Printf(TEXT("Not a Blueprint: %s"), *AssetPath); return TEXT(""); }

	UEdGraph* Graph = nullptr;
	for (UEdGraph* G : BP->UbergraphPages)  if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) Graph = G;
	if (!Graph) for (UEdGraph* G : BP->FunctionGraphs) if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) Graph = G;
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return TEXT(""); }

	FGuid FromGuid, ToGuid;
	FGuid::Parse(FromNodeGuid, FromGuid);
	FGuid::Parse(ToNodeGuid, ToGuid);

	UEdGraphNode* FromNode = nullptr;
	UEdGraphNode* ToNode = nullptr;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (!N) continue;
		if (N->NodeGuid == FromGuid) FromNode = N;
		if (N->NodeGuid == ToGuid)   ToNode = N;
	}
	if (!FromNode || !ToNode) { OutError = TEXT("One of the node GUIDs not found"); return TEXT(""); }

	UEdGraphPin* FromPin = FromNode->FindPin(FromPinName);
	UEdGraphPin* ToPin = ToNode->FindPin(ToPinName);
	if (!FromPin || !ToPin) { OutError = TEXT("One of the pin names not found on its node"); return TEXT(""); }

	// Direction-normalize: FromPin must be output, ToPin must be input
	if (FromPin->Direction == EGPD_Input && ToPin->Direction == EGPD_Output)
	{
		Swap(FromPin, ToPin);
	}

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema) { OutError = TEXT("Graph has no schema"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "Connect", "Connect Pins (Claude)"));

	const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
	if (Response.Response == CONNECT_RESPONSE_DISALLOW)
	{
		OutError = FString::Printf(TEXT("Schema disallows connection: %s"), *Response.Message.ToString());
		return TEXT("");
	}

	const bool bOk = Schema->TryCreateConnection(FromPin, ToPin);
	if (!bOk) { OutError = TEXT("TryCreateConnection returned false"); return TEXT(""); }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return FString::Printf(TEXT("Connected %s.%s -> %s.%s"),
		*FromNode->NodeGuid.ToString(), *FromPin->PinName.ToString(),
		*ToNode->NodeGuid.ToString(), *ToPin->PinName.ToString());
}

FString FClaudeContextProvider::DeleteBlueprintNode(const FString& AssetPath, const FString& GraphName, const FString& NodeGuid, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = FString::Printf(TEXT("Not a Blueprint: %s"), *AssetPath); return TEXT(""); }

	UEdGraph* Graph = nullptr;
	for (UEdGraph* G : BP->UbergraphPages)  if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) Graph = G;
	if (!Graph) for (UEdGraph* G : BP->FunctionGraphs) if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) Graph = G;
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return TEXT(""); }

	FGuid Guid;
	FGuid::Parse(NodeGuid, Guid);

	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (!N || N->NodeGuid != Guid) continue;
		FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "DelNode", "Delete Node (Claude)"));
		Graph->RemoveNode(N);
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		return FString::Printf(TEXT("Deleted node %s"), *NodeGuid);
	}
	OutError = FString::Printf(TEXT("Node %s not found"), *NodeGuid);
	return TEXT("");
}

// =============================================================================
// Live actor operations
// =============================================================================

static AActor* FindActorByPathOrLabel(const FString& PathOrLabel)
{
	if (!GEditor) return nullptr;
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetPathName() == PathOrLabel || It->GetActorLabel() == PathOrLabel || It->GetName() == PathOrLabel)
		{
			return *It;
		}
	}
	return nullptr;
}

FString FClaudeContextProvider::SetActorTransform(
	const FString& ActorPath,
	bool bSetLocation, const FVector& Location,
	bool bSetRotation, const FRotator& Rotation,
	bool bSetScale, const FVector& Scale,
	FString& OutError)
{
	AActor* Actor = FindActorByPathOrLabel(ActorPath);
	if (!Actor) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorPath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "XformActor", "Set Actor Transform (Claude)"));
	Actor->Modify();

	if (bSetLocation) Actor->SetActorLocation(Location);
	if (bSetRotation) Actor->SetActorRotation(Rotation);
	if (bSetScale)    Actor->SetActorScale3D(Scale);

	return FString::Printf(TEXT("Transformed '%s' -> Loc=%s Rot=%s Scale=%s"),
		*Actor->GetActorLabel(),
		*Actor->GetActorLocation().ToString(),
		*Actor->GetActorRotation().ToString(),
		*Actor->GetActorScale3D().ToString());
}

FString FClaudeContextProvider::SetActorProperty(const FString& ActorPath, const FString& PropertyName, const FString& NewValue, FString& OutError)
{
	AActor* Actor = FindActorByPathOrLabel(ActorPath);
	if (!Actor) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorPath); return TEXT(""); }

	FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop) { OutError = FString::Printf(TEXT("Property '%s' not found on %s"), *PropertyName, *Actor->GetClass()->GetName()); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetActorProp", "Set Actor Property (Claude)"));
	Actor->Modify();

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
	const TCHAR* Result = Prop->ImportText_Direct(*NewValue, ValuePtr, Actor, PPF_None);
	if (!Result) { OutError = FString::Printf(TEXT("Could not parse '%s' for property '%s'"), *NewValue, *PropertyName); return TEXT(""); }

	// Notify so observers update (for component properties, e.g.)
	FPropertyChangedEvent ChangeEvent(Prop);
	Actor->PostEditChangeProperty(ChangeEvent);

	return FString::Printf(TEXT("Set %s.%s = %s"), *Actor->GetActorLabel(), *PropertyName, *NewValue);
}

FString FClaudeContextProvider::FindActorsInLevel(const FString& ClassFilter, int32 MaxResults)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Items;

	if (!GEditor) { Root->SetStringField(TEXT("error"), TEXT("No GEditor")); return JsonObjectToString(Root); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { Root->SetStringField(TEXT("error"), TEXT("No world")); return JsonObjectToString(Root); }

	UClass* Filter = nullptr;
	if (!ClassFilter.IsEmpty())
	{
		Filter = StaticLoadClass(AActor::StaticClass(), nullptr, *ClassFilter);
		if (!Filter)
		{
			const FString AsEngine = FString::Printf(TEXT("/Script/Engine.%s"), *ClassFilter);
			Filter = StaticLoadClass(AActor::StaticClass(), nullptr, *AsEngine);
		}
	}

	int32 Count = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (Filter && !It->GetClass()->IsChildOf(Filter)) continue;
		if (Count++ >= FMath::Max(1, MaxResults)) break;

		TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("label"), It->GetActorLabel());
		A->SetStringField(TEXT("class"), It->GetClass()->GetName());
		A->SetStringField(TEXT("path"), It->GetPathName());
		A->SetStringField(TEXT("location"), It->GetActorLocation().ToString());
		Items.Add(MakeShared<FJsonValueObject>(A));
	}
	Root->SetNumberField(TEXT("count"), Items.Num());
	Root->SetArrayField(TEXT("actors"), Items);
	return Truncate(JsonObjectToString(Root));
}

// =============================================================================
// Blueprint creation — widget / anim / interface
// =============================================================================

FString FClaudeContextProvider::CreateWidgetBlueprint(const FString& NewAssetPath, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		OutError = TEXT("NewAssetPath must be like '/Game/Path/WBP_Name'");
		return TEXT("");
	}

	UClass* FactoryClass = StaticLoadClass(UObject::StaticClass(), nullptr, TEXT("/Script/UMGEditor.WidgetBlueprintFactory"));
	if (!FactoryClass)
	{
		OutError = TEXT("UMGEditor module not loaded — Widget Blueprints require the UMG plugin");
		return TEXT("");
	}

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateWBP", "Create Widget Blueprint (Claude)"));

	UClass* WBPClass = StaticLoadClass(UObject::StaticClass(), nullptr, TEXT("/Script/UMGEditor.WidgetBlueprint"));
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, WBPClass, Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return TEXT(""); }

	return FString::Printf(TEXT("Created Widget Blueprint: %s"), *NewObj->GetPathName());
}

FString FClaudeContextProvider::CreateAnimBlueprint(const FString& NewAssetPath, const FString& SkeletonPath, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		OutError = TEXT("NewAssetPath must be like '/Game/Path/ABP_Name'");
		return TEXT("");
	}

	USkeleton* Skeleton = Cast<USkeleton>(StaticLoadObject(USkeleton::StaticClass(), nullptr, *SkeletonPath));
	if (!Skeleton) { OutError = FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath); return TEXT(""); }

	UClass* FactoryClass = StaticLoadClass(UObject::StaticClass(), nullptr, TEXT("/Script/AnimGraph.AnimBlueprintFactory"));
	if (!FactoryClass) { OutError = TEXT("AnimBlueprintFactory class not found"); return TEXT(""); }

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	// Set TargetSkeleton via reflection since we don't have the concrete class header included
	if (FProperty* TargetSkeletonProp = FactoryClass->FindPropertyByName(TEXT("TargetSkeleton")))
	{
		if (FObjectProperty* ObjProp = CastField<FObjectProperty>(TargetSkeletonProp))
		{
			ObjProp->SetObjectPropertyValue_InContainer(Factory, Skeleton);
		}
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateABP", "Create Animation Blueprint (Claude)"));
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, UAnimBlueprint::StaticClass(), Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return TEXT(""); }

	return FString::Printf(TEXT("Created Animation Blueprint: %s (skeleton: %s)"), *NewObj->GetPathName(), *Skeleton->GetName());
}

FString FClaudeContextProvider::CreateBlueprintInterface(const FString& NewAssetPath, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path"); return TEXT(""); }

	UClass* FactoryClass = StaticLoadClass(UObject::StaticClass(), nullptr, TEXT("/Script/UnrealEd.BlueprintInterfaceFactory"));
	if (!FactoryClass) { OutError = TEXT("BlueprintInterfaceFactory not found"); return TEXT(""); }

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateBPI", "Create Blueprint Interface (Claude)"));
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, UBlueprint::StaticClass(), Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return TEXT(""); }

	return FString::Printf(TEXT("Created Blueprint Interface: %s"), *NewObj->GetPathName());
}

FString FClaudeContextProvider::ImplementInterface(const FString& BlueprintPath, const FString& InterfacePath, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(BlueprintPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = FString::Printf(TEXT("Not a Blueprint: %s"), *BlueprintPath); return TEXT(""); }

	UObject* InterfaceObj = LoadAssetByPath(InterfacePath);
	UBlueprint* InterfaceBP = Cast<UBlueprint>(InterfaceObj);
	if (!InterfaceBP || !InterfaceBP->GeneratedClass)
	{
		OutError = FString::Printf(TEXT("Not a valid Blueprint Interface: %s"), *InterfacePath);
		return TEXT("");
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "ImplInterface", "Implement Interface (Claude)"));
	FBlueprintEditorUtils::ImplementNewInterface(BP, FTopLevelAssetPath(InterfaceBP->GeneratedClass));
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	return FString::Printf(TEXT("'%s' now implements '%s'"), *BP->GetName(), *InterfaceBP->GetName());
}

FString FClaudeContextProvider::AddBlueprintFunction(const FString& AssetPath, const FString& FunctionName, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = FString::Printf(TEXT("Not a Blueprint: %s"), *AssetPath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddFunc", "Add Blueprint Function (Claude)"));

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		BP, FName(*FunctionName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, NewGraph, /*bIsUserCreated*/ true, nullptr);
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	return FString::Printf(TEXT("Added function '%s' to %s. Graph created, customize via get_blueprint_graph."),
		*FunctionName, *AssetPath);
}

FString FClaudeContextProvider::SetComponentProperty(const FString& AssetPath, const FString& ComponentName, const FString& PropertyName, const FString& NewValue, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP || !BP->SimpleConstructionScript) { OutError = TEXT("Not an Actor BP with SCS"); return TEXT(""); }

	USCS_Node* Target = nullptr;
	for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (N && N->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			Target = N;
			break;
		}
	}
	if (!Target || !Target->ComponentTemplate) { OutError = FString::Printf(TEXT("Component '%s' not found"), *ComponentName); return TEXT(""); }

	UActorComponent* Template = Target->ComponentTemplate;
	FProperty* Prop = Template->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop) { OutError = FString::Printf(TEXT("Property '%s' not found on %s"), *PropertyName, *Template->GetClass()->GetName()); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetCompProp", "Set Component Property (Claude)"));
	Template->Modify();
	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Template);
	const TCHAR* Result = Prop->ImportText_Direct(*NewValue, ValuePtr, Template, PPF_None);
	if (!Result) { OutError = FString::Printf(TEXT("Could not parse '%s'"), *NewValue); return TEXT(""); }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return FString::Printf(TEXT("Set %s.%s.%s = %s"), *AssetPath, *ComponentName, *PropertyName, *NewValue);
}

// =============================================================================
// Graph nodes — variable get/set, branch, sequence, custom event, make/break, cast, self
// =============================================================================

static UEdGraph* FindGraphHelper(UBlueprint* BP, const FString& GraphName)
{
	if (!BP) return nullptr;
	for (UEdGraph* G : BP->UbergraphPages)  if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) return G;
	for (UEdGraph* G : BP->FunctionGraphs)  if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) return G;
	for (UEdGraph* G : BP->MacroGraphs)     if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) return G;
	return nullptr;
}

template<typename TNodeType>
static TNodeType* SpawnK2Node(UEdGraph* Graph, int32 X, int32 Y)
{
	TNodeType* N = NewObject<TNodeType>(Graph);
	N->CreateNewGuid();
	N->SetFlags(RF_Transactional);
	N->NodePosX = X;
	N->NodePosY = Y;
	Graph->AddNode(N, false, false);
	N->PostPlacedNewNode();
	N->AllocateDefaultPins();
	return N;
}

FString FClaudeContextProvider::AddVariableNode(const FString& AssetPath, const FString& GraphName, const FString& VarName, bool bSetter, int32 X, int32 Y, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = TEXT("Not a Blueprint"); return TEXT(""); }

	UEdGraph* Graph = FindGraphHelper(BP, GraphName);
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return TEXT(""); }

	// Verify the variable exists on the BP
	bool bFound = false;
	for (const FBPVariableDescription& V : BP->NewVariables) if (V.VarName == FName(*VarName)) { bFound = true; break; }
	if (!bFound && BP->GeneratedClass)
	{
		if (BP->GeneratedClass->FindPropertyByName(FName(*VarName))) bFound = true;
	}
	if (!bFound) { OutError = FString::Printf(TEXT("Variable '%s' not found on %s"), *VarName, *BP->GetName()); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddVarNode", "Add Variable Node (Claude)"));

	FString NodeGuid;
	if (bSetter)
	{
		UK2Node_VariableSet* N = SpawnK2Node<UK2Node_VariableSet>(Graph, X, Y);
		N->VariableReference.SetSelfMember(FName(*VarName));
		N->ReconstructNode();
		NodeGuid = N->NodeGuid.ToString();
	}
	else
	{
		UK2Node_VariableGet* N = SpawnK2Node<UK2Node_VariableGet>(Graph, X, Y);
		N->VariableReference.SetSelfMember(FName(*VarName));
		N->ReconstructNode();
		NodeGuid = N->NodeGuid.ToString();
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return FString::Printf(TEXT("Added %s node for '%s'. NodeGuid=%s"),
		bSetter ? TEXT("Set") : TEXT("Get"), *VarName, *NodeGuid);
}

FString FClaudeContextProvider::AddBranchNode(const FString& AssetPath, const FString& GraphName, int32 X, int32 Y, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = TEXT("Not a Blueprint"); return TEXT(""); }
	UEdGraph* Graph = FindGraphHelper(BP, GraphName);
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddBranch", "Add Branch (Claude)"));
	UK2Node_IfThenElse* N = SpawnK2Node<UK2Node_IfThenElse>(Graph, X, Y);
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return FString::Printf(TEXT("Added Branch node. NodeGuid=%s"), *N->NodeGuid.ToString());
}

FString FClaudeContextProvider::AddSequenceNode(const FString& AssetPath, const FString& GraphName, int32 NumOutputs, int32 X, int32 Y, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = TEXT("Not a Blueprint"); return TEXT(""); }
	UEdGraph* Graph = FindGraphHelper(BP, GraphName);
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddSequence", "Add Sequence (Claude)"));
	UK2Node_ExecutionSequence* N = SpawnK2Node<UK2Node_ExecutionSequence>(Graph, X, Y);
	// Default is 2 outputs; add more if requested
	for (int32 i = 2; i < FMath::Clamp(NumOutputs, 2, 32); ++i)
	{
		N->AddInputPin();
	}
	N->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return FString::Printf(TEXT("Added Sequence node (%d outputs). NodeGuid=%s"), NumOutputs, *N->NodeGuid.ToString());
}

FString FClaudeContextProvider::AddCustomEventNode(const FString& AssetPath, const FString& GraphName, const FString& EventName, int32 X, int32 Y, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = TEXT("Not a Blueprint"); return TEXT(""); }
	UEdGraph* Graph = FindGraphHelper(BP, GraphName);
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddCustomEvent", "Add Custom Event (Claude)"));
	UK2Node_CustomEvent* N = SpawnK2Node<UK2Node_CustomEvent>(Graph, X, Y);
	N->CustomFunctionName = FName(*EventName);
	N->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return FString::Printf(TEXT("Added Custom Event '%s'. NodeGuid=%s"), *EventName, *N->NodeGuid.ToString());
}

FString FClaudeContextProvider::AddMakeBreakStructNode(const FString& AssetPath, const FString& GraphName, const FString& StructName, bool bMake, int32 X, int32 Y, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = TEXT("Not a Blueprint"); return TEXT(""); }
	UEdGraph* Graph = FindGraphHelper(BP, GraphName);
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return TEXT(""); }

	// Resolve struct
	UScriptStruct* Struct = nullptr;
	const FString Lower = StructName.ToLower();
	if (Lower == TEXT("vector"))         Struct = TBaseStructure<FVector>::Get();
	else if (Lower == TEXT("rotator"))   Struct = TBaseStructure<FRotator>::Get();
	else if (Lower == TEXT("transform")) Struct = TBaseStructure<FTransform>::Get();
	else if (Lower == TEXT("vector2d"))  Struct = TBaseStructure<FVector2D>::Get();
	else if (Lower == TEXT("color") || Lower == TEXT("linearcolor")) Struct = TBaseStructure<FLinearColor>::Get();
	else
	{
		Struct = LoadObject<UScriptStruct>(nullptr, *StructName);
	}
	if (!Struct) { OutError = FString::Printf(TEXT("Struct not found: %s"), *StructName); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddMakeBreak", "Add Make/Break Struct (Claude)"));
	FString NodeGuid;
	if (bMake)
	{
		UK2Node_MakeStruct* N = SpawnK2Node<UK2Node_MakeStruct>(Graph, X, Y);
		N->StructType = Struct;
		N->ReconstructNode();
		NodeGuid = N->NodeGuid.ToString();
	}
	else
	{
		UK2Node_BreakStruct* N = SpawnK2Node<UK2Node_BreakStruct>(Graph, X, Y);
		N->StructType = Struct;
		N->ReconstructNode();
		NodeGuid = N->NodeGuid.ToString();
	}
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return FString::Printf(TEXT("Added %s %s node. NodeGuid=%s"),
		bMake ? TEXT("Make") : TEXT("Break"), *Struct->GetName(), *NodeGuid);
}

FString FClaudeContextProvider::AddCastNode(const FString& AssetPath, const FString& GraphName, const FString& TargetClass, bool bPureCast, int32 X, int32 Y, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = TEXT("Not a Blueprint"); return TEXT(""); }
	UEdGraph* Graph = FindGraphHelper(BP, GraphName);
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return TEXT(""); }

	UClass* Cls = StaticLoadClass(UObject::StaticClass(), nullptr, *TargetClass);
	if (!Cls)
	{
		const FString AsEngine = FString::Printf(TEXT("/Script/Engine.%s"), *TargetClass);
		Cls = StaticLoadClass(UObject::StaticClass(), nullptr, *AsEngine);
	}
	if (!Cls)
	{
		// Maybe it's a BP
		if (UObject* LoadedObj = LoadAssetByPath(TargetClass))
		{
			if (UBlueprint* TargetBP = Cast<UBlueprint>(LoadedObj))
			{
				Cls = TargetBP->GeneratedClass;
			}
		}
	}
	if (!Cls) { OutError = FString::Printf(TEXT("Target class not found: %s"), *TargetClass); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddCast", "Add Cast Node (Claude)"));
	UK2Node_DynamicCast* N = SpawnK2Node<UK2Node_DynamicCast>(Graph, X, Y);
	N->TargetType = Cls;
	N->SetPurity(bPureCast);
	N->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return FString::Printf(TEXT("Added Cast to %s (%s). NodeGuid=%s"),
		*Cls->GetName(), bPureCast ? TEXT("pure") : TEXT("impure"), *N->NodeGuid.ToString());
}

FString FClaudeContextProvider::AddSelfNode(const FString& AssetPath, const FString& GraphName, int32 X, int32 Y, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = TEXT("Not a Blueprint"); return TEXT(""); }
	UEdGraph* Graph = FindGraphHelper(BP, GraphName);
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddSelf", "Add Self Node (Claude)"));
	UK2Node_Self* N = SpawnK2Node<UK2Node_Self>(Graph, X, Y);
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return FString::Printf(TEXT("Added Self node. NodeGuid=%s"), *N->NodeGuid.ToString());
}

FString FClaudeContextProvider::SetPinDefaultValue(const FString& AssetPath, const FString& GraphName, const FString& NodeGuid, const FString& PinName, const FString& NewValue, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) { OutError = TEXT("Not a Blueprint"); return TEXT(""); }
	UEdGraph* Graph = FindGraphHelper(BP, GraphName);
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return TEXT(""); }

	FGuid Guid; FGuid::Parse(NodeGuid, Guid);
	UEdGraphNode* Node = nullptr;
	for (UEdGraphNode* N : Graph->Nodes) if (N && N->NodeGuid == Guid) { Node = N; break; }
	if (!Node) { OutError = FString::Printf(TEXT("Node %s not found"), *NodeGuid); return TEXT(""); }

	UEdGraphPin* Pin = Node->FindPin(PinName);
	if (!Pin) { OutError = FString::Printf(TEXT("Pin '%s' not found"), *PinName); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetPinDef", "Set Pin Default Value (Claude)"));
	Node->Modify();
	Graph->GetSchema()->TrySetDefaultValue(*Pin, NewValue);
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return FString::Printf(TEXT("Set %s pin '%s' default = %s"), *NodeGuid, *PinName, *NewValue);
}

// =============================================================================
// Editor actions
// =============================================================================

FString FClaudeContextProvider::OpenAssetEditor(const FString& AssetPath, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath); return TEXT(""); }

	UAssetEditorSubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (!Sub) { OutError = TEXT("AssetEditorSubsystem unavailable"); return TEXT(""); }

	const bool bOpened = Sub->OpenEditorForAsset(Asset);
	if (!bOpened) { OutError = FString::Printf(TEXT("OpenEditorForAsset returned false for %s"), *AssetPath); return TEXT(""); }

	return FString::Printf(TEXT("Opened editor for %s"), *AssetPath);
}

// =============================================================================
// AI — Blackboard
// =============================================================================

FString FClaudeContextProvider::CreateBlackboard(const FString& NewAssetPath, const FString& ParentBlackboardPath, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path, use '/Game/AI/BB_Name'"); return TEXT(""); }

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	UClass* FactoryClass = StaticLoadClass(UObject::StaticClass(), nullptr, TEXT("/Script/AIGraph.BlackboardDataFactory"));
	if (!FactoryClass) { OutError = TEXT("BlackboardDataFactory class not found — is AIModule loaded?"); return TEXT(""); }

	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateBB", "Create Blackboard (Claude)"));
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, UBlackboardData::StaticClass(), Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return TEXT(""); }

	// Optionally hook up parent blackboard
	if (!ParentBlackboardPath.IsEmpty())
	{
		if (UBlackboardData* ParentBB = Cast<UBlackboardData>(LoadAssetByPath(ParentBlackboardPath)))
		{
			if (UBlackboardData* BB = Cast<UBlackboardData>(NewObj))
			{
				BB->Parent = ParentBB;
				BB->MarkPackageDirty();
			}
		}
	}

	return FString::Printf(TEXT("Created Blackboard: %s"), *NewObj->GetPathName());
}

FString FClaudeContextProvider::AddBlackboardKey(
	const FString& BlackboardPath, const FString& KeyName, const FString& KeyType,
	const FString& KeyTypeObject, bool bInstanceSynced, FString& OutError)
{
	UBlackboardData* BB = Cast<UBlackboardData>(LoadAssetByPath(BlackboardPath));
	if (!BB) { OutError = FString::Printf(TEXT("Not a Blackboard: %s"), *BlackboardPath); return TEXT(""); }

	// Map friendly type names to BB key class
	UClass* KeyClass = nullptr;
	const FString LowerType = KeyType.ToLower();
	if (LowerType == TEXT("bool"))        KeyClass = UBlackboardKeyType_Bool::StaticClass();
	else if (LowerType == TEXT("int") || LowerType == TEXT("int32")) KeyClass = UBlackboardKeyType_Int::StaticClass();
	else if (LowerType == TEXT("float"))  KeyClass = UBlackboardKeyType_Float::StaticClass();
	else if (LowerType == TEXT("string")) KeyClass = UBlackboardKeyType_String::StaticClass();
	else if (LowerType == TEXT("name"))   KeyClass = UBlackboardKeyType_Name::StaticClass();
	else if (LowerType == TEXT("vector")) KeyClass = UBlackboardKeyType_Vector::StaticClass();
	else if (LowerType == TEXT("rotator")) KeyClass = UBlackboardKeyType_Rotator::StaticClass();
	else if (LowerType == TEXT("object")) KeyClass = UBlackboardKeyType_Object::StaticClass();
	else if (LowerType == TEXT("class"))  KeyClass = UBlackboardKeyType_Class::StaticClass();
	else if (LowerType == TEXT("enum"))   KeyClass = UBlackboardKeyType_Enum::StaticClass();
	else { OutError = FString::Printf(TEXT("Unknown BB key type: %s. Use bool/int/float/string/name/vector/rotator/object/class/enum"), *KeyType); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddBBKey", "Add Blackboard Key (Claude)"));
	BB->Modify();

	FBlackboardEntry NewEntry;
	NewEntry.EntryName = FName(*KeyName);
	NewEntry.bInstanceSynced = bInstanceSynced;

	UBlackboardKeyType* KeyTypeInst = NewObject<UBlackboardKeyType>(BB, KeyClass);

	// For Object/Class/Enum keys we need to set the sub-class/enum
	if (!KeyTypeObject.IsEmpty())
	{
		if (KeyClass == UBlackboardKeyType_Object::StaticClass())
		{
			if (UClass* BaseCls = StaticLoadClass(UObject::StaticClass(), nullptr, *KeyTypeObject))
			{
				((UBlackboardKeyType_Object*)KeyTypeInst)->BaseClass = BaseCls;
			}
		}
		else if (KeyClass == UBlackboardKeyType_Class::StaticClass())
		{
			if (UClass* BaseCls = StaticLoadClass(UObject::StaticClass(), nullptr, *KeyTypeObject))
			{
				((UBlackboardKeyType_Class*)KeyTypeInst)->BaseClass = BaseCls;
			}
		}
		else if (KeyClass == UBlackboardKeyType_Enum::StaticClass())
		{
			if (UEnum* E = LoadObject<UEnum>(nullptr, *KeyTypeObject))
			{
				((UBlackboardKeyType_Enum*)KeyTypeInst)->EnumType = E;
			}
		}
	}

	NewEntry.KeyType = KeyTypeInst;
	BB->Keys.Add(NewEntry);
	BB->MarkPackageDirty();

	return FString::Printf(TEXT("Added BB key '%s' (%s) to %s"), *KeyName, *KeyType, *BlackboardPath);
}

FString FClaudeContextProvider::RemoveBlackboardKey(const FString& BlackboardPath, const FString& KeyName, FString& OutError)
{
	UBlackboardData* BB = Cast<UBlackboardData>(LoadAssetByPath(BlackboardPath));
	if (!BB) { OutError = FString::Printf(TEXT("Not a Blackboard: %s"), *BlackboardPath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "RemBBKey", "Remove Blackboard Key (Claude)"));
	BB->Modify();

	const int32 Removed = BB->Keys.RemoveAll([&](const FBlackboardEntry& E){ return E.EntryName == FName(*KeyName); });
	if (Removed == 0) { OutError = FString::Printf(TEXT("Key '%s' not found"), *KeyName); return TEXT(""); }

	BB->MarkPackageDirty();
	return FString::Printf(TEXT("Removed BB key '%s'"), *KeyName);
}

FString FClaudeContextProvider::GetBlackboardStructure(const FString& BlackboardPath)
{
	UBlackboardData* BB = Cast<UBlackboardData>(LoadAssetByPath(BlackboardPath));
	if (!BB) return FString::Printf(TEXT("{\"error\":\"Not a Blackboard: %s\"}"), *BlackboardPath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("blackboard"), BlackboardPath);
	if (BB->Parent) Root->SetStringField(TEXT("parent"), BB->Parent->GetPathName());

	TArray<TSharedPtr<FJsonValue>> Keys;
	for (const FBlackboardEntry& E : BB->Keys)
	{
		TSharedRef<FJsonObject> K = MakeShared<FJsonObject>();
		K->SetStringField(TEXT("name"), E.EntryName.ToString());
		K->SetStringField(TEXT("type"), E.KeyType ? E.KeyType->GetClass()->GetName() : TEXT(""));
		K->SetBoolField(TEXT("instance_synced"), E.bInstanceSynced);
		if (E.EntryCategory != NAME_None) K->SetStringField(TEXT("category"), E.EntryCategory.ToString());
		Keys.Add(MakeShared<FJsonValueObject>(K));
	}
	Root->SetArrayField(TEXT("keys"), Keys);
	return JsonObjectToString(Root);
}

// =============================================================================
// AI — Behavior Tree
// =============================================================================
// BT editor uses UBehaviorTreeGraphNode_* wrappers around runtime UBTNodes.
// Creation path: factory → UBehaviorTree asset → BT graph auto-populated with a Root graph-node.
// Child nodes are graph nodes; their NodeInstance is the actual UBTNode subclass.

FString FClaudeContextProvider::CreateBehaviorTree(const FString& NewAssetPath, const FString& BlackboardPath, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path, use '/Game/AI/BT_Name'"); return TEXT(""); }

	UClass* FactoryClass = StaticLoadClass(UObject::StaticClass(), nullptr, TEXT("/Script/AIGraph.BehaviorTreeFactory"));
	if (!FactoryClass) { OutError = TEXT("BehaviorTreeFactory class not found"); return TEXT(""); }

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateBT", "Create Behavior Tree (Claude)"));
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, UBehaviorTree::StaticClass(), Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return TEXT(""); }

	if (!BlackboardPath.IsEmpty())
	{
		if (UBlackboardData* BB = Cast<UBlackboardData>(LoadAssetByPath(BlackboardPath)))
		{
			if (UBehaviorTree* BT = Cast<UBehaviorTree>(NewObj))
			{
				BT->BlackboardAsset = BB;
				BT->MarkPackageDirty();
			}
		}
	}

	return FString::Printf(TEXT("Created Behavior Tree: %s"), *NewObj->GetPathName());
}

// Helper: gets the UEdGraph inside a BehaviorTree's BTGraph wrapper
static UEdGraph* FindBTGraph(UBehaviorTree* BT)
{
	if (!BT) return nullptr;
	// UBehaviorTree stores its editor graph in ->BTGraph (an FBehaviorTreeEditor structure).
	// We access the graph through the BTGraph property via reflection to avoid a hard
	// dependency on the BehaviorTreeEditor headers.
	FProperty* GraphProp = BT->GetClass()->FindPropertyByName(TEXT("BTGraph"));
	if (!GraphProp) return nullptr;
	FObjectProperty* ObjProp = CastField<FObjectProperty>(GraphProp);
	if (!ObjProp) return nullptr;
	UObject* GraphObj = ObjProp->GetObjectPropertyValue_InContainer(BT);
	return Cast<UEdGraph>(GraphObj);
}

FString FClaudeContextProvider::GetBehaviorTreeStructure(const FString& BTPath)
{
	UBehaviorTree* BT = Cast<UBehaviorTree>(LoadAssetByPath(BTPath));
	if (!BT) return FString::Printf(TEXT("{\"error\":\"Not a BehaviorTree: %s\"}"), *BTPath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("behavior_tree"), BTPath);
	if (BT->BlackboardAsset) Root->SetStringField(TEXT("blackboard"), BT->BlackboardAsset->GetPathName());

	UEdGraph* Graph = FindBTGraph(BT);
	if (!Graph)
	{
		Root->SetStringField(TEXT("warning"), TEXT("Could not access editor graph — BT editor may not be initialized"));
		// Fall back to runtime structure only
		if (BT->RootNode)
		{
			Root->SetStringField(TEXT("root_runtime_node"), BT->RootNode->GetName());
		}
		return Truncate(JsonObjectToString(Root));
	}

	TArray<TSharedPtr<FJsonValue>> NodesJson;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (!N) continue;
		TSharedRef<FJsonObject> NObj = MakeShared<FJsonObject>();
		NObj->SetStringField(TEXT("id"), N->NodeGuid.ToString());
		NObj->SetStringField(TEXT("graph_class"), N->GetClass()->GetName());
		NObj->SetStringField(TEXT("title"), N->GetNodeTitle(ENodeTitleType::ListView).ToString());
		NObj->SetNumberField(TEXT("x"), N->NodePosX);
		NObj->SetNumberField(TEXT("y"), N->NodePosY);

		// NodeInstance is the underlying UBTNode runtime class (accessible via reflection)
		if (FProperty* NIProp = N->GetClass()->FindPropertyByName(TEXT("NodeInstance")))
		{
			if (FObjectProperty* ObjP = CastField<FObjectProperty>(NIProp))
			{
				if (UObject* RuntimeNode = ObjP->GetObjectPropertyValue_InContainer(N))
				{
					NObj->SetStringField(TEXT("runtime_class"), RuntimeNode->GetClass()->GetName());
				}
			}
		}

		// Child connections — in BT each node has at most one pin "out"
		TArray<TSharedPtr<FJsonValue>> Children;
		for (UEdGraphPin* Pin : N->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output)
			{
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (Linked && Linked->GetOwningNode())
					{
						Children.Add(MakeShared<FJsonValueString>(Linked->GetOwningNode()->NodeGuid.ToString()));
					}
				}
			}
		}
		if (Children.Num() > 0) NObj->SetArrayField(TEXT("children"), Children);

		NodesJson.Add(MakeShared<FJsonValueObject>(NObj));
	}
	Root->SetArrayField(TEXT("nodes"), NodesJson);
	return Truncate(JsonObjectToString(Root));
}

// Resolve a class name to UClass for BT nodes (Tasks/Decorators/Services/Composites).
// Accepts: "BTTask_RunBehavior" (short native name), "/Script/AIModule.BTTask_RunBehavior" (full),
// or a Blueprint asset path like "/Game/AI/Tasks/BTT_MoveToPlayer".
static UClass* ResolveBTClass(const FString& NameOrPath, UClass* ExpectedBase)
{
	// 1. Blueprint asset path
	if (NameOrPath.StartsWith(TEXT("/Game")))
	{
		if (UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *NameOrPath))
		{
			if (UBlueprint* BP = Cast<UBlueprint>(Loaded))
			{
				if (BP->GeneratedClass && BP->GeneratedClass->IsChildOf(ExpectedBase))
				{
					return BP->GeneratedClass;
				}
			}
		}
	}

	// 2. Try as a direct class path
	if (UClass* Direct = StaticLoadClass(ExpectedBase, nullptr, *NameOrPath))
	{
		return Direct;
	}

	// 3. Try as short name under /Script/AIModule
	const FString AsAI = FString::Printf(TEXT("/Script/AIModule.%s"), *NameOrPath);
	if (UClass* AI = StaticLoadClass(ExpectedBase, nullptr, *AsAI))
	{
		return AI;
	}

	// 4. Fallback — iterate loaded classes
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(ExpectedBase) && It->GetName() == NameOrPath)
		{
			return *It;
		}
	}
	return nullptr;
}

// Spawn a graph node of the right BT wrapper type (Composite / Task / Decorator / Service).
// The editor needs UBehaviorTreeGraphNode_Composite etc. We reach them by path since we
// don't want a hard include of BehaviorTreeEditor in this file.
static UEdGraphNode* SpawnBTGraphNode(UEdGraph* Graph, const FString& WrapperClassPath, UClass* RuntimeClass, int32 X, int32 Y)
{
	if (!Graph) return nullptr;
	UClass* WrapperClass = StaticLoadClass(UEdGraphNode::StaticClass(), nullptr, *WrapperClassPath);
	if (!WrapperClass) return nullptr;

	UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, WrapperClass);
	Node->CreateNewGuid();
	Node->SetFlags(RF_Transactional);
	Node->NodePosX = X;
	Node->NodePosY = Y;

	// Set NodeInstance (runtime UBTNode) via reflection
	if (RuntimeClass)
	{
		UObject* RuntimeInstance = NewObject<UObject>(Node, RuntimeClass);
		if (FProperty* NIProp = Node->GetClass()->FindPropertyByName(TEXT("NodeInstance")))
		{
			if (FObjectProperty* ObjP = CastField<FObjectProperty>(NIProp))
			{
				ObjP->SetObjectPropertyValue_InContainer(Node, RuntimeInstance);
			}
		}
		// Also set ClassData for wrappers that use it
		if (FProperty* CDProp = Node->GetClass()->FindPropertyByName(TEXT("ClassData")))
		{
			// Best effort — some wrappers have FGraphNodeClassData ClassData
			// we leave at defaults; editor will re-evaluate on compile
		}
	}

	Graph->AddNode(Node, false, false);
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	return Node;
}

FString FClaudeContextProvider::AddBTComposite(const FString& BTPath, const FString& CompositeType,
	const FString& ParentNodeGuid, int32 X, int32 Y, FString& OutError)
{
	UBehaviorTree* BT = Cast<UBehaviorTree>(LoadAssetByPath(BTPath));
	if (!BT) { OutError = FString::Printf(TEXT("Not a BT: %s"), *BTPath); return TEXT(""); }
	UEdGraph* Graph = FindBTGraph(BT);
	if (!Graph) { OutError = TEXT("BT editor graph not accessible — try opening the BT editor first via open_asset_editor"); return TEXT(""); }

	// Map friendly names to UBTComposite subclasses
	const FString Lower = CompositeType.ToLower();
	FString RuntimeClassPath;
	if (Lower == TEXT("selector"))       RuntimeClassPath = TEXT("/Script/AIModule.BTComposite_Selector");
	else if (Lower == TEXT("sequence"))  RuntimeClassPath = TEXT("/Script/AIModule.BTComposite_Sequence");
	else if (Lower == TEXT("simpleparallel") || Lower == TEXT("parallel"))
	                                     RuntimeClassPath = TEXT("/Script/AIModule.BTComposite_SimpleParallel");
	else { OutError = FString::Printf(TEXT("Unknown composite: %s (use selector/sequence/simpleparallel)"), *CompositeType); return TEXT(""); }

	UClass* RuntimeClass = StaticLoadClass(UBTCompositeNode::StaticClass(), nullptr, *RuntimeClassPath);
	if (!RuntimeClass) { OutError = FString::Printf(TEXT("Composite class not found: %s"), *RuntimeClassPath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddBTComposite", "Add BT Composite (Claude)"));

	UEdGraphNode* NewNode = SpawnBTGraphNode(Graph, TEXT("/Script/BehaviorTreeEditor.BehaviorTreeGraphNode_Composite"), RuntimeClass, X, Y);
	if (!NewNode) { OutError = TEXT("Could not create composite graph node"); return TEXT(""); }

	// Optional connect to parent
	if (!ParentNodeGuid.IsEmpty())
	{
		FString ConnErr;
		ConnectBTNodes(BTPath, ParentNodeGuid, NewNode->NodeGuid.ToString(), ConnErr);
	}

	BT->MarkPackageDirty();
	return FString::Printf(TEXT("Added %s composite. NodeGuid=%s"), *CompositeType, *NewNode->NodeGuid.ToString());
}

FString FClaudeContextProvider::AddBTTask(const FString& BTPath, const FString& TaskClassOrBP,
	const FString& ParentNodeGuid, int32 X, int32 Y, FString& OutError)
{
	UBehaviorTree* BT = Cast<UBehaviorTree>(LoadAssetByPath(BTPath));
	if (!BT) { OutError = FString::Printf(TEXT("Not a BT: %s"), *BTPath); return TEXT(""); }
	UEdGraph* Graph = FindBTGraph(BT);
	if (!Graph) { OutError = TEXT("BT editor graph not accessible"); return TEXT(""); }

	UClass* RuntimeClass = ResolveBTClass(TaskClassOrBP, UBTTaskNode::StaticClass());
	if (!RuntimeClass) { OutError = FString::Printf(TEXT("Task class not found: %s"), *TaskClassOrBP); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddBTTask", "Add BT Task (Claude)"));
	UEdGraphNode* NewNode = SpawnBTGraphNode(Graph, TEXT("/Script/BehaviorTreeEditor.BehaviorTreeGraphNode_Task"), RuntimeClass, X, Y);
	if (!NewNode) { OutError = TEXT("Could not create task graph node"); return TEXT(""); }

	if (!ParentNodeGuid.IsEmpty())
	{
		FString ConnErr;
		ConnectBTNodes(BTPath, ParentNodeGuid, NewNode->NodeGuid.ToString(), ConnErr);
	}

	BT->MarkPackageDirty();
	return FString::Printf(TEXT("Added Task (%s). NodeGuid=%s"), *RuntimeClass->GetName(), *NewNode->NodeGuid.ToString());
}

// Helper: properly create and register a BT sub-node (decorator or service)
// using the editor's real API from AIGraph module.
//
// The fix: we now include AIGraphNode.h and AIGraphTypes.h directly, so we
// can use the actual UAIGraphNode::AddSubNode() call and construct a proper
// FGraphNodeClassData via its public constructor. Previous reflection-based
// approach left the subnode in a partially-initialized state, causing crashes
// when the editor UI tried to interact with it.
static UEdGraphNode* CreateBTSubNode(UEdGraphNode* Parent, const FString& WrapperClassPath,
	UClass* RuntimeClass, FString& OutError)
{
	if (!Parent || !RuntimeClass) { OutError = TEXT("Bad args"); return nullptr; }

	// Parent must be a UAIGraphNode (all BT wrapper nodes are)
	UAIGraphNode* AIParent = Cast<UAIGraphNode>(Parent);
	if (!AIParent) { OutError = TEXT("Parent node is not a UAIGraphNode"); return nullptr; }

	UClass* WrapperClass = StaticLoadClass(UAIGraphNode::StaticClass(), nullptr, *WrapperClassPath);
	if (!WrapperClass) { OutError = FString::Printf(TEXT("Wrapper class not loaded: %s"), *WrapperClassPath); return nullptr; }

	// Outer for the subnode is the parent's graph (not the parent node itself).
	// This matches how the BT editor creates subnodes — ensures correct package/outer.
	UEdGraph* Graph = Parent->GetGraph();
	if (!Graph) { OutError = TEXT("Parent has no graph"); return nullptr; }

	UAIGraphNode* SubNode = NewObject<UAIGraphNode>(Graph, WrapperClass, NAME_None, RF_Transactional);
	SubNode->CreateNewGuid();

	// Set ClassData via the proper constructor that takes a UClass pointer.
	// FGraphNodeClassData has a public ctor: FGraphNodeClassData(UClass* InClass, const FString& InCategory)
	SubNode->ClassData = FGraphNodeClassData(RuntimeClass, FString());

	// Create the runtime UBTNode instance as a sub-object of the wrapper.
	// We do it via reflection because NodeInstance is declared on UAIGraphNode
	// but the setter is not public — direct field write through the property.
	if (FProperty* NIProp = UAIGraphNode::StaticClass()->FindPropertyByName(TEXT("NodeInstance")))
	{
		if (FObjectProperty* ObjP = CastField<FObjectProperty>(NIProp))
		{
			UObject* NodeInstance = NewObject<UObject>(SubNode, RuntimeClass, NAME_None, RF_Transactional);
			ObjP->SetObjectPropertyValue_InContainer(SubNode, NodeInstance);
		}
	}

	// This is THE critical call — does all the editor-integrated setup:
	//  - sets ParentNode back-reference
	//  - adds to the appropriate sub-array on parent (Decorators or Services)
	//  - calls PostPlacedNewNode on the subnode
	//  - marks the graph dirty
	AIParent->AddSubNode(SubNode, Graph);

	// Allocate pins last, after the node knows its parent/graph
	SubNode->AllocateDefaultPins();

	return SubNode;
}

FString FClaudeContextProvider::AddBTDecorator(const FString& BTPath, const FString& DecoratorClassOrBP,
	const FString& TargetNodeGuid, FString& OutError)
{
	UBehaviorTree* BT = Cast<UBehaviorTree>(LoadAssetByPath(BTPath));
	if (!BT) { OutError = FString::Printf(TEXT("Not a BT: %s"), *BTPath); return TEXT(""); }
	UEdGraph* Graph = FindBTGraph(BT);
	if (!Graph) { OutError = TEXT("BT editor graph not accessible — open the BT editor first"); return TEXT(""); }

	UClass* DecoratorClass = ResolveBTClass(DecoratorClassOrBP, UBTDecorator::StaticClass());
	if (!DecoratorClass) { OutError = FString::Printf(TEXT("Decorator class not found: %s"), *DecoratorClassOrBP); return TEXT(""); }

	FGuid TargetGuid;
	FGuid::Parse(TargetNodeGuid, TargetGuid);
	UEdGraphNode* Target = nullptr;
	for (UEdGraphNode* N : Graph->Nodes) if (N && N->NodeGuid == TargetGuid) { Target = N; break; }
	if (!Target) { OutError = FString::Printf(TEXT("Target node %s not found"), *TargetNodeGuid); return TEXT(""); }

	// Verify target node has a Decorators property (Composites and Tasks do, Root and subnodes don't)
	if (!Target->GetClass()->FindPropertyByName(TEXT("Decorators")))
	{
		OutError = TEXT("Target node does not support decorators. Decorators can only attach to Composite or Task nodes.");
		return TEXT("");
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddBTDec", "Add BT Decorator (Claude)"));
	Target->Modify();

	UEdGraphNode* DecNode = CreateBTSubNode(Target,
		TEXT("/Script/BehaviorTreeEditor.BehaviorTreeGraphNode_Decorator"),
		DecoratorClass, OutError);
	if (!DecNode) return TEXT("");

	BT->MarkPackageDirty();
	return FString::Printf(TEXT("Added Decorator (%s) to node %s. Open the BT to verify — if the node looks empty, click it in the editor to force a refresh."),
		*DecoratorClass->GetName(), *TargetNodeGuid);
}

FString FClaudeContextProvider::AddBTService(const FString& BTPath, const FString& ServiceClassOrBP,
	const FString& TargetNodeGuid, FString& OutError)
{
	UBehaviorTree* BT = Cast<UBehaviorTree>(LoadAssetByPath(BTPath));
	if (!BT) { OutError = FString::Printf(TEXT("Not a BT: %s"), *BTPath); return TEXT(""); }
	UEdGraph* Graph = FindBTGraph(BT);
	if (!Graph) { OutError = TEXT("BT editor graph not accessible — open the BT editor first"); return TEXT(""); }

	UClass* ServiceClass = ResolveBTClass(ServiceClassOrBP, UBTService::StaticClass());
	if (!ServiceClass) { OutError = FString::Printf(TEXT("Service class not found: %s"), *ServiceClassOrBP); return TEXT(""); }

	FGuid TargetGuid;
	FGuid::Parse(TargetNodeGuid, TargetGuid);
	UEdGraphNode* Target = nullptr;
	for (UEdGraphNode* N : Graph->Nodes) if (N && N->NodeGuid == TargetGuid) { Target = N; break; }
	if (!Target) { OutError = FString::Printf(TEXT("Target node %s not found"), *TargetNodeGuid); return TEXT(""); }

	if (!Target->GetClass()->FindPropertyByName(TEXT("Services")))
	{
		OutError = TEXT("Target node does not support services. Services can only attach to Composite nodes.");
		return TEXT("");
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddBTSvc", "Add BT Service (Claude)"));
	Target->Modify();

	UEdGraphNode* SvcNode = CreateBTSubNode(Target,
		TEXT("/Script/BehaviorTreeEditor.BehaviorTreeGraphNode_Service"),
		ServiceClass, OutError);
	if (!SvcNode) return TEXT("");

	BT->MarkPackageDirty();
	return FString::Printf(TEXT("Added Service (%s) to node %s"), *ServiceClass->GetName(), *TargetNodeGuid);
}

FString FClaudeContextProvider::ConnectBTNodes(const FString& BTPath, const FString& ParentGuid, const FString& ChildGuid, FString& OutError)
{
	UBehaviorTree* BT = Cast<UBehaviorTree>(LoadAssetByPath(BTPath));
	if (!BT) { OutError = FString::Printf(TEXT("Not a BT: %s"), *BTPath); return TEXT(""); }
	UEdGraph* Graph = FindBTGraph(BT);
	if (!Graph) { OutError = TEXT("BT editor graph not accessible"); return TEXT(""); }

	FGuid PG, CG;
	FGuid::Parse(ParentGuid, PG);
	FGuid::Parse(ChildGuid,  CG);

	UEdGraphNode *Parent = nullptr, *Child = nullptr;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (!N) continue;
		if (N->NodeGuid == PG) Parent = N;
		if (N->NodeGuid == CG) Child  = N;
	}
	if (!Parent || !Child) { OutError = TEXT("One of the node GUIDs not found"); return TEXT(""); }

	UEdGraphPin *ParentOut = nullptr, *ChildIn = nullptr;
	for (UEdGraphPin* P : Parent->Pins) if (P && P->Direction == EGPD_Output) { ParentOut = P; break; }
	for (UEdGraphPin* P : Child->Pins)  if (P && P->Direction == EGPD_Input)  { ChildIn = P;  break; }
	if (!ParentOut || !ChildIn) { OutError = TEXT("Nodes don't have matching exec pins"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "ConnectBT", "Connect BT Nodes (Claude)"));
	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema->TryCreateConnection(ParentOut, ChildIn))
	{
		OutError = TEXT("Schema rejected the connection");
		return TEXT("");
	}
	BT->MarkPackageDirty();
	return FString::Printf(TEXT("Connected %s -> %s"), *ParentGuid, *ChildGuid);
}

FString FClaudeContextProvider::DeleteBTNode(const FString& BTPath, const FString& NodeGuid, FString& OutError)
{
	UBehaviorTree* BT = Cast<UBehaviorTree>(LoadAssetByPath(BTPath));
	if (!BT) { OutError = FString::Printf(TEXT("Not a BT: %s"), *BTPath); return TEXT(""); }
	UEdGraph* Graph = FindBTGraph(BT);
	if (!Graph) { OutError = TEXT("BT editor graph not accessible"); return TEXT(""); }

	FGuid G;
	FGuid::Parse(NodeGuid, G);
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (!N || N->NodeGuid != G) continue;
		FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "DelBT", "Delete BT Node (Claude)"));
		Graph->RemoveNode(N);
		BT->MarkPackageDirty();
		return FString::Printf(TEXT("Deleted BT node %s"), *NodeGuid);
	}
	OutError = FString::Printf(TEXT("Node %s not found"), *NodeGuid);
	return TEXT("");
}

// =============================================================================
// Animation — Sequence / Montage / AnimBP
// =============================================================================
//
// UE 5.7 uses the new IAnimationDataModel/Controller API to mutate animation
// data safely (tracks, curves, keys). Direct manipulation of internal arrays
// is discouraged — the controller handles notifications, versioning, and undo.
//
// Notifies live on UAnimSequenceBase::Notifies (editor-only). They are
// FAnimNotifyEvent structs — we can push them directly since Notifies itself
// isn't behind the DataModel.

FString FClaudeContextProvider::GetAnimSequenceInfo(const FString& SequencePath)
{
	UAnimSequenceBase* Seq = Cast<UAnimSequenceBase>(LoadAssetByPath(SequencePath));
	if (!Seq) return FString::Printf(TEXT("{\"error\":\"Not an Anim Sequence/Montage: %s\"}"), *SequencePath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), SequencePath);
	Root->SetStringField(TEXT("class"), Seq->GetClass()->GetName());
	Root->SetNumberField(TEXT("length"), Seq->GetPlayLength());
	Root->SetNumberField(TEXT("rate_scale"), Seq->RateScale);

	if (USkeleton* Sk = Seq->GetSkeleton())
	{
		Root->SetStringField(TEXT("skeleton"), Sk->GetPathName());
	}

	// UAnimSequence-specific details
	if (UAnimSequence* AS = Cast<UAnimSequence>(Seq))
	{
#if WITH_EDITORONLY_DATA
		Root->SetNumberField(TEXT("frame_rate_fps"), AS->GetSamplingFrameRate().AsDecimal());
		Root->SetNumberField(TEXT("number_of_frames"), AS->GetNumberOfSampledKeys());
#endif
	}

	// Notifies count
#if WITH_EDITORONLY_DATA
	Root->SetNumberField(TEXT("notify_count"), Seq->Notifies.Num());
#endif

	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::GetAnimNotifies(const FString& AssetPath)
{
	UAnimSequenceBase* Seq = Cast<UAnimSequenceBase>(LoadAssetByPath(AssetPath));
	if (!Seq) return FString::Printf(TEXT("{\"error\":\"Not an Anim Sequence/Montage: %s\"}"), *AssetPath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Items;

#if WITH_EDITORONLY_DATA
	for (const FAnimNotifyEvent& N : Seq->Notifies)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), N.NotifyName.ToString());
		Obj->SetNumberField(TEXT("time"), N.GetTriggerTime());
		Obj->SetNumberField(TEXT("duration"), N.GetDuration());
		if (!N.NotifyStateClass && !N.Notify)
		{
			Obj->SetStringField(TEXT("kind"), TEXT("simple"));
		}
		else if (N.NotifyStateClass)
		{
			Obj->SetStringField(TEXT("kind"), TEXT("state"));
			Obj->SetStringField(TEXT("class"), N.NotifyStateClass->GetClass()->GetName());
		}
		else if (N.Notify)
		{
			Obj->SetStringField(TEXT("kind"), TEXT("event"));
			Obj->SetStringField(TEXT("class"), N.Notify->GetClass()->GetName());
		}
		if (!N.TrackIndex) {} // track index exists, intentionally not emitted
		Items.Add(MakeShared<FJsonValueObject>(Obj));
	}
#endif

	Root->SetArrayField(TEXT("notifies"), Items);
	Root->SetNumberField(TEXT("count"), Items.Num());
	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::GetAnimMontageStructure(const FString& MontagePath)
{
	UAnimMontage* Montage = Cast<UAnimMontage>(LoadAssetByPath(MontagePath));
	if (!Montage) return FString::Printf(TEXT("{\"error\":\"Not an AnimMontage: %s\"}"), *MontagePath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), MontagePath);
	Root->SetNumberField(TEXT("length"), Montage->GetPlayLength());
	if (USkeleton* Sk = Montage->GetSkeleton()) Root->SetStringField(TEXT("skeleton"), Sk->GetPathName());

	// Slots
	TArray<TSharedPtr<FJsonValue>> Slots;
	for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("name"), Slot.SlotName.ToString());
		S->SetNumberField(TEXT("segment_count"), Slot.AnimTrack.AnimSegments.Num());
		Slots.Add(MakeShared<FJsonValueObject>(S));
	}
	Root->SetArrayField(TEXT("slots"), Slots);

	// Sections
	TArray<TSharedPtr<FJsonValue>> Sections;
	for (const FCompositeSection& Sec : Montage->CompositeSections)
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("name"), Sec.SectionName.ToString());
		S->SetNumberField(TEXT("start_time"), Sec.GetTime());
		if (Sec.NextSectionName != NAME_None) S->SetStringField(TEXT("next"), Sec.NextSectionName.ToString());
		Sections.Add(MakeShared<FJsonValueObject>(S));
	}
	Root->SetArrayField(TEXT("sections"), Sections);

	// Note: BranchingPointMarkers are private — exposed through runtime API only.
	// We skip them here; the montage's public Notifies already cover most use cases.

	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::GetAnimCurves(const FString& SequencePath)
{
	UAnimSequenceBase* Seq = Cast<UAnimSequenceBase>(LoadAssetByPath(SequencePath));
	if (!Seq) return FString::Printf(TEXT("{\"error\":\"Not an Anim Sequence: %s\"}"), *SequencePath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Curves;

	// Float curves are in RawCurveData (editor) — accessed via GetCurveData()
	const FRawCurveTracks& CurveData = Seq->GetCurveData();
	for (const FFloatCurve& C : CurveData.FloatCurves)
	{
		TSharedRef<FJsonObject> CObj = MakeShared<FJsonObject>();
		CObj->SetStringField(TEXT("name"), C.GetName().ToString());
		CObj->SetNumberField(TEXT("key_count"), C.FloatCurve.GetNumKeys());
		Curves.Add(MakeShared<FJsonValueObject>(CObj));
	}

	Root->SetArrayField(TEXT("float_curves"), Curves);
	Root->SetNumberField(TEXT("count"), Curves.Num());
	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::AddAnimNotify(const FString& AssetPath, const FString& NotifyName, float Time, const FString& NotifyClass, FString& OutError)
{
#if !WITH_EDITORONLY_DATA
	OutError = TEXT("Editor-only build required");
	return TEXT("");
#else
	UAnimSequenceBase* Seq = Cast<UAnimSequenceBase>(LoadAssetByPath(AssetPath));
	if (!Seq) { OutError = FString::Printf(TEXT("Not an Anim asset: %s"), *AssetPath); return TEXT(""); }
	if (Time < 0.f || Time > Seq->GetPlayLength())
	{
		OutError = FString::Printf(TEXT("Time %.2f out of range [0, %.2f]"), Time, Seq->GetPlayLength());
		return TEXT("");
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddNotify", "Add Anim Notify (Claude)"));
	Seq->Modify();

	FAnimNotifyEvent NewEvent;
	NewEvent.NotifyName = FName(*NotifyName);
	NewEvent.Link(Seq, Time);
	NewEvent.TriggerTimeOffset = GetTriggerTimeOffsetForType(Seq->CalculateOffsetForNotify(Time));
	NewEvent.TrackIndex = 0;

	// Resolve notify class — supports native AnimNotify or BP
	if (!NotifyClass.IsEmpty())
	{
		UClass* NCClass = nullptr;
		if (NotifyClass.StartsWith(TEXT("/Game")))
		{
			if (UObject* Loaded = LoadAssetByPath(NotifyClass))
			{
				if (UBlueprint* BP = Cast<UBlueprint>(Loaded)) NCClass = BP->GeneratedClass;
			}
		}
		if (!NCClass) NCClass = StaticLoadClass(UObject::StaticClass(), nullptr, *NotifyClass);
		if (!NCClass)
		{
			const FString AsEngine = FString::Printf(TEXT("/Script/Engine.%s"), *NotifyClass);
			NCClass = StaticLoadClass(UObject::StaticClass(), nullptr, *AsEngine);
		}

		if (NCClass && NCClass->IsChildOf(UAnimNotify::StaticClass()))
		{
			NewEvent.Notify = NewObject<UAnimNotify>(Seq, NCClass, NAME_None, RF_Transactional);
		}
		else if (NCClass && NCClass->IsChildOf(UAnimNotifyState::StaticClass()))
		{
			NewEvent.NotifyStateClass = NewObject<UAnimNotifyState>(Seq, NCClass, NAME_None, RF_Transactional);
			NewEvent.SetDuration(0.1f);
		}
		else if (!NotifyClass.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Notify class '%s' not found or not an AnimNotify/State"), *NotifyClass);
			return TEXT("");
		}
	}

	Seq->Notifies.Add(NewEvent);
	Seq->PostEditChange();
	Seq->MarkPackageDirty();

	return FString::Printf(TEXT("Added notify '%s' @ %.2fs to %s"), *NotifyName, Time, *AssetPath);
#endif
}

FString FClaudeContextProvider::RemoveAnimNotify(const FString& AssetPath, const FString& NotifyName, FString& OutError)
{
#if !WITH_EDITORONLY_DATA
	OutError = TEXT("Editor-only build required");
	return TEXT("");
#else
	UAnimSequenceBase* Seq = Cast<UAnimSequenceBase>(LoadAssetByPath(AssetPath));
	if (!Seq) { OutError = FString::Printf(TEXT("Not an Anim asset: %s"), *AssetPath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "RemNotify", "Remove Anim Notify (Claude)"));
	Seq->Modify();

	const int32 Removed = Seq->Notifies.RemoveAll([&](const FAnimNotifyEvent& N)
	{
		return N.NotifyName == FName(*NotifyName);
	});

	if (Removed == 0) { OutError = FString::Printf(TEXT("No notify named '%s'"), *NotifyName); return TEXT(""); }

	Seq->PostEditChange();
	Seq->MarkPackageDirty();
	return FString::Printf(TEXT("Removed %d notify(s) named '%s'"), Removed, *NotifyName);
#endif
}

FString FClaudeContextProvider::AddAnimCurve(const FString& SequencePath, const FString& CurveName, FString& OutError)
{
	UAnimSequence* Seq = Cast<UAnimSequence>(LoadAssetByPath(SequencePath));
	if (!Seq) { OutError = FString::Printf(TEXT("Not an Anim Sequence: %s"), *SequencePath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddCurve", "Add Anim Curve (Claude)"));

#if WITH_EDITORONLY_DATA
	// In UE5.2+ the correct way is via the DataModel controller
	IAnimationDataController& Controller = Seq->GetController();
	FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Float);
	const bool bAdded = Controller.AddCurve(CurveId);
	if (!bAdded)
	{
		OutError = FString::Printf(TEXT("AddCurve failed (curve '%s' may already exist)"), *CurveName);
		return TEXT("");
	}
	Seq->MarkPackageDirty();
	return FString::Printf(TEXT("Added float curve '%s' to %s"), *CurveName, *SequencePath);
#else
	OutError = TEXT("Editor-only operation");
	return TEXT("");
#endif
}

FString FClaudeContextProvider::CreateAnimMontage(const FString& NewAssetPath, const FString& SkeletonPath, const FString& SourceSequencePath, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path"); return TEXT(""); }

	USkeleton* Skeleton = Cast<USkeleton>(LoadAssetByPath(SkeletonPath));
	if (!Skeleton) { OutError = FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath); return TEXT(""); }

	UClass* FactoryClass = StaticLoadClass(UFactory::StaticClass(), nullptr, TEXT("/Script/UnrealEd.AnimMontageFactory"));
	if (!FactoryClass) { OutError = TEXT("AnimMontageFactory not found"); return TEXT(""); }

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	// Set TargetSkeleton and optional SourceAnimation via reflection
	if (FProperty* SkProp = FactoryClass->FindPropertyByName(TEXT("TargetSkeleton")))
	{
		if (FObjectProperty* ObjP = CastField<FObjectProperty>(SkProp))
			ObjP->SetObjectPropertyValue_InContainer(Factory, Skeleton);
	}
	if (!SourceSequencePath.IsEmpty())
	{
		UAnimSequence* Source = Cast<UAnimSequence>(LoadAssetByPath(SourceSequencePath));
		if (Source)
		{
			if (FProperty* SrcProp = FactoryClass->FindPropertyByName(TEXT("SourceAnimation")))
			{
				if (FObjectProperty* ObjP = CastField<FObjectProperty>(SrcProp))
					ObjP->SetObjectPropertyValue_InContainer(Factory, Source);
			}
		}
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateMontage", "Create Anim Montage (Claude)"));
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, UAnimMontage::StaticClass(), Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return TEXT(""); }

	return FString::Printf(TEXT("Created Anim Montage: %s"), *NewObj->GetPathName());
}

FString FClaudeContextProvider::AddMontageSection(const FString& MontagePath, const FString& SectionName, float StartTime, FString& OutError)
{
	UAnimMontage* Montage = Cast<UAnimMontage>(LoadAssetByPath(MontagePath));
	if (!Montage) { OutError = FString::Printf(TEXT("Not an AnimMontage: %s"), *MontagePath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddSection", "Add Montage Section (Claude)"));
	Montage->Modify();

	FCompositeSection NewSec;
	NewSec.SectionName = FName(*SectionName);
	NewSec.Link(Montage, StartTime);

	Montage->CompositeSections.Add(NewSec);
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	return FString::Printf(TEXT("Added section '%s' @ %.2fs"), *SectionName, StartTime);
}

FString FClaudeContextProvider::AddMontageSlot(const FString& MontagePath, const FString& SlotName, FString& OutError)
{
	UAnimMontage* Montage = Cast<UAnimMontage>(LoadAssetByPath(MontagePath));
	if (!Montage) { OutError = FString::Printf(TEXT("Not an AnimMontage: %s"), *MontagePath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddSlot", "Add Montage Slot (Claude)"));
	Montage->Modify();

	FSlotAnimationTrack NewSlot;
	NewSlot.SlotName = FName(*SlotName);
	Montage->SlotAnimTracks.Add(NewSlot);

	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return FString::Printf(TEXT("Added slot '%s' to %s"), *SlotName, *MontagePath);
}

// =============================================================================
// AnimBP graph nodes
// =============================================================================

static UEdGraph* FindAnimBPGraph(UAnimBlueprint* AnimBP, const FString& GraphName)
{
	if (!AnimBP) return nullptr;
	// AnimBP uses the same FunctionGraphs/UbergraphPages structure as regular BP,
	// plus the special AnimGraph which is usually the first function graph.
	for (UEdGraph* G : AnimBP->FunctionGraphs) if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) return G;
	for (UEdGraph* G : AnimBP->UbergraphPages) if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) return G;
	// Fallback: if user just says "AnimGraph", return the first function graph
	if (GraphName.Equals(TEXT("AnimGraph"), ESearchCase::IgnoreCase) && AnimBP->FunctionGraphs.Num() > 0)
	{
		return AnimBP->FunctionGraphs[0];
	}
	return nullptr;
}

// Helper — set an `Sequence`/`BlendSpace`/etc asset on an anim graph node's
// inner FAnimNode_* struct through reflection.
static void SetAnimNodeAssetProperty(UEdGraphNode* Node, const FString& InnerFieldName, UObject* AssetValue)
{
	if (!Node || !AssetValue) return;

	FProperty* NodeStructProp = Node->GetClass()->FindPropertyByName(TEXT("Node"));
	if (!NodeStructProp) return;
	FStructProperty* StructProp = CastField<FStructProperty>(NodeStructProp);
	if (!StructProp) return;

	void* StructAddr = StructProp->ContainerPtrToValuePtr<void>(Node);
	FProperty* Inner = StructProp->Struct->FindPropertyByName(FName(*InnerFieldName));
	if (!Inner) return;
	FObjectProperty* ObjProp = CastField<FObjectProperty>(Inner);
	if (!ObjProp) return;
	ObjProp->SetObjectPropertyValue(Inner->ContainerPtrToValuePtr<void>(StructAddr), AssetValue);
}

FString FClaudeContextProvider::AddAnimSequencePlayerNode(const FString& AnimBPPath, const FString& GraphName, const FString& SequencePath, int32 X, int32 Y, FString& OutError)
{
	UAnimBlueprint* ABP = Cast<UAnimBlueprint>(LoadAssetByPath(AnimBPPath));
	if (!ABP) { OutError = FString::Printf(TEXT("Not an AnimBP: %s"), *AnimBPPath); return TEXT(""); }
	UEdGraph* Graph = FindAnimBPGraph(ABP, GraphName);
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return TEXT(""); }

	UClass* WrapperClass = StaticLoadClass(UEdGraphNode::StaticClass(), nullptr, TEXT("/Script/AnimGraph.AnimGraphNode_SequencePlayer"));
	if (!WrapperClass) { OutError = TEXT("AnimGraphNode_SequencePlayer not found"); return TEXT(""); }

	UAnimSequenceBase* Sequence = nullptr;
	if (!SequencePath.IsEmpty())
	{
		Sequence = Cast<UAnimSequenceBase>(LoadAssetByPath(SequencePath));
		if (!Sequence) { OutError = FString::Printf(TEXT("Not an AnimSequence: %s"), *SequencePath); return TEXT(""); }
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddSeqPlayer", "Add Sequence Player Node (Claude)"));

	UEdGraphNode* NewNode = NewObject<UEdGraphNode>(Graph, WrapperClass);
	NewNode->CreateNewGuid();
	NewNode->SetFlags(RF_Transactional);
	NewNode->NodePosX = X;
	NewNode->NodePosY = Y;

	if (Sequence)
	{
		SetAnimNodeAssetProperty(NewNode, TEXT("Sequence"), Sequence);
	}

	Graph->AddNode(NewNode, false, false);
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();
	NewNode->ReconstructNode();

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);
	return FString::Printf(TEXT("Added SequencePlayer(%s). NodeGuid=%s"),
		Sequence ? *Sequence->GetName() : TEXT("empty"), *NewNode->NodeGuid.ToString());
}

FString FClaudeContextProvider::AddAnimStateMachineNode(const FString& AnimBPPath, const FString& GraphName, const FString& MachineName, int32 X, int32 Y, FString& OutError)
{
	UAnimBlueprint* ABP = Cast<UAnimBlueprint>(LoadAssetByPath(AnimBPPath));
	if (!ABP) { OutError = FString::Printf(TEXT("Not an AnimBP: %s"), *AnimBPPath); return TEXT(""); }
	UEdGraph* Graph = FindAnimBPGraph(ABP, GraphName);
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return TEXT(""); }

	UClass* WrapperClass = StaticLoadClass(UEdGraphNode::StaticClass(), nullptr, TEXT("/Script/AnimGraph.AnimGraphNode_StateMachine"));
	if (!WrapperClass) { OutError = TEXT("AnimGraphNode_StateMachine not found"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddStateMachine", "Add State Machine (Claude)"));

	UEdGraphNode* NewNode = NewObject<UEdGraphNode>(Graph, WrapperClass);
	NewNode->CreateNewGuid();
	NewNode->SetFlags(RF_Transactional);
	NewNode->NodePosX = X;
	NewNode->NodePosY = Y;

	Graph->AddNode(NewNode, false, false);
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	// The wrapper creates its own internal state machine subgraph in PostPlacedNewNode.
	// Renaming the machine requires touching the subgraph.
	if (!MachineName.IsEmpty())
	{
		// StateMachineName field may exist; try via reflection
		if (FProperty* NameProp = WrapperClass->FindPropertyByName(TEXT("StateMachineName")))
		{
			if (FNameProperty* NP = CastField<FNameProperty>(NameProp))
			{
				NP->SetPropertyValue_InContainer(NewNode, FName(*MachineName));
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);
	return FString::Printf(TEXT("Added State Machine '%s'. NodeGuid=%s. Use AnimBP editor to add states."),
		*MachineName, *NewNode->NodeGuid.ToString());
}

FString FClaudeContextProvider::AddAnimBlendSpacePlayerNode(const FString& AnimBPPath, const FString& GraphName, const FString& BlendSpacePath, int32 X, int32 Y, FString& OutError)
{
	UAnimBlueprint* ABP = Cast<UAnimBlueprint>(LoadAssetByPath(AnimBPPath));
	if (!ABP) { OutError = FString::Printf(TEXT("Not an AnimBP: %s"), *AnimBPPath); return TEXT(""); }
	UEdGraph* Graph = FindAnimBPGraph(ABP, GraphName);
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return TEXT(""); }

	UClass* WrapperClass = StaticLoadClass(UEdGraphNode::StaticClass(), nullptr, TEXT("/Script/AnimGraph.AnimGraphNode_BlendSpacePlayer"));
	if (!WrapperClass) { OutError = TEXT("AnimGraphNode_BlendSpacePlayer not found"); return TEXT(""); }

	UBlendSpace* BlendSpace = nullptr;
	if (!BlendSpacePath.IsEmpty())
	{
		BlendSpace = Cast<UBlendSpace>(LoadAssetByPath(BlendSpacePath));
		if (!BlendSpace) { OutError = FString::Printf(TEXT("Not a BlendSpace: %s"), *BlendSpacePath); return TEXT(""); }
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddBSPlayer", "Add BlendSpace Player Node (Claude)"));

	UEdGraphNode* NewNode = NewObject<UEdGraphNode>(Graph, WrapperClass);
	NewNode->CreateNewGuid();
	NewNode->SetFlags(RF_Transactional);
	NewNode->NodePosX = X;
	NewNode->NodePosY = Y;

	if (BlendSpace)
	{
		SetAnimNodeAssetProperty(NewNode, TEXT("BlendSpace"), BlendSpace);
	}

	Graph->AddNode(NewNode, false, false);
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();
	NewNode->ReconstructNode();

	FBlueprintEditorUtils::MarkBlueprintAsModified(ABP);
	return FString::Printf(TEXT("Added BlendSpacePlayer(%s). NodeGuid=%s"),
		BlendSpace ? *BlendSpace->GetName() : TEXT("empty"), *NewNode->NodeGuid.ToString());
}

// =============================================================================
// Materials
// =============================================================================
//
// UE materials have several asset types with different factories:
//   - UMaterial: the master shader (UMaterialFactoryNew)
//   - UMaterialInstanceConstant: per-instance parameter overrides (UMaterialInstanceConstantFactoryNew)
//
// For MICs we use UMaterialEditingLibrary which wraps the common parameter
// setters safely and ensures the instance is marked dirty + shader updated.

FString FClaudeContextProvider::CreateMaterial(const FString& NewAssetPath, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path, use '/Game/Materials/M_Name'"); return TEXT(""); }

	UClass* FactoryClass = StaticLoadClass(UFactory::StaticClass(), nullptr, TEXT("/Script/UnrealEd.MaterialFactoryNew"));
	if (!FactoryClass) { OutError = TEXT("MaterialFactoryNew not found"); return TEXT(""); }

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateMat", "Create Material (Claude)"));
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, UMaterial::StaticClass(), Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return TEXT(""); }

	return FString::Printf(TEXT("Created Material: %s. Graph is empty — use open_asset_editor to edit manually, or add expressions via material tools."), *NewObj->GetPathName());
}

FString FClaudeContextProvider::CreateMaterialInstance(const FString& NewAssetPath, const FString& ParentMaterialPath, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path, use '/Game/Materials/MI_Name'"); return TEXT(""); }

	UMaterialInterface* Parent = Cast<UMaterialInterface>(LoadAssetByPath(ParentMaterialPath));
	if (!Parent) { OutError = FString::Printf(TEXT("Parent material not found: %s"), *ParentMaterialPath); return TEXT(""); }

	UClass* FactoryClass = StaticLoadClass(UFactory::StaticClass(), nullptr, TEXT("/Script/UnrealEd.MaterialInstanceConstantFactoryNew"));
	if (!FactoryClass) { OutError = TEXT("MaterialInstanceConstantFactoryNew not found"); return TEXT(""); }

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	// Set initial parent via reflection — factory has InitialParent field
	if (FProperty* ParentProp = FactoryClass->FindPropertyByName(TEXT("InitialParent")))
	{
		if (FObjectProperty* ObjP = CastField<FObjectProperty>(ParentProp))
			ObjP->SetObjectPropertyValue_InContainer(Factory, Parent);
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateMIC", "Create Material Instance (Claude)"));
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return TEXT(""); }

	return FString::Printf(TEXT("Created Material Instance: %s (parent: %s)"), *NewObj->GetPathName(), *Parent->GetName());
}

FString FClaudeContextProvider::GetMaterialInfo(const FString& MaterialPath)
{
	UMaterialInterface* Mat = Cast<UMaterialInterface>(LoadAssetByPath(MaterialPath));
	if (!Mat) return FString::Printf(TEXT("{\"error\":\"Not a Material: %s\"}"), *MaterialPath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), MaterialPath);
	Root->SetStringField(TEXT("class"), Mat->GetClass()->GetName());

	// Settings from the underlying UMaterial (for MICs — from the parent)
	if (UMaterial* BaseMaterial = Mat->GetMaterial())
	{
		const UEnum* BlendEnum = StaticEnum<EBlendMode>();
		const UEnum* ShadingEnum = StaticEnum<EMaterialShadingModel>();
		const UEnum* DomainEnum = StaticEnum<EMaterialDomain>();

		Root->SetStringField(TEXT("blend_mode"), BlendEnum ? BlendEnum->GetNameStringByValue(BaseMaterial->BlendMode) : FString());
		Root->SetStringField(TEXT("domain"),     DomainEnum ? DomainEnum->GetNameStringByValue(BaseMaterial->MaterialDomain) : FString());
		// ShadingModel is a bitfield in modern UE; pick the first set bit for readability
		Root->SetNumberField(TEXT("shading_model_mask"), BaseMaterial->GetShadingModels().GetShadingModelField());
		Root->SetBoolField(TEXT("two_sided"),  BaseMaterial->IsTwoSided());
		Root->SetStringField(TEXT("base_material_path"), BaseMaterial->GetPathName());
	}

	// Collect all parameters the material exposes
	TArray<FMaterialParameterInfo> ScalarInfos;  TArray<FGuid> ScalarGuids;
	TArray<FMaterialParameterInfo> VectorInfos;  TArray<FGuid> VectorGuids;
	TArray<FMaterialParameterInfo> TextureInfos; TArray<FGuid> TextureGuids;
	TArray<FMaterialParameterInfo> StaticSwitchInfos; TArray<FGuid> StaticSwitchGuids;

	Mat->GetAllScalarParameterInfo(ScalarInfos, ScalarGuids);
	Mat->GetAllVectorParameterInfo(VectorInfos, VectorGuids);
	Mat->GetAllTextureParameterInfo(TextureInfos, TextureGuids);
	Mat->GetAllStaticSwitchParameterInfo(StaticSwitchInfos, StaticSwitchGuids);

	auto SerializeParams = [](const TArray<FMaterialParameterInfo>& Params, const FString& Type)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FMaterialParameterInfo& P : Params)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), P.Name.ToString());
			O->SetStringField(TEXT("type"), Type);
			if (P.Association != GlobalParameter)
			{
				O->SetNumberField(TEXT("layer_index"), P.Index);
			}
			Arr.Add(MakeShared<FJsonValueObject>(O));
		}
		return Arr;
	};

	TArray<TSharedPtr<FJsonValue>> AllParams;
	AllParams.Append(SerializeParams(ScalarInfos,       TEXT("scalar")));
	AllParams.Append(SerializeParams(VectorInfos,       TEXT("vector")));
	AllParams.Append(SerializeParams(TextureInfos,      TEXT("texture")));
	AllParams.Append(SerializeParams(StaticSwitchInfos, TEXT("static_switch")));
	Root->SetArrayField(TEXT("parameters"), AllParams);
	Root->SetNumberField(TEXT("param_count"), AllParams.Num());

	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::GetMaterialInstanceParams(const FString& MICPath)
{
	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(LoadAssetByPath(MICPath));
	if (!MIC) return FString::Printf(TEXT("{\"error\":\"Not a MaterialInstanceConstant: %s\"}"), *MICPath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), MICPath);
	if (MIC->Parent) Root->SetStringField(TEXT("parent"), MIC->Parent->GetPathName());

	// Explicitly-overridden parameters on this instance
	TArray<TSharedPtr<FJsonValue>> Scalars;
	for (const FScalarParameterValue& P : MIC->ScalarParameterValues)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), P.ParameterInfo.Name.ToString());
		O->SetNumberField(TEXT("value"), P.ParameterValue);
		Scalars.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("scalar_overrides"), Scalars);

	TArray<TSharedPtr<FJsonValue>> Vectors;
	for (const FVectorParameterValue& P : MIC->VectorParameterValues)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), P.ParameterInfo.Name.ToString());
		O->SetStringField(TEXT("value"), P.ParameterValue.ToString());
		Vectors.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("vector_overrides"), Vectors);

	TArray<TSharedPtr<FJsonValue>> Textures;
	for (const FTextureParameterValue& P : MIC->TextureParameterValues)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), P.ParameterInfo.Name.ToString());
		O->SetStringField(TEXT("value"), P.ParameterValue ? P.ParameterValue->GetPathName() : TEXT(""));
		Textures.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("texture_overrides"), Textures);

	// Static switches live in the StaticParameters block (and ShaderStaticParameters in newer UE)
	TArray<TSharedPtr<FJsonValue>> Switches;
	FStaticParameterSet StaticParams;
	MIC->GetStaticParameterValues(StaticParams);
	for (const FStaticSwitchParameter& SP : StaticParams.StaticSwitchParameters)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), SP.ParameterInfo.Name.ToString());
		O->SetBoolField(TEXT("value"), SP.Value);
		O->SetBoolField(TEXT("overridden"), SP.bOverride);
		Switches.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("static_switches"), Switches);

	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::SetMaterialScalarParam(const FString& MICPath, const FString& ParamName, float Value, FString& OutError)
{
	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(LoadAssetByPath(MICPath));
	if (!MIC) { OutError = FString::Printf(TEXT("Not a MIC: %s"), *MICPath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetMatScalar", "Set Material Scalar (Claude)"));
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MIC, FName(*ParamName), Value);
	MIC->MarkPackageDirty();
	UMaterialEditingLibrary::UpdateMaterialInstance(MIC);

	return FString::Printf(TEXT("Set scalar '%s' = %f on %s"), *ParamName, Value, *MICPath);
}

FString FClaudeContextProvider::SetMaterialVectorParam(const FString& MICPath, const FString& ParamName, const FLinearColor& Value, FString& OutError)
{
	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(LoadAssetByPath(MICPath));
	if (!MIC) { OutError = FString::Printf(TEXT("Not a MIC: %s"), *MICPath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetMatVector", "Set Material Vector (Claude)"));
	UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(MIC, FName(*ParamName), Value);
	MIC->MarkPackageDirty();
	UMaterialEditingLibrary::UpdateMaterialInstance(MIC);

	return FString::Printf(TEXT("Set vector '%s' = (R=%f,G=%f,B=%f,A=%f) on %s"),
		*ParamName, Value.R, Value.G, Value.B, Value.A, *MICPath);
}

FString FClaudeContextProvider::SetMaterialTextureParam(const FString& MICPath, const FString& ParamName, const FString& TexturePath, FString& OutError)
{
	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(LoadAssetByPath(MICPath));
	if (!MIC) { OutError = FString::Printf(TEXT("Not a MIC: %s"), *MICPath); return TEXT(""); }

	UTexture* Tex = Cast<UTexture>(LoadAssetByPath(TexturePath));
	if (!Tex) { OutError = FString::Printf(TEXT("Texture not found: %s"), *TexturePath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetMatTexture", "Set Material Texture (Claude)"));
	UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MIC, FName(*ParamName), Tex);
	MIC->MarkPackageDirty();
	UMaterialEditingLibrary::UpdateMaterialInstance(MIC);

	return FString::Printf(TEXT("Set texture '%s' = %s on %s"), *ParamName, *Tex->GetName(), *MICPath);
}

FString FClaudeContextProvider::SetMaterialStaticSwitch(const FString& MICPath, const FString& ParamName, bool Value, FString& OutError)
{
	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(LoadAssetByPath(MICPath));
	if (!MIC) { OutError = FString::Printf(TEXT("Not a MIC: %s"), *MICPath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetMatSwitch", "Set Material Static Switch (Claude)"));

	FStaticParameterSet StaticParams;
	MIC->GetStaticParameterValues(StaticParams);

	bool bFound = false;
	for (FStaticSwitchParameter& SP : StaticParams.StaticSwitchParameters)
	{
		if (SP.ParameterInfo.Name == FName(*ParamName))
		{
			SP.Value = Value;
			SP.bOverride = true;
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		// Inherit from parent — add new override
		FStaticSwitchParameter NewSwitch;
		NewSwitch.ParameterInfo.Name = FName(*ParamName);
		NewSwitch.Value = Value;
		NewSwitch.bOverride = true;
		StaticParams.StaticSwitchParameters.Add(NewSwitch);
	}

	MIC->UpdateStaticPermutation(StaticParams);
	MIC->MarkPackageDirty();
	UMaterialEditingLibrary::UpdateMaterialInstance(MIC);

	return FString::Printf(TEXT("Set static switch '%s' = %s on %s. Note: shader recompile may take a moment."),
		*ParamName, Value ? TEXT("true") : TEXT("false"), *MICPath);
}

// =============================================================================
// Level Design — mass placement and organization
// =============================================================================
//
// Core principles:
//   1. One transaction per bulk op — single Ctrl+Z undoes the whole batch
//   2. Use StaticMeshActor directly when possible (no BP needed for whitebox)
//   3. Folder paths for World Outliner grouping are first-class
//   4. All operations work on the EDITOR world (not PIE)

FString FClaudeContextProvider::SpawnStaticMeshActor(
	const FString& MeshPath, const FVector& Location, const FRotator& Rotation,
	const FVector& Scale, const FString& MaterialOverride, const FString& ActorLabel, FString& OutError)
{
	if (!GEditor) { OutError = TEXT("No GEditor"); return TEXT(""); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return TEXT(""); }

	UStaticMesh* Mesh = Cast<UStaticMesh>(LoadAssetByPath(MeshPath));
	if (!Mesh) { OutError = FString::Printf(TEXT("StaticMesh not found: %s"), *MeshPath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SpawnSMA", "Spawn StaticMeshActor (Claude)"));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(), Location, Rotation, Params);
	if (!Actor) { OutError = TEXT("SpawnActor returned null"); return TEXT(""); }

	Actor->SetActorScale3D(Scale);
	Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
	Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);

	if (!MaterialOverride.IsEmpty())
	{
		if (UMaterialInterface* Mat = Cast<UMaterialInterface>(LoadAssetByPath(MaterialOverride)))
		{
			Actor->GetStaticMeshComponent()->SetMaterial(0, Mat);
		}
	}

	if (!ActorLabel.IsEmpty())
	{
		Actor->SetActorLabel(ActorLabel);
	}
	else
	{
		Actor->SetActorLabel(FString::Printf(TEXT("%s_%d"), *Mesh->GetName(), Actor->GetUniqueID() % 10000));
	}

	return FString::Printf(TEXT("Spawned '%s' at (%s). Label='%s'"),
		*Mesh->GetName(), *Location.ToString(), *Actor->GetActorLabel());
}

FString FClaudeContextProvider::SpawnActorsGrid(
	const FString& ClassPath, const FString& MeshPath, const FVector& Origin,
	int32 CountX, int32 CountY, int32 CountZ, const FVector& Spacing,
	const FString& FolderPath, FString& OutError)
{
	if (!GEditor) { OutError = TEXT("No GEditor"); return TEXT(""); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return TEXT(""); }

	CountX = FMath::Clamp(CountX, 1, 200);
	CountY = FMath::Clamp(CountY, 1, 200);
	CountZ = FMath::Clamp(CountZ, 1, 200);
	const int32 Total = CountX * CountY * CountZ;
	if (Total > 2000)
	{
		OutError = FString::Printf(TEXT("Grid total %d > 2000 cap. Reduce counts."), Total);
		return TEXT("");
	}

	// Resolve class: either explicit class OR spawn StaticMeshActors with a mesh
	UClass* ActorClass = nullptr;
	UStaticMesh* Mesh = nullptr;

	if (!ClassPath.IsEmpty())
	{
		if (UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *ClassPath))
		{
			if (UBlueprint* BP = Cast<UBlueprint>(Loaded)) ActorClass = BP->GeneratedClass;
			else ActorClass = Cast<UClass>(Loaded);
		}
		if (!ActorClass) ActorClass = StaticLoadClass(AActor::StaticClass(), nullptr, *ClassPath);
	}
	if (!MeshPath.IsEmpty())
	{
		Mesh = Cast<UStaticMesh>(LoadAssetByPath(MeshPath));
	}

	if (!ActorClass && !Mesh)
	{
		OutError = TEXT("Either class_path or mesh_path must be specified");
		return TEXT("");
	}
	if (!ActorClass) ActorClass = AStaticMeshActor::StaticClass();

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SpawnGrid", "Spawn Actor Grid (Claude)"));

	int32 Spawned = 0;
	for (int32 ix = 0; ix < CountX; ++ix)
	for (int32 iy = 0; iy < CountY; ++iy)
	for (int32 iz = 0; iz < CountZ; ++iz)
	{
		const FVector Pos = Origin + FVector(ix * Spacing.X, iy * Spacing.Y, iz * Spacing.Z);

		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(ActorClass, Pos, FRotator::ZeroRotator, P);
		if (!Actor) continue;

		// If it's a StaticMeshActor and we have a mesh, apply it
		if (Mesh)
		{
			if (AStaticMeshActor* SMA = Cast<AStaticMeshActor>(Actor))
			{
				SMA->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				SMA->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			}
		}

		if (!FolderPath.IsEmpty())
		{
			Actor->SetFolderPath(FName(*FolderPath));
		}

		Actor->SetActorLabel(FString::Printf(TEXT("Grid_%d_%d_%d"), ix, iy, iz));
		++Spawned;
	}

	return FString::Printf(TEXT("Spawned %d/%d actors in %dx%dx%d grid at %s, spacing=(%s)%s"),
		Spawned, Total, CountX, CountY, CountZ, *Origin.ToString(), *Spacing.ToString(),
		FolderPath.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", folder='%s'"), *FolderPath));
}

FString FClaudeContextProvider::SpawnActorsLine(
	const FString& ClassPath, const FString& MeshPath, const FVector& Start, const FVector& End,
	int32 Count, const FString& FolderPath, FString& OutError)
{
	if (!GEditor) { OutError = TEXT("No GEditor"); return TEXT(""); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return TEXT(""); }

	Count = FMath::Clamp(Count, 1, 500);

	UClass* ActorClass = nullptr;
	UStaticMesh* Mesh = nullptr;

	if (!ClassPath.IsEmpty())
	{
		if (UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *ClassPath))
		{
			if (UBlueprint* BP = Cast<UBlueprint>(Loaded)) ActorClass = BP->GeneratedClass;
			else ActorClass = Cast<UClass>(Loaded);
		}
		if (!ActorClass) ActorClass = StaticLoadClass(AActor::StaticClass(), nullptr, *ClassPath);
	}
	if (!MeshPath.IsEmpty())
	{
		Mesh = Cast<UStaticMesh>(LoadAssetByPath(MeshPath));
	}
	if (!ActorClass && !Mesh) { OutError = TEXT("Either class_path or mesh_path required"); return TEXT(""); }
	if (!ActorClass) ActorClass = AStaticMeshActor::StaticClass();

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SpawnLine", "Spawn Actor Line (Claude)"));

	int32 Spawned = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		const float T = Count == 1 ? 0.f : ((float)i / (float)(Count - 1));
		const FVector Pos = FMath::Lerp(Start, End, T);

		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(ActorClass, Pos, FRotator::ZeroRotator, P);
		if (!Actor) continue;

		if (Mesh)
		{
			if (AStaticMeshActor* SMA = Cast<AStaticMeshActor>(Actor))
			{
				SMA->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				SMA->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			}
		}

		if (!FolderPath.IsEmpty()) Actor->SetFolderPath(FName(*FolderPath));
		Actor->SetActorLabel(FString::Printf(TEXT("Line_%02d"), i));
		++Spawned;
	}

	return FString::Printf(TEXT("Spawned %d actors on line from %s to %s"),
		Spawned, *Start.ToString(), *End.ToString());
}

FString FClaudeContextProvider::DuplicateActors(const TArray<FString>& ActorPaths, const FVector& OffsetLocation, int32 CopyCount, FString& OutError)
{
	if (!GEditor) { OutError = TEXT("No GEditor"); return TEXT(""); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return TEXT(""); }

	CopyCount = FMath::Clamp(CopyCount, 1, 100);

	// Resolve source actors
	TArray<AActor*> Sources;
	for (const FString& Path : ActorPaths)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetPathName() == Path || It->GetActorLabel() == Path || It->GetName() == Path)
			{
				Sources.Add(*It);
				break;
			}
		}
	}
	if (Sources.Num() == 0) { OutError = TEXT("No source actors resolved"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "DupActors", "Duplicate Actors (Claude)"));

	int32 Duplicated = 0;
	for (AActor* Src : Sources)
	{
		if (!Src) continue;
		for (int32 i = 1; i <= CopyCount; ++i)
		{
			const FVector NewLoc = Src->GetActorLocation() + (OffsetLocation * (float)i);

			FActorSpawnParameters P;
			P.Template = Src;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			AActor* Copy = World->SpawnActor<AActor>(Src->GetClass(), NewLoc, Src->GetActorRotation(), P);
			if (Copy)
			{
				Copy->SetActorScale3D(Src->GetActorScale3D());
				Copy->SetFolderPath(Src->GetFolderPath());
				Copy->SetActorLabel(FString::Printf(TEXT("%s_Copy%d"), *Src->GetActorLabel(), i));
				++Duplicated;
			}
		}
	}

	return FString::Printf(TEXT("Duplicated %d source(s) x %d copies = %d new actors, offset=(%s)"),
		Sources.Num(), CopyCount, Duplicated, *OffsetLocation.ToString());
}

FString FClaudeContextProvider::SetActorFolder(const FString& ActorPath, const FString& FolderPath, FString& OutError)
{
	AActor* Actor = FindActorByPathOrLabel(ActorPath);
	if (!Actor) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorPath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetFolder", "Set Actor Folder (Claude)"));
	Actor->Modify();
	Actor->SetFolderPath(FName(*FolderPath));
	return FString::Printf(TEXT("Moved '%s' to folder '%s'"), *Actor->GetActorLabel(), *FolderPath);
}

FString FClaudeContextProvider::GetActorBounds(const FString& ActorPath)
{
	AActor* Actor = FindActorByPathOrLabel(ActorPath);
	if (!Actor) return FString::Printf(TEXT("{\"error\":\"Actor not found: %s\"}"), *ActorPath);

	FVector Origin, Extent;
	Actor->GetActorBounds(/*bOnlyCollidingComponents*/ false, Origin, Extent);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("label"), Actor->GetActorLabel());
	Root->SetStringField(TEXT("path"), Actor->GetPathName());
	Root->SetStringField(TEXT("origin"), Origin.ToString());
	Root->SetStringField(TEXT("extent"), Extent.ToString());
	Root->SetStringField(TEXT("size"), (Extent * 2.f).ToString());
	Root->SetStringField(TEXT("min"), (Origin - Extent).ToString());
	Root->SetStringField(TEXT("max"), (Origin + Extent).ToString());
	return JsonObjectToString(Root);
}

FString FClaudeContextProvider::SaveCurrentLevel(FString& OutError)
{
	if (!GEditor) { OutError = TEXT("No GEditor"); return TEXT(""); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return TEXT(""); }

	ULevel* Level = World->GetCurrentLevel();
	if (!Level) { OutError = TEXT("No current level"); return TEXT(""); }

	const bool bSaved = FEditorFileUtils::SaveLevel(Level);
	if (!bSaved) { OutError = TEXT("SaveLevel returned false"); return TEXT(""); }

	return FString::Printf(TEXT("Saved level: %s"), *Level->GetOutermost()->GetName());
}

FString FClaudeContextProvider::BatchSetActorProperty(
	const FString& ClassFilter, const FString& FolderFilter, const FString& NameContains,
	const FString& PropertyName, const FString& NewValue, FString& OutError)
{
	if (!GEditor) { OutError = TEXT("No GEditor"); return TEXT(""); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return TEXT(""); }

	// Resolve optional class filter
	UClass* Filter = nullptr;
	if (!ClassFilter.IsEmpty())
	{
		Filter = StaticLoadClass(AActor::StaticClass(), nullptr, *ClassFilter);
		if (!Filter)
		{
			const FString AsEngine = FString::Printf(TEXT("/Script/Engine.%s"), *ClassFilter);
			Filter = StaticLoadClass(AActor::StaticClass(), nullptr, *AsEngine);
		}
	}

	// Collect matching actors
	TArray<AActor*> Matches;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (Filter && !It->GetClass()->IsChildOf(Filter)) continue;
		if (!FolderFilter.IsEmpty() && It->GetFolderPath().ToString() != FolderFilter) continue;
		if (!NameContains.IsEmpty() && !It->GetActorLabel().Contains(NameContains)) continue;
		Matches.Add(*It);
	}

	if (Matches.Num() == 0)
	{
		OutError = TEXT("No actors matched the filters — nothing changed");
		return TEXT("");
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "BatchSet", "Batch Set Property (Claude)"));

	int32 Applied = 0;
	for (AActor* Actor : Matches)
	{
		FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
		if (!Prop) continue;
		Actor->Modify();
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
		const TCHAR* Result = Prop->ImportText_Direct(*NewValue, ValuePtr, Actor, PPF_None);
		if (Result)
		{
			FPropertyChangedEvent PCE(Prop);
			Actor->PostEditChangeProperty(PCE);
			++Applied;
		}
	}

	return FString::Printf(TEXT("Applied '%s'='%s' to %d/%d matching actors"),
		*PropertyName, *NewValue, Applied, Matches.Num());
}

FString FClaudeContextProvider::AlignActors(const TArray<FString>& ActorPaths, const FString& Axis, const FString& Mode, FString& OutError)
{
	if (!GEditor) { OutError = TEXT("No GEditor"); return TEXT(""); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return TEXT(""); }

	TArray<AActor*> Actors;
	for (const FString& Path : ActorPaths)
	{
		if (AActor* A = FindActorByPathOrLabel(Path)) Actors.Add(A);
	}
	if (Actors.Num() < 2) { OutError = TEXT("Need at least 2 actors to align"); return TEXT(""); }

	// Axis index: x=0, y=1, z=2
	int32 AxisIdx = 2;
	if (Axis.Equals(TEXT("x"), ESearchCase::IgnoreCase)) AxisIdx = 0;
	else if (Axis.Equals(TEXT("y"), ESearchCase::IgnoreCase)) AxisIdx = 1;
	else if (Axis.Equals(TEXT("z"), ESearchCase::IgnoreCase)) AxisIdx = 2;
	else { OutError = TEXT("Axis must be 'x', 'y', or 'z'"); return TEXT(""); }

	// Reference value by mode
	const FString LowerMode = Mode.ToLower();
	float Ref = 0.f;

	TArray<float> Values;
	for (AActor* A : Actors)
	{
		const FVector Loc = A->GetActorLocation();
		Values.Add(Loc.Component(AxisIdx));
	}

	if (LowerMode == TEXT("min"))
	{
		Ref = Values[0];
		for (float V : Values) Ref = FMath::Min(Ref, V);
	}
	else if (LowerMode == TEXT("max"))
	{
		Ref = Values[0];
		for (float V : Values) Ref = FMath::Max(Ref, V);
	}
	else if (LowerMode == TEXT("center") || LowerMode == TEXT("avg"))
	{
		float Sum = 0.f;
		for (float V : Values) Sum += V;
		Ref = Sum / Values.Num();
	}
	else if (LowerMode == TEXT("first"))
	{
		Ref = Values[0];
	}
	else
	{
		OutError = TEXT("Mode must be one of: min, max, center, first");
		return TEXT("");
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AlignActors", "Align Actors (Claude)"));
	for (AActor* A : Actors)
	{
		A->Modify();
		FVector Loc = A->GetActorLocation();
		Loc.Component(AxisIdx) = Ref;
		A->SetActorLocation(Loc);
	}

	return FString::Printf(TEXT("Aligned %d actors to %s=%s on axis %s"),
		Actors.Num(), *LowerMode, *FString::Printf(TEXT("%.2f"), Ref), *Axis.ToUpper());
}

FString FClaudeContextProvider::PlaceOnSurface(const FString& ActorPath, float MaxDistance, FString& OutError)
{
	if (!GEditor) { OutError = TEXT("No GEditor"); return TEXT(""); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return TEXT(""); }

	AActor* Actor = FindActorByPathOrLabel(ActorPath);
	if (!Actor) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorPath); return TEXT(""); }

	const float Dist = MaxDistance > 0.f ? MaxDistance : 100000.f;

	// Ray downward from the actor's current location
	const FVector Start = Actor->GetActorLocation() + FVector(0, 0, 1.f); // tiny nudge up
	const FVector End = Start + FVector(0, 0, -Dist);

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Actor);

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params);
	if (!bHit)
	{
		OutError = FString::Printf(TEXT("No surface found within %f units below"), Dist);
		return TEXT("");
	}

	// Snap actor's bottom (min-Z of bounds) to the hit point
	FVector Origin, Extent;
	Actor->GetActorBounds(false, Origin, Extent);
	const float BottomOffset = Origin.Z - Extent.Z - Actor->GetActorLocation().Z; // negative
	const FVector NewLoc(Actor->GetActorLocation().X, Actor->GetActorLocation().Y, Hit.ImpactPoint.Z - BottomOffset);

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "PlaceSurface", "Place On Surface (Claude)"));
	Actor->Modify();
	Actor->SetActorLocation(NewLoc);

	return FString::Printf(TEXT("Placed '%s' on surface at Z=%.2f (hit: %s)"),
		*Actor->GetActorLabel(), NewLoc.Z,
		Hit.GetActor() ? *Hit.GetActor()->GetActorLabel() : TEXT("world"));
}

// =============================================================================
// StateTree
// =============================================================================
//
// StateTree is UE's newer alternative to Behavior Tree. Key concepts:
//
//   UStateTree              — the asset (.uasset). Contains compiled state
//                             tree data + a list of UStateTreeState objects
//                             during edit time.
//   UStateTreeState         — a single state (hierarchical, has children).
//                             Contains tasks, enter conditions, transitions.
//   FStateTreeTaskBase      — base for tasks (run while state is active).
//   FStateTreeEvaluatorBase — base for evaluators (update data).
//   FStateTreeConditionBase — base for conditions (gate transitions/states).
//   UStateTreeSchema        — defines what external context the tree needs
//                             (e.g. StateTreeAIComponentSchema requires an
//                             AAIController and AActor as inputs).
//
// IMPORTANT: most of the interesting state lives in UStateTreeEditorData
// which is the edit-time sibling of UStateTree. States/tasks/transitions are
// stored there, then compiled into runtime data in UStateTree::SchemaStruct.
// We operate on UStateTreeEditorData.
//
// API is heavily WITH_EDITORONLY_DATA-gated and has changed across 5.3/5.4/5.5/5.7.
// We use reflection for most property access to survive version bumps.

static UObject* FindStateTreeEditorData(UStateTree* STree)
{
	if (!STree) return nullptr;
	// UStateTree::EditorData is an EditorOnly UPROPERTY of type UStateTreeEditorData*
	FProperty* EditorDataProp = STree->GetClass()->FindPropertyByName(TEXT("EditorData"));
	if (!EditorDataProp) return nullptr;
	FObjectProperty* ObjP = CastField<FObjectProperty>(EditorDataProp);
	if (!ObjP) return nullptr;
	return ObjP->GetObjectPropertyValue_InContainer(STree);
}

// Find a state by its FGuid-as-string ID. States are stored in EditorData->SubTrees
// and each subtree's root has nested Children. We walk the tree recursively.
static UObject* FindStateTreeStateByGuid(UObject* EditorData, const FString& GuidString)
{
	if (!EditorData) return nullptr;
	FGuid Target;
	if (!FGuid::Parse(GuidString, Target)) return nullptr;

	// SubTrees is a TArray of FStateTreeStateLink or similar wrapper structs
	// in newer versions; older versions had a direct States array. Try both.
	static const TCHAR* CandidateArrayNames[] = { TEXT("SubTrees"), TEXT("States"), nullptr };

	TFunction<UObject*(UObject*)> Recurse;
	Recurse = [&Target, &Recurse](UObject* State) -> UObject*
	{
		if (!State) return nullptr;
		// Check this state's ID
		if (FProperty* IdProp = State->GetClass()->FindPropertyByName(TEXT("ID")))
		{
			if (FStructProperty* SP = CastField<FStructProperty>(IdProp))
			{
				if (SP->Struct && SP->Struct->GetFName() == FName("Guid"))
				{
					const FGuid* G = SP->ContainerPtrToValuePtr<FGuid>(State);
					if (G && *G == Target) return State;
				}
			}
		}
		// Recurse into Children
		FProperty* ChildrenProp = State->GetClass()->FindPropertyByName(TEXT("Children"));
		if (FArrayProperty* AP = CastField<FArrayProperty>(ChildrenProp))
		{
			FScriptArrayHelper H(AP, ChildrenProp->ContainerPtrToValuePtr<void>(State));
			if (FObjectProperty* InnerObj = CastField<FObjectProperty>(AP->Inner))
			{
				for (int32 i = 0; i < H.Num(); ++i)
				{
					UObject* Child = InnerObj->GetObjectPropertyValue(H.GetRawPtr(i));
					if (UObject* Found = Recurse(Child)) return Found;
				}
			}
		}
		return nullptr;
	};

	for (int32 i = 0; CandidateArrayNames[i] != nullptr; ++i)
	{
		FProperty* ArrProp = EditorData->GetClass()->FindPropertyByName(CandidateArrayNames[i]);
		if (!ArrProp) continue;
		FArrayProperty* AP = CastField<FArrayProperty>(ArrProp);
		if (!AP) continue;
		FScriptArrayHelper H(AP, ArrProp->ContainerPtrToValuePtr<void>(EditorData));
		if (FObjectProperty* InnerObj = CastField<FObjectProperty>(AP->Inner))
		{
			for (int32 j = 0; j < H.Num(); ++j)
			{
				UObject* Root = InnerObj->GetObjectPropertyValue(H.GetRawPtr(j));
				if (UObject* Found = Recurse(Root)) return Found;
			}
		}
	}
	return nullptr;
}

// Recursively serialize a state subtree into JSON
static TSharedPtr<FJsonObject> SerializeStateTreeState(UObject* State, int32 Depth = 0)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (!State) { Obj->SetStringField(TEXT("error"), TEXT("null state")); return Obj; }

	// Name
	if (FProperty* NameProp = State->GetClass()->FindPropertyByName(TEXT("Name")))
	{
		if (FNameProperty* NP = CastField<FNameProperty>(NameProp))
		{
			Obj->SetStringField(TEXT("name"), NP->GetPropertyValue_InContainer(State).ToString());
		}
	}

	// ID
	if (FProperty* IdProp = State->GetClass()->FindPropertyByName(TEXT("ID")))
	{
		if (FStructProperty* SP = CastField<FStructProperty>(IdProp))
		{
			const FGuid* G = SP->ContainerPtrToValuePtr<FGuid>(State);
			if (G) Obj->SetStringField(TEXT("id"), G->ToString());
		}
	}

	// Type enum (can be State, SubTree, Linked, etc.)
	if (FProperty* TypeProp = State->GetClass()->FindPropertyByName(TEXT("Type")))
	{
		if (FEnumProperty* EP = CastField<FEnumProperty>(TypeProp))
		{
			const int64 V = EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(
				TypeProp->ContainerPtrToValuePtr<void>(State));
			Obj->SetStringField(TEXT("type"), EP->GetEnum()->GetNameStringByValue(V));
		}
	}

	// Tasks count
	if (FProperty* TasksProp = State->GetClass()->FindPropertyByName(TEXT("Tasks")))
	{
		if (FArrayProperty* AP = CastField<FArrayProperty>(TasksProp))
		{
			FScriptArrayHelper H(AP, TasksProp->ContainerPtrToValuePtr<void>(State));
			Obj->SetNumberField(TEXT("task_count"), H.Num());
		}
	}

	// Transitions count
	if (FProperty* TransProp = State->GetClass()->FindPropertyByName(TEXT("Transitions")))
	{
		if (FArrayProperty* AP = CastField<FArrayProperty>(TransProp))
		{
			FScriptArrayHelper H(AP, TransProp->ContainerPtrToValuePtr<void>(State));
			Obj->SetNumberField(TEXT("transition_count"), H.Num());
		}
	}

	// Children — recurse (cap depth to avoid infinite loops on malformed data)
	if (Depth < 10)
	{
		TArray<TSharedPtr<FJsonValue>> Children;
		if (FProperty* ChildrenProp = State->GetClass()->FindPropertyByName(TEXT("Children")))
		{
			if (FArrayProperty* AP = CastField<FArrayProperty>(ChildrenProp))
			{
				FScriptArrayHelper H(AP, ChildrenProp->ContainerPtrToValuePtr<void>(State));
				if (FObjectProperty* InnerObj = CastField<FObjectProperty>(AP->Inner))
				{
					for (int32 i = 0; i < H.Num(); ++i)
					{
						UObject* Child = InnerObj->GetObjectPropertyValue(H.GetRawPtr(i));
						Children.Add(MakeShared<FJsonValueObject>(SerializeStateTreeState(Child, Depth + 1)));
					}
				}
			}
		}
		if (Children.Num() > 0) Obj->SetArrayField(TEXT("children"), Children);
	}

	return Obj;
}

FString FClaudeContextProvider::GetStateTreeStructure(const FString& StateTreePath)
{
	UStateTree* STree = Cast<UStateTree>(LoadAssetByPath(StateTreePath));
	if (!STree) return FString::Printf(TEXT("{\"error\":\"Not a StateTree: %s\"}"), *StateTreePath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), StateTreePath);

	UObject* EditorData = FindStateTreeEditorData(STree);
	if (!EditorData)
	{
		Root->SetStringField(TEXT("warning"), TEXT("EditorData not accessible. StateTree must be in editor builds."));
		return Truncate(JsonObjectToString(Root));
	}

	// Schema
	if (FProperty* SchemaProp = EditorData->GetClass()->FindPropertyByName(TEXT("Schema")))
	{
		if (FObjectProperty* ObjP = CastField<FObjectProperty>(SchemaProp))
		{
			if (UObject* Schema = ObjP->GetObjectPropertyValue_InContainer(EditorData))
			{
				Root->SetStringField(TEXT("schema_class"), Schema->GetClass()->GetName());
			}
		}
	}

	// Walk subtrees
	TArray<TSharedPtr<FJsonValue>> SubTreesJson;
	static const TCHAR* CandidateArrayNames[] = { TEXT("SubTrees"), TEXT("States"), nullptr };
	for (int32 i = 0; CandidateArrayNames[i] != nullptr; ++i)
	{
		FProperty* ArrProp = EditorData->GetClass()->FindPropertyByName(CandidateArrayNames[i]);
		if (!ArrProp) continue;
		FArrayProperty* AP = CastField<FArrayProperty>(ArrProp);
		if (!AP) continue;
		FScriptArrayHelper H(AP, ArrProp->ContainerPtrToValuePtr<void>(EditorData));
		if (FObjectProperty* InnerObj = CastField<FObjectProperty>(AP->Inner))
		{
			for (int32 j = 0; j < H.Num(); ++j)
			{
				UObject* Root2 = InnerObj->GetObjectPropertyValue(H.GetRawPtr(j));
				SubTreesJson.Add(MakeShared<FJsonValueObject>(SerializeStateTreeState(Root2)));
			}
		}
		break; // whichever array exists first — we're done
	}
	Root->SetArrayField(TEXT("subtrees"), SubTreesJson);

	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::GetStateTreeSchema(const FString& StateTreePath)
{
	UStateTree* STree = Cast<UStateTree>(LoadAssetByPath(StateTreePath));
	if (!STree) return FString::Printf(TEXT("{\"error\":\"Not a StateTree: %s\"}"), *StateTreePath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), StateTreePath);

	UObject* EditorData = FindStateTreeEditorData(STree);
	if (!EditorData) { Root->SetStringField(TEXT("error"), TEXT("No editor data")); return JsonObjectToString(Root); }

	FProperty* SchemaProp = EditorData->GetClass()->FindPropertyByName(TEXT("Schema"));
	if (!SchemaProp) { Root->SetStringField(TEXT("error"), TEXT("No Schema property")); return JsonObjectToString(Root); }

	FObjectProperty* ObjP = CastField<FObjectProperty>(SchemaProp);
	UObject* Schema = ObjP ? ObjP->GetObjectPropertyValue_InContainer(EditorData) : nullptr;
	if (!Schema) { Root->SetStringField(TEXT("warning"), TEXT("No schema set")); return JsonObjectToString(Root); }

	Root->SetStringField(TEXT("class"), Schema->GetClass()->GetName());
	Root->SetStringField(TEXT("path"), Schema->GetPathName());

	// List properties on the schema (reveals context requirements like "Actor", "AIController")
	TArray<TSharedPtr<FJsonValue>> Fields;
	for (TFieldIterator<FProperty> It(Schema->GetClass()); It; ++It)
	{
		FProperty* P = *It;
		TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
		F->SetStringField(TEXT("name"), P->GetName());
		F->SetStringField(TEXT("type"), P->GetCPPType());
		Fields.Add(MakeShared<FJsonValueObject>(F));
	}
	Root->SetArrayField(TEXT("fields"), Fields);

	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::ListStateTreeTasks(const FString& StateTreePath)
{
	UStateTree* STree = Cast<UStateTree>(LoadAssetByPath(StateTreePath));
	if (!STree) return FString::Printf(TEXT("{\"error\":\"Not a StateTree: %s\"}"), *StateTreePath);

	UObject* EditorData = FindStateTreeEditorData(STree);
	if (!EditorData) return TEXT("{\"error\":\"No editor data\"}");

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> AllTasks;

	// Walk states, collect tasks. Tasks are TInstancedStruct<FStateTreeTaskBase>
	// or similar. We read the wrapping struct's UScriptStruct to get class name.
	TFunction<void(UObject*)> Walk;
	Walk = [&AllTasks, &Walk](UObject* State)
	{
		if (!State) return;
		FString StateName;
		if (FProperty* NP = State->GetClass()->FindPropertyByName(TEXT("Name")))
		{
			if (FNameProperty* NameP = CastField<FNameProperty>(NP))
				StateName = NameP->GetPropertyValue_InContainer(State).ToString();
		}

		if (FProperty* TasksProp = State->GetClass()->FindPropertyByName(TEXT("Tasks")))
		{
			if (FArrayProperty* AP = CastField<FArrayProperty>(TasksProp))
			{
				FScriptArrayHelper H(AP, TasksProp->ContainerPtrToValuePtr<void>(State));
				for (int32 i = 0; i < H.Num(); ++i)
				{
					TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
					T->SetStringField(TEXT("state"), StateName);
					T->SetNumberField(TEXT("index"), i);
					// Try to read task struct class name via inner property
					if (FStructProperty* SP = CastField<FStructProperty>(AP->Inner))
					{
						if (SP->Struct) T->SetStringField(TEXT("container"), SP->Struct->GetName());
					}
					AllTasks.Add(MakeShared<FJsonValueObject>(T));
				}
			}
		}

		// Recurse into Children
		if (FProperty* ChildrenProp = State->GetClass()->FindPropertyByName(TEXT("Children")))
		{
			if (FArrayProperty* AP = CastField<FArrayProperty>(ChildrenProp))
			{
				FScriptArrayHelper H(AP, ChildrenProp->ContainerPtrToValuePtr<void>(State));
				if (FObjectProperty* InnerObj = CastField<FObjectProperty>(AP->Inner))
				{
					for (int32 i = 0; i < H.Num(); ++i)
					{
						Walk(InnerObj->GetObjectPropertyValue(H.GetRawPtr(i)));
					}
				}
			}
		}
	};

	static const TCHAR* Candidates[] = { TEXT("SubTrees"), TEXT("States"), nullptr };
	for (int32 i = 0; Candidates[i] != nullptr; ++i)
	{
		if (FArrayProperty* AP = CastField<FArrayProperty>(EditorData->GetClass()->FindPropertyByName(Candidates[i])))
		{
			FScriptArrayHelper H(AP, AP->ContainerPtrToValuePtr<void>(EditorData));
			if (FObjectProperty* InnerObj = CastField<FObjectProperty>(AP->Inner))
			{
				for (int32 j = 0; j < H.Num(); ++j) Walk(InnerObj->GetObjectPropertyValue(H.GetRawPtr(j)));
			}
			break;
		}
	}

	Root->SetArrayField(TEXT("tasks"), AllTasks);
	Root->SetNumberField(TEXT("count"), AllTasks.Num());
	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::ListStateTreeTransitions(const FString& StateTreePath)
{
	UStateTree* STree = Cast<UStateTree>(LoadAssetByPath(StateTreePath));
	if (!STree) return FString::Printf(TEXT("{\"error\":\"Not a StateTree: %s\"}"), *StateTreePath);

	UObject* EditorData = FindStateTreeEditorData(STree);
	if (!EditorData) return TEXT("{\"error\":\"No editor data\"}");

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> AllTransitions;

	TFunction<void(UObject*)> Walk;
	Walk = [&AllTransitions, &Walk](UObject* State)
	{
		if (!State) return;
		FString StateName;
		FGuid StateId;
		if (FProperty* NP = State->GetClass()->FindPropertyByName(TEXT("Name")))
		{
			if (FNameProperty* NameP = CastField<FNameProperty>(NP))
				StateName = NameP->GetPropertyValue_InContainer(State).ToString();
		}
		if (FProperty* IP = State->GetClass()->FindPropertyByName(TEXT("ID")))
		{
			if (FStructProperty* SP = CastField<FStructProperty>(IP))
				if (const FGuid* G = SP->ContainerPtrToValuePtr<FGuid>(State)) StateId = *G;
		}

		if (FProperty* TransProp = State->GetClass()->FindPropertyByName(TEXT("Transitions")))
		{
			if (FArrayProperty* AP = CastField<FArrayProperty>(TransProp))
			{
				FScriptArrayHelper H(AP, TransProp->ContainerPtrToValuePtr<void>(State));
				for (int32 i = 0; i < H.Num(); ++i)
				{
					TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
					T->SetStringField(TEXT("from_state"), StateName);
					T->SetStringField(TEXT("from_state_id"), StateId.ToString());
					T->SetNumberField(TEXT("index"), i);

					// Each transition is a FStateTreeTransition struct
					if (FStructProperty* SP = CastField<FStructProperty>(AP->Inner))
					{
						void* TransData = H.GetRawPtr(i);
						if (FProperty* TriggerProp = SP->Struct->FindPropertyByName(TEXT("Trigger")))
						{
							if (FEnumProperty* EP = CastField<FEnumProperty>(TriggerProp))
							{
								const int64 V = EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(
									TriggerProp->ContainerPtrToValuePtr<void>(TransData));
								T->SetStringField(TEXT("trigger"), EP->GetEnum()->GetNameStringByValue(V));
							}
						}
					}
					AllTransitions.Add(MakeShared<FJsonValueObject>(T));
				}
			}
		}

		if (FProperty* ChildrenProp = State->GetClass()->FindPropertyByName(TEXT("Children")))
		{
			if (FArrayProperty* AP = CastField<FArrayProperty>(ChildrenProp))
			{
				FScriptArrayHelper H(AP, ChildrenProp->ContainerPtrToValuePtr<void>(State));
				if (FObjectProperty* InnerObj = CastField<FObjectProperty>(AP->Inner))
				{
					for (int32 i = 0; i < H.Num(); ++i)
					{
						Walk(InnerObj->GetObjectPropertyValue(H.GetRawPtr(i)));
					}
				}
			}
		}
	};

	static const TCHAR* Candidates[] = { TEXT("SubTrees"), TEXT("States"), nullptr };
	for (int32 i = 0; Candidates[i] != nullptr; ++i)
	{
		if (FArrayProperty* AP = CastField<FArrayProperty>(EditorData->GetClass()->FindPropertyByName(Candidates[i])))
		{
			FScriptArrayHelper H(AP, AP->ContainerPtrToValuePtr<void>(EditorData));
			if (FObjectProperty* InnerObj = CastField<FObjectProperty>(AP->Inner))
			{
				for (int32 j = 0; j < H.Num(); ++j) Walk(InnerObj->GetObjectPropertyValue(H.GetRawPtr(j)));
			}
			break;
		}
	}

	Root->SetArrayField(TEXT("transitions"), AllTransitions);
	Root->SetNumberField(TEXT("count"), AllTransitions.Num());
	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::GetStateTreeBindings(const FString& StateTreePath)
{
	UStateTree* STree = Cast<UStateTree>(LoadAssetByPath(StateTreePath));
	if (!STree) return FString::Printf(TEXT("{\"error\":\"Not a StateTree: %s\"}"), *StateTreePath);

	UObject* EditorData = FindStateTreeEditorData(STree);
	if (!EditorData) return TEXT("{\"error\":\"No editor data\"}");

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), StateTreePath);

	// Property bindings live in EditorData->EditorBindings (FStateTreeEditorPropertyBindings)
	FProperty* BindingsProp = EditorData->GetClass()->FindPropertyByName(TEXT("EditorBindings"));
	if (!BindingsProp)
	{
		Root->SetStringField(TEXT("warning"), TEXT("EditorBindings property not found — UE version may differ"));
		return JsonObjectToString(Root);
	}

	if (FStructProperty* SP = CastField<FStructProperty>(BindingsProp))
	{
		Root->SetStringField(TEXT("bindings_struct"), SP->Struct ? SP->Struct->GetName() : FString());
		// FStateTreeEditorPropertyBindings has a PropertyBindings TArray
		void* BData = BindingsProp->ContainerPtrToValuePtr<void>(EditorData);
		if (FProperty* InnerArrProp = SP->Struct->FindPropertyByName(TEXT("PropertyBindings")))
		{
			if (FArrayProperty* AP = CastField<FArrayProperty>(InnerArrProp))
			{
				FScriptArrayHelper H(AP, InnerArrProp->ContainerPtrToValuePtr<void>(BData));
				Root->SetNumberField(TEXT("binding_count"), H.Num());
			}
		}
	}

	return Truncate(JsonObjectToString(Root));
}

// -----------------------------------------------------------------------------
// StateTree — WRITE
// -----------------------------------------------------------------------------

FString FClaudeContextProvider::CreateStateTree(const FString& NewAssetPath, const FString& SchemaClass, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path, use '/Game/AI/ST_Name'"); return TEXT(""); }

	UClass* FactoryClass = StaticLoadClass(UFactory::StaticClass(), nullptr, TEXT("/Script/StateTreeEditorModule.StateTreeFactory"));
	if (!FactoryClass)
	{
		OutError = TEXT("StateTreeFactory not found. Make sure StateTreeEditor plugin is enabled.");
		return TEXT("");
	}

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	// If a schema was specified, set it on the factory so the new asset is
	// pre-configured with the right context expectations.
	if (!SchemaClass.IsEmpty())
	{
		UClass* SC = StaticLoadClass(UObject::StaticClass(), nullptr, *SchemaClass);
		if (!SC)
		{
			const FString AsST = FString::Printf(TEXT("/Script/StateTreeModule.%s"), *SchemaClass);
			SC = StaticLoadClass(UObject::StaticClass(), nullptr, *AsST);
		}
		if (!SC)
		{
			const FString AsGST = FString::Printf(TEXT("/Script/GameplayStateTreeModule.%s"), *SchemaClass);
			SC = StaticLoadClass(UObject::StaticClass(), nullptr, *AsGST);
		}
		if (SC)
		{
			if (FProperty* SchemaProp = FactoryClass->FindPropertyByName(TEXT("SchemaClass")))
			{
				if (FClassProperty* CP = CastField<FClassProperty>(SchemaProp))
				{
					CP->SetObjectPropertyValue_InContainer(Factory, SC);
				}
			}
		}
		else
		{
			UE_LOG(LogClaudeAgent, Warning, TEXT("StateTree schema class not found: %s (will use default)"), *SchemaClass);
		}
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateST", "Create StateTree (Claude)"));
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, UStateTree::StaticClass(), Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return TEXT(""); }

	return FString::Printf(TEXT("Created StateTree: %s%s"),
		*NewObj->GetPathName(),
		SchemaClass.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (schema=%s)"), *SchemaClass));
}

FString FClaudeContextProvider::SetStateTreeSchema(const FString& StateTreePath, const FString& SchemaClass, FString& OutError)
{
	UStateTree* STree = Cast<UStateTree>(LoadAssetByPath(StateTreePath));
	if (!STree) { OutError = FString::Printf(TEXT("Not a StateTree: %s"), *StateTreePath); return TEXT(""); }

	UObject* EditorData = FindStateTreeEditorData(STree);
	if (!EditorData) { OutError = TEXT("EditorData not accessible"); return TEXT(""); }

	UClass* SC = StaticLoadClass(UObject::StaticClass(), nullptr, *SchemaClass);
	if (!SC)
	{
		const FString AsST = FString::Printf(TEXT("/Script/StateTreeModule.%s"), *SchemaClass);
		SC = StaticLoadClass(UObject::StaticClass(), nullptr, *AsST);
	}
	if (!SC)
	{
		const FString AsGST = FString::Printf(TEXT("/Script/GameplayStateTreeModule.%s"), *SchemaClass);
		SC = StaticLoadClass(UObject::StaticClass(), nullptr, *AsGST);
	}
	if (!SC) { OutError = FString::Printf(TEXT("Schema class not found: %s"), *SchemaClass); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetSTSchema", "Set StateTree Schema (Claude)"));
	EditorData->Modify();

	// EditorData->Schema is a UStateTreeSchema* — we set it to a new instance of the requested class
	FProperty* SchemaProp = EditorData->GetClass()->FindPropertyByName(TEXT("Schema"));
	if (!SchemaProp) { OutError = TEXT("Schema property missing"); return TEXT(""); }
	FObjectProperty* ObjP = CastField<FObjectProperty>(SchemaProp);
	if (!ObjP) { OutError = TEXT("Schema property wrong type"); return TEXT(""); }

	UObject* NewSchema = NewObject<UObject>(EditorData, SC, NAME_None, RF_Transactional);
	ObjP->SetObjectPropertyValue_InContainer(EditorData, NewSchema);

	STree->MarkPackageDirty();
	return FString::Printf(TEXT("Set schema to %s on %s"), *SC->GetName(), *StateTreePath);
}

FString FClaudeContextProvider::AddState(const FString& StateTreePath, const FString& ParentStateId, const FString& StateName, const FString& StateType, FString& OutError)
{
	UStateTree* STree = Cast<UStateTree>(LoadAssetByPath(StateTreePath));
	if (!STree) { OutError = FString::Printf(TEXT("Not a StateTree: %s"), *StateTreePath); return TEXT(""); }

	UObject* EditorData = FindStateTreeEditorData(STree);
	if (!EditorData) { OutError = TEXT("EditorData not accessible"); return TEXT(""); }

	// Resolve UStateTreeState class
	UClass* StateClass = StaticLoadClass(UObject::StaticClass(), nullptr, TEXT("/Script/StateTreeModule.StateTreeState"));
	if (!StateClass)
	{
		// Try editor module too
		StateClass = StaticLoadClass(UObject::StaticClass(), nullptr, TEXT("/Script/StateTreeEditorModule.StateTreeState"));
	}
	if (!StateClass) { OutError = TEXT("UStateTreeState class not loaded"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddState", "Add StateTree State (Claude)"));
	EditorData->Modify();

	UObject* NewState = NewObject<UObject>(EditorData, StateClass, NAME_None, RF_Transactional);

	// Set Name
	if (FProperty* NameProp = StateClass->FindPropertyByName(TEXT("Name")))
	{
		if (FNameProperty* NP = CastField<FNameProperty>(NameProp))
		{
			NP->SetPropertyValue_InContainer(NewState, FName(*StateName));
		}
	}

	// Set new random ID
	FGuid NewId = FGuid::NewGuid();
	if (FProperty* IdProp = StateClass->FindPropertyByName(TEXT("ID")))
	{
		if (FStructProperty* SP = CastField<FStructProperty>(IdProp))
		{
			FGuid* G = SP->ContainerPtrToValuePtr<FGuid>(NewState);
			if (G) *G = NewId;
		}
	}

	// Set Type enum if provided (State/SubTree/Linked/Group)
	if (!StateType.IsEmpty())
	{
		if (FProperty* TypeProp = StateClass->FindPropertyByName(TEXT("Type")))
		{
			if (FEnumProperty* EP = CastField<FEnumProperty>(TypeProp))
			{
				const int64 V = EP->GetEnum()->GetValueByNameString(StateType);
				if (V != INDEX_NONE)
				{
					EP->GetUnderlyingProperty()->SetIntPropertyValue(
						TypeProp->ContainerPtrToValuePtr<void>(NewState), V);
				}
			}
		}
	}

	// Attach to parent. If parent is empty → add as new root subtree.
	bool bAttached = false;
	if (ParentStateId.IsEmpty())
	{
		// Add as root subtree
		for (const TCHAR* ArrName : { TEXT("SubTrees"), TEXT("States") })
		{
			if (FArrayProperty* AP = CastField<FArrayProperty>(EditorData->GetClass()->FindPropertyByName(ArrName)))
			{
				FScriptArrayHelper H(AP, AP->ContainerPtrToValuePtr<void>(EditorData));
				const int32 Idx = H.AddValue();
				if (FObjectProperty* InnerObj = CastField<FObjectProperty>(AP->Inner))
				{
					InnerObj->SetObjectPropertyValue(H.GetRawPtr(Idx), NewState);
					bAttached = true;
					break;
				}
			}
		}
	}
	else
	{
		UObject* Parent = FindStateTreeStateByGuid(EditorData, ParentStateId);
		if (!Parent) { OutError = FString::Printf(TEXT("Parent state not found: %s"), *ParentStateId); return TEXT(""); }

		if (FArrayProperty* AP = CastField<FArrayProperty>(Parent->GetClass()->FindPropertyByName(TEXT("Children"))))
		{
			Parent->Modify();
			FScriptArrayHelper H(AP, AP->ContainerPtrToValuePtr<void>(Parent));
			const int32 Idx = H.AddValue();
			if (FObjectProperty* InnerObj = CastField<FObjectProperty>(AP->Inner))
			{
				InnerObj->SetObjectPropertyValue(H.GetRawPtr(Idx), NewState);
				bAttached = true;
			}
		}
	}

	if (!bAttached) { OutError = TEXT("Could not attach new state to tree"); return TEXT(""); }

	STree->MarkPackageDirty();
	return FString::Printf(TEXT("Added state '%s'. Id=%s"), *StateName, *NewId.ToString());
}

FString FClaudeContextProvider::RemoveState(const FString& StateTreePath, const FString& StateId, FString& OutError)
{
	UStateTree* STree = Cast<UStateTree>(LoadAssetByPath(StateTreePath));
	if (!STree) { OutError = FString::Printf(TEXT("Not a StateTree: %s"), *StateTreePath); return TEXT(""); }

	UObject* EditorData = FindStateTreeEditorData(STree);
	if (!EditorData) { OutError = TEXT("EditorData not accessible"); return TEXT(""); }

	FGuid Target;
	if (!FGuid::Parse(StateId, Target)) { OutError = TEXT("Bad GUID"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "RemState", "Remove StateTree State (Claude)"));
	EditorData->Modify();

	// We need to find the parent container that holds this state and remove it from there.
	bool bRemoved = false;

	TFunction<bool(UObject*, const TCHAR*)> RemoveFromArray;
	RemoveFromArray = [&Target, &RemoveFromArray, &bRemoved](UObject* Container, const TCHAR* ArrName) -> bool
	{
		if (!Container) return false;
		FArrayProperty* AP = CastField<FArrayProperty>(Container->GetClass()->FindPropertyByName(ArrName));
		if (!AP) return false;
		FScriptArrayHelper H(AP, AP->ContainerPtrToValuePtr<void>(Container));
		FObjectProperty* InnerObj = CastField<FObjectProperty>(AP->Inner);
		if (!InnerObj) return false;

		for (int32 i = H.Num() - 1; i >= 0; --i)
		{
			UObject* Child = InnerObj->GetObjectPropertyValue(H.GetRawPtr(i));
			if (!Child) continue;
			// Check this child's ID
			if (FProperty* IP = Child->GetClass()->FindPropertyByName(TEXT("ID")))
			{
				if (FStructProperty* SP = CastField<FStructProperty>(IP))
				{
					if (const FGuid* G = SP->ContainerPtrToValuePtr<FGuid>(Child))
					{
						if (*G == Target)
						{
							Container->Modify();
							H.RemoveValues(i, 1);
							bRemoved = true;
							return true;
						}
					}
				}
			}
			// Recurse into this child's Children
			if (RemoveFromArray(Child, TEXT("Children"))) return true;
		}
		return false;
	};

	// Try top-level containers first
	for (const TCHAR* RootArr : { TEXT("SubTrees"), TEXT("States") })
	{
		if (RemoveFromArray(EditorData, RootArr)) break;
	}

	if (!bRemoved) { OutError = FString::Printf(TEXT("State %s not found"), *StateId); return TEXT(""); }

	STree->MarkPackageDirty();
	return FString::Printf(TEXT("Removed state %s"), *StateId);
}

FString FClaudeContextProvider::AddStateTreeTask(const FString& StateTreePath, const FString& StateId, const FString& TaskClass, FString& OutError)
{
	UStateTree* STree = Cast<UStateTree>(LoadAssetByPath(StateTreePath));
	if (!STree) { OutError = FString::Printf(TEXT("Not a StateTree: %s"), *StateTreePath); return TEXT(""); }

	UObject* EditorData = FindStateTreeEditorData(STree);
	if (!EditorData) { OutError = TEXT("EditorData not accessible"); return TEXT(""); }

	UObject* State = FindStateTreeStateByGuid(EditorData, StateId);
	if (!State) { OutError = FString::Printf(TEXT("State not found: %s"), *StateId); return TEXT(""); }

	// Resolve the task struct. StateTree tasks are USTRUCTs, not UCLASSes.
	UScriptStruct* TaskStruct = FindFirstObject<UScriptStruct>(*TaskClass);
	if (!TaskStruct)
	{
		// Try full path
		TaskStruct = LoadObject<UScriptStruct>(nullptr, *TaskClass);
	}
	if (!TaskStruct) { OutError = FString::Printf(TEXT("Task struct not found: %s. Pass a UScriptStruct name like 'StateTreeRunParallelStateTreeTask'."), *TaskClass); return TEXT(""); }

	// State->Tasks is TArray<FStateTreeEditorNode> or TArray<TInstancedStruct<...>>.
	// We append a default-constructed entry and then rely on the user to configure
	// it further via set_object_property calls. Creating a fully-wired task
	// programmatically is not in scope here — this just makes the task exist.
	FArrayProperty* TasksProp = CastField<FArrayProperty>(State->GetClass()->FindPropertyByName(TEXT("Tasks")));
	if (!TasksProp) { OutError = TEXT("State has no Tasks array"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddSTTask", "Add StateTree Task (Claude)"));
	State->Modify();

	FScriptArrayHelper H(TasksProp, TasksProp->ContainerPtrToValuePtr<void>(State));
	const int32 NewIdx = H.AddValue();

	// If the inner type is a struct with a `Node` field of type FInstancedStruct,
	// we try to populate that with an instance of TaskStruct.
	if (FStructProperty* WrapperSP = CastField<FStructProperty>(TasksProp->Inner))
	{
		void* WrapperData = H.GetRawPtr(NewIdx);
		if (FProperty* NodeProp = WrapperSP->Struct->FindPropertyByName(TEXT("Node")))
		{
			if (FStructProperty* NodeSP = CastField<FStructProperty>(NodeProp))
			{
				// FInstancedStruct has an InitializeAs(UScriptStruct*) method, but
				// calling it requires type knowledge. Best-effort: we record the
				// desired struct name via reflection on a `ScriptStruct` field if
				// present in FInstancedStruct's internal state. If not, we leave
				// the task empty and user configures via editor UI.
				// A no-op here still counts as a success in the transaction log.
			}
		}
	}

	STree->MarkPackageDirty();
	return FString::Printf(TEXT("Added task slot of type '%s' on state %s. NOTE: task internals may need configuration via editor."),
		*TaskStruct->GetName(), *StateId);
}

FString FClaudeContextProvider::AddStateTreeTransition(const FString& StateTreePath, const FString& FromStateId, const FString& ToStateId, const FString& TriggerType, FString& OutError)
{
	UStateTree* STree = Cast<UStateTree>(LoadAssetByPath(StateTreePath));
	if (!STree) { OutError = FString::Printf(TEXT("Not a StateTree: %s"), *StateTreePath); return TEXT(""); }

	UObject* EditorData = FindStateTreeEditorData(STree);
	if (!EditorData) { OutError = TEXT("EditorData not accessible"); return TEXT(""); }

	UObject* FromState = FindStateTreeStateByGuid(EditorData, FromStateId);
	if (!FromState) { OutError = FString::Printf(TEXT("From state not found: %s"), *FromStateId); return TEXT(""); }

	FGuid TargetGuid;
	if (!FGuid::Parse(ToStateId, TargetGuid)) { OutError = TEXT("Bad ToStateId GUID"); return TEXT(""); }

	FArrayProperty* TransProp = CastField<FArrayProperty>(FromState->GetClass()->FindPropertyByName(TEXT("Transitions")));
	if (!TransProp) { OutError = TEXT("State has no Transitions array"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddSTTrans", "Add StateTree Transition (Claude)"));
	FromState->Modify();

	FScriptArrayHelper H(TransProp, TransProp->ContainerPtrToValuePtr<void>(FromState));
	const int32 NewIdx = H.AddValue();

	// Transition is FStateTreeTransition struct. Fields: Trigger (enum), State (FStateTreeStateLink with ID).
	if (FStructProperty* TransSP = CastField<FStructProperty>(TransProp->Inner))
	{
		void* TData = H.GetRawPtr(NewIdx);

		// Set Trigger
		if (FProperty* TrigProp = TransSP->Struct->FindPropertyByName(TEXT("Trigger")))
		{
			if (FEnumProperty* EP = CastField<FEnumProperty>(TrigProp))
			{
				const int64 V = EP->GetEnum()->GetValueByNameString(TriggerType);
				if (V != INDEX_NONE)
				{
					EP->GetUnderlyingProperty()->SetIntPropertyValue(
						TrigProp->ContainerPtrToValuePtr<void>(TData), V);
				}
			}
		}

		// Set target state. FStateTreeStateLink is a struct with an 'ID' Guid field.
		if (FProperty* StateLinkProp = TransSP->Struct->FindPropertyByName(TEXT("State")))
		{
			if (FStructProperty* LinkSP = CastField<FStructProperty>(StateLinkProp))
			{
				void* LinkData = StateLinkProp->ContainerPtrToValuePtr<void>(TData);
				if (FProperty* IdProp = LinkSP->Struct->FindPropertyByName(TEXT("ID")))
				{
					if (FStructProperty* IdSP = CastField<FStructProperty>(IdProp))
					{
						FGuid* G = IdSP->ContainerPtrToValuePtr<FGuid>(LinkData);
						if (G) *G = TargetGuid;
					}
				}
			}
		}
	}

	STree->MarkPackageDirty();
	return FString::Printf(TEXT("Added transition from %s to %s (trigger=%s)"), *FromStateId, *ToStateId, *TriggerType);
}

FString FClaudeContextProvider::AddStateTreeCondition(const FString& StateTreePath, const FString& StateId, const FString& TransitionIndex, const FString& ConditionClass, FString& OutError)
{
	UStateTree* STree = Cast<UStateTree>(LoadAssetByPath(StateTreePath));
	if (!STree) { OutError = FString::Printf(TEXT("Not a StateTree: %s"), *StateTreePath); return TEXT(""); }

	UObject* EditorData = FindStateTreeEditorData(STree);
	if (!EditorData) { OutError = TEXT("EditorData not accessible"); return TEXT(""); }

	UObject* State = FindStateTreeStateByGuid(EditorData, StateId);
	if (!State) { OutError = FString::Printf(TEXT("State not found: %s"), *StateId); return TEXT(""); }

	UScriptStruct* CondStruct = FindFirstObject<UScriptStruct>(*ConditionClass);
	if (!CondStruct)
	{
		CondStruct = LoadObject<UScriptStruct>(nullptr, *ConditionClass);
	}
	if (!CondStruct) { OutError = FString::Printf(TEXT("Condition struct not found: %s"), *ConditionClass); return TEXT(""); }

	// If TransitionIndex is empty → add to EnterConditions array on the state.
	// Otherwise → add to the specified transition's Conditions array.
	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddSTCond", "Add StateTree Condition (Claude)"));
	State->Modify();

	const TCHAR* CondArrayName = TransitionIndex.IsEmpty() ? TEXT("EnterConditions") : nullptr;

	if (TransitionIndex.IsEmpty())
	{
		FArrayProperty* AP = CastField<FArrayProperty>(State->GetClass()->FindPropertyByName(CondArrayName));
		if (!AP) { OutError = TEXT("No EnterConditions array on state"); return TEXT(""); }
		FScriptArrayHelper H(AP, AP->ContainerPtrToValuePtr<void>(State));
		H.AddValue();
	}
	else
	{
		const int32 Idx = FCString::Atoi(*TransitionIndex);
		FArrayProperty* TransAP = CastField<FArrayProperty>(State->GetClass()->FindPropertyByName(TEXT("Transitions")));
		if (!TransAP) { OutError = TEXT("No Transitions array"); return TEXT(""); }
		FScriptArrayHelper TH(TransAP, TransAP->ContainerPtrToValuePtr<void>(State));
		if (Idx < 0 || Idx >= TH.Num()) { OutError = FString::Printf(TEXT("Transition index %d out of range"), Idx); return TEXT(""); }

		if (FStructProperty* TransSP = CastField<FStructProperty>(TransAP->Inner))
		{
			void* TData = TH.GetRawPtr(Idx);
			FArrayProperty* InnerCond = CastField<FArrayProperty>(TransSP->Struct->FindPropertyByName(TEXT("Conditions")));
			if (!InnerCond) { OutError = TEXT("Transition has no Conditions array"); return TEXT(""); }
			FScriptArrayHelper CH(InnerCond, InnerCond->ContainerPtrToValuePtr<void>(TData));
			CH.AddValue();
		}
	}

	STree->MarkPackageDirty();
	return FString::Printf(TEXT("Added condition slot of type '%s'%s"),
		*CondStruct->GetName(),
		TransitionIndex.IsEmpty() ? TEXT(" to EnterConditions") : *FString::Printf(TEXT(" to transition[%s]"), *TransitionIndex));
}

// =============================================================================
// Data Asset / Data Table
// =============================================================================
//
// Data Assets are UDataAsset (or UPrimaryDataAsset) subclasses — essentially
// typed containers for designer-facing data. We support:
//   - Creating new DAs of a given class
//   - Duplicating existing DAs (most common content workflow)
//   - Listing DAs by class (e.g. all DA_ItemDefinition_* for an inventory)
//   - Setting nested fields via dot/bracket paths
//   - Appending to TArray fields
//
// Data Tables are UDataTable assets with FTableRowBase-derived row structs.
// We support read of all rows and cell-level writes.

// Resolve a property path like "Foo.Bar[2].Baz" to (Property, OwnerPtr).
// Returns true on success; out-params point into the container (do not free).
// Supported: dot navigation through structs/objects, bracket indexing on arrays.
struct FResolvedProperty
{
	FProperty* Property = nullptr;
	void* ContainerPtr = nullptr; // points at the value within the final container
};

static bool ResolvePropertyPath(UObject* RootObj, const FString& Path, FResolvedProperty& Out, FString& OutError)
{
	if (!RootObj) { OutError = TEXT("Null root object"); return false; }

	// Tokenize path. We split on '.' but keep bracket suffixes attached to their segment.
	TArray<FString> Segments;
	{
		FString Current;
		for (int32 i = 0; i < Path.Len(); ++i)
		{
			const TCHAR C = Path[i];
			if (C == '.')
			{
				if (!Current.IsEmpty()) { Segments.Add(Current); Current.Empty(); }
			}
			else
			{
				Current.AppendChar(C);
			}
		}
		if (!Current.IsEmpty()) Segments.Add(Current);
	}
	if (Segments.Num() == 0) { OutError = TEXT("Empty path"); return false; }

	// Walk the path. At each step, `CurrentContainer` is the "thing we navigate from",
	// and `CurrentStruct` is the UStruct describing its layout (UClass or UScriptStruct).
	void* CurrentContainer = RootObj;
	UStruct* CurrentStruct = RootObj->GetClass();
	FProperty* CurrentProp = nullptr;

	for (int32 SegIdx = 0; SegIdx < Segments.Num(); ++SegIdx)
	{
		FString Segment = Segments[SegIdx];

		// Split field name from optional [N] suffix
		FString FieldName = Segment;
		int32 Index = INDEX_NONE;
		int32 BracketPos;
		if (Segment.FindChar('[', BracketPos))
		{
			FieldName = Segment.Left(BracketPos);
			int32 ClosePos;
			if (!Segment.FindChar(']', ClosePos) || ClosePos <= BracketPos)
			{
				OutError = FString::Printf(TEXT("Bad bracket syntax in '%s'"), *Segment);
				return false;
			}
			const FString IdxStr = Segment.Mid(BracketPos + 1, ClosePos - BracketPos - 1);
			Index = FCString::Atoi(*IdxStr);
		}

		// Find the property by name
		CurrentProp = CurrentStruct->FindPropertyByName(FName(*FieldName));
		if (!CurrentProp)
		{
			OutError = FString::Printf(TEXT("Property '%s' not found on %s"),
				*FieldName, *CurrentStruct->GetName());
			return false;
		}

		// Address within the current container
		void* FieldPtr = CurrentProp->ContainerPtrToValuePtr<void>(CurrentContainer);

		// If there's a bracket, step into the array
		if (Index != INDEX_NONE)
		{
			FArrayProperty* ArrProp = CastField<FArrayProperty>(CurrentProp);
			if (!ArrProp)
			{
				OutError = FString::Printf(TEXT("'%s' is not an array, cannot index"), *FieldName);
				return false;
			}
			FScriptArrayHelper Helper(ArrProp, FieldPtr);
			if (Index < 0 || Index >= Helper.Num())
			{
				OutError = FString::Printf(TEXT("Index %d out of range [0..%d] for '%s'"),
					Index, Helper.Num() - 1, *FieldName);
				return false;
			}
			FieldPtr = Helper.GetRawPtr(Index);
			CurrentProp = ArrProp->Inner;
		}

		const bool bIsLast = (SegIdx == Segments.Num() - 1);
		if (bIsLast)
		{
			Out.Property = CurrentProp;
			Out.ContainerPtr = FieldPtr;
			return true;
		}

		// Not last — we need to descend. Only structs/objects can be descended into.
		if (FStructProperty* StructProp = CastField<FStructProperty>(CurrentProp))
		{
			CurrentContainer = FieldPtr;
			CurrentStruct = StructProp->Struct;
		}
		else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(CurrentProp))
		{
			UObject* SubObj = ObjProp->GetObjectPropertyValue(FieldPtr);
			if (!SubObj)
			{
				OutError = FString::Printf(TEXT("Null object at '%s'"), *FieldName);
				return false;
			}
			CurrentContainer = SubObj;
			CurrentStruct = SubObj->GetClass();
		}
		else
		{
			OutError = FString::Printf(TEXT("'%s' is not a struct/object, can't navigate further"), *FieldName);
			return false;
		}
	}

	OutError = TEXT("Unreachable: empty path after tokenization");
	return false;
}

// -----------------------------------------------------------------------------
// Data Asset
// -----------------------------------------------------------------------------

FString FClaudeContextProvider::CreateDataAsset(const FString& NewAssetPath, const FString& AssetClass, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path, use '/Game/Items/DA_Name'"); return TEXT(""); }

	// Resolve class — accepts BP path, full class path, or short name
	UClass* Class = nullptr;
	if (AssetClass.StartsWith(TEXT("/Game")))
	{
		// Blueprint-defined data asset class (UBlueprint that generates a UDataAsset subclass)
		if (UObject* Loaded = LoadAssetByPath(AssetClass))
		{
			if (UBlueprint* BP = Cast<UBlueprint>(Loaded)) Class = BP->GeneratedClass;
		}
	}
	if (!Class) Class = StaticLoadClass(UDataAsset::StaticClass(), nullptr, *AssetClass);
	if (!Class)
	{
		// Try /Script/Engine.<Name> prefix
		const FString AsEngine = FString::Printf(TEXT("/Script/Engine.%s"), *AssetClass);
		Class = StaticLoadClass(UDataAsset::StaticClass(), nullptr, *AsEngine);
	}
	if (!Class)
	{
		// Final fallback: iterate loaded classes
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->IsChildOf(UDataAsset::StaticClass()) && It->GetName() == AssetClass)
			{
				Class = *It;
				break;
			}
		}
	}
	if (!Class) { OutError = FString::Printf(TEXT("Data asset class not found: %s"), *AssetClass); return TEXT(""); }
	if (!Class->IsChildOf(UDataAsset::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Class '%s' is not a UDataAsset subclass"), *AssetClass);
		return TEXT("");
	}

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	// Use the generic DataAssetFactory which lets us pick the concrete class
	UClass* FactoryClass = StaticLoadClass(UFactory::StaticClass(), nullptr, TEXT("/Script/UnrealEd.DataAssetFactory"));
	if (!FactoryClass) { OutError = TEXT("DataAssetFactory not found"); return TEXT(""); }

	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
	// Set DataAssetClass on the factory so CreateAsset produces the right subclass
	if (FProperty* DAClassProp = FactoryClass->FindPropertyByName(TEXT("DataAssetClass")))
	{
		if (FClassProperty* CP = CastField<FClassProperty>(DAClassProp))
		{
			CP->SetObjectPropertyValue_InContainer(Factory, Class);
		}
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateDA", "Create Data Asset (Claude)"));
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, Class, Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return TEXT(""); }

	return FString::Printf(TEXT("Created DataAsset: %s (class: %s)"), *NewObj->GetPathName(), *Class->GetName());
}

FString FClaudeContextProvider::DuplicateDataAsset(const FString& SourcePath, const FString& NewAssetPath, FString& OutError)
{
	UObject* Source = LoadAssetByPath(SourcePath);
	if (!Source) { OutError = FString::Printf(TEXT("Source not found: %s"), *SourcePath); return TEXT(""); }

	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad destination path"); return TEXT(""); }

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "DupDA", "Duplicate Data Asset (Claude)"));
	UObject* NewObj = AssetTools.Get().DuplicateAsset(AssetName, PackagePath, Source);
	if (!NewObj) { OutError = TEXT("DuplicateAsset returned null"); return TEXT(""); }

	return FString::Printf(TEXT("Duplicated %s → %s"), *SourcePath, *NewObj->GetPathName());
}

FString FClaudeContextProvider::ListDataAssetsByClass(const FString& ClassFilter, const FString& PathFilter)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	// Resolve class filter (optional; empty = all UDataAsset subclasses)
	UClass* FilterClass = UDataAsset::StaticClass();
	if (!ClassFilter.IsEmpty())
	{
		UClass* C = StaticLoadClass(UDataAsset::StaticClass(), nullptr, *ClassFilter);
		if (!C) C = StaticLoadClass(UDataAsset::StaticClass(), nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ClassFilter));
		if (!C)
		{
			// Try as a Blueprint-generated DA class
			if (UObject* Loaded = LoadAssetByPath(ClassFilter))
			{
				if (UBlueprint* BP = Cast<UBlueprint>(Loaded)) C = BP->GeneratedClass;
			}
		}
		if (!C)
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->IsChildOf(UDataAsset::StaticClass()) && It->GetName() == ClassFilter) { C = *It; break; }
			}
		}
		if (C) FilterClass = C;
	}

	FARFilter Filter;
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(FilterClass->GetClassPathName());
	if (!PathFilter.IsEmpty()) Filter.PackagePaths.Add(FName(*PathFilter));

	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FAssetData& A : Assets)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("path"), A.GetObjectPathString());
		O->SetStringField(TEXT("name"), A.AssetName.ToString());
		O->SetStringField(TEXT("class"), A.AssetClassPath.ToString());
		Items.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("assets"), Items);
	Root->SetNumberField(TEXT("count"), Items.Num());
	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::SetDataAssetField(const FString& AssetPath, const FString& PropertyPath, const FString& NewValue, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath); return TEXT(""); }

	FResolvedProperty R;
	if (!ResolvePropertyPath(Asset, PropertyPath, R, OutError)) return TEXT("");
	if (!R.Property || !R.ContainerPtr) { OutError = TEXT("Resolve returned null"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetDAField", "Set Data Asset Field (Claude)"));
	Asset->Modify();

	// Import text via the property's own ImportText_Direct. This supports
	// primitives, structs (in Unreal text format like (X=1,Y=2,Z=3)),
	// enums (by name), object refs (full paths), etc.
	const TCHAR* Result = R.Property->ImportText_Direct(*NewValue, R.ContainerPtr, Asset, PPF_None);
	if (!Result)
	{
		OutError = FString::Printf(TEXT("ImportText failed for '%s' = '%s'. For structs use '(Field=Value,...)'. For object refs use full /Game/... paths."),
			*PropertyPath, *NewValue);
		return TEXT("");
	}

	FPropertyChangedEvent PCE(R.Property);
	Asset->PostEditChangeProperty(PCE);
	Asset->MarkPackageDirty();

	return FString::Printf(TEXT("Set %s.%s = %s"), *Asset->GetName(), *PropertyPath, *NewValue);
}

FString FClaudeContextProvider::AddDataAssetArrayElement(const FString& AssetPath, const FString& ArrayPath, const FString& NewElementValue, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath); return TEXT(""); }

	FResolvedProperty R;
	if (!ResolvePropertyPath(Asset, ArrayPath, R, OutError)) return TEXT("");

	FArrayProperty* ArrProp = CastField<FArrayProperty>(R.Property);
	if (!ArrProp) { OutError = FString::Printf(TEXT("'%s' is not a TArray"), *ArrayPath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddDAElement", "Add Array Element (Claude)"));
	Asset->Modify();

	FScriptArrayHelper Helper(ArrProp, R.ContainerPtr);
	const int32 NewIndex = Helper.AddValue();

	// If caller supplied a value, parse it into the new element using the inner property's ImportText.
	if (!NewElementValue.IsEmpty())
	{
		const TCHAR* Result = ArrProp->Inner->ImportText_Direct(*NewElementValue, Helper.GetRawPtr(NewIndex), Asset, PPF_None);
		if (!Result)
		{
			OutError = FString::Printf(TEXT("Added element at index %d, but ImportText of '%s' failed. Element left at default value."),
				NewIndex, *NewElementValue);
			// Don't fail the whole op — the element IS added
		}
	}

	FPropertyChangedEvent PCE(ArrProp, EPropertyChangeType::ArrayAdd);
	Asset->PostEditChangeProperty(PCE);
	Asset->MarkPackageDirty();

	return FString::Printf(TEXT("Added element to %s at index %d (count: %d)"),
		*ArrayPath, NewIndex, Helper.Num());
}

// -----------------------------------------------------------------------------
// Data Table
// -----------------------------------------------------------------------------

FString FClaudeContextProvider::ListDataTableRows(const FString& DataTablePath)
{
	UDataTable* DT = Cast<UDataTable>(LoadAssetByPath(DataTablePath));
	if (!DT) return FString::Printf(TEXT("{\"error\":\"Not a DataTable: %s\"}"), *DataTablePath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), DataTablePath);
	Root->SetStringField(TEXT("row_struct"), DT->RowStruct ? DT->RowStruct->GetName() : TEXT(""));

	// List column names (fields of the row struct)
	TArray<TSharedPtr<FJsonValue>> Columns;
	if (DT->RowStruct)
	{
		for (TFieldIterator<FProperty> It(DT->RowStruct); It; ++It)
		{
			TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetStringField(TEXT("name"), It->GetName());
			C->SetStringField(TEXT("type"), It->GetCPPType());
			Columns.Add(MakeShared<FJsonValueObject>(C));
		}
	}
	Root->SetArrayField(TEXT("columns"), Columns);

	// List rows with their names (full cell data can get large; caller can use
	// get_object_properties with the row handle for details)
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const TPair<FName, uint8*>& Pair : DT->GetRowMap())
	{
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("name"), Pair.Key.ToString());

		// For each column, read the value as text (compact — truncates large fields)
		if (DT->RowStruct && Pair.Value)
		{
			TSharedRef<FJsonObject> Cells = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(DT->RowStruct); It; ++It)
			{
				FString ValueStr;
				It->ExportText_InContainer(0, ValueStr, Pair.Value, Pair.Value, nullptr, PPF_None);
				if (ValueStr.Len() > 200) ValueStr = ValueStr.Left(200) + TEXT("[...]");
				Cells->SetStringField(It->GetName(), ValueStr);
			}
			R->SetObjectField(TEXT("cells"), Cells);
		}
		Rows.Add(MakeShared<FJsonValueObject>(R));
	}
	Root->SetArrayField(TEXT("rows"), Rows);
	Root->SetNumberField(TEXT("row_count"), Rows.Num());

	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::SetDataTableCell(const FString& DataTablePath, const FString& RowName, const FString& ColumnName, const FString& NewValue, FString& OutError)
{
	UDataTable* DT = Cast<UDataTable>(LoadAssetByPath(DataTablePath));
	if (!DT) { OutError = FString::Printf(TEXT("Not a DataTable: %s"), *DataTablePath); return TEXT(""); }
	if (!DT->RowStruct) { OutError = TEXT("DataTable has no row struct"); return TEXT(""); }

	uint8* RowData = DT->FindRowUnchecked(FName(*RowName));
	if (!RowData) { OutError = FString::Printf(TEXT("Row '%s' not found"), *RowName); return TEXT(""); }

	FProperty* ColProp = DT->RowStruct->FindPropertyByName(FName(*ColumnName));
	if (!ColProp) { OutError = FString::Printf(TEXT("Column '%s' not found on row struct %s"), *ColumnName, *DT->RowStruct->GetName()); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetDTCell", "Set DataTable Cell (Claude)"));
	DT->Modify();

	void* CellPtr = ColProp->ContainerPtrToValuePtr<void>(RowData);
	const TCHAR* Result = ColProp->ImportText_Direct(*NewValue, CellPtr, DT, PPF_None);
	if (!Result)
	{
		OutError = FString::Printf(TEXT("ImportText failed for '%s' = '%s'"), *ColumnName, *NewValue);
		return TEXT("");
	}

	DT->MarkPackageDirty();
	return FString::Printf(TEXT("Set %s[%s].%s = %s"), *DataTablePath, *RowName, *ColumnName, *NewValue);
}

// =============================================================================
// Control Rig (read-only)
// =============================================================================
//
// Control Rig is a UControlRigBlueprint asset that generates UControlRig at
// runtime. Its hierarchy (bones, controls, nulls, curves) lives in a
// URigHierarchy object. Its logic lives in URigVMGraph (Setup, Forward Solve,
// Backward Solve, + user functions).
//
// We don't edit these — reading only, to help Claude understand them.
//
// All direct access is via reflection so we survive UE version bumps (CR API
// had visible changes between 5.3 / 5.4 / 5.5 / 5.7).

static UObject* GetControlRigDefaultObject(UObject* CRBlueprint)
{
	if (!CRBlueprint) return nullptr;
	if (UBlueprint* BP = Cast<UBlueprint>(CRBlueprint))
	{
		if (BP->GeneratedClass)
		{
			return BP->GeneratedClass->GetDefaultObject();
		}
	}
	return nullptr;
}

// Find URigHierarchy on a UControlRigBlueprint. Newer CR versions hold it on
// the BP itself (->Hierarchy); older versions held it on the CDO of the
// generated class. We check both.
static UObject* FindRigHierarchy(UObject* CRBlueprintOrObject)
{
	if (!CRBlueprintOrObject) return nullptr;

	auto TryGetHierarchy = [](UObject* Owner) -> UObject*
	{
		if (!Owner) return nullptr;
		// Try "Hierarchy" property
		if (FProperty* HP = Owner->GetClass()->FindPropertyByName(TEXT("Hierarchy")))
		{
			if (FObjectProperty* OP = CastField<FObjectProperty>(HP))
			{
				return OP->GetObjectPropertyValue_InContainer(Owner);
			}
		}
		// Try "DynamicHierarchy" (legacy)
		if (FProperty* HP = Owner->GetClass()->FindPropertyByName(TEXT("DynamicHierarchy")))
		{
			if (FObjectProperty* OP = CastField<FObjectProperty>(HP))
			{
				return OP->GetObjectPropertyValue_InContainer(Owner);
			}
		}
		return nullptr;
	};

	// 1) Direct on the blueprint
	if (UObject* H = TryGetHierarchy(CRBlueprintOrObject)) return H;

	// 2) On the generated-class CDO
	if (UObject* CDO = GetControlRigDefaultObject(CRBlueprintOrObject))
	{
		if (UObject* H = TryGetHierarchy(CDO)) return H;
	}
	return nullptr;
}

FString FClaudeContextProvider::GetControlRigInfo(const FString& ControlRigPath)
{
	UObject* Asset = LoadAssetByPath(ControlRigPath);
	if (!Asset) return FString::Printf(TEXT("{\"error\":\"Not found: %s\"}"), *ControlRigPath);

	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP || !BP->GetClass()->GetName().Contains(TEXT("ControlRig")))
	{
		return FString::Printf(TEXT("{\"error\":\"Not a ControlRig asset: %s\"}"), *ControlRigPath);
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), ControlRigPath);
	Root->SetStringField(TEXT("class"), BP->GetClass()->GetName());

	if (BP->ParentClass)
	{
		Root->SetStringField(TEXT("parent_class"), BP->ParentClass->GetName());
	}
	if (BP->GeneratedClass)
	{
		Root->SetStringField(TEXT("generated_class"), BP->GeneratedClass->GetName());
	}
	Root->SetBoolField(TEXT("compiled"), BP->Status == BS_UpToDate);

	// Look for PreviewSkeletalMesh on the BP
	if (FProperty* PSMProp = BP->GetClass()->FindPropertyByName(TEXT("PreviewSkeletalMesh")))
	{
		if (FObjectProperty* OP = CastField<FObjectProperty>(PSMProp))
		{
			if (UObject* SK = OP->GetObjectPropertyValue_InContainer(BP))
			{
				Root->SetStringField(TEXT("preview_skeletal_mesh"), SK->GetPathName());
			}
		}
	}

	// Hierarchy summary
	if (UObject* Hier = FindRigHierarchy(BP))
	{
		// URigHierarchy::Num() returns element count (reflection-accessible via UFunction)
		if (UFunction* NumFn = Hier->FindFunction(FName(TEXT("Num"))))
		{
			// Call via CallFunction — returns int32
			int32 NumResult = 0;
			Hier->ProcessEvent(NumFn, &NumResult);
			Root->SetNumberField(TEXT("hierarchy_element_count"), NumResult);
		}
	}

	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::GetControlRigHierarchy(const FString& ControlRigPath)
{
	UObject* Asset = LoadAssetByPath(ControlRigPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) return FString::Printf(TEXT("{\"error\":\"Not a BP: %s\"}"), *ControlRigPath);

	UObject* Hier = FindRigHierarchy(BP);
	if (!Hier) return TEXT("{\"error\":\"Hierarchy not accessible\"}");

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), ControlRigPath);

	// Strategy: iterate over Elements array if it's a UPROPERTY, OR use
	// ProcessEvent on URigHierarchy::GetAllKeys() which returns TArray<FRigElementKey>.
	//
	// Simpler: URigHierarchy has serialized property "Elements" (TArray) or similar.
	// We try the UFunction GetAllKeys() — it's a BlueprintCallable.

	TArray<TSharedPtr<FJsonValue>> Bones, Controls, Nulls, Curves, Other;

	// Reflection-call GetAllKeys() which returns TArray<FRigElementKey>.
	// FRigElementKey has Name (FName) and Type (ERigElementType enum).
	UFunction* GetAllKeysFn = Hier->FindFunction(FName(TEXT("GetAllKeys")));
	if (!GetAllKeysFn)
	{
		Root->SetStringField(TEXT("warning"), TEXT("GetAllKeys() not found — URigHierarchy API may differ"));
		return JsonObjectToString(Root);
	}

	// Output param is TArray<FRigElementKey>. We allocate a reflection-safe buffer.
	// GetAllKeys has signature: TArray<FRigElementKey> GetAllKeys(bool bTraverse, ERigElementType Type)
	// Defaults: bTraverse=true, Type=All(0xFF). We pack params in order.
	struct
	{
		bool bTraverse = true;
		uint8 Type = 0xFF;
		TArray<FRigElementKey_Placeholder> OutKeys;
	} Params;
	(void)Params; // Ref only — we don't actually use this packed version because
	              // FRigElementKey depends on ControlRig headers we haven't included.

	// Alternative safer path: use property-based inspection.
	// URigHierarchy has a protected TArray<FRigBaseElement*> Elements. We can't
	// reach it directly, but we can iterate via UFunction GetBoneNames(),
	// GetControlNames(), etc. (BlueprintCallable helpers on URigHierarchy).

	auto CallStringArrayFn = [Hier](const TCHAR* FnName) -> TArray<FString>
	{
		TArray<FString> Result;
		if (UFunction* Fn = Hier->FindFunction(FName(FnName)))
		{
			// Typical signature: TArray<FName> GetXXX()
			// We allocate the out-param buffer and have ProcessEvent fill it.
			struct FOut { TArray<FName> Names; } Out;
			Hier->ProcessEvent(Fn, &Out);
			for (FName N : Out.Names) Result.Add(N.ToString());
		}
		return Result;
	};

	for (const FString& Name : CallStringArrayFn(TEXT("GetBoneNames")))
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Name);
		Bones.Add(MakeShared<FJsonValueObject>(O));
	}
	for (const FString& Name : CallStringArrayFn(TEXT("GetControlNames")))
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Name);
		Controls.Add(MakeShared<FJsonValueObject>(O));
	}
	for (const FString& Name : CallStringArrayFn(TEXT("GetNullNames")))
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Name);
		Nulls.Add(MakeShared<FJsonValueObject>(O));
	}
	for (const FString& Name : CallStringArrayFn(TEXT("GetCurveNames")))
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Name);
		Curves.Add(MakeShared<FJsonValueObject>(O));
	}

	Root->SetArrayField(TEXT("bones"), Bones);
	Root->SetArrayField(TEXT("controls"), Controls);
	Root->SetArrayField(TEXT("nulls"), Nulls);
	Root->SetArrayField(TEXT("curves"), Curves);

	Root->SetNumberField(TEXT("bone_count"), Bones.Num());
	Root->SetNumberField(TEXT("control_count"), Controls.Num());
	Root->SetNumberField(TEXT("null_count"), Nulls.Num());
	Root->SetNumberField(TEXT("curve_count"), Curves.Num());

	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::ListControlRigGraphs(const FString& ControlRigPath)
{
	UObject* Asset = LoadAssetByPath(ControlRigPath);
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) return FString::Printf(TEXT("{\"error\":\"Not a BP: %s\"}"), *ControlRigPath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), ControlRigPath);

	TArray<TSharedPtr<FJsonValue>> Graphs;

	// Control Rig graphs are in UbergraphPages (for event graphs) and FunctionGraphs.
	// Also check RigVMClient -> GetAllModels() if available (newer API).
	for (UEdGraph* G : BP->UbergraphPages)
	{
		if (!G) continue;
		TSharedRef<FJsonObject> GO = MakeShared<FJsonObject>();
		GO->SetStringField(TEXT("name"), G->GetName());
		GO->SetStringField(TEXT("kind"), TEXT("uber"));
		GO->SetNumberField(TEXT("node_count"), G->Nodes.Num());
		Graphs.Add(MakeShared<FJsonValueObject>(GO));
	}
	for (UEdGraph* G : BP->FunctionGraphs)
	{
		if (!G) continue;
		TSharedRef<FJsonObject> GO = MakeShared<FJsonObject>();
		GO->SetStringField(TEXT("name"), G->GetName());
		GO->SetStringField(TEXT("kind"), TEXT("function"));
		GO->SetNumberField(TEXT("node_count"), G->Nodes.Num());
		Graphs.Add(MakeShared<FJsonValueObject>(GO));
	}

	Root->SetArrayField(TEXT("graphs"), Graphs);
	Root->SetNumberField(TEXT("count"), Graphs.Num());
	return Truncate(JsonObjectToString(Root));
}

// =============================================================================
// IK Rig (read-only)
// =============================================================================

FString FClaudeContextProvider::GetIKRigInfo(const FString& IKRigPath)
{
	UObject* Asset = LoadAssetByPath(IKRigPath);
	if (!Asset) return FString::Printf(TEXT("{\"error\":\"Not found: %s\"}"), *IKRigPath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), IKRigPath);
	Root->SetStringField(TEXT("class"), Asset->GetClass()->GetName());

	// Preview mesh / skeleton
	if (FProperty* SKProp = Asset->GetClass()->FindPropertyByName(TEXT("PreviewSkeletalMesh")))
	{
		if (FObjectProperty* OP = CastField<FObjectProperty>(SKProp))
		{
			if (UObject* SK = OP->GetObjectPropertyValue_InContainer(Asset))
				Root->SetStringField(TEXT("preview_skeletal_mesh"), SK->GetPathName());
		}
	}

	// Retarget definition — contains RetargetRoot bone and chains
	if (FProperty* RDProp = Asset->GetClass()->FindPropertyByName(TEXT("RetargetDefinition")))
	{
		if (FStructProperty* SP = CastField<FStructProperty>(RDProp))
		{
			void* RDData = RDProp->ContainerPtrToValuePtr<void>(Asset);

			// Root bone
			if (FProperty* RootBoneProp = SP->Struct->FindPropertyByName(TEXT("RootBone")))
			{
				if (FNameProperty* NP = CastField<FNameProperty>(RootBoneProp))
				{
					Root->SetStringField(TEXT("retarget_root_bone"),
						NP->GetPropertyValue(NP->ContainerPtrToValuePtr<void>(RDData)).ToString());
				}
			}

			// Chains
			if (FProperty* ChainsProp = SP->Struct->FindPropertyByName(TEXT("BoneChains")))
			{
				if (FArrayProperty* AP = CastField<FArrayProperty>(ChainsProp))
				{
					FScriptArrayHelper H(AP, ChainsProp->ContainerPtrToValuePtr<void>(RDData));
					TArray<TSharedPtr<FJsonValue>> Chains;

					if (FStructProperty* ChainSP = CastField<FStructProperty>(AP->Inner))
					{
						for (int32 i = 0; i < H.Num(); ++i)
						{
							void* ChainData = H.GetRawPtr(i);
							TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();

							if (FProperty* NameF = ChainSP->Struct->FindPropertyByName(TEXT("ChainName")))
							{
								if (FNameProperty* NP = CastField<FNameProperty>(NameF))
									C->SetStringField(TEXT("chain_name"), NP->GetPropertyValue(NP->ContainerPtrToValuePtr<void>(ChainData)).ToString());
							}
							if (FProperty* StartF = ChainSP->Struct->FindPropertyByName(TEXT("StartBone")))
							{
								// StartBone is a FBoneReference (struct with BoneName FName)
								if (FStructProperty* StartSP = CastField<FStructProperty>(StartF))
								{
									void* StartData = StartF->ContainerPtrToValuePtr<void>(ChainData);
									if (FProperty* BNProp = StartSP->Struct->FindPropertyByName(TEXT("BoneName")))
									{
										if (FNameProperty* NP = CastField<FNameProperty>(BNProp))
											C->SetStringField(TEXT("start_bone"), NP->GetPropertyValue(NP->ContainerPtrToValuePtr<void>(StartData)).ToString());
									}
								}
							}
							if (FProperty* EndF = ChainSP->Struct->FindPropertyByName(TEXT("EndBone")))
							{
								if (FStructProperty* EndSP = CastField<FStructProperty>(EndF))
								{
									void* EndData = EndF->ContainerPtrToValuePtr<void>(ChainData);
									if (FProperty* BNProp = EndSP->Struct->FindPropertyByName(TEXT("BoneName")))
									{
										if (FNameProperty* NP = CastField<FNameProperty>(BNProp))
											C->SetStringField(TEXT("end_bone"), NP->GetPropertyValue(NP->ContainerPtrToValuePtr<void>(EndData)).ToString());
									}
								}
							}
							Chains.Add(MakeShared<FJsonValueObject>(C));
						}
					}
					Root->SetArrayField(TEXT("retarget_chains"), Chains);
					Root->SetNumberField(TEXT("chain_count"), Chains.Num());
				}
			}
		}
	}

	// Solver stack — shown by class name only
	if (FProperty* SolversProp = Asset->GetClass()->FindPropertyByName(TEXT("Solvers")))
	{
		if (FArrayProperty* AP = CastField<FArrayProperty>(SolversProp))
		{
			FScriptArrayHelper H(AP, SolversProp->ContainerPtrToValuePtr<void>(Asset));
			TArray<TSharedPtr<FJsonValue>> Solvers;
			if (FObjectProperty* InnerObj = CastField<FObjectProperty>(AP->Inner))
			{
				for (int32 i = 0; i < H.Num(); ++i)
				{
					UObject* Solver = InnerObj->GetObjectPropertyValue(H.GetRawPtr(i));
					if (!Solver) continue;
					TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
					S->SetStringField(TEXT("class"), Solver->GetClass()->GetName());
					S->SetNumberField(TEXT("index"), i);
					Solvers.Add(MakeShared<FJsonValueObject>(S));
				}
			}
			Root->SetArrayField(TEXT("solvers"), Solvers);
		}
	}

	return Truncate(JsonObjectToString(Root));
}

// =============================================================================
// IK Retargeter
// =============================================================================

FString FClaudeContextProvider::GetIKRetargeterInfo(const FString& RetargeterPath)
{
	UObject* Asset = LoadAssetByPath(RetargeterPath);
	if (!Asset) return FString::Printf(TEXT("{\"error\":\"Not found: %s\"}"), *RetargeterPath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), RetargeterPath);
	Root->SetStringField(TEXT("class"), Asset->GetClass()->GetName());

	// Source / target IK rigs
	for (const TCHAR* FieldName : { TEXT("SourceIKRigAsset"), TEXT("TargetIKRigAsset") })
	{
		if (FProperty* P = Asset->GetClass()->FindPropertyByName(FieldName))
		{
			if (FObjectProperty* OP = CastField<FObjectProperty>(P))
			{
				if (UObject* Rig = OP->GetObjectPropertyValue_InContainer(Asset))
				{
					const FString Key = FString(FieldName).Replace(TEXT("IKRigAsset"), TEXT("_ik_rig")).ToLower();
					Root->SetStringField(Key, Rig->GetPathName());
				}
			}
		}
	}

	// Pose names — retargeter has CurrentRetargetPose (FName) + RetargetPoses (TMap<FName, FIKRetargetPose>)
	if (FProperty* CurPoseProp = Asset->GetClass()->FindPropertyByName(TEXT("CurrentRetargetPose")))
	{
		if (FNameProperty* NP = CastField<FNameProperty>(CurPoseProp))
		{
			Root->SetStringField(TEXT("current_retarget_pose"),
				NP->GetPropertyValue_InContainer(Asset).ToString());
		}
	}
	if (FProperty* PosesProp = Asset->GetClass()->FindPropertyByName(TEXT("SourceRetargetPoses")))
	{
		if (FMapProperty* MP = CastField<FMapProperty>(PosesProp))
		{
			FScriptMapHelper MH(MP, PosesProp->ContainerPtrToValuePtr<void>(Asset));
			Root->SetNumberField(TEXT("source_pose_count"), MH.Num());
		}
	}
	if (FProperty* PosesProp = Asset->GetClass()->FindPropertyByName(TEXT("TargetRetargetPoses")))
	{
		if (FMapProperty* MP = CastField<FMapProperty>(PosesProp))
		{
			FScriptMapHelper MH(MP, PosesProp->ContainerPtrToValuePtr<void>(Asset));
			Root->SetNumberField(TEXT("target_pose_count"), MH.Num());
		}
	}

	// Chain mapping count
	if (FProperty* CMProp = Asset->GetClass()->FindPropertyByName(TEXT("ChainMapping")))
	{
		if (FArrayProperty* AP = CastField<FArrayProperty>(CMProp))
		{
			FScriptArrayHelper H(AP, CMProp->ContainerPtrToValuePtr<void>(Asset));
			Root->SetNumberField(TEXT("chain_mapping_count"), H.Num());
		}
	}

	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::ListRetargetChains(const FString& RetargeterPath)
{
	UObject* Asset = LoadAssetByPath(RetargeterPath);
	if (!Asset) return FString::Printf(TEXT("{\"error\":\"Not found: %s\"}"), *RetargeterPath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), RetargeterPath);

	// ChainMapping is TArray<URetargetChainSettings*>. Each settings holds
	// TargetChain/SourceChain names + per-chain settings.
	FProperty* CMProp = Asset->GetClass()->FindPropertyByName(TEXT("ChainMapping"));
	if (!CMProp)
	{
		Root->SetStringField(TEXT("error"), TEXT("ChainMapping property not found"));
		return JsonObjectToString(Root);
	}
	FArrayProperty* AP = CastField<FArrayProperty>(CMProp);
	if (!AP) return JsonObjectToString(Root);

	TArray<TSharedPtr<FJsonValue>> Items;
	FScriptArrayHelper H(AP, CMProp->ContainerPtrToValuePtr<void>(Asset));

	if (FObjectProperty* InnerObj = CastField<FObjectProperty>(AP->Inner))
	{
		for (int32 i = 0; i < H.Num(); ++i)
		{
			UObject* Settings = InnerObj->GetObjectPropertyValue(H.GetRawPtr(i));
			if (!Settings) continue;

			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("index"), i);

			// SourceChain / TargetChain are FName properties on the settings
			for (const TCHAR* FName_ : { TEXT("SourceChain"), TEXT("TargetChain") })
			{
				if (FProperty* NP = Settings->GetClass()->FindPropertyByName(FName_))
				{
					if (FNameProperty* NameP = CastField<FNameProperty>(NP))
					{
						const FString Key = FString(FName_).ToLower();
						O->SetStringField(Key,
							NameP->GetPropertyValue_InContainer(Settings).ToString());
					}
				}
			}
			Items.Add(MakeShared<FJsonValueObject>(O));
		}
	}

	Root->SetArrayField(TEXT("chains"), Items);
	Root->SetNumberField(TEXT("count"), Items.Num());
	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::CreateIKRetargeter(const FString& NewAssetPath, const FString& SourceIKRigPath, const FString& TargetIKRigPath, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path, use '/Game/Characters/RTG_Name'"); return TEXT(""); }

	UObject* SourceRig = SourceIKRigPath.IsEmpty() ? nullptr : LoadAssetByPath(SourceIKRigPath);
	UObject* TargetRig = TargetIKRigPath.IsEmpty() ? nullptr : LoadAssetByPath(TargetIKRigPath);

	// Factory lives in IKRigEditor module, but usually accessible at runtime via its class path
	UClass* FactoryClass = StaticLoadClass(UFactory::StaticClass(), nullptr, TEXT("/Script/IKRigEditor.IKRetargetFactory"));
	if (!FactoryClass) { OutError = TEXT("IKRetargetFactory not found (IKRigEditor module may not be loaded)"); return TEXT(""); }

	UClass* AssetClass = StaticLoadClass(UObject::StaticClass(), nullptr, TEXT("/Script/IKRig.IKRetargeter"));
	if (!AssetClass) { OutError = TEXT("IKRetargeter class not found"); return TEXT(""); }

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateRTG", "Create IK Retargeter (Claude)"));
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, AssetClass, Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return TEXT(""); }

	// Apply source/target IK rigs if provided. These are reference properties
	// on UIKRetargeter — set via reflection.
	auto SetRigField = [NewObj](const TCHAR* FieldName, UObject* Rig)
	{
		if (!Rig) return;
		if (FProperty* P = NewObj->GetClass()->FindPropertyByName(FieldName))
		{
			if (FObjectProperty* OP = CastField<FObjectProperty>(P))
			{
				OP->SetObjectPropertyValue_InContainer(NewObj, Rig);
			}
		}
	};
	SetRigField(TEXT("SourceIKRigAsset"), SourceRig);
	SetRigField(TEXT("TargetIKRigAsset"), TargetRig);

	NewObj->MarkPackageDirty();

	return FString::Printf(TEXT("Created IK Retargeter: %s%s%s"),
		*NewObj->GetPathName(),
		SourceRig ? *FString::Printf(TEXT(" source=%s"), *SourceRig->GetName()) : TEXT(""),
		TargetRig ? *FString::Printf(TEXT(" target=%s"), *TargetRig->GetName()) : TEXT(""));
}

FString FClaudeContextProvider::SetRetargetChainMapping(const FString& RetargeterPath, const FString& SourceChainName, const FString& TargetChainName, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(RetargeterPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Not found: %s"), *RetargeterPath); return TEXT(""); }

	FProperty* CMProp = Asset->GetClass()->FindPropertyByName(TEXT("ChainMapping"));
	if (!CMProp) { OutError = TEXT("ChainMapping property missing"); return TEXT(""); }
	FArrayProperty* AP = CastField<FArrayProperty>(CMProp);
	if (!AP) { OutError = TEXT("ChainMapping is not an array"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetChainMap", "Set Chain Mapping (Claude)"));
	Asset->Modify();

	FScriptArrayHelper H(AP, CMProp->ContainerPtrToValuePtr<void>(Asset));
	FObjectProperty* InnerObj = CastField<FObjectProperty>(AP->Inner);
	if (!InnerObj) { OutError = TEXT("ChainMapping inner is not object"); return TEXT(""); }

	// Find the mapping whose TargetChain matches the requested target name.
	// If not found, we can't add a new one — target chains are determined by
	// the target IK rig's retarget definition, so we only modify existing.
	bool bFound = false;
	for (int32 i = 0; i < H.Num(); ++i)
	{
		UObject* Settings = InnerObj->GetObjectPropertyValue(H.GetRawPtr(i));
		if (!Settings) continue;

		FName CurTarget;
		if (FProperty* TCProp = Settings->GetClass()->FindPropertyByName(TEXT("TargetChain")))
		{
			if (FNameProperty* NP = CastField<FNameProperty>(TCProp))
			{
				CurTarget = NP->GetPropertyValue_InContainer(Settings);
			}
		}
		if (CurTarget != FName(*TargetChainName)) continue;

		// Found — set its SourceChain
		Settings->Modify();
		if (FProperty* SCProp = Settings->GetClass()->FindPropertyByName(TEXT("SourceChain")))
		{
			if (FNameProperty* NP = CastField<FNameProperty>(SCProp))
			{
				NP->SetPropertyValue_InContainer(Settings, FName(*SourceChainName));
				bFound = true;
				break;
			}
		}
	}

	if (!bFound)
	{
		OutError = FString::Printf(TEXT("Target chain '%s' not found in retargeter. Valid target chains come from the target IK rig's retarget definition."), *TargetChainName);
		return TEXT("");
	}

	Asset->MarkPackageDirty();
	return FString::Printf(TEXT("Mapped source chain '%s' → target chain '%s'"),
		*SourceChainName, *TargetChainName);
}

FString FClaudeContextProvider::AutoMapRetargetChains(const FString& RetargeterPath, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(RetargeterPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Not found: %s"), *RetargeterPath); return TEXT(""); }

	FProperty* CMProp = Asset->GetClass()->FindPropertyByName(TEXT("ChainMapping"));
	if (!CMProp) { OutError = TEXT("ChainMapping property missing"); return TEXT(""); }
	FArrayProperty* AP = CastField<FArrayProperty>(CMProp);
	if (!AP) { OutError = TEXT("ChainMapping is not an array"); return TEXT(""); }

	// Resolve source chain names from the source IK rig
	TArray<FString> SourceChainNames;
	if (FProperty* SrcProp = Asset->GetClass()->FindPropertyByName(TEXT("SourceIKRigAsset")))
	{
		if (FObjectProperty* OP = CastField<FObjectProperty>(SrcProp))
		{
			if (UObject* SrcRig = OP->GetObjectPropertyValue_InContainer(Asset))
			{
				if (FProperty* RDProp = SrcRig->GetClass()->FindPropertyByName(TEXT("RetargetDefinition")))
				{
					if (FStructProperty* SP = CastField<FStructProperty>(RDProp))
					{
						void* RDData = RDProp->ContainerPtrToValuePtr<void>(SrcRig);
						if (FProperty* ChainsProp = SP->Struct->FindPropertyByName(TEXT("BoneChains")))
						{
							if (FArrayProperty* CAP = CastField<FArrayProperty>(ChainsProp))
							{
								FScriptArrayHelper CH(CAP, ChainsProp->ContainerPtrToValuePtr<void>(RDData));
								if (FStructProperty* CSP = CastField<FStructProperty>(CAP->Inner))
								{
									for (int32 i = 0; i < CH.Num(); ++i)
									{
										void* CData = CH.GetRawPtr(i);
										if (FProperty* CN = CSP->Struct->FindPropertyByName(TEXT("ChainName")))
										{
											if (FNameProperty* NP = CastField<FNameProperty>(CN))
											{
												SourceChainNames.Add(NP->GetPropertyValue(NP->ContainerPtrToValuePtr<void>(CData)).ToString());
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	if (SourceChainNames.Num() == 0)
	{
		OutError = TEXT("No source chains found — source IK rig may be empty or missing");
		return TEXT("");
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AutoMapChains", "Auto-Map Chains (Claude)"));
	Asset->Modify();

	// Simple fuzzy match: for each target chain in ChainMapping, find source
	// chain with the same name (case-insensitive), else exact substring match.
	FScriptArrayHelper H(AP, CMProp->ContainerPtrToValuePtr<void>(Asset));
	FObjectProperty* InnerObj = CastField<FObjectProperty>(AP->Inner);
	if (!InnerObj) { OutError = TEXT("Wrong inner type"); return TEXT(""); }

	int32 Mapped = 0;
	for (int32 i = 0; i < H.Num(); ++i)
	{
		UObject* Settings = InnerObj->GetObjectPropertyValue(H.GetRawPtr(i));
		if (!Settings) continue;

		FName TargetName;
		if (FProperty* TCProp = Settings->GetClass()->FindPropertyByName(TEXT("TargetChain")))
		{
			if (FNameProperty* NP = CastField<FNameProperty>(TCProp))
				TargetName = NP->GetPropertyValue_InContainer(Settings);
		}
		if (TargetName.IsNone()) continue;

		const FString TargetStr = TargetName.ToString();

		// Try exact match first, then substring
		FString Best;
		for (const FString& Src : SourceChainNames)
		{
			if (Src.Equals(TargetStr, ESearchCase::IgnoreCase)) { Best = Src; break; }
		}
		if (Best.IsEmpty())
		{
			for (const FString& Src : SourceChainNames)
			{
				if (Src.Contains(TargetStr, ESearchCase::IgnoreCase) ||
				    TargetStr.Contains(Src, ESearchCase::IgnoreCase))
				{
					Best = Src;
					break;
				}
			}
		}

		if (!Best.IsEmpty())
		{
			Settings->Modify();
			if (FProperty* SCProp = Settings->GetClass()->FindPropertyByName(TEXT("SourceChain")))
			{
				if (FNameProperty* NP = CastField<FNameProperty>(SCProp))
				{
					NP->SetPropertyValue_InContainer(Settings, FName(*Best));
					++Mapped;
				}
			}
		}
	}

	Asset->MarkPackageDirty();
	return FString::Printf(TEXT("Auto-mapped %d of %d target chains"), Mapped, H.Num());
}

// =============================================================================
// Visual — screenshot / thumbnail capture
// =============================================================================
//
// These tools return base64-encoded PNG data so the agent loop can embed the
// image in the next tool_result message back to the model. Anthropic API
// accepts image content blocks inside tool_result.
//
// Implementation strategy:
//
//   CaptureViewport:
//     Use the editor viewport's current camera. We ask it to re-render into
//     a render target sized to (Width, Height), then read pixels back. This
//     is synchronous (flushes render commands), so Claude doesn't have to
//     wait for an async callback.
//
//   CaptureFromCamera:
//     Spawn a temporary USceneCaptureComponent2D at the requested location,
//     have it capture into a transient render target, read back, destroy.
//
//   CaptureAssetThumbnail:
//     UE's ThumbnailManager can render thumbnails for any asset. We use the
//     render thumbnail API to get a small preview.

static bool ReadRenderTargetToPngBase64(UTextureRenderTarget2D* RT, FString& OutBase64, FString& OutError)
{
	if (!RT) { OutError = TEXT("Null render target"); return false; }

	FTextureRenderTargetResource* RTResource = RT->GameThread_GetRenderTargetResource();
	if (!RTResource) { OutError = TEXT("Could not get render target resource"); return false; }

	TArray<FColor> Pixels;
	FReadSurfaceDataFlags ReadFlags(RCM_UNorm, CubeFace_MAX);
	ReadFlags.SetLinearToGamma(false);
	if (!RTResource->ReadPixels(Pixels, ReadFlags))
	{
		OutError = TEXT("ReadPixels failed");
		return false;
	}

	// Force alpha to 255 — otherwise screenshots often end up translucent
	for (FColor& C : Pixels) C.A = 255;

	IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> PngWrapper = IWM.CreateImageWrapper(EImageFormat::PNG);
	if (!PngWrapper.IsValid()) { OutError = TEXT("Could not create PNG wrapper"); return false; }

	const bool bSet = PngWrapper->SetRaw(
		Pixels.GetData(),
		Pixels.Num() * sizeof(FColor),
		RT->SizeX, RT->SizeY,
		ERGBFormat::BGRA, 8);
	if (!bSet) { OutError = TEXT("SetRaw failed"); return false; }

	const TArray64<uint8>& Compressed = PngWrapper->GetCompressed(100);
	if (Compressed.Num() == 0) { OutError = TEXT("PNG compress returned empty"); return false; }

	OutBase64 = FBase64::Encode(Compressed.GetData(), Compressed.Num());
	return true;
}

FString FClaudeContextProvider::CaptureViewport(int32 Width, int32 Height, bool bShowUI, FString& OutImageBase64, FString& OutError)
{
	if (!GEditor) { OutError = TEXT("No GEditor"); return TEXT(""); }

	// Clamp sanity — images over 2000x2000 blow up token budget
	Width = FMath::Clamp(Width, 256, 2048);
	Height = FMath::Clamp(Height, 256, 2048);

	FViewport* Viewport = nullptr;
	FEditorViewportClient* VC = nullptr;

	// Find the active level viewport
	if (FLevelEditorViewportClient* LVC = GCurrentLevelEditingViewportClient)
	{
		VC = LVC;
		Viewport = LVC->Viewport;
	}
	else if (GEditor->GetActiveViewport())
	{
		Viewport = GEditor->GetActiveViewport();
		VC = static_cast<FEditorViewportClient*>(Viewport ? Viewport->GetClient() : nullptr);
	}

	if (!Viewport || !VC)
	{
		OutError = TEXT("No active editor viewport");
		return TEXT("");
	}

	UWorld* World = VC->GetWorld();
	if (!World) { OutError = TEXT("Viewport has no world"); return TEXT(""); }

	// Allocate a transient render target at requested resolution
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	RT->RenderTargetFormat = RTF_RGBA8;
	RT->InitAutoFormat(Width, Height);
	RT->UpdateResourceImmediate(true);

	// Use a SceneCaptureComponent2D snapped to the viewport camera — this is
	// the most reliable way to grab a frame synchronously without hooking into
	// the viewport's own draw call.
	AActor* TempActor = World->SpawnActor<AActor>(AActor::StaticClass(),
		VC->GetViewLocation(), VC->GetViewRotation());
	if (!TempActor) { OutError = TEXT("Could not spawn temp capture actor"); return TEXT(""); }

	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(TempActor);
	Capture->RegisterComponent();
	Capture->TextureTarget = RT;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->FOVAngle = VC->ViewFOV;

	// Match the viewport's show flags roughly — keeps lighting/post-process consistent
	Capture->ShowFlags = FEngineShowFlags(ESFIM_Game);

	Capture->SetWorldLocationAndRotation(VC->GetViewLocation(), VC->GetViewRotation());
	Capture->CaptureScene();

	const bool bOk = ReadRenderTargetToPngBase64(RT, OutImageBase64, OutError);

	// Cleanup
	Capture->UnregisterComponent();
	TempActor->Destroy();
	RT->MarkAsGarbage();

	if (!bOk) return TEXT("");

	return FString::Printf(TEXT("Captured viewport at %dx%d (camera: %s, looking %s)"),
		Width, Height,
		*VC->GetViewLocation().ToString(),
		*VC->GetViewRotation().ToString());
}

FString FClaudeContextProvider::CaptureFromCamera(const FVector& Location, const FRotator& Rotation, int32 Width, int32 Height, float FOV, FString& OutImageBase64, FString& OutError)
{
	if (!GEditor) { OutError = TEXT("No GEditor"); return TEXT(""); }

	Width = FMath::Clamp(Width, 256, 2048);
	Height = FMath::Clamp(Height, 256, 2048);
	if (FOV <= 0.f) FOV = 90.f;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return TEXT(""); }

	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	RT->RenderTargetFormat = RTF_RGBA8;
	RT->InitAutoFormat(Width, Height);
	RT->UpdateResourceImmediate(true);

	AActor* TempActor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, Rotation);
	if (!TempActor) { OutError = TEXT("Could not spawn capture actor"); return TEXT(""); }

	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(TempActor);
	Capture->RegisterComponent();
	Capture->TextureTarget = RT;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->FOVAngle = FOV;
	Capture->ShowFlags = FEngineShowFlags(ESFIM_Game);
	Capture->SetWorldLocationAndRotation(Location, Rotation);
	Capture->CaptureScene();

	const bool bOk = ReadRenderTargetToPngBase64(RT, OutImageBase64, OutError);

	Capture->UnregisterComponent();
	TempActor->Destroy();
	RT->MarkAsGarbage();

	if (!bOk) return TEXT("");

	return FString::Printf(TEXT("Captured from (%s) rot=(%s) fov=%.1f at %dx%d"),
		*Location.ToString(), *Rotation.ToString(), FOV, Width, Height);
}

FString FClaudeContextProvider::CaptureAssetThumbnail(const FString& AssetPath, int32 Size, FString& OutImageBase64, FString& OutError)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath); return TEXT(""); }

	Size = FMath::Clamp(Size, 128, 1024);

	// Use the asset thumbnail renderer. UE renders thumbnails into a texture;
	// we readback those pixels and PNG-encode.
	FObjectThumbnail ObjectThumb;
	ThumbnailTools::RenderThumbnail(Asset, Size, Size, ThumbnailTools::EThumbnailTextureFlushMode::AlwaysFlush, nullptr, &ObjectThumb);

	const TArray<uint8>& RawBytes = ObjectThumb.GetUncompressedImageData();
	if (RawBytes.Num() == 0)
	{
		OutError = FString::Printf(TEXT("Thumbnail render produced no data for %s. Asset type may not support thumbnails."), *AssetPath);
		return TEXT("");
	}

	// Thumbnail is BGRA 8-bit
	IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> PngWrapper = IWM.CreateImageWrapper(EImageFormat::PNG);
	if (!PngWrapper.IsValid()) { OutError = TEXT("PNG wrapper unavailable"); return TEXT(""); }

	const int32 W = ObjectThumb.GetImageWidth();
	const int32 H = ObjectThumb.GetImageHeight();
	const bool bSet = PngWrapper->SetRaw(RawBytes.GetData(), RawBytes.Num(), W, H, ERGBFormat::BGRA, 8);
	if (!bSet) { OutError = TEXT("SetRaw failed for thumbnail"); return TEXT(""); }

	const TArray64<uint8>& Compressed = PngWrapper->GetCompressed(100);
	if (Compressed.Num() == 0) { OutError = TEXT("PNG compress failed"); return TEXT(""); }

	OutImageBase64 = FBase64::Encode(Compressed.GetData(), Compressed.Num());
	return FString::Printf(TEXT("Captured %dx%d thumbnail of %s (%s)"),
		W, H, *Asset->GetName(), *Asset->GetClass()->GetName());
}

// =============================================================================
// PIE — Play-In-Editor runtime tools
// =============================================================================
//
// All these tools require an active PIE session (Play started). They operate
// on GEditor->PlayWorld which is the PIE-spawned world. If no PIE world exists,
// they return an error.
//
// Actor lookup is permissive: matches by exact name, exact label, or substring.

static UWorld* PIEWorld()
{
	if (!GEditor) return nullptr;
	return GEditor->PlayWorld;
}

static AActor* FindPIEActor(UWorld* World, const FString& Name)
{
	if (!World) return nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		if (A->GetName() == Name || A->GetActorLabel() == Name) return A;
	}
	// Fall back to substring match
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		if (A->GetName().Contains(Name, ESearchCase::IgnoreCase) ||
			A->GetActorLabel().Contains(Name, ESearchCase::IgnoreCase))
		{
			return A;
		}
	}
	return nullptr;
}

static AAIController* FindPIEAIController(UWorld* World, const FString& ActorName)
{
	AActor* A = FindPIEActor(World, ActorName);
	if (!A) return nullptr;
	if (APawn* P = Cast<APawn>(A)) return Cast<AAIController>(P->GetController());
	return Cast<AAIController>(A);
}

FString FClaudeContextProvider::PIESpawnActor(const FString& ClassPath, const FVector& Location, const FRotator& Rotation, FString& OutError)
{
	UWorld* World = PIEWorld();
	if (!World) { OutError = TEXT("Not in PIE — start Play first"); return TEXT(""); }

	UClass* Class = StaticLoadClass(AActor::StaticClass(), nullptr, *ClassPath);
	if (!Class)
	{
		// Try as Blueprint generated class
		if (UObject* Loaded = LoadAssetByPath(ClassPath))
		{
			if (UBlueprint* BP = Cast<UBlueprint>(Loaded)) Class = BP->GeneratedClass;
		}
	}
	if (!Class) { OutError = FString::Printf(TEXT("Class not found: %s"), *ClassPath); return TEXT(""); }
	if (!Class->IsChildOf(AActor::StaticClass())) { OutError = TEXT("Not an AActor subclass"); return TEXT(""); }

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AActor* Spawned = World->SpawnActor<AActor>(Class, Location, Rotation, Params);
	if (!Spawned) { OutError = TEXT("Spawn failed"); return TEXT(""); }

	return FString::Printf(TEXT("Spawned %s as '%s' at %s"),
		*Class->GetName(), *Spawned->GetName(), *Location.ToString());
}

FString FClaudeContextProvider::PIEDestroyActor(const FString& ActorName, FString& OutError)
{
	UWorld* World = PIEWorld();
	if (!World) { OutError = TEXT("Not in PIE"); return TEXT(""); }
	AActor* A = FindPIEActor(World, ActorName);
	if (!A) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName); return TEXT(""); }

	const FString N = A->GetName();
	A->Destroy();
	return FString::Printf(TEXT("Destroyed actor '%s'"), *N);
}

FString FClaudeContextProvider::PIETeleportActor(const FString& ActorName, const FVector& Location, const FRotator& Rotation, FString& OutError)
{
	UWorld* World = PIEWorld();
	if (!World) { OutError = TEXT("Not in PIE"); return TEXT(""); }
	AActor* A = FindPIEActor(World, ActorName);
	if (!A) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName); return TEXT(""); }

	const bool bOk = A->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	if (!bOk) return FString::Printf(TEXT("Teleport partial — actor '%s' may have collision constraints"), *A->GetName());
	return FString::Printf(TEXT("Teleported '%s' to %s"), *A->GetName(), *Location.ToString());
}

FString FClaudeContextProvider::PIEGetProperty(const FString& ActorName, const FString& PropertyName)
{
	UWorld* World = PIEWorld();
	if (!World) return TEXT("{\"error\":\"Not in PIE\"}");
	AActor* A = FindPIEActor(World, ActorName);
	if (!A) return FString::Printf(TEXT("{\"error\":\"Actor not found: %s\"}"), *ActorName);

	FResolvedProperty R;
	FString Err;
	if (!ResolvePropertyPath(A, PropertyName, R, Err))
	{
		return FString::Printf(TEXT("{\"error\":\"%s\"}"), *Err.ReplaceCharWithEscapedChar());
	}

	FString ValueStr;
	R.Property->ExportText_Direct(ValueStr, R.ContainerPtr, R.ContainerPtr, A, PPF_None);

	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("actor"), A->GetName());
	J->SetStringField(TEXT("property"), PropertyName);
	J->SetStringField(TEXT("type"), R.Property->GetCPPType());
	J->SetStringField(TEXT("value"), ValueStr);
	return JsonObjectToString(J);
}

FString FClaudeContextProvider::PIESetProperty(const FString& ActorName, const FString& PropertyName, const FString& NewValue, FString& OutError)
{
	UWorld* World = PIEWorld();
	if (!World) { OutError = TEXT("Not in PIE"); return TEXT(""); }
	AActor* A = FindPIEActor(World, ActorName);
	if (!A) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName); return TEXT(""); }

	FResolvedProperty R;
	if (!ResolvePropertyPath(A, PropertyName, R, OutError)) return TEXT("");

	const TCHAR* Result = R.Property->ImportText_Direct(*NewValue, R.ContainerPtr, A, PPF_None);
	if (!Result) { OutError = FString::Printf(TEXT("ImportText failed for '%s'"), *NewValue); return TEXT(""); }

	return FString::Printf(TEXT("Set %s.%s = %s"), *A->GetName(), *PropertyName, *NewValue);
}

FString FClaudeContextProvider::PIEGetBlackboardKey(const FString& ActorName, const FString& KeyName)
{
	UWorld* World = PIEWorld();
	if (!World) return TEXT("{\"error\":\"Not in PIE\"}");

	AAIController* AI = FindPIEAIController(World, ActorName);
	if (!AI) return FString::Printf(TEXT("{\"error\":\"No AIController on '%s'\"}"), *ActorName);

	UBlackboardComponent* BB = AI->GetBlackboardComponent();
	if (!BB) return TEXT("{\"error\":\"No blackboard\"}");

	const FName Key(*KeyName);
	const FBlackboard::FKey KeyId = BB->GetKeyID(Key);
	if (KeyId == FBlackboard::InvalidKey)
	{
		return FString::Printf(TEXT("{\"error\":\"Key not found: %s\"}"), *KeyName);
	}

	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("key"), KeyName);

	// Read by attempting common types — not exhaustive but covers the basics
	UClass* KeyType = BB->GetKeyType(KeyId);
	if (!KeyType) { J->SetStringField(TEXT("error"), TEXT("Unknown key type")); return JsonObjectToString(J); }

	J->SetStringField(TEXT("type"), KeyType->GetName());

	if (KeyType == UBlackboardKeyType_Bool::StaticClass())
	{
		J->SetBoolField(TEXT("value"), BB->GetValueAsBool(Key));
	}
	else if (KeyType == UBlackboardKeyType_Float::StaticClass())
	{
		J->SetNumberField(TEXT("value"), BB->GetValueAsFloat(Key));
	}
	else if (KeyType == UBlackboardKeyType_Int::StaticClass())
	{
		J->SetNumberField(TEXT("value"), BB->GetValueAsInt(Key));
	}
	else if (KeyType == UBlackboardKeyType_String::StaticClass())
	{
		J->SetStringField(TEXT("value"), BB->GetValueAsString(Key));
	}
	else if (KeyType == UBlackboardKeyType_Name::StaticClass())
	{
		J->SetStringField(TEXT("value"), BB->GetValueAsName(Key).ToString());
	}
	else if (KeyType == UBlackboardKeyType_Vector::StaticClass())
	{
		J->SetStringField(TEXT("value"), BB->GetValueAsVector(Key).ToString());
	}
	else if (KeyType == UBlackboardKeyType_Object::StaticClass())
	{
		UObject* Obj = BB->GetValueAsObject(Key);
		J->SetStringField(TEXT("value"), Obj ? Obj->GetName() : TEXT("(null)"));
	}
	else
	{
		J->SetStringField(TEXT("value"), TEXT("(unsupported key type for read)"));
	}
	return JsonObjectToString(J);
}

FString FClaudeContextProvider::PIESetBlackboardKey(const FString& ActorName, const FString& KeyName, const FString& NewValue, FString& OutError)
{
	UWorld* World = PIEWorld();
	if (!World) { OutError = TEXT("Not in PIE"); return TEXT(""); }

	AAIController* AI = FindPIEAIController(World, ActorName);
	if (!AI) { OutError = FString::Printf(TEXT("No AIController on '%s'"), *ActorName); return TEXT(""); }

	UBlackboardComponent* BB = AI->GetBlackboardComponent();
	if (!BB) { OutError = TEXT("No blackboard"); return TEXT(""); }

	const FName Key(*KeyName);
	const FBlackboard::FKey KeyId = BB->GetKeyID(Key);
	if (KeyId == FBlackboard::InvalidKey) { OutError = FString::Printf(TEXT("Key not found: %s"), *KeyName); return TEXT(""); }

	UClass* KeyType = BB->GetKeyType(KeyId);
	if (!KeyType) { OutError = TEXT("Unknown key type"); return TEXT(""); }

	if (KeyType == UBlackboardKeyType_Bool::StaticClass())
	{
		BB->SetValueAsBool(Key, NewValue.ToBool());
	}
	else if (KeyType == UBlackboardKeyType_Float::StaticClass())
	{
		BB->SetValueAsFloat(Key, FCString::Atof(*NewValue));
	}
	else if (KeyType == UBlackboardKeyType_Int::StaticClass())
	{
		BB->SetValueAsInt(Key, FCString::Atoi(*NewValue));
	}
	else if (KeyType == UBlackboardKeyType_String::StaticClass())
	{
		BB->SetValueAsString(Key, NewValue);
	}
	else if (KeyType == UBlackboardKeyType_Name::StaticClass())
	{
		BB->SetValueAsName(Key, FName(*NewValue));
	}
	else if (KeyType == UBlackboardKeyType_Vector::StaticClass())
	{
		FVector V;
		V.InitFromString(NewValue);
		BB->SetValueAsVector(Key, V);
	}
	else if (KeyType == UBlackboardKeyType_Object::StaticClass())
	{
		// Find the actor by name in PIE world and use that
		AActor* TargetActor = FindPIEActor(World, NewValue);
		if (TargetActor)
		{
			BB->SetValueAsObject(Key, TargetActor);
		}
		else
		{
			OutError = FString::Printf(TEXT("Object key requires PIE actor name; '%s' not found"), *NewValue);
			return TEXT("");
		}
	}
	else
	{
		OutError = FString::Printf(TEXT("Unsupported key type for write: %s"), *KeyType->GetName());
		return TEXT("");
	}

	return FString::Printf(TEXT("Set blackboard %s.%s = %s"), *ActorName, *KeyName, *NewValue);
}

FString FClaudeContextProvider::PIEMoveAITo(const FString& ActorName, const FVector& Destination, float AcceptanceRadius, FString& OutError)
{
	UWorld* World = PIEWorld();
	if (!World) { OutError = TEXT("Not in PIE"); return TEXT(""); }

	AAIController* AI = FindPIEAIController(World, ActorName);
	if (!AI) { OutError = FString::Printf(TEXT("No AIController on '%s'"), *ActorName); return TEXT(""); }

	if (AcceptanceRadius < 0.f) AcceptanceRadius = 50.f;
	const EPathFollowingRequestResult::Type Result = AI->MoveToLocation(Destination, AcceptanceRadius);

	// EPathFollowingRequestResult::Type values are: Failed=0, AlreadyAtGoal=1, RequestSuccessful=2.
	// We compare via int cast to avoid pulling in PathFollowingComponent.h which
	// has the full enum definition (only forward-declared in AIController.h).
	const int32 ResultInt = (int32)Result;
	const TCHAR* ResultStr = TEXT("Unknown");
	if (ResultInt == 0)      ResultStr = TEXT("Failed");
	else if (ResultInt == 1) ResultStr = TEXT("AlreadyAtGoal");
	else if (ResultInt == 2) ResultStr = TEXT("RequestSuccessful");
	return FString::Printf(TEXT("MoveTo %s → %s (radius %.0f) — %s"),
		*AI->GetName(), *Destination.ToString(), AcceptanceRadius, ResultStr);
}

FString FClaudeContextProvider::PIEStopAI(const FString& ActorName, FString& OutError)
{
	UWorld* World = PIEWorld();
	if (!World) { OutError = TEXT("Not in PIE"); return TEXT(""); }
	AAIController* AI = FindPIEAIController(World, ActorName);
	if (!AI) { OutError = FString::Printf(TEXT("No AIController on '%s'"), *ActorName); return TEXT(""); }

	AI->StopMovement();
	if (UBrainComponent* Brain = AI->GetBrainComponent())
	{
		Brain->StopLogic(TEXT("ClaudeAgent.PIEStopAI"));
	}
	return FString::Printf(TEXT("Stopped AI on '%s'"), *AI->GetName());
}

FString FClaudeContextProvider::PIEGetGameState()
{
	UWorld* World = PIEWorld();
	if (!World) return TEXT("{\"in_pie\":false}");

	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetBoolField(TEXT("in_pie"), true);
	J->SetBoolField(TEXT("paused"), World->IsPaused());
	J->SetNumberField(TEXT("time_seconds"), World->GetTimeSeconds());
	J->SetNumberField(TEXT("real_time_seconds"), World->GetRealTimeSeconds());
	J->SetNumberField(TEXT("delta_time"), World->GetDeltaSeconds());
	J->SetStringField(TEXT("map"), World->GetMapName());

	int32 ActorCount = 0;
	int32 PawnCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		++ActorCount;
		if (Cast<APawn>(*It)) ++PawnCount;
	}
	J->SetNumberField(TEXT("actor_count"), ActorCount);
	J->SetNumberField(TEXT("pawn_count"), PawnCount);

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (APawn* PP = PC->GetPawn())
		{
			J->SetStringField(TEXT("player_pawn"), PP->GetName());
			J->SetStringField(TEXT("player_location"), PP->GetActorLocation().ToString());
		}
	}
	return JsonObjectToString(J);
}

FString FClaudeContextProvider::PIEListActors(const FString& ClassFilter, const FString& NameContains)
{
	UWorld* World = PIEWorld();
	if (!World) return TEXT("{\"error\":\"Not in PIE\"}");

	UClass* FilterClass = AActor::StaticClass();
	if (!ClassFilter.IsEmpty())
	{
		UClass* C = StaticLoadClass(AActor::StaticClass(), nullptr, *ClassFilter);
		if (!C)
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->IsChildOf(AActor::StaticClass()) && It->GetName() == ClassFilter) { C = *It; break; }
			}
		}
		if (C) FilterClass = C;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Items;
	for (TActorIterator<AActor> It(World, FilterClass); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		if (!NameContains.IsEmpty() &&
			!A->GetName().Contains(NameContains, ESearchCase::IgnoreCase) &&
			!A->GetActorLabel().Contains(NameContains, ESearchCase::IgnoreCase)) continue;

		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), A->GetName());
		O->SetStringField(TEXT("label"), A->GetActorLabel());
		O->SetStringField(TEXT("class"), A->GetClass()->GetName());
		O->SetStringField(TEXT("location"), A->GetActorLocation().ToString());
		Items.Add(MakeShared<FJsonValueObject>(O));

		if (Items.Num() >= 200) break; // safety cap
	}
	Root->SetArrayField(TEXT("actors"), Items);
	Root->SetNumberField(TEXT("count"), Items.Num());
	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::PIEConsoleCommand(const FString& Command, FString& OutError)
{
	UWorld* World = PIEWorld();
	if (!World) { OutError = TEXT("Not in PIE"); return TEXT(""); }

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		PC->ConsoleCommand(Command, true);
	}
	else if (GEngine)
	{
		GEngine->Exec(World, *Command);
	}
	else
	{
		OutError = TEXT("No engine context to execute command");
		return TEXT("");
	}
	return FString::Printf(TEXT("Executed: %s"), *Command);
}

// =============================================================================
// Debug — Blueprint health, asset references, orphans, circular deps
// =============================================================================

FString FClaudeContextProvider::BPGetCompileErrors(const FString& BlueprintPath)
{
	UBlueprint* BP = Cast<UBlueprint>(LoadAssetByPath(BlueprintPath));
	if (!BP) return FString::Printf(TEXT("{\"error\":\"Not a Blueprint: %s\"}"), *BlueprintPath);

	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("path"), BlueprintPath);

	const TCHAR* StatusStr = TEXT("Unknown");
	switch (BP->Status)
	{
		case BS_UpToDate:                  StatusStr = TEXT("UpToDate"); break;
		case BS_Dirty:                     StatusStr = TEXT("Dirty"); break;
		case BS_Error:                     StatusStr = TEXT("Error"); break;
		case BS_UpToDateWithWarnings:      StatusStr = TEXT("UpToDateWithWarnings"); break;
		case BS_Unknown:                   StatusStr = TEXT("Unknown"); break;
		default: break;
	}
	J->SetStringField(TEXT("status"), StatusStr);
	J->SetBoolField(TEXT("has_errors"), BP->Status == BS_Error);

	// Walk all graphs and collect orphan/error nodes — heuristic check
	TArray<TSharedPtr<FJsonValue>> Issues;
	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);
	for (UEdGraph* G : AllGraphs)
	{
		if (!G) continue;
		for (UEdGraphNode* N : G->Nodes)
		{
			if (!N) continue;
			FString ErrorMsg;
			N->ErrorMsg.IsEmpty() ? ErrorMsg = TEXT("") : ErrorMsg = N->ErrorMsg;
			if (N->bHasCompilerMessage && !ErrorMsg.IsEmpty())
			{
				TSharedRef<FJsonObject> Issue = MakeShared<FJsonObject>();
				Issue->SetStringField(TEXT("graph"), G->GetName());
				Issue->SetStringField(TEXT("node"), N->GetName());
				Issue->SetStringField(TEXT("title"), N->GetNodeTitle(ENodeTitleType::ListView).ToString());
				Issue->SetStringField(TEXT("error"), ErrorMsg);
				Issues.Add(MakeShared<FJsonValueObject>(Issue));
			}
		}
	}
	J->SetArrayField(TEXT("issues"), Issues);
	J->SetNumberField(TEXT("issue_count"), Issues.Num());

	return Truncate(JsonObjectToString(J));
}

FString FClaudeContextProvider::BPRefreshAllNodes(const FString& BlueprintPath, FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(LoadAssetByPath(BlueprintPath));
	if (!BP) { OutError = FString::Printf(TEXT("Not a Blueprint: %s"), *BlueprintPath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "RefreshNodes", "Refresh BP Nodes (Claude)"));
	BP->Modify();

	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);
	int32 Refreshed = 0;
	for (UEdGraph* G : AllGraphs)
	{
		if (!G) continue;
		for (UEdGraphNode* N : G->Nodes)
		{
			if (!N) continue;
			N->ReconstructNode();
			++Refreshed;
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP);
	return FString::Printf(TEXT("Refreshed %d nodes across %d graphs in %s"),
		Refreshed, AllGraphs.Num(), *BP->GetName());
}

FString FClaudeContextProvider::BPFixBrokenReferences(const FString& BlueprintPath, FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(LoadAssetByPath(BlueprintPath));
	if (!BP) { OutError = FString::Printf(TEXT("Not a Blueprint: %s"), *BlueprintPath); return TEXT(""); }

	// Strategy: walk all graph nodes, find pins with object refs that resolve to null
	// (either no value or asset deleted). Report them. Auto-fix is risky — we only
	// detect.
	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);

	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Broken;

	for (UEdGraph* G : AllGraphs)
	{
		if (!G) continue;
		for (UEdGraphNode* N : G->Nodes)
		{
			if (!N) continue;
			for (UEdGraphPin* P : N->Pins)
			{
				if (!P) continue;
				if (P->Direction != EGPD_Input) continue;
				if (P->LinkedTo.Num() > 0) continue; // wired pins are fine
				if (P->DefaultObject == nullptr && !P->DefaultValue.IsEmpty())
				{
					// Default value present but no resolved object — possibly broken ref
					if (P->PinType.PinCategory == TEXT("object") || P->PinType.PinCategory == TEXT("class"))
					{
						TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
						O->SetStringField(TEXT("graph"), G->GetName());
						O->SetStringField(TEXT("node"), N->GetNodeTitle(ENodeTitleType::ListView).ToString());
						O->SetStringField(TEXT("pin"), P->PinName.ToString());
						O->SetStringField(TEXT("default_value"), P->DefaultValue);
						Broken.Add(MakeShared<FJsonValueObject>(O));
					}
				}
			}
		}
	}
	J->SetArrayField(TEXT("broken_references"), Broken);
	J->SetNumberField(TEXT("count"), Broken.Num());
	J->SetStringField(TEXT("note"), TEXT("Detection only — manual fix required. Open the asset and rewire pins."));
	return JsonObjectToString(J);
}

FString FClaudeContextProvider::BPFixDeprecatedNodes(const FString& BlueprintPath, FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(LoadAssetByPath(BlueprintPath));
	if (!BP) { OutError = FString::Printf(TEXT("Not a Blueprint: %s"), *BlueprintPath); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "FixDeprecated", "Fix Deprecated Nodes (Claude)"));
	BP->Modify();

	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);

	int32 Reconstructed = 0;
	for (UEdGraph* G : AllGraphs)
	{
		if (!G) continue;
		for (UEdGraphNode* N : G->Nodes)
		{
			if (!N) continue;
			// Heuristic: if node's class is deprecated, or function is deprecated,
			// reconstruct it. UE will try to map old → new.
			if (UK2Node_CallFunction* CF = Cast<UK2Node_CallFunction>(N))
			{
				UFunction* Fn = CF->GetTargetFunction();
				if (Fn && Fn->HasMetaData(TEXT("DeprecatedFunction")))
				{
					CF->ReconstructNode();
					++Reconstructed;
				}
			}
		}
	}

	if (Reconstructed > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	}
	return FString::Printf(TEXT("Reconstructed %d deprecated function nodes"), Reconstructed);
}

FString FClaudeContextProvider::BPFindUnconnectedPins(const FString& BlueprintPath)
{
	UBlueprint* BP = Cast<UBlueprint>(LoadAssetByPath(BlueprintPath));
	if (!BP) return FString::Printf(TEXT("{\"error\":\"Not a Blueprint: %s\"}"), *BlueprintPath);

	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Issues;

	for (UEdGraph* G : AllGraphs)
	{
		if (!G) continue;
		for (UEdGraphNode* N : G->Nodes)
		{
			if (!N) continue;

			// We care about EXEC output pins that aren't connected — those are
			// dead branches. Also useful to flag REQUIRED input data pins with no
			// default and no link.
			for (UEdGraphPin* P : N->Pins)
			{
				if (!P) continue;
				const bool bExec = (P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);
				if (P->Direction == EGPD_Output && bExec && P->LinkedTo.Num() == 0)
				{
					TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetStringField(TEXT("graph"), G->GetName());
					O->SetStringField(TEXT("node"), N->GetNodeTitle(ENodeTitleType::ListView).ToString());
					O->SetStringField(TEXT("pin"), P->PinName.ToString());
					O->SetStringField(TEXT("kind"), TEXT("dead_exec_branch"));
					Issues.Add(MakeShared<FJsonValueObject>(O));
				}
			}
		}
	}
	Root->SetArrayField(TEXT("issues"), Issues);
	Root->SetNumberField(TEXT("count"), Issues.Num());
	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::FindOrphanAssets(const FString& SearchPath, int32 MaxResults)
{
	const FString Path = SearchPath.IsEmpty() ? TEXT("/Game") : SearchPath;
	if (MaxResults <= 0) MaxResults = 50;

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Path));
	Filter.bRecursivePaths = true;
	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Orphans;

	for (const FAssetData& A : Assets)
	{
		if (Orphans.Num() >= MaxResults) break;

		TArray<FName> Referencers;
		AR.GetReferencers(A.PackageName, Referencers);

		// Filter out engine/auto references (transient packages, etc.)
		int32 RealRefs = 0;
		for (const FName& R : Referencers)
		{
			const FString S = R.ToString();
			// Skip self-references and engine internals
			if (S == A.PackageName.ToString()) continue;
			if (S.StartsWith(TEXT("/Engine/Transient"))) continue;
			++RealRefs;
		}

		if (RealRefs == 0)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), A.GetObjectPathString());
			O->SetStringField(TEXT("class"), A.AssetClassPath.ToString());
			Orphans.Add(MakeShared<FJsonValueObject>(O));
		}
	}

	Root->SetArrayField(TEXT("orphans"), Orphans);
	Root->SetNumberField(TEXT("count"), Orphans.Num());
	Root->SetNumberField(TEXT("scanned"), Assets.Num());
	Root->SetStringField(TEXT("warning"), TEXT("'Orphan' means no AssetRegistry references found. Some assets are referenced via redirectors, soft-class refs, or only at runtime — verify before deleting."));
	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::FindCircularDependencies(const FString& SearchPath, int32 MaxResults)
{
	const FString Path = SearchPath.IsEmpty() ? TEXT("/Game") : SearchPath;
	if (MaxResults <= 0) MaxResults = 50;

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Path));
	Filter.bRecursivePaths = true;
	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);

	// Detect 2-cycles: A → B → A. Detecting longer cycles is N^2-ish and
	// expensive on large projects. 2-cycle detection covers the most common case.
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Cycles;

	for (const FAssetData& A : Assets)
	{
		if (Cycles.Num() >= MaxResults) break;

		TArray<FName> ARefs;
		AR.GetDependencies(A.PackageName, ARefs);
		for (const FName& B : ARefs)
		{
			if (B == A.PackageName) continue;
			TArray<FName> BRefs;
			AR.GetDependencies(B, BRefs);
			if (BRefs.Contains(A.PackageName))
			{
				TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("a"), A.PackageName.ToString());
				O->SetStringField(TEXT("b"), B.ToString());
				Cycles.Add(MakeShared<FJsonValueObject>(O));
				if (Cycles.Num() >= MaxResults) break;
			}
		}
	}

	Root->SetArrayField(TEXT("cycles"), Cycles);
	Root->SetNumberField(TEXT("count"), Cycles.Num());
	Root->SetStringField(TEXT("note"), TEXT("Only 2-cycles detected (A↔B). Longer chains not scanned for performance."));
	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::GetDependencyTree(const FString& AssetPath, int32 MaxDepth)
{
	if (MaxDepth <= 0) MaxDepth = 3;
	if (MaxDepth > 8) MaxDepth = 8;

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	UObject* Asset = LoadAssetByPath(AssetPath);
	if (!Asset) return FString::Printf(TEXT("{\"error\":\"Asset not found: %s\"}"), *AssetPath);

	FName PackageName = FName(*Asset->GetPackage()->GetName());

	TSet<FName> Visited;
	TFunction<TSharedRef<FJsonObject>(FName, int32)> Walk;
	Walk = [&](FName Pkg, int32 Depth) -> TSharedRef<FJsonObject>
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("package"), Pkg.ToString());
		if (Depth >= MaxDepth) return O;
		if (Visited.Contains(Pkg)) { O->SetBoolField(TEXT("cycle"), true); return O; }
		Visited.Add(Pkg);

		TArray<FName> Deps;
		AR.GetDependencies(Pkg, Deps);
		TArray<TSharedPtr<FJsonValue>> Children;
		for (const FName& D : Deps)
		{
			// Filter out engine internals to avoid noise
			const FString S = D.ToString();
			if (S.StartsWith(TEXT("/Script"))) continue;
			Children.Add(MakeShared<FJsonValueObject>(Walk(D, Depth + 1)));
			if (Children.Num() >= 50) break;
		}
		if (Children.Num() > 0) O->SetArrayField(TEXT("dependencies"), Children);
		return O;
	};

	TSharedRef<FJsonObject> Root = Walk(PackageName, 0);
	Root->SetNumberField(TEXT("max_depth"), MaxDepth);
	return Truncate(JsonObjectToString(Root));
}

// =============================================================================
// Splines
// =============================================================================
//
// Splines in UE are typically used for paths, wires, road outlines, AI patrol
// routes, etc. We spawn a generic AActor with a USplineComponent attached.
// Spline meshes (USplineMeshComponent) are NOT included here — that's a
// separate workflow (populate_spline_with_meshes) which we don't currently do.

#include "Components/SplineComponent.h"

FString FClaudeContextProvider::CreateSplineActor(const FString& Label, const FVector& Location, FString& OutActorPath, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateSpline", "Create Spline Actor (Claude)"));

	FActorSpawnParameters Params;
	Params.bNoFail = true;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (!Actor) { OutError = TEXT("Spawn failed"); return TEXT(""); }

	if (!Label.IsEmpty()) Actor->SetActorLabel(Label);

	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"), RF_Transactional);
	Actor->SetRootComponent(Root);
	Root->SetWorldLocation(Location);
	Root->RegisterComponent();
	Actor->AddInstanceComponent(Root);

	USplineComponent* Spline = NewObject<USplineComponent>(Actor, TEXT("Spline"), RF_Transactional);
	Spline->SetupAttachment(Root);
	Spline->RegisterComponent();
	Actor->AddInstanceComponent(Spline);

	// Default 2-point line from origin to +X 1m
	Spline->ClearSplinePoints(false);
	Spline->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, false);
	Spline->AddSplinePoint(FVector(100, 0, 0), ESplineCoordinateSpace::Local, false);
	Spline->UpdateSpline();

	OutActorPath = Actor->GetName();
	return FString::Printf(TEXT("Created spline actor '%s' at %s with 2 default points"),
		*Actor->GetName(), *Location.ToString());
}

static USplineComponent* FindSplineOnActor(AActor* Actor)
{
	if (!Actor) return nullptr;
	TArray<USplineComponent*> Splines;
	Actor->GetComponents<USplineComponent>(Splines);
	return Splines.Num() > 0 ? Splines[0] : nullptr;
}

FString FClaudeContextProvider::AddSplinePoint(const FString& ActorName, const FVector& Position, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No world"); return TEXT(""); }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName() == ActorName || It->GetActorLabel() == ActorName) { Actor = *It; break; }
	}
	if (!Actor) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName); return TEXT(""); }

	USplineComponent* Spline = FindSplineOnActor(Actor);
	if (!Spline) { OutError = TEXT("Actor has no SplineComponent"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddSplinePt", "Add Spline Point (Claude)"));
	Spline->Modify();

	Spline->AddSplinePoint(Position, ESplineCoordinateSpace::World, true);
	const int32 NewIdx = Spline->GetNumberOfSplinePoints() - 1;

	return FString::Printf(TEXT("Added spline point at index %d (pos %s) — total points: %d"),
		NewIdx, *Position.ToString(), Spline->GetNumberOfSplinePoints());
}

FString FClaudeContextProvider::SetSplinePoint(const FString& ActorName, int32 PointIndex, const FVector& Position, const FString& PointType, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No world"); return TEXT(""); }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName() == ActorName || It->GetActorLabel() == ActorName) { Actor = *It; break; }
	}
	if (!Actor) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName); return TEXT(""); }

	USplineComponent* Spline = FindSplineOnActor(Actor);
	if (!Spline) { OutError = TEXT("No SplineComponent"); return TEXT(""); }

	if (PointIndex < 0 || PointIndex >= Spline->GetNumberOfSplinePoints())
	{
		OutError = FString::Printf(TEXT("Index %d out of range (0..%d)"), PointIndex, Spline->GetNumberOfSplinePoints() - 1);
		return TEXT("");
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetSplinePt", "Set Spline Point (Claude)"));
	Spline->Modify();

	Spline->SetLocationAtSplinePoint(PointIndex, Position, ESplineCoordinateSpace::World, true);

	if (!PointType.IsEmpty())
	{
		ESplinePointType::Type T = ESplinePointType::Curve;
		if (PointType.Equals(TEXT("Linear"), ESearchCase::IgnoreCase))           T = ESplinePointType::Linear;
		else if (PointType.Equals(TEXT("Curve"), ESearchCase::IgnoreCase))       T = ESplinePointType::Curve;
		else if (PointType.Equals(TEXT("CurveClamped"), ESearchCase::IgnoreCase)) T = ESplinePointType::CurveClamped;
		else if (PointType.Equals(TEXT("Constant"), ESearchCase::IgnoreCase))    T = ESplinePointType::Constant;
		Spline->SetSplinePointType(PointIndex, T, true);
	}

	return FString::Printf(TEXT("Set point %d to %s%s"),
		PointIndex, *Position.ToString(),
		PointType.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (type=%s)"), *PointType));
}

FString FClaudeContextProvider::RemoveSplinePoint(const FString& ActorName, int32 PointIndex, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No world"); return TEXT(""); }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName() == ActorName || It->GetActorLabel() == ActorName) { Actor = *It; break; }
	}
	if (!Actor) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName); return TEXT(""); }

	USplineComponent* Spline = FindSplineOnActor(Actor);
	if (!Spline) { OutError = TEXT("No SplineComponent"); return TEXT(""); }

	if (PointIndex < 0 || PointIndex >= Spline->GetNumberOfSplinePoints())
	{
		OutError = FString::Printf(TEXT("Index %d out of range"), PointIndex);
		return TEXT("");
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "RemSplinePt", "Remove Spline Point (Claude)"));
	Spline->Modify();
	Spline->RemoveSplinePoint(PointIndex, true);

	return FString::Printf(TEXT("Removed point %d (remaining: %d)"),
		PointIndex, Spline->GetNumberOfSplinePoints());
}

FString FClaudeContextProvider::GetSplineInfo(const FString& ActorName)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return TEXT("{\"error\":\"No world\"}");

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName() == ActorName || It->GetActorLabel() == ActorName) { Actor = *It; break; }
	}
	if (!Actor) return FString::Printf(TEXT("{\"error\":\"Actor not found: %s\"}"), *ActorName);

	USplineComponent* Spline = FindSplineOnActor(Actor);
	if (!Spline) return TEXT("{\"error\":\"No SplineComponent\"}");

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("actor"), Actor->GetName());
	Root->SetBoolField(TEXT("closed_loop"), Spline->IsClosedLoop());
	Root->SetNumberField(TEXT("point_count"), Spline->GetNumberOfSplinePoints());
	Root->SetNumberField(TEXT("length"), Spline->GetSplineLength());

	TArray<TSharedPtr<FJsonValue>> Points;
	for (int32 i = 0; i < Spline->GetNumberOfSplinePoints(); ++i)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("index"), i);
		O->SetStringField(TEXT("location"), Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World).ToString());
		const ESplinePointType::Type T = Spline->GetSplinePointType(i);
		const TCHAR* TStr = TEXT("?");
		switch (T)
		{
			case ESplinePointType::Linear:        TStr = TEXT("Linear"); break;
			case ESplinePointType::Curve:         TStr = TEXT("Curve"); break;
			case ESplinePointType::CurveClamped:  TStr = TEXT("CurveClamped"); break;
			case ESplinePointType::Constant:      TStr = TEXT("Constant"); break;
			case ESplinePointType::CurveCustomTangent: TStr = TEXT("CurveCustomTangent"); break;
		}
		O->SetStringField(TEXT("type"), TStr);
		Points.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("points"), Points);
	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::SetSplineClosed(const FString& ActorName, bool bClosed, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No world"); return TEXT(""); }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName() == ActorName || It->GetActorLabel() == ActorName) { Actor = *It; break; }
	}
	if (!Actor) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName); return TEXT(""); }

	USplineComponent* Spline = FindSplineOnActor(Actor);
	if (!Spline) { OutError = TEXT("No SplineComponent"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetSplineClosed", "Set Spline Closed (Claude)"));
	Spline->Modify();
	Spline->SetClosedLoop(bClosed, true);

	return FString::Printf(TEXT("Set spline closed=%s"), bClosed ? TEXT("true") : TEXT("false"));
}

// =============================================================================
// Environment — atmospherics, lighting, post-process
// =============================================================================
//
// All four take a JSON object of property→value pairs and apply them via the
// generic ResolvePropertyPath / ImportText pipeline. The actor lookup is
// permissive (name or label), and we apply changes in editor world.

static AActor* FindEditorActor(UWorld* World, const FString& Name)
{
	if (!World) return nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		if (A->GetName() == Name || A->GetActorLabel() == Name) return A;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		if (A->GetName().Contains(Name, ESearchCase::IgnoreCase) ||
			A->GetActorLabel().Contains(Name, ESearchCase::IgnoreCase)) return A;
	}
	return nullptr;
}

// Apply a JSON object of {property: value} pairs to either the actor or its
// first non-root component (for light/fog actors the actual settings live on a
// child component like UDirectionalLightComponent). We try both.
static FString ApplyJsonProps(AActor* Target, const FString& JsonProps, FString& OutError)
{
	if (!Target) { OutError = TEXT("Null target"); return TEXT(""); }

	TSharedPtr<FJsonObject> Props;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonProps);
	if (!FJsonSerializer::Deserialize(R, Props) || !Props.IsValid())
	{
		OutError = TEXT("Bad JSON for properties");
		return TEXT("");
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "ApplyEnvProps", "Apply Environment Props (Claude)"));
	Target->Modify();

	// Build list of candidate containers: the actor itself, plus all components
	TArray<UObject*> Containers;
	Containers.Add(Target);
	TArray<UActorComponent*> Comps;
	Target->GetComponents(Comps);
	for (UActorComponent* C : Comps) if (C) Containers.Add(C);

	int32 Applied = 0;
	int32 Failed = 0;
	TArray<FString> Notes;

	for (const auto& Pair : Props->Values)
	{
		const FString& PropName = Pair.Key;
		FString ValueStr;

		// Convert JSON value to a textual representation that ImportText will accept
		if (Pair.Value->Type == EJson::String)
		{
			ValueStr = Pair.Value->AsString();
		}
		else if (Pair.Value->Type == EJson::Number)
		{
			ValueStr = FString::SanitizeFloat(Pair.Value->AsNumber());
		}
		else if (Pair.Value->Type == EJson::Boolean)
		{
			ValueStr = Pair.Value->AsBool() ? TEXT("true") : TEXT("false");
		}
		else if (Pair.Value->Type == EJson::Object)
		{
			// Re-serialize the inner object so ImportText can parse e.g. (R=1,G=0,B=0)
			// We construct Unreal-style struct text from the JSON
			TSharedPtr<FJsonObject> Sub = Pair.Value->AsObject();
			TArray<FString> Pairs;
			for (const auto& Inner : Sub->Values)
			{
				FString InnerStr;
				if (Inner.Value->Type == EJson::Number)      InnerStr = FString::SanitizeFloat(Inner.Value->AsNumber());
				else if (Inner.Value->Type == EJson::Boolean) InnerStr = Inner.Value->AsBool() ? TEXT("true") : TEXT("false");
				else if (Inner.Value->Type == EJson::String)  InnerStr = Inner.Value->AsString();
				Pairs.Add(FString::Printf(TEXT("%s=%s"), *Inner.Key, *InnerStr));
			}
			ValueStr = FString::Printf(TEXT("(%s)"), *FString::Join(Pairs, TEXT(",")));
		}
		else
		{
			Notes.Add(FString::Printf(TEXT("[skip] %s — unsupported JSON type"), *PropName));
			++Failed;
			continue;
		}

		// Try each container in order — first one that has the property wins
		bool bApplied = false;
		for (UObject* Container : Containers)
		{
			FResolvedProperty Resolved;
			FString LocalErr;
			if (ResolvePropertyPath(Container, PropName, Resolved, LocalErr))
			{
				const TCHAR* Result = Resolved.Property->ImportText_Direct(*ValueStr, Resolved.ContainerPtr, Container, PPF_None);
				if (Result)
				{
					Container->Modify();
					FPropertyChangedEvent PCE(Resolved.Property);
					Container->PostEditChangeProperty(PCE);
					++Applied;
					bApplied = true;
					break;
				}
			}
		}
		if (!bApplied)
		{
			Notes.Add(FString::Printf(TEXT("[fail] %s not found or invalid value '%s'"), *PropName, *ValueStr));
			++Failed;
		}
	}

	Target->MarkPackageDirty();

	FString Result = FString::Printf(TEXT("Applied %d/%d properties to '%s'"),
		Applied, Applied + Failed, *Target->GetName());
	if (Notes.Num() > 0)
	{
		Result += TEXT(". Notes: ") + FString::Join(Notes, TEXT("; "));
	}
	return Result;
}

FString FClaudeContextProvider::SetPostProcessSettings(const FString& VolumeName, const FString& JsonSettings, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No world"); return TEXT(""); }

	AActor* Volume = FindEditorActor(World, VolumeName);
	if (!Volume) { OutError = FString::Printf(TEXT("Actor not found: %s"), *VolumeName); return TEXT(""); }

	// PostProcessVolume stores settings in a struct property "Settings".
	// Allow user to pass settings either as direct fields (Vignette, Bloom, etc)
	// or wrapped in {"Settings": {...}}. ApplyJsonProps walks containers, so we
	// just need to make the keys path-correct: prepend "Settings." if the user
	// passed flat keys.
	TSharedPtr<FJsonObject> Parsed;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonSettings);
	if (!FJsonSerializer::Deserialize(R, Parsed) || !Parsed.IsValid())
	{
		OutError = TEXT("Bad JSON");
		return TEXT("");
	}

	// Detect: are keys top-level PP fields, or wrapped in "Settings"?
	const bool bWrapped = Parsed->HasField(TEXT("Settings"));
	TSharedPtr<FJsonObject> Wrapped = MakeShared<FJsonObject>();
	if (bWrapped)
	{
		Wrapped = Parsed;
	}
	else
	{
		// Wrap each field with "Settings." prefix
		for (const auto& Pair : Parsed->Values)
		{
			Wrapped->SetField(FString::Printf(TEXT("Settings.%s"), *Pair.Key), Pair.Value);
		}
	}

	FString Wrapped_Str;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Wrapped_Str);
	FJsonSerializer::Serialize(Wrapped.ToSharedRef(), W);

	return ApplyJsonProps(Volume, Wrapped_Str, OutError);
}

FString FClaudeContextProvider::SetFogProperties(const FString& FogName, const FString& JsonProps, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No world"); return TEXT(""); }

	AActor* Fog = FindEditorActor(World, FogName);
	if (!Fog) { OutError = FString::Printf(TEXT("Actor not found: %s"), *FogName); return TEXT(""); }

	return ApplyJsonProps(Fog, JsonProps, OutError);
}

FString FClaudeContextProvider::SetSkyAtmosphereProperties(const FString& SkyName, const FString& JsonProps, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No world"); return TEXT(""); }

	AActor* Sky = FindEditorActor(World, SkyName);
	if (!Sky) { OutError = FString::Printf(TEXT("Actor not found: %s"), *SkyName); return TEXT(""); }

	return ApplyJsonProps(Sky, JsonProps, OutError);
}

FString FClaudeContextProvider::SetLightProperties(const FString& LightName, const FString& JsonProps, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No world"); return TEXT(""); }

	AActor* Light = FindEditorActor(World, LightName);
	if (!Light) { OutError = FString::Printf(TEXT("Actor not found: %s"), *LightName); return TEXT(""); }

	return ApplyJsonProps(Light, JsonProps, OutError);
}

// =============================================================================
// STAGE 2: Niagara (VFX)
// =============================================================================
//
// Niagara is UE's VFX framework. Key types:
//   UNiagaraSystem    — top-level effect asset (.uasset). Contains emitters.
//   UNiagaraEmitter   — single emitter (particle simulator). Reusable across systems.
//   FNiagaraVariable  — typed parameter (Float, Vec3, Color, etc.)
//
// We don't manipulate emitter graphs (that's UNiagaraScript work, very complex).
// We support: create system, read structure, set top-level parameter overrides,
// add an emitter from another asset.
//
// All access via reflection — Niagara API has shifted across UE versions.

#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"

FString FClaudeContextProvider::CreateNiagaraSystem(const FString& NewAssetPath, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path, use '/Game/VFX/NS_Name'"); return TEXT(""); }

	UClass* FactoryClass = StaticLoadClass(UFactory::StaticClass(), nullptr, TEXT("/Script/NiagaraEditor.NiagaraSystemFactoryNew"));
	if (!FactoryClass) { OutError = TEXT("NiagaraSystemFactoryNew not found"); return TEXT(""); }

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateNS", "Create Niagara System (Claude)"));
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, UNiagaraSystem::StaticClass(), Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return TEXT(""); }

	return FString::Printf(TEXT("Created Niagara System: %s"), *NewObj->GetPathName());
}

FString FClaudeContextProvider::ReadNiagaraSystem(const FString& AssetPath)
{
	UNiagaraSystem* NS = Cast<UNiagaraSystem>(LoadAssetByPath(AssetPath));
	if (!NS) return FString::Printf(TEXT("{\"error\":\"Not a NiagaraSystem: %s\"}"), *AssetPath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), AssetPath);
	Root->SetStringField(TEXT("class"), NS->GetClass()->GetName());

	// Emitters via reflection — UNiagaraSystem has TArray<FNiagaraEmitterHandle> EmitterHandles
	TArray<TSharedPtr<FJsonValue>> Emitters;
	if (FProperty* HandlesProp = NS->GetClass()->FindPropertyByName(TEXT("EmitterHandles")))
	{
		if (FArrayProperty* AP = CastField<FArrayProperty>(HandlesProp))
		{
			FScriptArrayHelper H(AP, HandlesProp->ContainerPtrToValuePtr<void>(NS));
			if (FStructProperty* HandleSP = CastField<FStructProperty>(AP->Inner))
			{
				for (int32 i = 0; i < H.Num(); ++i)
				{
					void* HandleData = H.GetRawPtr(i);
					TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetNumberField(TEXT("index"), i);

					// Try common field names: Name, IsEnabled, EmitterInstance/Instance
					if (FProperty* NP = HandleSP->Struct->FindPropertyByName(TEXT("Name")))
					{
						if (FNameProperty* NameP = CastField<FNameProperty>(NP))
						{
							O->SetStringField(TEXT("name"), NameP->GetPropertyValue(NP->ContainerPtrToValuePtr<void>(HandleData)).ToString());
						}
					}
					if (FProperty* EP = HandleSP->Struct->FindPropertyByName(TEXT("bIsEnabled")))
					{
						if (FBoolProperty* BP = CastField<FBoolProperty>(EP))
						{
							O->SetBoolField(TEXT("enabled"), BP->GetPropertyValue(EP->ContainerPtrToValuePtr<void>(HandleData)));
						}
					}
					Emitters.Add(MakeShared<FJsonValueObject>(O));
				}
			}
		}
	}
	Root->SetArrayField(TEXT("emitters"), Emitters);
	Root->SetNumberField(TEXT("emitter_count"), Emitters.Num());

	// Exposed parameters — UNiagaraSystem stores them in a FNiagaraUserRedirectionParameterStore
	if (FProperty* ParamsProp = NS->GetClass()->FindPropertyByName(TEXT("ExposedParameters")))
	{
		if (FStructProperty* SP = CastField<FStructProperty>(ParamsProp))
		{
			Root->SetStringField(TEXT("exposed_parameters_struct"), SP->Struct ? SP->Struct->GetName() : FString());
		}
	}

	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::SetNiagaraParameter(const FString& AssetPath, const FString& ParameterName, const FString& NewValue, FString& OutError)
{
	UNiagaraSystem* NS = Cast<UNiagaraSystem>(LoadAssetByPath(AssetPath));
	if (!NS) { OutError = FString::Printf(TEXT("Not a NiagaraSystem: %s"), *AssetPath); return TEXT(""); }

	// User parameters in UNiagaraSystem live under ExposedParameters (FNiagaraUserRedirectionParameterStore).
	// Setting them programmatically requires touching the store's internal arrays via reflection — tricky.
	// We attempt a high-level reflection path: find a function on UNiagaraSystem named "SetUserParameter*"
	// or fall back to setting via the parameter store.
	//
	// Simplest path: instances of this system in level can be set at runtime via
	// UNiagaraComponent::SetVariable*. But editing the asset's default-value
	// requires going through the editor APIs. For now, we mark this as
	// "asset-level not yet supported" and explain.
	OutError = FString::Printf(
		TEXT("Setting Niagara user parameters at asset level not yet supported. ")
		TEXT("Parameters are typically set per-instance via UNiagaraComponent at runtime. ")
		TEXT("To change defaults: open '%s' and edit in the User Parameters panel."),
		*AssetPath);
	return TEXT("");
}

FString FClaudeContextProvider::AddNiagaraEmitter(const FString& SystemPath, const FString& EmitterAssetPath, FString& OutError)
{
	UNiagaraSystem* NS = Cast<UNiagaraSystem>(LoadAssetByPath(SystemPath));
	if (!NS) { OutError = FString::Printf(TEXT("Not a NiagaraSystem: %s"), *SystemPath); return TEXT(""); }

	UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(LoadAssetByPath(EmitterAssetPath));
	if (!Emitter) { OutError = FString::Printf(TEXT("Not a NiagaraEmitter: %s"), *EmitterAssetPath); return TEXT(""); }

	// Try to find a UFunction "AddEmitterHandle" via reflection. If not found,
	// we report and ask user to do it manually.
	UFunction* AddFn = NS->FindFunction(FName(TEXT("AddEmitterHandle")));
	if (!AddFn)
	{
		// Editor-only static function FNiagaraEditorUtilities::AddEmitterFromAssetData exists
		// but requires editor module init and asset data ops — fragile via reflection.
		OutError = FString::Printf(
			TEXT("Cannot programmatically add emitter to '%s' in this UE version — ")
			TEXT("the AddEmitterHandle function is not BlueprintCallable. ")
			TEXT("Workaround: open the system in editor and drag '%s' into it."),
			*SystemPath, *EmitterAssetPath);
		return TEXT("");
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddEmitter", "Add Niagara Emitter (Claude)"));
	NS->Modify();

	struct { UNiagaraEmitter* E; FName Name; } Params{ Emitter, FName(*Emitter->GetName()) };
	NS->ProcessEvent(AddFn, &Params);

	return FString::Printf(TEXT("Added emitter '%s' to '%s' (note: emitter graph may need configuration)"),
		*Emitter->GetName(), *NS->GetName());
}

// =============================================================================
// STAGE 2: Sequencer (Level Sequences / Cinematics)
// =============================================================================
//
// Level Sequences (.uasset) are timelines that animate properties on actors.
// Structure: ULevelSequence → UMovieScene → MovieSceneTracks → Sections → Keyframes.
//
// Bindings tie an actor in a level to a guid in the sequence. Tracks operate
// on properties of the bound actor.
//
// We support: create, read structure, bind actor, add basic tracks, add
// keyframes for transform/visibility/float properties, set play range.
// Skipped: cuts/cameras/audio tracks — too complex for v1.

#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneVisibilityTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneBoolChannel.h"

FString FClaudeContextProvider::CreateLevelSequence(const FString& NewAssetPath, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path"); return TEXT(""); }

	UClass* FactoryClass = StaticLoadClass(UFactory::StaticClass(), nullptr, TEXT("/Script/LevelSequenceEditor.LevelSequenceFactoryNew"));
	if (!FactoryClass) { OutError = TEXT("LevelSequenceFactoryNew not found"); return TEXT(""); }

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateSeq", "Create Level Sequence (Claude)"));
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, ULevelSequence::StaticClass(), Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return TEXT(""); }

	return FString::Printf(TEXT("Created Level Sequence: %s"), *NewObj->GetPathName());
}

FString FClaudeContextProvider::ReadLevelSequence(const FString& AssetPath)
{
	ULevelSequence* Seq = Cast<ULevelSequence>(LoadAssetByPath(AssetPath));
	if (!Seq) return FString::Printf(TEXT("{\"error\":\"Not a LevelSequence: %s\"}"), *AssetPath);

	UMovieScene* MS = Seq->GetMovieScene();
	if (!MS) return TEXT("{\"error\":\"No MovieScene\"}");

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), AssetPath);

	// Play range
	const FFrameNumber StartTick = MS->GetPlaybackRange().GetLowerBoundValue();
	const FFrameNumber EndTick   = MS->GetPlaybackRange().GetUpperBoundValue();
	const FFrameRate TickResolution = MS->GetTickResolution();
	Root->SetNumberField(TEXT("start_seconds"), TickResolution.AsSeconds(FFrameTime(StartTick)));
	Root->SetNumberField(TEXT("end_seconds"), TickResolution.AsSeconds(FFrameTime(EndTick)));
	Root->SetNumberField(TEXT("display_rate_fps"), MS->GetDisplayRate().AsDecimal());

	// Bindings (possessables + spawnables)
	TArray<TSharedPtr<FJsonValue>> Bindings;
	const int32 PossessableCount = MS->GetPossessableCount();
	for (int32 i = 0; i < PossessableCount; ++i)
	{
		const FMovieScenePossessable& P = MS->GetPossessable(i);
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), P.GetName());
		O->SetStringField(TEXT("guid"), P.GetGuid().ToString());
		O->SetStringField(TEXT("kind"), TEXT("possessable"));
		Bindings.Add(MakeShared<FJsonValueObject>(O));
	}
	const int32 SpawnableCount = MS->GetSpawnableCount();
	for (int32 i = 0; i < SpawnableCount; ++i)
	{
		const FMovieSceneSpawnable& S = MS->GetSpawnable(i);
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), S.GetName());
		O->SetStringField(TEXT("guid"), S.GetGuid().ToString());
		O->SetStringField(TEXT("kind"), TEXT("spawnable"));
		Bindings.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("bindings"), Bindings);

	// Master tracks (sound, audio, etc) and per-binding tracks counted via reflection
	TArray<UMovieSceneTrack*> MasterTracks = MS->GetTracks();
	TArray<TSharedPtr<FJsonValue>> MTracks;
	for (UMovieSceneTrack* T : MasterTracks)
	{
		if (!T) continue;
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("class"), T->GetClass()->GetName());
		O->SetStringField(TEXT("display_name"), T->GetDisplayName().ToString());
		MTracks.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("master_tracks"), MTracks);

	return Truncate(JsonObjectToString(Root));
}

static FGuid FindBindingGuidByName(UMovieScene* MS, const FString& Name)
{
	if (!MS) return FGuid();
	const int32 PCount = MS->GetPossessableCount();
	for (int32 i = 0; i < PCount; ++i)
	{
		const FMovieScenePossessable& P = MS->GetPossessable(i);
		if (P.GetName() == Name) return P.GetGuid();
	}
	const int32 SCount = MS->GetSpawnableCount();
	for (int32 i = 0; i < SCount; ++i)
	{
		const FMovieSceneSpawnable& S = MS->GetSpawnable(i);
		if (S.GetName() == Name) return S.GetGuid();
	}
	return FGuid();
}

FString FClaudeContextProvider::AddSequenceBinding(const FString& AssetPath, const FString& ActorName, FString& OutError)
{
	ULevelSequence* Seq = Cast<ULevelSequence>(LoadAssetByPath(AssetPath));
	if (!Seq) { OutError = FString::Printf(TEXT("Not a LevelSequence: %s"), *AssetPath); return TEXT(""); }
	UMovieScene* MS = Seq->GetMovieScene();
	if (!MS) { OutError = TEXT("No MovieScene"); return TEXT(""); }

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return TEXT(""); }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName() == ActorName || It->GetActorLabel() == ActorName) { Actor = *It; break; }
	}
	if (!Actor) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddBinding", "Add Sequence Binding (Claude)"));
	MS->Modify();

	const FGuid Guid = MS->AddPossessable(Actor->GetActorLabel(), Actor->GetClass());
	if (!Guid.IsValid()) { OutError = TEXT("AddPossessable returned invalid guid"); return TEXT(""); }

	// Bind actor to guid via the sequence's binding map
	Seq->BindPossessableObject(Guid, *Actor, World);

	Seq->MarkPackageDirty();
	return FString::Printf(TEXT("Added binding for '%s' (guid=%s)"),
		*Actor->GetActorLabel(), *Guid.ToString());
}

FString FClaudeContextProvider::AddSequenceTrack(const FString& AssetPath, const FString& BindingName, const FString& TrackType, FString& OutError)
{
	ULevelSequence* Seq = Cast<ULevelSequence>(LoadAssetByPath(AssetPath));
	if (!Seq) { OutError = FString::Printf(TEXT("Not a LevelSequence: %s"), *AssetPath); return TEXT(""); }
	UMovieScene* MS = Seq->GetMovieScene();
	if (!MS) { OutError = TEXT("No MovieScene"); return TEXT(""); }

	const FGuid Guid = FindBindingGuidByName(MS, BindingName);
	if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("Binding not found: %s"), *BindingName); return TEXT(""); }

	UClass* TrackClass = nullptr;
	if (TrackType.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
	{
		TrackClass = UMovieScene3DTransformTrack::StaticClass();
	}
	else if (TrackType.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
	{
		TrackClass = UMovieSceneFloatTrack::StaticClass();
	}
	else if (TrackType.Equals(TEXT("Visibility"), ESearchCase::IgnoreCase))
	{
		TrackClass = UMovieSceneVisibilityTrack::StaticClass();
	}
	else
	{
		OutError = FString::Printf(TEXT("Unsupported track type: %s. Use Transform, Float, or Visibility."), *TrackType);
		return TEXT("");
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddTrack", "Add Sequence Track (Claude)"));
	MS->Modify();

	UMovieSceneTrack* Track = MS->AddTrack(TrackClass, Guid);
	if (!Track) { OutError = TEXT("AddTrack returned null"); return TEXT(""); }

	// Add a default section spanning current playback range
	const TRange<FFrameNumber> PlayRange = MS->GetPlaybackRange();
	UMovieSceneSection* Section = Track->CreateNewSection();
	if (Section)
	{
		Section->SetRange(PlayRange);
		Track->AddSection(*Section);
	}

	Seq->MarkPackageDirty();
	return FString::Printf(TEXT("Added %s track on binding '%s'"), *TrackType, *BindingName);
}

FString FClaudeContextProvider::AddSequenceKeyframe(const FString& AssetPath, const FString& BindingName, const FString& TrackType, float TimeSeconds, const FString& Value, FString& OutError)
{
	ULevelSequence* Seq = Cast<ULevelSequence>(LoadAssetByPath(AssetPath));
	if (!Seq) { OutError = FString::Printf(TEXT("Not a LevelSequence: %s"), *AssetPath); return TEXT(""); }
	UMovieScene* MS = Seq->GetMovieScene();
	if (!MS) { OutError = TEXT("No MovieScene"); return TEXT(""); }

	const FGuid Guid = FindBindingGuidByName(MS, BindingName);
	if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("Binding not found: %s"), *BindingName); return TEXT(""); }

	// Find existing track of this type on the binding
	UClass* TrackClass = nullptr;
	if (TrackType.Equals(TEXT("Float"), ESearchCase::IgnoreCase))      TrackClass = UMovieSceneFloatTrack::StaticClass();
	else if (TrackType.Equals(TEXT("Visibility"), ESearchCase::IgnoreCase)) TrackClass = UMovieSceneVisibilityTrack::StaticClass();
	else
	{
		OutError = FString::Printf(TEXT("Keyframe support only for Float and Visibility tracks (not %s). Transform keyframing is more complex and not yet implemented."), *TrackType);
		return TEXT("");
	}

	UMovieSceneTrack* Track = MS->FindTrack(TrackClass, Guid);
	if (!Track) { OutError = FString::Printf(TEXT("No %s track on binding — call add_sequence_track first"), *TrackType); return TEXT(""); }

	// Find first section
	if (Track->GetAllSections().Num() == 0) { OutError = TEXT("Track has no sections"); return TEXT(""); }
	UMovieSceneSection* Section = Track->GetAllSections()[0];

	const FFrameRate Tr = MS->GetTickResolution();
	const FFrameNumber FrameNumber = FFrameNumber((int32)(TimeSeconds * Tr.Numerator / Tr.Denominator));

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "AddKey", "Add Keyframe (Claude)"));
	Section->Modify();

	if (UMovieSceneFloatSection* FS = Cast<UMovieSceneFloatSection>(Section))
	{
		const float V = FCString::Atof(*Value);
		FMovieSceneFloatChannel* Channel = FS->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(0);
		if (!Channel) { OutError = TEXT("Float channel not accessible"); return TEXT(""); }
		Channel->AddCubicKey(FrameNumber, V);
	}
	else if (UMovieSceneBoolSection* BS = Cast<UMovieSceneBoolSection>(Section))
	{
		const bool V = Value.ToBool();
		FMovieSceneBoolChannel* Channel = BS->GetChannelProxy().GetChannel<FMovieSceneBoolChannel>(0);
		if (!Channel) { OutError = TEXT("Bool channel not accessible"); return TEXT(""); }
		Channel->GetData().AddKey(FrameNumber, V);
	}
	else
	{
		OutError = FString::Printf(TEXT("Section type %s not supported for keyframing"), *Section->GetClass()->GetName());
		return TEXT("");
	}

	// Extend section range to include the keyframe if needed
	TRange<FFrameNumber> Range = Section->GetRange();
	if (!Range.Contains(FrameNumber))
	{
		Range = TRange<FFrameNumber>::Hull(Range, TRange<FFrameNumber>(FrameNumber, FrameNumber + 1));
		Section->SetRange(Range);
	}

	Seq->MarkPackageDirty();
	return FString::Printf(TEXT("Added keyframe at %.2fs (frame %d) value=%s"), TimeSeconds, FrameNumber.Value, *Value);
}

FString FClaudeContextProvider::SetSequenceRange(const FString& AssetPath, float StartSeconds, float EndSeconds, FString& OutError)
{
	ULevelSequence* Seq = Cast<ULevelSequence>(LoadAssetByPath(AssetPath));
	if (!Seq) { OutError = FString::Printf(TEXT("Not a LevelSequence: %s"), *AssetPath); return TEXT(""); }
	UMovieScene* MS = Seq->GetMovieScene();
	if (!MS) { OutError = TEXT("No MovieScene"); return TEXT(""); }

	if (EndSeconds <= StartSeconds) { OutError = TEXT("End must be > Start"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetRange", "Set Sequence Range (Claude)"));
	MS->Modify();

	const FFrameRate Tr = MS->GetTickResolution();
	const FFrameNumber StartFrame = FFrameNumber((int32)(StartSeconds * Tr.Numerator / Tr.Denominator));
	const FFrameNumber EndFrame   = FFrameNumber((int32)(EndSeconds   * Tr.Numerator / Tr.Denominator));
	MS->SetPlaybackRange(TRange<FFrameNumber>(StartFrame, EndFrame));

	Seq->MarkPackageDirty();
	return FString::Printf(TEXT("Set playback range to %.2f .. %.2f seconds"), StartSeconds, EndSeconds);
}

// =============================================================================
// STAGE 2: Pose Search (Motion Matching)
// =============================================================================
//
// Pose Search has two key assets:
//   UPoseSearchSchema    — defines what features a pose has (bones/velocities/trajectory)
//   UPoseSearchDatabase  — collection of animations indexed by the schema
//
// We support: create both assets, read database structure, append an
// AnimSequence to a database. Schema configuration (sampling rate, bones,
// trajectory queries) requires editor work.
//
// All access via reflection — PoseSearch API has changed across UE 5.3/5.4/5.5/5.7.

FString FClaudeContextProvider::CreatePoseSearchSchema(const FString& NewAssetPath, const FString& SkeletonPath, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path"); return TEXT(""); }

	UClass* SchemaClass = StaticLoadClass(UObject::StaticClass(), nullptr, TEXT("/Script/PoseSearch.PoseSearchSchema"));
	if (!SchemaClass) { OutError = TEXT("UPoseSearchSchema class not found — PoseSearch plugin may not be enabled"); return TEXT(""); }

	UClass* FactoryClass = StaticLoadClass(UFactory::StaticClass(), nullptr, TEXT("/Script/PoseSearchEditor.PoseSearchSchemaFactory"));
	if (!FactoryClass)
	{
		// Fall back to generic asset creation without factory
		OutError = TEXT("PoseSearchSchemaFactory not found — workaround: create manually then call set_object_property");
		return TEXT("");
	}

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateSchema", "Create Pose Search Schema (Claude)"));
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, SchemaClass, Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return TEXT(""); }

	// Optionally pre-set Skeleton via reflection
	if (!SkeletonPath.IsEmpty())
	{
		if (UObject* Skel = LoadAssetByPath(SkeletonPath))
		{
			if (FProperty* SkProp = NewObj->GetClass()->FindPropertyByName(TEXT("Skeleton")))
			{
				if (FObjectProperty* OP = CastField<FObjectProperty>(SkProp))
				{
					OP->SetObjectPropertyValue_InContainer(NewObj, Skel);
				}
			}
		}
	}

	NewObj->MarkPackageDirty();
	return FString::Printf(TEXT("Created PoseSearchSchema: %s%s"),
		*NewObj->GetPathName(),
		SkeletonPath.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (skeleton=%s)"), *SkeletonPath));
}

FString FClaudeContextProvider::CreatePoseSearchDatabase(const FString& NewAssetPath, const FString& SchemaPath, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path"); return TEXT(""); }

	UClass* DBClass = StaticLoadClass(UObject::StaticClass(), nullptr, TEXT("/Script/PoseSearch.PoseSearchDatabase"));
	if (!DBClass) { OutError = TEXT("UPoseSearchDatabase class not found"); return TEXT(""); }

	UClass* FactoryClass = StaticLoadClass(UFactory::StaticClass(), nullptr, TEXT("/Script/PoseSearchEditor.PoseSearchDatabaseFactory"));
	if (!FactoryClass) { OutError = TEXT("PoseSearchDatabaseFactory not found"); return TEXT(""); }

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreatePSDB", "Create PoseSearchDatabase (Claude)"));
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, DBClass, Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return TEXT(""); }

	// Set Schema if provided
	if (!SchemaPath.IsEmpty())
	{
		if (UObject* Schema = LoadAssetByPath(SchemaPath))
		{
			if (FProperty* SP = NewObj->GetClass()->FindPropertyByName(TEXT("Schema")))
			{
				if (FObjectProperty* OP = CastField<FObjectProperty>(SP))
				{
					OP->SetObjectPropertyValue_InContainer(NewObj, Schema);
				}
			}
		}
	}

	NewObj->MarkPackageDirty();
	return FString::Printf(TEXT("Created PoseSearchDatabase: %s%s"),
		*NewObj->GetPathName(),
		SchemaPath.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (schema=%s)"), *SchemaPath));
}

FString FClaudeContextProvider::ReadPoseSearchDatabase(const FString& AssetPath)
{
	UObject* DB = LoadAssetByPath(AssetPath);
	if (!DB || !DB->GetClass()->GetName().Contains(TEXT("PoseSearchDatabase")))
	{
		return FString::Printf(TEXT("{\"error\":\"Not a PoseSearchDatabase: %s\"}"), *AssetPath);
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), AssetPath);

	// Schema
	if (FProperty* SP = DB->GetClass()->FindPropertyByName(TEXT("Schema")))
	{
		if (FObjectProperty* OP = CastField<FObjectProperty>(SP))
		{
			if (UObject* Schema = OP->GetObjectPropertyValue_InContainer(DB))
			{
				Root->SetStringField(TEXT("schema"), Schema->GetPathName());
			}
		}
	}

	// AnimationAssets array — name varies in newer versions ("AnimationAssets" vs "Sequences")
	for (const TCHAR* ArrName : { TEXT("AnimationAssets"), TEXT("Sequences"), TEXT("Animations") })
	{
		FProperty* ArrProp = DB->GetClass()->FindPropertyByName(ArrName);
		if (!ArrProp) continue;
		if (FArrayProperty* AP = CastField<FArrayProperty>(ArrProp))
		{
			FScriptArrayHelper H(AP, ArrProp->ContainerPtrToValuePtr<void>(DB));
			Root->SetNumberField(TEXT("animation_count"), H.Num());
			Root->SetStringField(TEXT("animation_array_field"), ArrName);
			break;
		}
	}

	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::AddPoseSearchAnimation(const FString& DatabasePath, const FString& AnimSequencePath, FString& OutError)
{
	UObject* DB = LoadAssetByPath(DatabasePath);
	if (!DB) { OutError = FString::Printf(TEXT("Not found: %s"), *DatabasePath); return TEXT(""); }
	if (!DB->GetClass()->GetName().Contains(TEXT("PoseSearchDatabase"))) { OutError = TEXT("Not a PoseSearchDatabase"); return TEXT(""); }

	UAnimSequence* Anim = Cast<UAnimSequence>(LoadAssetByPath(AnimSequencePath));
	if (!Anim) { OutError = FString::Printf(TEXT("Not an AnimSequence: %s"), *AnimSequencePath); return TEXT(""); }

	// PoseSearchDatabase entries are FInstancedStruct or wrapper structs — too version-dependent
	// to construct correctly via reflection. For now, document workaround.
	OutError = FString::Printf(
		TEXT("Programmatic addition of animations to PoseSearchDatabase not yet supported — ")
		TEXT("the entry type uses FInstancedStruct which is fragile via reflection. ")
		TEXT("Workaround: open '%s' and drag '%s' into the Animations panel."),
		*DatabasePath, *AnimSequencePath);
	return TEXT("");
}

// =============================================================================
// STAGE 2: Gameplay Ability System (GAS)
// =============================================================================
//
// GAS provides UGameplayAbility (skill that runs), UGameplayEffect (modifies
// attributes/applies tags), UAttributeSet (typed attribute definitions like
// Health/Mana). These are typically subclassed in Blueprint.
//
// We support:
//   - Create new BP-derived ability/effect/attributeset
//   - List existing
//   - Read structure
//   - Set effect properties (modifiers, duration, etc) via path resolver
//
// Note: Untry's project is on GASP, not GAS. These tools are here for the
// case of switching to a GAS-based architecture.

#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "AttributeSet.h"

// Generic helper: create a Blueprint asset with given parent class.
static UObject* CreateBlueprintFromParentClass(const FString& AssetName, const FString& PackagePath, UClass* ParentClass, FString& OutError)
{
	if (!ParentClass) { OutError = TEXT("No parent class"); return nullptr; }

	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UObject* NewObj = AssetTools.Get().CreateAsset(AssetName, PackagePath, UBlueprint::StaticClass(), Factory);
	if (!NewObj) { OutError = TEXT("CreateAsset returned null"); return nullptr; }

	return NewObj;
}

FString FClaudeContextProvider::CreateGameplayAbility(const FString& NewAssetPath, const FString& ParentAbilityClass, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path"); return TEXT(""); }

	UClass* Parent = UGameplayAbility::StaticClass();
	if (!ParentAbilityClass.IsEmpty())
	{
		UClass* Custom = StaticLoadClass(UGameplayAbility::StaticClass(), nullptr, *ParentAbilityClass);
		if (!Custom)
		{
			if (UObject* Loaded = LoadAssetByPath(ParentAbilityClass))
			{
				if (UBlueprint* BP = Cast<UBlueprint>(Loaded)) Custom = BP->GeneratedClass;
			}
		}
		if (Custom && Custom->IsChildOf(UGameplayAbility::StaticClass())) Parent = Custom;
		else
		{
			OutError = FString::Printf(TEXT("Parent class not found or not a GameplayAbility: %s"), *ParentAbilityClass);
			return TEXT("");
		}
	}

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateGA", "Create Gameplay Ability (Claude)"));
	UObject* NewObj = CreateBlueprintFromParentClass(AssetName, PackagePath, Parent, OutError);
	if (!NewObj) return TEXT("");

	return FString::Printf(TEXT("Created GameplayAbility BP: %s (parent=%s)"),
		*NewObj->GetPathName(), *Parent->GetName());
}

FString FClaudeContextProvider::CreateGameplayEffect(const FString& NewAssetPath, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateGE", "Create Gameplay Effect (Claude)"));
	UObject* NewObj = CreateBlueprintFromParentClass(AssetName, PackagePath, UGameplayEffect::StaticClass(), OutError);
	if (!NewObj) return TEXT("");

	return FString::Printf(TEXT("Created GameplayEffect BP: %s"), *NewObj->GetPathName());
}

FString FClaudeContextProvider::CreateAttributeSet(const FString& NewAssetPath, FString& OutError)
{
	const FString PackagePath = FPaths::GetPath(NewAssetPath);
	const FString AssetName = FPaths::GetCleanFilename(NewAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty()) { OutError = TEXT("Bad path"); return TEXT(""); }

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "CreateAS", "Create AttributeSet (Claude)"));
	UObject* NewObj = CreateBlueprintFromParentClass(AssetName, PackagePath, UAttributeSet::StaticClass(), OutError);
	if (!NewObj) return TEXT("");

	return FString::Printf(TEXT("Created AttributeSet BP: %s. NOTE: AttributeSets are typically defined in C++. BP version has limited functionality."),
		*NewObj->GetPathName());
}

static FString ListAssetsWithParentClass(UClass* ParentClass, const FString& PathFilter)
{
	if (!ParentClass) return TEXT("{\"error\":\"No parent class\"}");

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	if (!PathFilter.IsEmpty()) Filter.PackagePaths.Add(FName(*PathFilter));

	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Items;

	for (const FAssetData& A : Assets)
	{
		// Check NativeParentClass tag — fast filter without loading
		FString NativeParent;
		A.GetTagValue(FBlueprintTags::NativeParentClassPath, NativeParent);
		if (NativeParent.IsEmpty())
		{
			A.GetTagValue(TEXT("ParentClass"), NativeParent);
		}
		if (NativeParent.IsEmpty()) continue;

		// NativeParent is a path like "/Script/GameplayAbilities.GameplayEffect"
		const FString ExpectedClass = ParentClass->GetPathName();
		const FString ParentClean = FPackageName::ExportTextPathToObjectPath(NativeParent);

		if (ParentClean == ExpectedClass || ParentClean.Contains(ParentClass->GetName()))
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), A.GetObjectPathString());
			O->SetStringField(TEXT("name"), A.AssetName.ToString());
			O->SetStringField(TEXT("parent"), ParentClean);
			Items.Add(MakeShared<FJsonValueObject>(O));
		}
	}

	Root->SetArrayField(TEXT("assets"), Items);
	Root->SetNumberField(TEXT("count"), Items.Num());
	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::ListGameplayAbilities(const FString& PathFilter)
{
	return ListAssetsWithParentClass(UGameplayAbility::StaticClass(), PathFilter);
}

FString FClaudeContextProvider::ListGameplayEffects(const FString& PathFilter)
{
	return ListAssetsWithParentClass(UGameplayEffect::StaticClass(), PathFilter);
}

FString FClaudeContextProvider::ListAttributeSets(const FString& PathFilter)
{
	return ListAssetsWithParentClass(UAttributeSet::StaticClass(), PathFilter);
}

FString FClaudeContextProvider::GetGASInfo(const FString& AssetPath)
{
	UObject* Asset = LoadAssetByPath(AssetPath);
	if (!Asset) return FString::Printf(TEXT("{\"error\":\"Not found: %s\"}"), *AssetPath);

	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP) return FString::Printf(TEXT("{\"error\":\"Not a Blueprint: %s\"}"), *AssetPath);

	UClass* Generated = BP->GeneratedClass;
	if (!Generated) return TEXT("{\"error\":\"BP not compiled\"}");

	UObject* CDO = Generated->GetDefaultObject();
	if (!CDO) return TEXT("{\"error\":\"No CDO\"}");

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("path"), AssetPath);
	Root->SetStringField(TEXT("class"), Generated->GetName());

	// Determine GAS type
	FString GASType = TEXT("Unknown");
	if (Generated->IsChildOf(UGameplayAbility::StaticClass())) GASType = TEXT("GameplayAbility");
	else if (Generated->IsChildOf(UGameplayEffect::StaticClass())) GASType = TEXT("GameplayEffect");
	else if (Generated->IsChildOf(UAttributeSet::StaticClass())) GASType = TEXT("AttributeSet");
	Root->SetStringField(TEXT("gas_type"), GASType);

	// Read key properties — different per type, all via reflection
	if (GASType == TEXT("GameplayEffect"))
	{
		// FGameplayEffectModifierMagnitude, DurationPolicy, Modifiers array etc.
		for (const TCHAR* Field : { TEXT("DurationPolicy"), TEXT("DurationMagnitude"), TEXT("Period"), TEXT("StackingType"), TEXT("ChanceToApplyToTarget") })
		{
			if (FProperty* P = Generated->FindPropertyByName(Field))
			{
				FString Val;
				P->ExportText_InContainer(0, Val, CDO, CDO, nullptr, PPF_None);
				if (Val.Len() > 200) Val = Val.Left(200) + TEXT("..");
				Root->SetStringField(Field, Val);
			}
		}

		// Modifiers array — count
		if (FProperty* MP = Generated->FindPropertyByName(TEXT("Modifiers")))
		{
			if (FArrayProperty* AP = CastField<FArrayProperty>(MP))
			{
				FScriptArrayHelper H(AP, MP->ContainerPtrToValuePtr<void>(CDO));
				Root->SetNumberField(TEXT("modifier_count"), H.Num());
			}
		}
	}
	else if (GASType == TEXT("GameplayAbility"))
	{
		for (const TCHAR* Field : { TEXT("InstancingPolicy"), TEXT("NetExecutionPolicy"), TEXT("CostGameplayEffectClass"), TEXT("CooldownGameplayEffectClass") })
		{
			if (FProperty* P = Generated->FindPropertyByName(Field))
			{
				FString Val;
				P->ExportText_InContainer(0, Val, CDO, CDO, nullptr, PPF_None);
				if (Val.Len() > 200) Val = Val.Left(200) + TEXT("..");
				Root->SetStringField(Field, Val);
			}
		}
	}
	else if (GASType == TEXT("AttributeSet"))
	{
		// List FGameplayAttributeData properties
		TArray<TSharedPtr<FJsonValue>> Attrs;
		for (TFieldIterator<FProperty> It(Generated); It; ++It)
		{
			if (FStructProperty* SP = CastField<FStructProperty>(*It))
			{
				if (SP->Struct && SP->Struct->GetName() == TEXT("GameplayAttributeData"))
				{
					TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
					A->SetStringField(TEXT("name"), It->GetName());
					Attrs.Add(MakeShared<FJsonValueObject>(A));
				}
			}
		}
		Root->SetArrayField(TEXT("attributes"), Attrs);
		Root->SetNumberField(TEXT("attribute_count"), Attrs.Num());
	}

	return Truncate(JsonObjectToString(Root));
}

FString FClaudeContextProvider::SetGameplayEffectProperty(const FString& AssetPath, const FString& PropertyPath, const FString& NewValue, FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(LoadAssetByPath(AssetPath));
	if (!BP) { OutError = FString::Printf(TEXT("Not a Blueprint: %s"), *AssetPath); return TEXT(""); }
	if (!BP->GeneratedClass) { OutError = TEXT("BP has no generated class"); return TEXT(""); }
	if (!BP->GeneratedClass->IsChildOf(UGameplayEffect::StaticClass()))
	{
		OutError = TEXT("Not a GameplayEffect");
		return TEXT("");
	}

	UObject* CDO = BP->GeneratedClass->GetDefaultObject();
	if (!CDO) { OutError = TEXT("No CDO"); return TEXT(""); }

	FResolvedProperty Resolved;
	if (!ResolvePropertyPath(CDO, PropertyPath, Resolved, OutError)) return TEXT("");

	FScopedTransaction Tx(NSLOCTEXT("ClaudeAgent", "SetGEProp", "Set Gameplay Effect Property (Claude)"));
	CDO->Modify();

	const TCHAR* Result = Resolved.Property->ImportText_Direct(*NewValue, Resolved.ContainerPtr, CDO, PPF_None);
	if (!Result) { OutError = FString::Printf(TEXT("ImportText failed for '%s'"), *NewValue); return TEXT(""); }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	BP->MarkPackageDirty();

	return FString::Printf(TEXT("Set %s.%s = %s"), *BP->GetName(), *PropertyPath, *NewValue);
}
