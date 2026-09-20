// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

using UnrealBuildTool;

public class WiesbadenRealTarget : TargetRules
{
	public WiesbadenRealTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		// Protokollausgaben im Shipping-Build: bewusst NICHT gesetzt.
		//
		// Hier stand `bUseLoggingInShipping = true`. Das Paketieren brach
		// daran ab:
		//
		//   WiesbadenReal modifies the values of properties:
		//   [ bUseLoggingInShipping: True != False ]. This is not allowed, as
		//   WiesbadenReal has build products in common with UnrealGame.
		//
		// Unreal teilt vorkompilierte Engine-Module zwischen Projekten; wer
		// eine Einstellung aendert, die diese Module betrifft, muss sich
		// abkoppeln. Der naheliegende Weg dorthin -
		// `BuildEnvironment = TargetBuildEnvironment.Unique` - scheitert
		// ebenfalls:
		//
		//   Targets with a unique build environment cannot be built with an
		//   installed engine.
		//
		// Die Engine ist eine Launcher-Installation, also ohne eigene
		// Quellen. Bleibt `bOverrideBuildEnvironment = true` als Notausgang.
		//
		// Gebraucht wird die Einstellung aber gar nicht: Paketiert wird
		// DEVELOPMENT, und dort sind die Protokollausgaben ohnehin an. Sie
		// haette nur fuer einen Shipping-Build etwas geaendert - und die
		// Diagnosewerkzeuge dieses Projekts (Bildzeitmessung, Strassenabzug,
		// GPU-Aufteilung) laufen alle im Development-Build.
		//
		// Wer spaeter Shipping mit Protokoll braucht, setzt beides:
		//   bOverrideBuildEnvironment = true;
		//   bUseLoggingInShipping = true;

		ExtraModuleNames.Add("WiesbadenReal");
	}
}
