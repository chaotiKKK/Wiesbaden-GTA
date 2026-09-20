// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenChaosWheels.h"

UWiesbadenWheelBase::UWiesbadenWheelBase()
{
	// Am Modell vermessen: Reifen 68,6 cm Durchmesser, 17,2 cm breit.
	WheelRadius = 34.3f;
	WheelWidth = 17.2f;

	// Federweg. Der Kaefer federt weich - 12 cm Einfederung und 8 cm
	// Ausfederung sind fuer einen Wagen von 1969 realistisch und lassen ihn
	// ueber Bordsteinkanten arbeiten, statt sie zu ueberspringen.
	// KOMPLEXE Kollision abtasten - das war die Ursache.
	//
	// Chaos prueft die Fahrbahn vorgabegemaess nur gegen EINFACHE
	// Kollisionsgeometrie:
	//
	//   SweepType = ESweepType::SimpleSweep;
	//   /** Sweeps against simple geometry only */
	//
	// Die Strassen dieser Stadt sind UProceduralMeshComponent, und die haben
	// ausschliesslich komplexe Kollision - die Dreiecke selbst, keine
	// Ersatzkoerper. Die Raeder tasteten damit an der Fahrbahn vorbei, ganz
	// gleich wie gross der Federweg war: Alle vier meldeten "Luft", der Wagen
	// ruhte auf seinem Fahrgestell, der Motor drehte lastfrei gegen den
	// Begrenzer.
	//
	// Der Weg dorthin ging ueber sechs falsche Erklaerungen - Koerper an den
	// Radknochen, geparkte Autos, fehlende Physiksimulation (die war ein
	// echter Teilfehler), Kollisionskanal, Schlafzustand, Federweg. Drei
	// Messgeraete waren dabei selbst defekt.
	SweepType = ESweepType::ComplexSweep;

	// Federweg bewusst GROSSZUEGIG.
	//
	// 8 cm auf und 12 cm ab entspraechen dem Kaefer, reichen hier aber nicht:
	// Chaos tastet nach der Fahrbahn nur bis SuspensionMaxDrop + WheelRadius
	// unter der Ruhelage, also bisher 46,3 cm. Gemessen lag der Boden unter
	// den vier Raedern 24 bis 62 cm darunter - ein Rad ausserhalb der
	// Reichweite, und die uebrigen so weit unten, dass der Wagen auf dem
	// Fahrgestell auflag statt auf den Reifen. Alle vier meldeten "Luft",
	// der Motor drehte ohne Last gegen den Begrenzer.
	//
	// 40 cm Ausfederung bringen die Reichweite auf 74,3 cm. Das ist mehr
	// Federweg, als ein Kaefer hat, aber ein Fahrzeug, das den Boden findet,
	// ist einem naturgetreuen Federweg vorzuziehen, der ihn verfehlt. Sobald
	// die Fahrprobe traegt, laesst sich der Wert wieder senken - dann mit
	// einer Messung als Grundlage statt einer Vermutung.
	SuspensionMaxRaise = 15.0f;
	SuspensionMaxDrop = 40.0f;

	// Federrate je Rad in N/cm-Naeherung der Engine. 820 kg auf vier Raedern
	// sind rund 205 kg je Rad; mit der weichen Auslegung des Kaefers ergibt
	// das eine deutlich niedrigere Rate als bei einem modernen Fahrzeug.
	SpringRate = 180.0f;
	SpringPreload = 50.0f;

	// Reifengriff. 1.0 ist der Vorgabewert der Engine fuer trockenen Asphalt.
	// Der Kaefer lief auf Diagonalreifen, die deutlich weniger Seitenfuehrung
	// hatten als heutige Guerteilreifen.
	FrictionForceMultiplier = 2.0f;

	MaxBrakeTorque = 1500.0f;
	MaxHandBrakeTorque = 0.0f;
}

UWiesbadenWheelFront::UWiesbadenWheelFront()
{
	AxleType = EAxleType::Front;

	bAffectedBySteering = true;
	bAffectedByBrake = true;
	bAffectedByEngine = false;

	// 36 Grad Einschlag -> Wendekreis rund 11 m bei 2,40 m Radstand, wie beim
	// Original.
	MaxSteerAngle = 36.0f;

	// Vorn traegt der Kaefer nur rund 40 Prozent des Gewichts. Die
	// Bremskraft ist entsprechend geringer als hinten - anders herum waere es
	// falsch, auch wenn es bei Frontmotorwagen so ist.
	MaxBrakeTorque = 1300.0f;
	MaxHandBrakeTorque = 0.0f;

	SpringRate = 160.0f;
}

UWiesbadenWheelRear::UWiesbadenWheelRear()
{
	AxleType = EAxleType::Rear;

	bAffectedBySteering = false;
	bAffectedByBrake = true;
	bAffectedByEngine = true;

	MaxSteerAngle = 0.0f;
	MaxBrakeTorque = 1500.0f;

	// Handbremse wirkt beim Kaefer auf die Hinterraeder.
	MaxHandBrakeTorque = 3000.0f;

	// Hinten liegt der Motor, hier steht mehr Gewicht auf der Achse.
	SpringRate = 200.0f;
}
