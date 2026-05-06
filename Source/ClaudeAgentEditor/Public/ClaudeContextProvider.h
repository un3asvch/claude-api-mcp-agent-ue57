// Copyright Untry. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

class UBlueprint;
class UEdGraph;
class UObject;

/**
 * Static helpers that pull structured data out of the editor for the agent.
 * All methods produce strings (usually JSON or compact text) suitable for
 * including in a tool result.
 */
class FClaudeContextProvider
{
public:
	// Asset Registry
	static FString ListAssets(const FString& ClassName, const FString& PathFilter, int32 MaxResults);
	static FString GetAssetReferences(const FString& AssetPath, bool bReverse);

	// Object inspection
	static FString GetObjectProperties(const FString& AssetPath, int32 MaxDepth);
	static FString SetObjectProperty(const FString& AssetPath, const FString& PropertyName, const FString& NewValue, FString& OutError);

	// Blueprint — read
	static FString GetBlueprintGraph(const FString& AssetPath, const FString& GraphName);
	static FString CompileBlueprint(const FString& AssetPath, FString& OutError);
	static FString ListBlueprintFunctions(const FString& AssetPath);
	static FString GetNodeDetails(const FString& AssetPath, const FString& GraphName, const FString& NodeGuid);

	// Blueprint — write
	static FString CreateBlueprint(const FString& ParentClassPath, const FString& NewAssetPath, FString& OutError);
	static FString CreateWidgetBlueprint(const FString& NewAssetPath, FString& OutError);
	static FString CreateAnimBlueprint(const FString& NewAssetPath, const FString& SkeletonPath, FString& OutError);
	static FString CreateBlueprintInterface(const FString& NewAssetPath, FString& OutError);
	static FString ImplementInterface(const FString& BlueprintPath, const FString& InterfacePath, FString& OutError);
	static FString AddBlueprintFunction(const FString& AssetPath, const FString& FunctionName, FString& OutError);
	static FString AddBlueprintVariable(const FString& AssetPath, const FString& VarName, const FString& VarType, const FString& ContainerType, const FString& DefaultValue, const FString& Category, bool bInstanceEditable, bool bBlueprintReadOnly, FString& OutError);
	static FString RemoveBlueprintVariable(const FString& AssetPath, const FString& VarName, FString& OutError);
	static FString AddComponentToBlueprint(const FString& AssetPath, const FString& ComponentClass, const FString& ComponentName, const FString& AttachParentName, FString& OutError);
	static FString SetComponentProperty(const FString& AssetPath, const FString& ComponentName, const FString& PropertyName, const FString& NewValue, FString& OutError);
	static FString RemoveComponentFromBlueprint(const FString& AssetPath, const FString& ComponentName, FString& OutError);
	static FString SaveAsset(const FString& AssetPath, FString& OutError);

	// Blueprint — graph editing
	static FString AddFunctionCallNode(const FString& AssetPath, const FString& GraphName, const FString& FunctionClass, const FString& FunctionName, int32 X, int32 Y, FString& OutError);
	static FString AddVariableNode(const FString& AssetPath, const FString& GraphName, const FString& VarName, bool bSetter, int32 X, int32 Y, FString& OutError);
	static FString AddBranchNode(const FString& AssetPath, const FString& GraphName, int32 X, int32 Y, FString& OutError);
	static FString AddSequenceNode(const FString& AssetPath, const FString& GraphName, int32 NumOutputs, int32 X, int32 Y, FString& OutError);
	static FString AddCustomEventNode(const FString& AssetPath, const FString& GraphName, const FString& EventName, int32 X, int32 Y, FString& OutError);
	static FString AddMakeBreakStructNode(const FString& AssetPath, const FString& GraphName, const FString& StructName, bool bMake, int32 X, int32 Y, FString& OutError);
	static FString AddCastNode(const FString& AssetPath, const FString& GraphName, const FString& TargetClass, bool bPureCast, int32 X, int32 Y, FString& OutError);
	static FString AddSelfNode(const FString& AssetPath, const FString& GraphName, int32 X, int32 Y, FString& OutError);
	static FString ConnectBlueprintPins(const FString& AssetPath, const FString& GraphName, const FString& FromNodeGuid, const FString& FromPinName, const FString& ToNodeGuid, const FString& ToPinName, FString& OutError);
	static FString SetPinDefaultValue(const FString& AssetPath, const FString& GraphName, const FString& NodeGuid, const FString& PinName, const FString& NewValue, FString& OutError);
	static FString DeleteBlueprintNode(const FString& AssetPath, const FString& GraphName, const FString& NodeGuid, FString& OutError);

