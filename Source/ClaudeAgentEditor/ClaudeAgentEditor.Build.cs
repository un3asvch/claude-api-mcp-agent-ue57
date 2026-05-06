// Copyright Untry. All Rights Reserved.

using UnrealBuildTool;

public class ClaudeAgentEditor : ModuleRules
{
	public ClaudeAgentEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"EditorStyle",
			"EditorSubsystem",
			"UnrealEd",
			"ToolMenus",
			"WorkspaceMenuStructure",
			"Projects",
			"DeveloperSettings",

			// Asset / Blueprint introspection
			"AssetRegistry",
			"AssetTools",
			"ContentBrowser",
			"ContentBrowserData",
			"BlueprintGraph",
			"Kismet",
			"KismetCompiler",
			"GraphEditor",
			"PropertyEditor",

			// Animation
			"AnimGraph",
			"AnimGraphRuntime",

			// AI / Behavior Tree
			"AIModule",
			"NavigationSystem",
			"GameplayTasks",
			"AIGraph",
			"BehaviorTreeEditor",

			// Materials
			"MaterialEditor",

			// StateTree
			"StateTreeModule",
			"StateTreeEditorModule",
			"GameplayStateTreeModule",

			// Control Rig / IK Rig / IK Retargeter
			"ControlRig",
			"ControlRigDeveloper",
			"IKRig",
			"IKRigDeveloper",

			// Visual capture
			"ImageWrapper",
			"RenderCore",
			"RHI",

			// Stage 2 — Visual & Effects
			"Niagara",
			"NiagaraEditor",
			"MovieScene",
			"MovieSceneTracks",
			"LevelSequence",
			"LevelSequenceEditor",
			"Sequencer",
			"GameplayAbilities",
			"GameplayAbilitiesEditor",
			"GameplayTags",
			"PoseSearch",

			// Networking
			"HTTP",
			"HTTPServer",
			"Json",
			"JsonUtilities",
			"WebSockets",

			// Logging
			"OutputLog",

			// Misc
			"DesktopPlatform",
			"ApplicationCore",
		});
	}
}
