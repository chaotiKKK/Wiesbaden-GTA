// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenCityPipeline.h"

#include "WiesbadenReal.h"

#include "GIS/BuildingGenerator.h"
#include "GIS/CityPrompt.h"
#include "GIS/GeoCoordinateConverter.h"
#include "GIS/HeightmapImporter.h"
#include "GIS/OSMDataParser.h"
#include "GIS/RoadNetworkGenerator.h"
#include "GIS/RoadTypeLibrary.h"
#include "GIS/TerrainGenerator.h"
#include "GIS/WiesbadenRegion.h"
#include "GIS/WiesbadenRegionAssets.h"

namespace WiesbadenCityPipeline
{
	EBuildResult BuildCityData(
		const FBuildInput& Input,
		const FBuildTools& Tools,
		FWiesbadenCityData& OutData,
		FProgressCallback Progress,
		FCancelCallback Cancel)
	{
		OutData.OsmFilePath = Input.OsmFilePath;
		OutData.DemFilePath = Input.DemFilePath;
		OutData.VerticalReferenceMeters = Input.VerticalReferenceMeters;

		const auto Report = [&Progress](int32 Percent, EBuildStage Stage)
		{
			if (Progress)
			{
				Progress(Percent, Stage);
			}
		};
		const auto Cancelled = [&Cancel]()
		{
			return Cancel && Cancel();
		};

		Report(5, EBuildStage::ParseOsm);
		OutData.ParseResult = Tools.Parser->ParseFile(Input.OsmFilePath, OutData.OSMData);
		if (!OutData.ParseResult.bSuccess)
		{
			OutData.ErrorMessage = OutData.ParseResult.ErrorMessage;
			return EBuildResult::Failed;
		}

		if (Cancelled()) { return EBuildResult::Cancelled; }

		// Amtliche Gebaeudegrundrisse, falls konfiguriert. Ein Fehlschlag ist
		// hier NICHT fatal: die Stadt laesst sich weiterhin aus den
		// OSM-Grundrissen bauen, nur eben weniger genau. Ein harter Abbruch
		// waere die schlechtere Wahl - er wuerde einen 20-minuetigen Build an
		// einer optionalen Zusatzquelle scheitern lassen.
		if (!Input.AlkisFilePath.IsEmpty())
		{
			const FOSMParseResult AlkisResult =
				Tools.Parser->ParseFile(Input.AlkisFilePath, OutData.AlkisData);

			if (AlkisResult.bSuccess)
			{
				UE_LOG(LogWbGIS, Log,
					TEXT("ALKIS-Grundrisse geladen: %d Gebaeude - sie ersetzen die OSM-Grundrisse."),
					OutData.AlkisData.Ways.Num());
			}
			else
			{
				UE_LOG(LogWbGIS, Warning,
					TEXT("ALKIS-Grundrisse nicht ladbar (%s) - es gelten die OSM-Grundrisse."),
					*AlkisResult.ErrorMessage);
				OutData.AlkisData.Reset();
			}
		}

		if (Cancelled()) { return EBuildResult::Cancelled; }

		// Hoehenmodell. Der Raster-Sampler haelt einen Zeiger auf das Raster in
		// OutData; gesampelt wird erst nach dem Import, der Zeiger bleibt also
		// die ganze Zeit gueltig. Ohne DEM: flacher Fallback (Z = 0).
		FRasterHeightSampler RasterSampler(OutData.DemRaster, *Tools.Converter);
		FFlatHeightSampler FlatSampler(0.0);
		FTileHeightSampler TileSampler(OutData.TerrainTile);
		const IHeightSampler* HeightSampler = &FlatSampler;

		if (Input.bImportDem && !Input.DemFilePath.IsEmpty())
		{
			Report(25, EBuildStage::ImportDem);
			OutData.DemImportResult = Tools.Importer->ImportFile(Input.DemFilePath, OutData.DemRaster);
			if (!OutData.DemImportResult.bSuccess)
			{
				OutData.ErrorMessage = OutData.DemImportResult.ErrorMessage;
				return EBuildResult::Failed;
			}

			OutData.DemRaster.VerticalReferenceMeters = Input.VerticalReferenceMeters;

			// Terrain-Tile SOFORT erzeugen, noch vor den Strassen.
			//
			// Grund: Strassen und Gebaeude holten ihre Hoehe bisher aus dem
			// DEM-Raster, waehrend das Landscape das daraus abgeleitete Gitter
			// (7,81 m Maschenweite) rendert - zwei Flaechen, die nie exakt
			// uebereinstimmen. Strassen zerfielen dadurch in Flecken und Gebaeude
			// wurden horizontal abgeschnitten. Ueber den Tile-Sampler sitzt die
			// Stadt auf DERSELBEN Flaeche, die auch gezeichnet wird.
			if (Input.bGenerateTerrain)
			{
				Report(28, EBuildStage::Terrain);
				OutData.TerrainReport = Tools.TerrainGenerator->Generate(
					OutData.DemRaster, *Tools.Converter, Input.TerrainSettings, OutData.TerrainTile,
					OutData.OSMData.Bounds.IsValid() ? &OutData.OSMData.Bounds : nullptr);

				if (!OutData.TerrainReport.bSuccess)
				{
					OutData.ErrorMessage = OutData.TerrainReport.ErrorMessage;
					return EBuildResult::Failed;
				}
			}

			// Tile-Sampler bevorzugen; ohne Terrain bleibt das Raster.
			HeightSampler = OutData.TerrainTile.IsValid()
				? static_cast<const IHeightSampler*>(&TileSampler)
				: static_cast<const IHeightSampler*>(&RasterSampler);
		}
		else if (Input.bGenerateTerrain)
		{
			UE_LOG(LogWbCore, Warning,
				TEXT("BuildCityData: Terrain-Generierung aktiv, aber kein DEM - Terrain wird uebersprungen."));
		}

		// Strassentypen laden (JSON oder RASt-06/RAA-Defaults).
		Tools.TypeLibrary->LoadFromJsonFile(
			Input.RoadTypeConfigPath.IsEmpty()
				? URoadTypeLibrary::GetDefaultConfigPath()
				: Input.RoadTypeConfigPath);

		// City-Prompt: Text -> Spezifikation -> Pipeline-Parameter (regelbasiert,
		// WorldClaw-artig "Prompt -> Welt", aber ohne LLM/Cloud). Angewandt werden:
		// Bebauungsdichte -> Mindest-Grundflaeche der Gebaeude (dicht = auch
		// kleine Gebaeude, locker = nur groessere); Fassadenstil-Gewichte ->
		// gewichtete PromptStyle:-Keys je Gebaeude; Landmarken -> PromptLandmark:-
		// Keys + bIsLandmark-Markierung (siehe BuildingGenerator). Die
		// Varianten-Pipeline (SelectMaterialVariant + Section-Gruppierung) bleibt
		// bewusst unangetastet - die Override-Keys trennen nur Abschnitte, deren
		// Material die Render-Seite ueber PromptFacadeMaterials waehlt.
		FBuildingGenerationSettings BuildingSettings = Input.BuildingSettings;
		if (!Input.CityPrompt.IsEmpty())
		{
			OutData.CityPromptSpec = CityPromptParser::Parse(Input.CityPrompt);
			BuildingSettings.MinFootprintAreaSqm =
				FMath::Lerp(25.0, 3.0, OutData.CityPromptSpec.BuildingDensity);
			BuildingSettings.PromptFacadeVariantWeights = OutData.CityPromptSpec.FacadeVariantWeights;
			BuildingSettings.PromptLandmarkNames = OutData.CityPromptSpec.DetectedLandmarks;
			BuildingSettings.bApplyPromptOverrides = true;
			BuildingSettings.HeightScale = OutData.CityPromptSpec.BuildingHeightScale;
			UE_LOG(LogWbCore, Log,
				TEXT("City-Prompt: %s (Dichte %.2f, Hoehen %.2f, Verkehr %.2f, Zeit %.1fh, %d Landmarken, %d Fassadenstile, Wetter %d)"),
				*OutData.CityPromptSpec.DisplayName,
				OutData.CityPromptSpec.BuildingDensity,
				OutData.CityPromptSpec.BuildingHeightScale,
				OutData.CityPromptSpec.TrafficDensity,
				OutData.CityPromptSpec.TimeOfDayHours,
				OutData.CityPromptSpec.DetectedLandmarks.Num(),
				OutData.CityPromptSpec.FacadeVariantWeights.Num(),
				static_cast<int32>(OutData.CityPromptSpec.Weather));
		}

		if (Cancelled()) { return EBuildResult::Cancelled; }

		if (Input.bGenerateRoads)
		{
			Report(45, EBuildStage::Roads);
			OutData.RoadReport = Tools.RoadGenerator->Generate(
				OutData.OSMData, Tools.Converter, Tools.TypeLibrary, HeightSampler,
				Input.RoadSettings, OutData.RoadNetwork, &OutData.RoadMesh);

			if (!OutData.RoadReport.bSuccess)
			{
				OutData.ErrorMessage = OutData.RoadReport.ErrorMessage;
				return EBuildResult::Failed;
			}

			// Verkehrs-Simulation konfigurieren: Dichte aus dem City-Prompt
			// (falls gesetzt, z. B. "Stau" -> 0.95), sonst Input-Defaults.
			// Initialisiert wird die Simulation erst zur Laufzeit im
			// City-Subsystem auf dem finalen Datencontainer - ein Netz-Zeiger
			// wuerde den Move in den GameInstance nicht ueberleben.
			OutData.TrafficSettings = Input.TrafficSettings;
			if (!Input.CityPrompt.IsEmpty())
			{
				OutData.TrafficSettings.TrafficDensity = OutData.CityPromptSpec.TrafficDensity;
			}
		}

		if (Cancelled()) { return EBuildResult::Cancelled; }

		// Stadt-Regionen (WorldClaw-Schritt 3: Regionen-Planung VOR der
		// Objekt-Erzeugung - global-to-regional). OSM-Flaechen (Wasser/Gruen/
		// Wohnen/Gewerbe/Industrie) werden zu Regionen; der BuildingGenerator
		// erhaelt die Regionen-Karte und ordnet jedes Gebaeude beim Bauen
		// seiner Region zu - inkl. Wasser-Filter direkt im Mesh (kein Haus im
		// See) und regionalem Hoehen-/Fassaden-Feintuning. Nicht-fatal.
		if (Input.bGenerateRegions && Tools.RegionGenerator)
		{
			Report(62, EBuildStage::Regions);
			OutData.RegionReport = Tools.RegionGenerator->Generate(
				OutData.OSMData, Tools.Converter, OutData.Regions);
			if (OutData.RegionReport.bSuccess)
			{
				BuildingSettings.RegionMap = OutData.Regions;
				BuildingSettings.bApplyRegionRules = true;
				BuildingSettings.bRemoveWaterBuildings = Input.bRemoveWaterBuildings;
			}
		}

		if (Cancelled()) { return EBuildResult::Cancelled; }

		// Regionen-abhaengige Assets (WorldClaw-Schritt 3: Objekte logisch
		// platzieren - Baeume nur im Park, kein Container im Gruen). Nutzt die
		// Regionen-Karte aus dem Regions-Pass oben. Nicht-fatal.
		if (Input.bGenerateRegionAssets && Tools.RegionAssetGenerator && OutData.RegionReport.bSuccess)
		{
			Report(66, EBuildStage::RegionAssets);

			// MIT Strassennetz: sonst stehen Baeume auf der Fahrbahn.
			//
			// Die Streuung kennt nur das Regionspolygon, und
			// Landnutzungsflaechen ueberlappen Strassen regelmaessig. Als
			// Fehler faellt das nie auf - die Baum-Instanzen tragen gar keine
			// Kollision, der Verkehr faehrt also lautlos durch die Staemme.
			// Der Strassen-Pass laeuft vorher, das Netz liegt hier bereit.
			OutData.RegionAssetReport = Tools.RegionAssetGenerator->GenerateClearOfRoads(
				OutData.Regions, HeightSampler, Input.RegionAssetSettings,
				OutData.RoadNetwork, OutData.RegionAssetLayout);

			// Baeume aus den ECHTEN OSM-Punkten (natural=tree) + dichte Fuellung nur
			// echter Waldflaechen - haengt an das Layout an (GenerateClearOfRoads liess
			// die Baum-Kategorie bei bUseOsmTrees aus). So stehen Baeume nur, wo real.
			if (Input.RegionAssetSettings.bUseOsmTrees)
			{
				const FRegionAssetReport OsmTrees = Tools.RegionAssetGenerator->PlaceOsmTrees(
					OutData.OSMData, Tools.Converter, HeightSampler,
					OutData.RoadNetwork, Input.RegionAssetSettings, OutData.RegionAssetLayout);
				OutData.RegionAssetReport.TreeCount += OsmTrees.TreeCount;
				OutData.RegionAssetReport.AssetCount = OutData.RegionAssetLayout.Assets.Num();
				OutData.RegionAssetReport.SkippedOnRoadCount += OsmTrees.SkippedOnRoadCount;
			}
		}

		if (Cancelled()) { return EBuildResult::Cancelled; }

		if (Input.bGenerateBuildings)
		{
			Report(68, EBuildStage::Buildings);
			// Der Gebaeude-Pass ist die laengste Stufe - der Abbruch-Callback
			// wird hier direkt durchgereicht, damit ein Shutdown nicht bis zu
			// 120 s auf eine laufende Stufe warten muss (der Generator pollt
			// zwischen den Gebaeuden und meldet Report.bCancelled).
			// Amtliche Grundrisse haben Vorrang, wenn sie vorliegen. Beide
			// Datensaetze zu verwenden waere falsch: ALKIS und OSM erfassen
			// dieselben Gebaeude, das Ergebnis waere jedes Haus doppelt.
			const FOSMDataSet& BuildingSource =
				(OutData.AlkisData.Ways.Num() > 0) ? OutData.AlkisData : OutData.OSMData;

			OutData.BuildingReport = Tools.BuildingGenerator->Generate(
				BuildingSource, Tools.Converter, HeightSampler,
				BuildingSettings, OutData.Buildings, &OutData.BuildingMesh,
				Cancel);

			if (OutData.BuildingReport.bCancelled)
			{
				return EBuildResult::Cancelled;
			}

			if (!OutData.BuildingReport.bSuccess)
			{
				OutData.ErrorMessage = OutData.BuildingReport.ErrorMessage;
				return EBuildResult::Failed;
			}

			if (OutData.BuildingReport.WaterRemovedCount > 0)
			{
				UE_LOG(LogWbCore, Log,
					TEXT("Regionen: %d Gebaeude im Wasser verworfen (kein Haus im See)."),
					OutData.BuildingReport.WaterRemovedCount);
			}
		}

		if (Cancelled()) { return EBuildResult::Cancelled; }

		if (Input.bGenerateTerrain && OutData.DemRaster.IsValid())
		{
			Report(85, EBuildStage::Terrain);
			// Das Tile entstand bereits vor den Strassen (siehe oben); hier wird
			// nur noch unter Strassen und Gebaeuden eingeebnet.

			if (Input.bGenerateBuildings && Input.TerrainSettings.bFlattenUnderBuildings)
			{
				OutData.TerrainReport.BuildingFlattenedCellCount =
					Tools.TerrainGenerator->FlattenUnderBuildings(
						OutData.OSMData, *Tools.Converter, Input.TerrainSettings, OutData.TerrainTile);
			}

			// REIHENFOLGE IST WESENTLICH: Gebaeude zuerst, Strassen danach.
			//
			// Beide Durchgaenge schreiben in dieselben Landscape-Zellen. Lief die
			// Gebaeude-Einebnung zuletzt, hob sie das Gelaende an strassennahen
			// Grundstuecken wieder ueber die Fahrbahn - gemessen 80 cm hoeher als
			// die Strassen-Einebnung es hinterlassen hatte. Die Strasse muss das
			// letzte Wort haben, sonst verschwindet sie unter dem Vorgarten.
			// Einebnung braucht den Fahrspur-Graphen bzw. die OSM-Grundrisse.
			if (Input.bGenerateRoads && Input.TerrainSettings.bFlattenUnderRoads)
			{
				OutData.TerrainReport.RoadFlattenedCellCount =
					Tools.TerrainGenerator->FlattenUnderRoads(
						OutData.RoadNetwork, Input.TerrainSettings, OutData.TerrainTile);
			}

			// Nachpruefung: Liegt die Fahrbahn nach dem Einebnen ueber Grund?
			//
			// "Strassensegmente fehlen" liess sich nicht durch fehlende
			// Geometrie erklaeren - alle 121.721 Segmente haben eine Flaeche.
			// Eine im Boden versunkene Strasse sieht aber genauso aus wie eine
			// fehlende. Zur Laufzeit gemessen lagen 10 % der Fahrbahn-Vertices
			// unter dem Gelaende, bis 2,28 m tief.
			//
			// Getrennt nach Ebene: Tunnel (Layer < 0) GEHOEREN unter die Erde,
			// ebenerdige Strassen nicht. Nur die zweite Zahl ist ein Fehler.
			if (OutData.TerrainTile.IsValid())
			{
				int32 GroundLevelChecked = 0;
				int32 GroundLevelBuried = 0;
				double WorstGroundBurialCm = 0.0;
				int32 TunnelChecked = 0;
				int32 BuriedOutsideTile = 0;
				double ClearanceSumCm = 0.0;
				double WorstClearanceCm = 0.0;
				int32 ClearanceCount = 0;

				for (const FRoadSegment& Segment : OutData.RoadNetwork.Segments)
				{
					const TArray<FVector>& Line = Segment.TrimmedCenterline.Num() >= 2
						? Segment.TrimmedCenterline
						: Segment.Centerline;

					for (const FVector& Point : Line)
					{
						if (Segment.Layer < 0)
						{
							++TunnelChecked;
							continue;
						}
						if (Segment.Layer > 0)
						{
							continue;   // Bruecken stehen bauartbedingt hoch
						}

						++GroundLevelChecked;

						const float TerrainZ = OutData.TerrainTile.SampleHeightBilinearCm(
							FVector2D(Point.X, Point.Y));

						const double Burial = TerrainZ - Point.Z;

						// BEIDE Seiten messen.
						//
						// Eine Verbesserung der Verdeckung, die nur die eine
						// Richtung betrachtet, erkauft sich die Zahl mit
						// schwebenden Fahrbahnraendern - im Spiel als
						// aufgerissene Strassenkante sichtbar. Genau dieser
						// Fehler ist hier schon einmal aufgetreten: Die
						// Fahrbahn schwebte im Mittel 91,6 cm, waehrend die
						// Verdeckungsstatistik gruen war.
						if (Burial <= 0.0)
						{
							ClearanceSumCm += -Burial;
							WorstClearanceCm = FMath::Max(WorstClearanceCm, -Burial);
							++ClearanceCount;
						}

						if (Burial > 0.0)
						{
							++GroundLevelBuried;
							WorstGroundBurialCm = FMath::Max(WorstGroundBurialCm, Burial);

							// Liegt der Punkt ueberhaupt INNERHALB der Kachel?
							// Ausserhalb liefert SampleHeightBilinearCm den
							// Randwert, und die Einebnung erreicht die Zelle
							// gar nicht - beides ergaebe scheinbare Verdeckung
							// ohne echten Erdaushub.
							const double TileMaxX = OutData.TerrainTile.WorldMinXY.X
								+ (OutData.TerrainTile.GridSize - 1) * OutData.TerrainTile.CellSizeCm;
							const double TileMaxY = OutData.TerrainTile.WorldMinXY.Y
								+ (OutData.TerrainTile.GridSize - 1) * OutData.TerrainTile.CellSizeCm;

							if (Point.X < OutData.TerrainTile.WorldMinXY.X || Point.X > TileMaxX
								|| Point.Y < OutData.TerrainTile.WorldMinXY.Y || Point.Y > TileMaxY)
							{
								++BuriedOutsideTile;
							}
						}
					}
				}

				if (GroundLevelChecked > 0)
				{
					UE_LOG(LogWbTerrain, Log,
						TEXT("Fahrbahn ueber Grund: %d von %d ebenerdigen Punkten liegen UNTER dem ")
						TEXT("Gelaende (%.1f %%), schlimmster Fall %.0f cm. Tunnelpunkte ")
						TEXT("uebersprungen: %d."),
						GroundLevelBuried, GroundLevelChecked,
						100.0 * GroundLevelBuried / GroundLevelChecked,
						WorstGroundBurialCm, TunnelChecked);

					UE_LOG(LogWbTerrain, Log,
						TEXT("Davon ausserhalb der Gelaendekachel: %d von %d."),
						BuriedOutsideTile, GroundLevelBuried);

					// Kreuzungen getrennt messen.
					//
					// Sie fallen bei der Segment-Messung durch: Die
					// Mittellinien sind an der Kreuzung getrimmt, im Inneren
					// liegt gar kein Messpunkt. Genau dort stand das Gras
					// durch die Platte.
					int32 JunctionPointsChecked = 0;
					int32 JunctionPointsBuried = 0;
					double WorstJunctionBurialCm = 0.0;

					for (const FRoadIntersection& Intersection : OutData.RoadNetwork.Intersections)
					{
						for (const FVector& Point : Intersection.Polygon)
						{
							++JunctionPointsChecked;
							const float TerrainZ = OutData.TerrainTile.SampleHeightBilinearCm(
								FVector2D(Point.X, Point.Y));
							const double Burial = TerrainZ - Point.Z;
							if (Burial > 0.0)
							{
								++JunctionPointsBuried;
								WorstJunctionBurialCm = FMath::Max(WorstJunctionBurialCm, Burial);
							}
						}
					}

					if (JunctionPointsChecked > 0)
					{
						UE_LOG(LogWbTerrain, Log,
							TEXT("Kreuzungsplatten: %d von %d Umrisspunkten liegen UNTER dem ")
							TEXT("Gelaende (%.1f %%), schlimmster Fall %.0f cm."),
							JunctionPointsBuried, JunctionPointsChecked,
							100.0 * JunctionPointsBuried / JunctionPointsChecked,
							WorstJunctionBurialCm);
					}

					if (ClearanceCount > 0)
					{
						UE_LOG(LogWbTerrain, Log,
							TEXT("Gegenprobe Freiraum: %d Punkte liegen ueber Grund, ")
							TEXT("im Mittel %.1f cm, hoechstens %.0f cm."),
							ClearanceCount, ClearanceSumCm / ClearanceCount, WorstClearanceCm);
					}
				}
			}

			// Terrain-Qualitaetskontrolle (datenrein): Warnt, wenn das Tile
			// deutlich groesser als die OSM-Ausdehnung ist (Crop fehlt?) oder
			// die Hoehenspanne unplausibel gross/klein ist. Editor zeigt sie im
			// Details-Panel, Runtime loggt sie in der Build-Zusammenfassung.
			// Die Schwellen kommen aus dem City-Prompt (Defaults 2.0 / 1..3000).
			OutData.TerrainQuality = UTerrainGenerator::CheckTerrainQuality(
				OutData.TerrainReport, OutData.OSMData.Bounds, *Tools.Converter,
				OutData.CityPromptSpec.TerrainMaxTileToOsmRatio,
				OutData.CityPromptSpec.TerrainMinHeightSpanMeters,
				OutData.CityPromptSpec.TerrainMaxHeightSpanMeters);
		}

		if (Cancelled()) { return EBuildResult::Cancelled; }

		// Strassenausstattung (Schilder, Leitpfosten, Halt-/Wartelinien).
		// Bewusst nicht-fatal: ein Fehler bricht den Gesamt-Build nicht ab.
		if (Input.bGenerateFurniture && Input.bGenerateRoads && Tools.FurnitureGenerator)
		{
			Report(92, EBuildStage::Furniture);
			// Die Gebaeude sind hier fertig (Stufe 68) - ihre Grundriss-Boxen
			// fangen im Moebel-Pass die OSM-Knoten ab, die im Haus liegen.
			OutData.FurnitureReport = Tools.FurnitureGenerator->Generate(
				OutData.RoadNetwork, &OutData.OSMData, Tools.Converter, HeightSampler,
				Input.FurnitureSettings, OutData.FurnitureLayout, &OutData.Buildings);

			if (!OutData.FurnitureReport.bSuccess)
			{
				UE_LOG(LogWbCore, Warning, TEXT("Strassenausstattung: %s"),
					*OutData.FurnitureReport.ErrorMessage);
			}
		}

		if (Cancelled()) { return EBuildResult::Cancelled; }

		// Pickup-Standorte (Treibstoff an Tankstellen, Gesundheit an Apotheken
		// und Krankenhaeusern). Ebenfalls nicht-fatal: Fehlende oder leere
		// OSM-Amenity-Daten duerfen den Stadt-Build nicht scheitern lassen.
		if (Input.bGeneratePickupSpots && Tools.PickupSpotGenerator)
		{
			Report(96, EBuildStage::PickupSpots);
			OutData.PickupSpotReport = Tools.PickupSpotGenerator->Generate(
				OutData.OSMData, *Tools.Converter, &OutData.RoadNetwork, HeightSampler,
				Input.PickupSpotSettings, OutData.PickupSpots);

			if (!OutData.PickupSpotReport.bSuccess)
			{
				UE_LOG(LogWbCore, Warning, TEXT("Pickup-Spots: %s"),
					*OutData.PickupSpotReport.ErrorMessage);
			}
		}

		// Terrain-Qualitaetswarnung in den Status einhaengen, damit sie auch
		// ueber GetCityStatus()/Blueprint abrufbar ist (nicht nur im Log).
		OutData.Status = OutData.TerrainQuality.AppendToStatus(FString::Printf(TEXT("%s; %s"),
			*OutData.ParseResult.ToString(), *OutData.RoadNetwork.GetStatisticsString()));

		Report(100, EBuildStage::Done);
		return EBuildResult::Success;
	}
}
