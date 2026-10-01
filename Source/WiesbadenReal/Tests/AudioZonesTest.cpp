// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Audio/WiesbadenAudioZones.h"

/**
 * Test der Untergrund-Aufloesung. Deckt die reale Namensliste der
 * Stadtmaterialien ab UND die Sackgassen (leer, unbekannt, MAX).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioZonesSurfaceTest,
	"WiesbadenReal.Audio.Footsteps.Surface",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAudioZonesSurfaceTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenAudioZones;

	// Reale Materialnamen aus Content/Materials/City (Stand 2026-09-27).
	TestEqual(TEXT("MI_WbFahrbahn_Asphalt -> Asphalt"),
		SurfaceFromMaterialName(TEXT("MI_WbFahrbahn_Asphalt")), EWbFootstepSurface::Asphalt);
	TestEqual(TEXT("MI_WbFahrbahn_Asphalt_Alt -> Asphalt"),
		SurfaceFromMaterialName(TEXT("MI_WbFahrbahn_Asphalt_Alt")), EWbFootstepSurface::Asphalt);
	TestEqual(TEXT("MI_WbGehweg_Pflaster -> Pflaster"),
		SurfaceFromMaterialName(TEXT("MI_WbGehweg_Pflaster")), EWbFootstepSurface::Pflaster);
	TestEqual(TEXT("MI_WbGehweg_Platten -> Pflaster"),
		SurfaceFromMaterialName(TEXT("MI_WbGehweg_Platten")), EWbFootstepSurface::Pflaster);
	TestEqual(TEXT("MI_WbPlatz_Pflaster -> Pflaster"),
		SurfaceFromMaterialName(TEXT("MI_WbPlatz_Pflaster")), EWbFootstepSurface::Pflaster);
	// Der Bordstein heisst MI_WbBordstein_Beton (M_WbKerb ist der Mesh, nicht
	// das Material) - beide Zweige geprueft, damit die Aufloesung nicht an einem
	// ausgedachten Namen haengt.
	TestEqual(TEXT("MI_WbBordstein_Beton -> Pflaster"),
		SurfaceFromMaterialName(TEXT("MI_WbBordstein_Beton")), EWbFootstepSurface::Pflaster);
	TestEqual(TEXT("M_WbKerb -> Pflaster"),
		SurfaceFromMaterialName(TEXT("M_WbKerb")), EWbFootstepSurface::Pflaster);
	TestEqual(TEXT("MI_WbGelaende_Gras -> Wiese"),
		SurfaceFromMaterialName(TEXT("MI_WbGelaende_Gras")), EWbFootstepSurface::Wiese);
	TestEqual(TEXT("MI_WbGelaende_Wiese -> Wiese"),
		SurfaceFromMaterialName(TEXT("MI_WbGelaende_Wiese")), EWbFootstepSurface::Wiese);
	TestEqual(TEXT("M_AAA_GroundDirt -> Wiese"),
		SurfaceFromMaterialName(TEXT("M_AAA_GroundDirt")), EWbFootstepSurface::Wiese);
	TestEqual(TEXT("M_AAA_TerrainGrass -> Wiese"),
		SurfaceFromMaterialName(TEXT("M_AAA_TerrainGrass")), EWbFootstepSurface::Wiese);

	// Sackgassen: Default statt Fehler.
	TestEqual(TEXT("leerer Name -> Pflaster"),
		SurfaceFromMaterialName(TEXT("")), EWbFootstepSurface::Pflaster);
	TestEqual(TEXT("unbekannter Name -> Pflaster"),
		SurfaceFromMaterialName(TEXT("M_Irgendwas")), EWbFootstepSurface::Pflaster);

	// Reihenfolge ist Absicht: Gras wird VOR Asphalt geprueft, sonst klebt ein
	// gemischter Name wie "M_WbGelaende_Gras_AsphaltRand" am Asphalt.
	// Genau dieser Test ist die Sabotage-Gegenprobe fuer die Reihenfolge.
	TestEqual(TEXT("Gras schlaegt Asphalt im Mischnamen"),
		SurfaceFromMaterialName(TEXT("M_WbGelaende_Gras_AsphaltRand")), EWbFootstepSurface::Wiese);

	// Bandpass: vier verschiedene Frequenzen, monoton fallend nach Weichheit.
	const float Asphalt = BandpassHzForSurface(EWbFootstepSurface::Asphalt);
	const float Pflaster = BandpassHzForSurface(EWbFootstepSurface::Pflaster);
	const float Wiese = BandpassHzForSurface(EWbFootstepSurface::Wiese);
	const float Innen = BandpassHzForSurface(EWbFootstepSurface::Innenraum);
	TestTrue(TEXT("Asphalt heller als Pflaster"), Asphalt > Pflaster);
	TestTrue(TEXT("Pflaster heller als Wiese"), Pflaster > Wiese);
	TestTrue(TEXT("alle Bandbreiten im hoerbaren Bereich"),
		Asphalt >= 200.0f && Asphalt <= 8000.0f && Innen >= 200.0f && Innen <= 8000.0f);

	// Klangauflosung: nur ein Sound des angefragten Materials darf gewaehlt
	// werden. Insbesondere darf ein fehlender Klang nicht auf den naechsten
	// geladenen Oberflaechenklang fallen.
	const TArray<EWbFootstepSurface> LoadedSurfaces = {
		EWbFootstepSurface::Asphalt,
		EWbFootstepSurface::Pflaster,
		EWbFootstepSurface::Wiese,
		EWbFootstepSurface::Innenraum
	};
	TestEqual(TEXT("Asphalt waehlt Asphalt-Sound"),
		FindStepSoundIndex(LoadedSurfaces, EWbFootstepSurface::Asphalt), 0);
	TestEqual(TEXT("Pflaster waehlt Pflaster-Sound"),
		FindStepSoundIndex(LoadedSurfaces, EWbFootstepSurface::Pflaster), 1);
	TestEqual(TEXT("Wiese waehlt Wiese-Sound"),
		FindStepSoundIndex(LoadedSurfaces, EWbFootstepSurface::Wiese), 2);
	TestEqual(TEXT("Innenraum waehlt Innenraum-Sound"),
		FindStepSoundIndex(LoadedSurfaces, EWbFootstepSurface::Innenraum), 3);
	TestEqual(TEXT("fehlende Oberflaeche bleibt still"),
		FindStepSoundIndex(TArray<EWbFootstepSurface>{ EWbFootstepSurface::Asphalt },
			EWbFootstepSurface::Wiese), INDEX_NONE);
	TestEqual(TEXT("leere Klangliste bleibt still"),
		FindStepSoundIndex(TArray<EWbFootstepSurface>(), EWbFootstepSurface::Asphalt), INDEX_NONE);

	// MAX und Ausreisser: kein Absturz, kein Unsinn.
	TestEqual(TEXT("MAX -> Pflaster-Bandpass"),
		BandpassHzForSurface(EWbFootstepSurface::MAX),
		BandpassHzForSurface(EWbFootstepSurface::Pflaster));
	TestTrue(TEXT("SurfaceName liefert Text"),
		!SurfaceName(EWbFootstepSurface::Asphalt).IsEmpty());
	TestTrue(TEXT("SurfaceName(MAX) faellt nicht zurueck auf leer"),
		!SurfaceName(EWbFootstepSurface::MAX).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioAmbienceZoneTest,
	"WiesbadenReal.Audio.Ambience.Zones",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAudioAmbienceZoneTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenAudioZones;
	TestEqual(TEXT("leere Daten fallen auf Wohngebiet"), ClassifyZone(0, 0, 0), EWbAudioZone::Residential);
	TestEqual(TEXT("viele Baeume ergeben ruhige Zone"), ClassifyZone(12, 0, 4), EWbAudioZone::Quiet);
	TestEqual(TEXT("einzelner Strassenbaum ergibt keinen Wald"), ClassifyZone(1, 0, 0), EWbAudioZone::Residential);
	TestEqual(TEXT("Gewerbe ergibt Innenstadt"), ClassifyZone(0, 0, 3), EWbAudioZone::Commercial);
	TestEqual(TEXT("Industrie hat Vorrang"), ClassifyZone(30, 1, 5), EWbAudioZone::Industrial);
	TestTrue(TEXT("Wald hat weniger Stadtsummen als Innenstadt"),
		AmbienceMix(EWbAudioZone::Quiet).City < AmbienceMix(EWbAudioZone::Commercial).City);
	TestTrue(TEXT("Wald hat mehr Vogelanteil als Industrie"),
		AmbienceMix(EWbAudioZone::Quiet).Birds > AmbienceMix(EWbAudioZone::Industrial).Birds);
	TestEqual(TEXT("ungueltige Zone faellt auf Wohnpegel"),
		AmbienceMix(EWbAudioZone::MAX).City, AmbienceMix(EWbAudioZone::Residential).City);
	return true;
}
