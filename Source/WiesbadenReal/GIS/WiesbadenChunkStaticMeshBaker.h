// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UProceduralMeshComponent;
class UStaticMesh;

/**
 * Bake-Hilfsroutine: aus einem fertig gefuellten ProceduralMeshComponent (wie
 * ApplyChunk RoadMesh/BuildingMesh baut) ein UStaticMesh mit VORGEKOCHTER
 * Complex-as-Simple-Trimesh-Kollision erzeugen und als Asset speichern.
 *
 * Zweck: der teuerste Zellwechsel-Posten ist der Laufzeit-Aufbau des
 * ProcMesh-Render-Proxys (~40-50 ms/Chunk, per stat dumphitches gemessen) plus
 * der Kollisions-Cook. Ein StaticMesh serialisiert Render-Daten UND gekochte
 * Kollision -> beim Stream-in wird nur geladen, nichts neu gebaut/gekocht.
 *
 * Das Modul ist bewusst NICHT im (tabuen) WiesbadenWorldBuilder.h deklariert,
 * damit der Worker es aus WiesbadenCityChunk::ApplyChunk (ebenfalls Worker-WIP)
 * per #include aufrufen kann, ohne dass hier eine fremde Datei angefasst wird.
 *
 * Nur im Editor (Bake-Pfad). Ausserhalb des Editors -> nullptr + OutError.
 *
 * Aufruf-Beispiel (im Worker, in ApplyChunk, nachdem RoadMesh gefuellt ist):
 *   FString Err;
 *   UStaticMesh* RoadSM = WiesbadenChunkStaticMeshBaker::BakeFromProcMesh(
 *       RoadMesh,
 *       FString::Printf(TEXT("/Game/Generated/Chunks/SM_Road_%d_%d"), CellX, CellY),
 *       bRoadCollision, Err);
 *   // danach RoadSM einem UStaticMeshComponent zuweisen (Worker-Verdrahtung).
 */
namespace WiesbadenChunkStaticMeshBaker
{
	/**
	 * @param Source              Fertig gefuelltes ProcMesh (eine Section = ein
	 *                            Material-Slot; Reihenfolge bleibt erhalten).
	 * @param PackagePath         Ziel-Assetpfad, z. B. "/Game/Generated/Chunks/SM_..".
	 * @param bCookComplexCollision  true -> Trimesh-Kollision (Complex-as-Simple)
	 *                            gekocht ins Asset; false -> ohne Kollision.
	 * @param OutError            Fehlermeldung bei Rueckgabe nullptr.
	 * @return                    Gespeichertes UStaticMesh oder nullptr.
	 */
	WIESBADENREAL_API UStaticMesh* BakeFromProcMesh(
		UProceduralMeshComponent* Source,
		const FString& PackagePath,
		bool bCookComplexCollision,
		FString& OutError);
}
