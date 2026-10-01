// Erzeugt von Blender/bugtank/export_bugtank_teile.py am Scene.
// Gelenk- und Spitzenpunkte in cm, Koerperraum, Ursprung = Koerpermitte.
#pragma once

#include "CoreMinimal.h"

/** Ein statisches Teil des Insekt-Rigs, Koerperraum in cm.
 * Gelenk = Drehpunkt, um den der Pawn das Teil dreht.
 * Spitze = vom Gelenkpunkt am weitesten entfernter Vertex des
 *   Teils, also Fusssohle bzw. Antennenkolben; im Test der Messpunkt.
 */
struct FBugTankTeil
{
	const TCHAR* Name;
	FVector Gelenk;
	FVector Spitze;
};

namespace BugTankTeile
{
	const FBugTankTeil Body{ TEXT("Body"), FVector(0.000f, 0.000f, 0.000f), FVector(62.561f, 6.532f, -1.216f) };
	const FBugTankTeil BeinL1_Ober{ TEXT("BeinL1_Ober"), FVector(28.000f, 12.000f, 4.000f), FVector(33.000f, 30.299f, 23.074f) };
	const FBugTankTeil BeinL1_Unter{ TEXT("BeinL1_Unter"), FVector(33.000f, 28.000f, 23.000f), FVector(39.439f, 30.440f, -55.105f) };
	const FBugTankTeil BeinL2_Ober{ TEXT("BeinL2_Ober"), FVector(2.000f, 13.000f, 2.000f), FVector(7.000f, 31.299f, 21.074f) };
	const FBugTankTeil BeinL2_Unter{ TEXT("BeinL2_Unter"), FVector(7.000f, 29.000f, 21.000f), FVector(13.440f, 31.441f, -55.120f) };
	const FBugTankTeil BeinL3_Ober{ TEXT("BeinL3_Ober"), FVector(-26.000f, 11.000f, 4.000f), FVector(-21.000f, 29.299f, 23.074f) };
	const FBugTankTeil BeinL3_Unter{ TEXT("BeinL3_Unter"), FVector(-21.000f, 27.000f, 23.000f), FVector(-14.561f, 29.440f, -55.105f) };
	const FBugTankTeil BeinR1_Ober{ TEXT("BeinR1_Ober"), FVector(28.000f, -12.000f, 4.000f), FVector(33.000f, -30.299f, 23.074f) };
	const FBugTankTeil BeinR1_Unter{ TEXT("BeinR1_Unter"), FVector(33.000f, -28.000f, 23.000f), FVector(39.439f, -30.440f, -55.105f) };
	const FBugTankTeil BeinR2_Ober{ TEXT("BeinR2_Ober"), FVector(2.000f, -13.000f, 2.000f), FVector(7.000f, -31.299f, 21.074f) };
	const FBugTankTeil BeinR2_Unter{ TEXT("BeinR2_Unter"), FVector(7.000f, -29.000f, 21.000f), FVector(13.440f, -31.441f, -55.120f) };
	const FBugTankTeil BeinR3_Ober{ TEXT("BeinR3_Ober"), FVector(-26.000f, -11.000f, 4.000f), FVector(-21.000f, -29.299f, 23.074f) };
	const FBugTankTeil BeinR3_Unter{ TEXT("BeinR3_Unter"), FVector(-21.000f, -27.000f, 23.000f), FVector(-14.561f, -29.440f, -55.105f) };
	const FBugTankTeil AntenneL{ TEXT("AntenneL"), FVector(51.000f, 7.500f, 12.500f), FVector(66.235f, 33.483f, 16.668f) };
	const FBugTankTeil AntenneR{ TEXT("AntenneR"), FVector(51.000f, -7.500f, 12.500f), FVector(66.235f, -33.483f, 16.668f) };
}
