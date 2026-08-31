// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Paths.h"

/**
 * Gemeinsame Config-Pfade unter Content/Config/.
 *
 * Eine einzige Quelle fuer die Lage der Datenkataloge (Verkehrszeichen- und
 * Strassentyp-JSON), damit die beiden Lader nicht auseinanderdriften - beide
 * liegen als lose Dateien unter Content/Config und werden zur Laufzeit
 * gelesen.
 */
namespace WiesbadenConfigPaths
{
	/** Dateisystem-Pfad Content/Config/<FileName>. */
	inline FString ConfigFile(const TCHAR* FileName)
	{
		return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Config"), FileName);
	}
}
