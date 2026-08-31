// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

using UnrealBuildTool;

public class WiesbadenRealEditorTarget : TargetRules
{
	public WiesbadenRealEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		bUseLoggingInShipping = true;

		// bUseLoggingInShipping weicht vom Shared-Environment (V7: false) ab.
		// Unique ist bei installed engines verboten, daher der dokumentierte
		// Override: die Projektcode-Einstellung bleibt, der Build teilt die
		// Engine-Module.
		bOverrideBuildEnvironment = true;

		ExtraModuleNames.Add("WiesbadenReal");
	}
}