	// Level / actor
	static FString SpawnActor(const FString& ClassPath, const FVector& Location, FString& OutError);
	static FString DestroyActor(const FString& ActorPath, FString& OutError);
	static FString GetActorComponentTree(const FString& ActorPathOrBlueprintPath);
	static FString SetActorTransform(const FString& ActorPath, bool bSetLocation, const FVector& Location, bool bSetRotation, const FRotator& Rotation, bool bSetScale, const FVector& Scale, FString& OutError);
	static FString SetActorProperty(const FString& ActorPath, const FString& PropertyName, const FString& NewValue, FString& OutError);
	static FString FindActorsInLevel(const FString& ClassFilter, int32 MaxResults);

	// Editor actions
	static FString OpenAssetEditor(const FString& AssetPath, FString& OutError);

	// AI — Behavior Tree & Blackboard
	static FString CreateBehaviorTree(const FString& NewAssetPath, const FString& BlackboardPath, FString& OutError);
	static FString CreateBlackboard(const FString& NewAssetPath, const FString& ParentBlackboardPath, FString& OutError);
	static FString AddBlackboardKey(const FString& BlackboardPath, const FString& KeyName, const FString& KeyType, const FString& KeyTypeObject, bool bInstanceSynced, FString& OutError);
	static FString RemoveBlackboardKey(const FString& BlackboardPath, const FString& KeyName, FString& OutError);
	static FString GetBlackboardStructure(const FString& BlackboardPath);
	static FString GetBehaviorTreeStructure(const FString& BTPath);
	static FString AddBTComposite(const FString& BTPath, const FString& CompositeType, const FString& ParentNodeGuid, int32 X, int32 Y, FString& OutError);
	static FString AddBTTask(const FString& BTPath, const FString& TaskClassOrBP, const FString& ParentNodeGuid, int32 X, int32 Y, FString& OutError);
	static FString AddBTDecorator(const FString& BTPath, const FString& DecoratorClassOrBP, const FString& TargetNodeGuid, FString& OutError);
	static FString AddBTService(const FString& BTPath, const FString& ServiceClassOrBP, const FString& TargetNodeGuid, FString& OutError);
	static FString ConnectBTNodes(const FString& BTPath, const FString& ParentGuid, const FString& ChildGuid, FString& OutError);
	static FString DeleteBTNode(const FString& BTPath, const FString& NodeGuid, FString& OutError);

	// Animation — Sequence / Montage / AnimBP
	static FString GetAnimSequenceInfo(const FString& SequencePath);
	static FString GetAnimNotifies(const FString& AssetPath);
	static FString GetAnimMontageStructure(const FString& MontagePath);
	static FString GetAnimCurves(const FString& SequencePath);
	static FString AddAnimNotify(const FString& AssetPath, const FString& NotifyName, float Time, const FString& NotifyClass, FString& OutError);
	static FString RemoveAnimNotify(const FString& AssetPath, const FString& NotifyName, FString& OutError);
	static FString AddAnimCurve(const FString& SequencePath, const FString& CurveName, FString& OutError);
	static FString CreateAnimMontage(const FString& NewAssetPath, const FString& SkeletonPath, const FString& SourceSequencePath, FString& OutError);
	static FString AddMontageSection(const FString& MontagePath, const FString& SectionName, float StartTime, FString& OutError);
	static FString AddMontageSlot(const FString& MontagePath, const FString& SlotName, FString& OutError);
	static FString AddAnimSequencePlayerNode(const FString& AnimBPPath, const FString& GraphName, const FString& SequencePath, int32 X, int32 Y, FString& OutError);
	static FString AddAnimStateMachineNode(const FString& AnimBPPath, const FString& GraphName, const FString& MachineName, int32 X, int32 Y, FString& OutError);
	static FString AddAnimBlendSpacePlayerNode(const FString& AnimBPPath, const FString& GraphName, const FString& BlendSpacePath, int32 X, int32 Y, FString& OutError);

