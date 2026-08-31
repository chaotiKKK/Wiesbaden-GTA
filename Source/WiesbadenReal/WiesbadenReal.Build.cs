// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

using UnrealBuildTool;

public class WiesbadenReal : ModuleRules
{
	public WiesbadenReal(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// Seit BuildSettingsVersion V2+ ist der Modul-Root nicht mehr automatisch
		// im Include-Pfad (nur Public/Private/Classes-Unterordner). Das Projekt
		// includiert konsistent mit Unterordner-Praefix ("GIS/...", "World/..."),
		// daher den Modul-Root explizit als Include-Pfad registrieren.
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"ProceduralMeshComponent",
			"ChaosVehicles",
			"NavigationSystem",
			"AIModule",
			"GameplayTasks",
			"UMG",
			"Niagara",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"RenderCore",
			"RHI",
			"Json",
			"JsonUtilities",
			"XmlParser",
			"HTTP",
			"MeshDescription",
			"StaticMeshDescription",
			"GeometryCore",
			"Landscape",
			"PhysicsCore",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
				"AssetRegistry",
			});
		}

		// Automation-Tests werden nur in Builds mit aktivierten Tests kompiliert.
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.Add("FunctionalTesting");
		}
	}
}
