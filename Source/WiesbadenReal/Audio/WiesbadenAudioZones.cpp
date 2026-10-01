// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Audio/WiesbadenAudioZones.h"

EWbAudioZone WiesbadenAudioZones::ClassifyZone(int32 NearbyTrees, int32 NearbyIndustry, int32 CommercialBuildings)
{
	if (NearbyIndustry > 0) { return EWbAudioZone::Industrial; }
	if (NearbyTrees >= 12) { return EWbAudioZone::Quiet; }
	if (CommercialBuildings >= 3) { return EWbAudioZone::Commercial; }
	return EWbAudioZone::Residential;
}

FWbAmbienceMix WiesbadenAudioZones::AmbienceMix(EWbAudioZone Zone)
{
	// Reihenfolge der Felder: Wind, City, Birds, Night, Crowd, Children,
	// Industry (FWbAmbienceMix). Jede Lage ist eine echte Aufnahme; die Zonen
	// unterscheiden sich damit im KLANGBILD (welche Lagen spielen), nicht nur
	// im Pegel.
	switch (Zone)
	{
	case EWbAudioZone::Quiet:
		// Wald/Wiese: Wind und Voegel tragen, Verkehr nur als fernes Summen.
		return { 0.65f, 0.10f, 0.65f, 0.55f, 0.05f, 0.05f, 0.00f };
	case EWbAudioZone::Commercial:
		// Innenstadt: Verkehr und Menschenmenge dominieren, Voegel treten zurueck.
		return { 0.30f, 0.85f, 0.15f, 0.20f, 0.75f, 0.35f, 0.10f };
	case EWbAudioZone::Industrial:
		// Industrie: Maschinenlage ist die Signatur, kaum Natur, wenig Menschen.
		return { 0.40f, 0.55f, 0.08f, 0.15f, 0.15f, 0.05f, 0.85f };
	default:
		// Wohngebiet: ausgeglichen, mit Strassenleben (Kinder) als Merkmal.
		return { 0.50f, 0.45f, 0.30f, 0.30f, 0.25f, 0.55f, 0.05f };
	}
}

FString WiesbadenAudioZones::ZoneName(EWbAudioZone Zone)
{
	switch (Zone)
	{
	case EWbAudioZone::Quiet: return TEXT("Gruen/Wald");
	case EWbAudioZone::Commercial: return TEXT("Innenstadt/Gewerbe");
	case EWbAudioZone::Industrial: return TEXT("Industrie");
	default: return TEXT("Wohngebiet");
	}
}

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