	// Materials — info + instances + parameters
	static FString CreateMaterial(const FString& NewAssetPath, FString& OutError);
	static FString CreateMaterialInstance(const FString& NewAssetPath, const FString& ParentMaterialPath, FString& OutError);
	static FString GetMaterialInfo(const FString& MaterialPath);
	static FString GetMaterialInstanceParams(const FString& MICPath);
	static FString SetMaterialScalarParam(const FString& MICPath, const FString& ParamName, float Value, FString& OutError);
	static FString SetMaterialVectorParam(const FString& MICPath, const FString& ParamName, const FLinearColor& Value, FString& OutError);
	static FString SetMaterialTextureParam(const FString& MICPath, const FString& ParamName, const FString& TexturePath, FString& OutError);
	static FString SetMaterialStaticSwitch(const FString& MICPath, const FString& ParamName, bool Value, FString& OutError);

	// Level Design — mass actor placement and organization
	static FString SpawnStaticMeshActor(const FString& MeshPath, const FVector& Location, const FRotator& Rotation, const FVector& Scale, const FString& MaterialOverride, const FString& ActorLabel, FString& OutError);
	static FString SpawnActorsGrid(const FString& ClassPath, const FString& MeshPath, const FVector& Origin, int32 CountX, int32 CountY, int32 CountZ, const FVector& Spacing, const FString& FolderPath, FString& OutError);
	static FString SpawnActorsLine(const FString& ClassPath, const FString& MeshPath, const FVector& Start, const FVector& End, int32 Count, const FString& FolderPath, FString& OutError);
	static FString DuplicateActors(const TArray<FString>& ActorPaths, const FVector& OffsetLocation, int32 CopyCount, FString& OutError);
	static FString SetActorFolder(const FString& ActorPath, const FString& FolderPath, FString& OutError);
	static FString GetActorBounds(const FString& ActorPath);
	static FString SaveCurrentLevel(FString& OutError);
	static FString BatchSetActorProperty(const FString& ClassFilter, const FString& FolderFilter, const FString& NameContains, const FString& PropertyName, const FString& NewValue, FString& OutError);
	static FString AlignActors(const TArray<FString>& ActorPaths, const FString& Axis, const FString& Mode, FString& OutError);
	static FString PlaceOnSurface(const FString& ActorPath, float MaxDistance, FString& OutError);

	// StateTree — read
	static FString GetStateTreeStructure(const FString& StateTreePath);
	static FString GetStateTreeSchema(const FString& StateTreePath);
	static FString ListStateTreeTasks(const FString& StateTreePath);
	static FString ListStateTreeTransitions(const FString& StateTreePath);

	// StateTree — write
	static FString CreateStateTree(const FString& NewAssetPath, const FString& SchemaClass, FString& OutError);
	static FString AddState(const FString& StateTreePath, const FString& ParentStateId, const FString& StateName, const FString& StateType, FString& OutError);
	static FString RemoveState(const FString& StateTreePath, const FString& StateId, FString& OutError);
	static FString AddStateTreeTask(const FString& StateTreePath, const FString& StateId, const FString& TaskClass, FString& OutError);
	static FString AddStateTreeTransition(const FString& StateTreePath, const FString& FromStateId, const FString& ToStateId, const FString& TriggerType, FString& OutError);
	static FString AddStateTreeCondition(const FString& StateTreePath, const FString& StateId, const FString& TransitionIndex, const FString& ConditionClass, FString& OutError);
	static FString SetStateTreeSchema(const FString& StateTreePath, const FString& SchemaClass, FString& OutError);
	static FString GetStateTreeBindings(const FString& StateTreePath);

