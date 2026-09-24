// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

using UnrealBuildTool;

public class WiesbadenReal : ModuleRules
{
	public WiesbadenReal(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// Unity-Build AUS: Jede .cpp wird als eigene Uebersetzungseinheit gebaut.
		//
		// Der Grund ist eine Falle des Unity-Builds: Er fasst mehrere .cpp zu
		// einer Uebersetzungseinheit zusammen, wodurch anonyme Namespaces
		// verschiedener Dateien VERSCHMELZEN. Gleichnamige dateilokale Helfer
		// (z. B. NextNoise, SamplesPerPush, BytesPerSample, MakeLane, Dt) - in
		// Standard-C++ voellig legal, weil jede Datei ihre eigene Einheit ist -
		// kollidieren dann als Doppel-Definitionen. Welche Dateien zusammen
		// gebuendelt werden, haengt an der Datei-Reihenfolge; das Hinzufuegen
		// neuer Quellen verschiebt die Grenzen und deckt latente Kollisionen auf.
		// Ohne Unity gibt es diese Klasse von Fehlern gar nicht erst, und
		// inkrementelle Einzeldatei-Builds sind schneller.
		bUseUnity = false;

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
			"AudioMixer",
			"MetasoundGraphCore",
			"MetasoundFrontend",
			"MetasoundStandardNodes",
			"MetasoundEngine",
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
