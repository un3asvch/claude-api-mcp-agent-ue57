// Copyright Untry. All Rights Reserved.

#include "ClaudeAgentSettings.h"

UClaudeAgentSettings::UClaudeAgentSettings()
{
	SystemPrompt = GetDefaultSystemPrompt();
}

FString UClaudeAgentSettings::GetModelString() const
{
	switch (DefaultModel)
	{
		case EClaudeModel::Opus_4_7:    return TEXT("claude-opus-4-7");
		case EClaudeModel::Opus_4_6:    return TEXT("claude-opus-4-6");
		case EClaudeModel::Sonnet_4_6:  return TEXT("claude-sonnet-4-6");
		case EClaudeModel::Haiku_4_5:   return TEXT("claude-haiku-4-5-20251001");
		default:                        return TEXT("claude-sonnet-4-6");
	}
}

bool UClaudeAgentSettings::IsOpus47() const
{
	return DefaultModel == EClaudeModel::Opus_4_7;
}

bool UClaudeAgentSettings::IsCategoryEnabled(EClaudeToolCategory Cat) const
{
	switch (Cat)
	{
		case EClaudeToolCategory::General:        return bEnableGeneral;
		case EClaudeToolCategory::Blueprint:      return bEnableBlueprint;
		case EClaudeToolCategory::Level:          return bEnableLevel;
		case EClaudeToolCategory::Animation:      return bEnableAnimation;
		case EClaudeToolCategory::Material:       return bEnableMaterial;
		case EClaudeToolCategory::BehaviorTree:   return bEnableBehaviorTree;
		case EClaudeToolCategory::StateTree:      return bEnableStateTree;
		case EClaudeToolCategory::DataAsset:      return bEnableDataAsset;
		case EClaudeToolCategory::ControlRig:     return bEnableControlRig;
		case EClaudeToolCategory::Visual:         return bEnableVisual;
		case EClaudeToolCategory::PIE:            return bEnablePIE;
		case EClaudeToolCategory::Debug:          return bEnableDebug;
		case EClaudeToolCategory::Spline:         return bEnableSpline;
		case EClaudeToolCategory::Environment:    return bEnableEnvironment;
		case EClaudeToolCategory::Niagara:        return bEnableNiagara;
		case EClaudeToolCategory::Sequencer:      return bEnableSequencer;
		case EClaudeToolCategory::GAS:            return bEnableGAS;
		case EClaudeToolCategory::PCG:            return bEnablePCG;
		case EClaudeToolCategory::Input:          return bEnableInput;
		case EClaudeToolCategory::Widget:         return bEnableWidget;
		case EClaudeToolCategory::PoseSearch:     return bEnablePoseSearch;
		case EClaudeToolCategory::Settings:       return bEnableSettings;
		default:                                  return true;
	}
}

FString UClaudeAgentSettings::GetDefaultSystemPrompt()
{
	return TEXT(
		"You are an expert Unreal Engine 5 technical assistant embedded directly in the editor as a plugin. "
		"You have access to tools that let you inspect, analyze, and modify the project: Asset Registry queries, "
		"Blueprint graph reading, object property reflection, animation blueprint state inspection, log reading, "
		"editor selection, blueprint compilation, and (with user confirmation) property writes and node creation.\n\n"

		"PROJECT CONTEXT:\n"
		"- Engine: Unreal Engine 5.7\n"
		"- Primary approach: Blueprint-first, with C++ where appropriate\n"
		"- Foundation: GASP (Game Animation Sample Project) — DO NOT rewrite its core systems, build on top\n"
		"- Active work: First-person system, traversal mechanics (GASP-native only — no custom parkour), "
		"left-hand equipment system with Gameplay Tags + Data Assets\n"
		"- Two parallel branches: CMC (`SandboxCharacter_CMC`) and Mover (`CBP_SandboxCharacter_Mover`)\n\n"

		"BEHAVIOR:\n"
		"- Always inspect before suggesting. Use `list_assets`, `get_blueprint_graph`, `get_object_properties` "
		"to ground your answers in the actual project state instead of guessing.\n"
		"- Prefer minimal, targeted changes that respect the GASP architecture.\n"
		"- When proposing edits, explain what will change and why before calling write tools.\n"
		"- Default to Russian for explanations (the user works in Russian), but keep code, type names, "
		"and asset paths in English.\n"
		"- Be concise. Avoid restating the obvious. Show your reasoning, not your apologies.\n"
	);
}