	// Data Asset / Data Table
	static FString CreateDataAsset(const FString& NewAssetPath, const FString& AssetClass, FString& OutError);
	static FString DuplicateDataAsset(const FString& SourcePath, const FString& NewAssetPath, FString& OutError);
	static FString ListDataAssetsByClass(const FString& ClassFilter, const FString& PathFilter);
	static FString SetDataAssetField(const FString& AssetPath, const FString& PropertyPath, const FString& NewValue, FString& OutError);
	static FString AddDataAssetArrayElement(const FString& AssetPath, const FString& ArrayPath, const FString& NewElementValue, FString& OutError);
	static FString ListDataTableRows(const FString& DataTablePath);
	static FString SetDataTableCell(const FString& DataTablePath, const FString& RowName, const FString& ColumnName, const FString& NewValue, FString& OutError);

	// Control Rig — read-only
	static FString GetControlRigInfo(const FString& ControlRigPath);
	static FString GetControlRigHierarchy(const FString& ControlRigPath);
	static FString ListControlRigGraphs(const FString& ControlRigPath);

	// IK Rig — read-only
	static FString GetIKRigInfo(const FString& IKRigPath);

	// IK Retargeter — read + write
	static FString GetIKRetargeterInfo(const FString& RetargeterPath);
	static FString ListRetargetChains(const FString& RetargeterPath);
	static FString CreateIKRetargeter(const FString& NewAssetPath, const FString& SourceIKRigPath, const FString& TargetIKRigPath, FString& OutError);
	static FString SetRetargetChainMapping(const FString& RetargeterPath, const FString& SourceChainName, const FString& TargetChainName, FString& OutError);
	static FString AutoMapRetargetChains(const FString& RetargeterPath, FString& OutError);

	// Visual — screenshot / thumbnail capture
	// These return base64-encoded image data via OutImageBase64 so the agent
	// loop can embed it in the next tool_result sent to the model.
	static FString CaptureViewport(int32 Width, int32 Height, bool bShowUI, FString& OutImageBase64, FString& OutError);
	static FString CaptureFromCamera(const FVector& Location, const FRotator& Rotation, int32 Width, int32 Height, float FOV, FString& OutImageBase64, FString& OutError);
	static FString CaptureAssetThumbnail(const FString& AssetPath, int32 Size, FString& OutImageBase64, FString& OutError);

	// PIE — Play-In-Editor runtime debugging
	static FString PIESpawnActor(const FString& ClassPath, const FVector& Location, const FRotator& Rotation, FString& OutError);
	static FString PIEDestroyActor(const FString& ActorName, FString& OutError);
	static FString PIETeleportActor(const FString& ActorName, const FVector& Location, const FRotator& Rotation, FString& OutError);
	static FString PIEGetProperty(const FString& ActorName, const FString& PropertyName);
	static FString PIESetProperty(const FString& ActorName, const FString& PropertyName, const FString& NewValue, FString& OutError);
	static FString PIEGetBlackboardKey(const FString& ActorName, const FString& KeyName);
	static FString PIESetBlackboardKey(const FString& ActorName, const FString& KeyName, const FString& NewValue, FString& OutError);
	static FString PIEMoveAITo(const FString& ActorName, const FVector& Destination, float AcceptanceRadius, FString& OutError);
	static FString PIEStopAI(const FString& ActorName, FString& OutError);
	static FString PIEGetGameState();
	static FString PIEListActors(const FString& ClassFilter, const FString& NameContains);
	static FString PIEConsoleCommand(const FString& Command, FString& OutError);

	// Debug — compile errors, asset references, blueprint health
	static FString BPGetCompileErrors(const FString& BlueprintPath);
	static FString BPRefreshAllNodes(const FString& BlueprintPath, FString& OutError);
	static FString BPFixBrokenReferences(const FString& BlueprintPath, FString& OutError);
	static FString BPFixDeprecatedNodes(const FString& BlueprintPath, FString& OutError);
	static FString BPFindUnconnectedPins(const FString& BlueprintPath);
	static FString FindOrphanAssets(const FString& SearchPath, int32 MaxResults);
	static FString FindCircularDependencies(const FString& SearchPath, int32 MaxResults);
	static FString GetDependencyTree(const FString& AssetPath, int32 MaxDepth);

