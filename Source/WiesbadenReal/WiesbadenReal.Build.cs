// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

using UnrealBuildTool;
using EpicGames.Core;
using System.Collections.Generic;
using System.IO;

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

		// Dateilader bleiben unveraendert: nur die explizite Liste in DefaultGame.ini
		// wird als UFS unter dem Projektpfad gestaged, nicht der ganze Data-Baum.
		DirectoryReference ProjectRoot = DirectoryReference.Combine(
			new DirectoryReference(ModuleDirectory), "..", "..");
		ConfigHierarchy GameConfig = ConfigCache.ReadHierarchy(
			ConfigHierarchyType.Game, ProjectRoot, Target.Platform, Target.CustomConfig);
		// Alle Orte der Game-Hierarchie im Projekt als ExternalDependency,
		// auch die noch nicht existierenden. GEMESSEN am 30.09.2026: mit nur
		// DefaultGame.ini blieb eine nachtraeglich angelegte
		// Config/Windows/WindowsGame.ini unentdeckt - UBT meldete
		// "Result: Succeeded" und schrieb die alte Dateiliste in den Receipt.
		foreach (FileReference ConfigFile in ConfigHierarchy.EnumerateConfigFileLocations(
			ConfigHierarchyType.Game, ProjectRoot, Target.Platform, Target.CustomConfig, null))
		{
			if (ConfigFile.FullName.StartsWith(ProjectRoot.FullName, System.StringComparison.OrdinalIgnoreCase))
			{
				ExternalDependencies.Add(ConfigFile.FullName);
			}
		}
		List<string> RuntimeFiles;
		if (!GameConfig.GetArray("WiesbadenReal.RuntimeStaging", "RuntimeFile", out RuntimeFiles)
			|| RuntimeFiles.Count == 0)
		{
			throw new BuildException("RuntimeStaging: explizite RuntimeFile-Liste fehlt in DefaultGame.ini.");
		}
		foreach (string RelativePath in RuntimeFiles)
		{
			// Keine absoluten, aus dem Projekt fuehrenden oder Nicht-JSON-Pfade.
			if (Path.IsPathRooted(RelativePath) || RelativePath.Contains("\\")
				|| RelativePath.Contains("..") || !RelativePath.EndsWith(".json")
				|| !(RelativePath.StartsWith("Data/") || RelativePath.StartsWith("Content/Config/")))
			{
				throw new BuildException("RuntimeStaging: kein expliziter Projekt-JSON-Pfad: {0}", RelativePath);
			}
			FileReference RuntimeFile = FileReference.Combine(ProjectRoot, RelativePath);
			if (!FileReference.Exists(RuntimeFile))
			{
				throw new BuildException("RuntimeStaging: benoetigte Laufzeitdatei fehlt: {0}", RuntimeFile);
			}
			RuntimeDependencies.Add("$(ProjectDir)/" + RelativePath, StagedFileType.UFS);
		}

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
			"AnimationCore",   // SolveTwoBoneIK: Fuss-IK der Spielerfigur
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
			// Kundenfiguren finden (WiesbadenCustomerFigures) - auch im gekochten Spiel.
			"AssetRegistry",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
			});
		}

		// Automation-Tests werden nur in Builds mit aktivierten Tests kompiliert.
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.Add("FunctionalTesting");
		}
	}
}
