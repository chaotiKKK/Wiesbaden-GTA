// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Abstraktion der Hoehenabfrage.
 *
 * Strassen- und Gebaeudegenerator brauchen Terrainhoehen, sollen aber nicht vom
 * konkreten Hoehenmodell abhaengen: im Editor kommt die Hoehe aus einem
 * importierten DEM-Raster (UHeightmapImporter), in Tests aus einer analytischen
 * Funktion, und fuer Flachland-Testlevel aus einer Konstanten. Ohne diese
 * Trennung waere die Geometrieerzeugung nicht ohne geladenes Terrain testbar.
 */
class WIESBADENREAL_API IHeightSampler
{
public:
	virtual ~IHeightSampler() = default;

	/**
	 * Terrainhoehe an einer Weltposition.
	 * @param WorldXY Position in Unreal Units (cm), XY-Ebene.
	 * @return Hoehe in Unreal Units (cm). Ausserhalb des Datenbereichs wird der
	 *         Randwert geliefert (Clamping), nicht 0 - eine 0 wuerde am
	 *         Kartenrand eine senkrechte Klippe erzeugen.
	 */
	virtual double SampleHeightCm(const FVector2D& WorldXY) const = 0;

	/** True, wenn gueltige Hoehendaten vorliegen. */
	virtual bool HasValidData() const = 0;
};

/**
 * Hoehenmodell mit konstanter Hoehe. Fuer Unit-Tests und Flachland-Testlevel,
 * in denen das Terrain nicht Gegenstand des Tests ist.
 */
class WIESBADENREAL_API FFlatHeightSampler final : public IHeightSampler
{
public:
	explicit FFlatHeightSampler(double InHeightCm = 0.0)
		: HeightCm(InHeightCm)
	{
	}

	virtual double SampleHeightCm(const FVector2D& /*WorldXY*/) const override { return HeightCm; }
	virtual bool HasValidData() const override { return true; }

private:
	double HeightCm;
};