	// Splines
	static FString CreateSplineActor(const FString& Label, const FVector& Location, FString& OutActorPath, FString& OutError);
	static FString AddSplinePoint(const FString& ActorName, const FVector& Position, FString& OutError);
	static FString SetSplinePoint(const FString& ActorName, int32 PointIndex, const FVector& Position, const FString& PointType, FString& OutError);
	static FString RemoveSplinePoint(const FString& ActorName, int32 PointIndex, FString& OutError);
	static FString GetSplineInfo(const FString& ActorName);
	static FString SetSplineClosed(const FString& ActorName, bool bClosed, FString& OutError);

	// Environment — atmospherics, lighting, post-process
	static FString SetPostProcessSettings(const FString& VolumeName, const FString& JsonSettings, FString& OutError);
	static FString SetFogProperties(const FString& FogName, const FString& JsonProps, FString& OutError);
	static FString SetSkyAtmosphereProperties(const FString& SkyName, const FString& JsonProps, FString& OutError);
	static FString SetLightProperties(const FString& LightName, const FString& JsonProps, FString& OutError);

	// ========== STAGE 2: Visual & Effects ==========

	// Niagara
	static FString CreateNiagaraSystem(const FString& NewAssetPath, FString& OutError);
	static FString ReadNiagaraSystem(const FString& AssetPath);
	static FString SetNiagaraParameter(const FString& AssetPath, const FString& ParameterName, const FString& NewValue, FString& OutError);
	static FString AddNiagaraEmitter(const FString& SystemPath, const FString& EmitterAssetPath, FString& OutError);

	// Sequencer (Level Sequences)
	static FString CreateLevelSequence(const FString& NewAssetPath, FString& OutError);
	static FString ReadLevelSequence(const FString& AssetPath);
	static FString AddSequenceBinding(const FString& AssetPath, const FString& ActorName, FString& OutError);
	static FString AddSequenceTrack(const FString& AssetPath, const FString& BindingName, const FString& TrackType, FString& OutError);
	static FString AddSequenceKeyframe(const FString& AssetPath, const FString& BindingName, const FString& TrackType, float TimeSeconds, const FString& Value, FString& OutError);
	static FString SetSequenceRange(const FString& AssetPath, float StartSeconds, float EndSeconds, FString& OutError);

	// Pose Search
	static FString CreatePoseSearchSchema(const FString& NewAssetPath, const FString& SkeletonPath, FString& OutError);
	static FString CreatePoseSearchDatabase(const FString& NewAssetPath, const FString& SchemaPath, FString& OutError);
	static FString ReadPoseSearchDatabase(const FString& AssetPath);
	static FString AddPoseSearchAnimation(const FString& DatabasePath, const FString& AnimSequencePath, FString& OutError);

	// GAS (Gameplay Ability System)
	static FString CreateGameplayAbility(const FString& NewAssetPath, const FString& ParentAbilityClass, FString& OutError);
	static FString CreateGameplayEffect(const FString& NewAssetPath, FString& OutError);
	static FString CreateAttributeSet(const FString& NewAssetPath, FString& OutError);
	static FString ListGameplayAbilities(const FString& PathFilter);
	static FString ListGameplayEffects(const FString& PathFilter);
	static FString ListAttributeSets(const FString& PathFilter);
	static FString GetGASInfo(const FString& AssetPath);
	static FString SetGameplayEffectProperty(const FString& AssetPath, const FString& PropertyPath, const FString& NewValue, FString& OutError);

	// Animation Blueprint
	static FString GetAnimBlueprintInfo(const FString& AssetPath);

	// Editor selection
	static FString GetEditorSelection();

	// Logs
	static FString ReadOutputLog(const FString& Filter, int32 LastNLines);

	// Project
	static FString GetProjectInfo();

private:
	static UObject* LoadAssetByPath(const FString& AssetPath);
	static FString JsonifyProperties(UObject* Object, int32 Depth, int32 MaxDepth);
	static FString JsonifyEdGraph(UEdGraph* Graph);
};
