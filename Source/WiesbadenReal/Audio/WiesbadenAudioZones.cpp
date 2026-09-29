// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Audio/WiesbadenAudioZones.h"

EWbFootstepSurface WiesbadenAudioZones::SurfaceFromMaterialName(const FString& MaterialName)
{
	// Reihenfolge = Prioritaet. Gras steht vor Asphalt, weil ein gemischter
	// Name wie "M_WbGelaende_Gras_AsphaltRand" sonst am Asphalt klebenbleibt.
	//
	// Wiese zuerst.
	if (MaterialName.Contains(TEXT("Gras"))
		|| MaterialName.Contains(TEXT("Wiese"))
		|| MaterialName.Contains(TEXT("Ground"))
		|| MaterialName.Contains(TEXT("Terrain"))
		|| MaterialName.Contains(TEXT("Wald")))
	{
		return EWbFootstepSurface::Wiese;
	}

	// Innenraum vor Asphalt: ein Boden mit beiden Merkmalen soll im Raum sein.
	if (MaterialName.Contains(TEXT("Indoor"))
		|| MaterialName.Contains(TEXT("Floor"))
		|| MaterialName.Contains(TEXT("Interior")))
	{
		return EWbFootstepSurface::Innenraum;
	}

	// Asphalt: Fahrbahn, Gehweg sind in diesem Projekt Asphalt, ausser sie
	// tragen Pflaster im Namen (vorher geprueft).
	if (MaterialName.Contains(TEXT("Asphalt"))
		|| MaterialName.Contains(TEXT("Fahrbahn")))
	{
		return EWbFootstepSurface::Asphalt;
	}

	// Pflaster: Bordstein, Platten, Platz, Gehweg, Stein.
	if (MaterialName.Contains(TEXT("Pflaster"))
		|| MaterialName.Contains(TEXT("Platten"))
		|| MaterialName.Contains(TEXT("Platz"))
		|| MaterialName.Contains(TEXT("Gehweg"))
		|| MaterialName.Contains(TEXT("Kerb"))
		|| MaterialName.Contains(TEXT("Bordstein"))
		|| MaterialName.Contains(TEXT("Stein"))
		|| MaterialName.Contains(TEXT("Paving")))
	{
		return EWbFootstepSurface::Pflaster;
	}

	return EWbFootstepSurface::Pflaster;
}

int32 WiesbadenAudioZones::FindStepSoundIndex(
	const TArray<EWbFootstepSurface>& LoadedSurfaces,
	EWbFootstepSurface Surface)
{
	for (int32 Index = 0; Index < LoadedSurfaces.Num(); ++Index)
	{
		if (LoadedSurfaces[Index] == Surface)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

float WiesbadenAudioZones::BandpassHzForSurface(EWbFootstepSurface Surface)
{
	switch (Surface)
	{
	case EWbFootstepSurface::Asphalt:   return 2200.0f;
	case EWbFootstepSurface::Pflaster:  return 1400.0f;
	case EWbFootstepSurface::Wiese:     return 620.0f;
	case EWbFootstepSurface::Innenraum: return 900.0f;
	default:                            return 1400.0f;
	}
}

FString WiesbadenAudioZones::SurfaceName(EWbFootstepSurface Surface)
{
	switch (Surface)
	{
	case EWbFootstepSurface::Asphalt:   return TEXT("Asphalt");
	case EWbFootstepSurface::Pflaster:  return TEXT("Pflaster");
	case EWbFootstepSurface::Wiese:     return TEXT("Wiese");
	case EWbFootstepSurface::Innenraum: return TEXT("Innenraum");
	default:                            return TEXT("Pflaster");
	}
}
