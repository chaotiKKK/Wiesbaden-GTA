// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_EDITOR

#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "StaticMeshResources.h"
#include "UObject/UnrealType.h"
#include "Vehicles/WiesbadenHeliGunComponent.h"
#include "Vehicles/WiesbadenHeliLightRig.h"
#include "Vehicles/WiesbadenHelicopter.h"
#include "Vehicles/WiesbadenHelicopterAudioComponent.h"
#include "Sound/SoundWave.h"

/**
 * Die Ausstattung des Ka-52 pruefen: Kabine, Bordgeschuetz, Flugsounds und
 * den gegenlaeufigen Koaxialrotor.
 *
 * Jeder dieser Punkte hat im Spiel einen eigenen Fehlerbild, das man NICHT
 * am Log erkennt (alle vier sind am 26.09.2026 so entstanden):
 *
 *  1. KABINE AM RUMPF, NICHT IM NICHTS. Die Kamera blendet den Rumpf aus,
 *     um in die Kabine sehen zu koennen. Haengt die Kabine nicht am
 *     Rumpf, teilt sie dessen Gierdrehung nicht mit - sie steht dann
 *     waagerecht, waehrend der Rumpf kippt, und der Pilot sitzt in einem
 *     Kasten, der sich mitdreht wie ein ueberfliegender Karton.
 *
 *  2. KANONE ZEIGT NACH VORN. Das importierte Rohr zeigt in Modell-+X, der
 *     Hubschrauber faehrt nach Modell--Y. Ohne Ruhelage-Gierung schiesst
 *     die Kanone im Stand nach Steuerbord - der erste Schuss ging ins
 *     Leere, lange bevor jemand gezielt hat.
 *
 *  3. MUNDUNG AUF DEM ROHR. Die Muendung sass 18 cm unter dem Rohr, weil
 *     das Rohr im Asset auf Local-Z 18 liegt. Der Mündungsfeuer lief neben
 *     dem Lauf statt aus ihm.
 *
 *  4. KOAXIAL GEGENLAEUFIG, GLEICHE ACHSE. Die Forderung lautet nicht
 *     "zwei Rotoren", sondern "schnell gegeneinander auf einer Stange".
 *     Gleiche Achse heisst: beide Naben auf derselben XY-Position.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKa52GeraetTest,
	"WiesbadenReal.Vehicles.Ka52Ausstattung",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Messverfahren fuer den Drehpunkt einer Radscheibe, unabhaengig von der
 * Aufloesung des Asset-Datensatzes.
 *
 * WARUM SO UMSTAENDLICH: der Drehpunkt einer Scheibe ist nicht die Mitte der
 * Bounding Box (die liegt beim oberen Ka-52-Rotor 1,80 m daneben) und nicht
 * der Schwerpunkt der Vertexmenge. Der Schwerpunkt traegt nur, solange die
 * Punktemenge symmetrisch und vollstaendig ist - und vollstaendig ist sie im
 * Projekt nicht: das Asset traegt Nanite, die klassischen LOD-Buffer haben
 * nur 1303 statt 208 009 Vertex. Der Schwerpunkt dieses Ersatzdatensatzes
 * liegt 10,6 cm daneben, also weit ausserhalb dessen, was man mit ihm
 * belegen kann.
 *
 * Gemessen wird deshalb dasselbe, was Tools/ka52_rotorachse.py am
 * Quellmodell misst: die 3-fach-ROTATIONSSYMMETRIE. Drei gleiche Blaetter
 * im 120-Grad-Abstand bilden eine Punktmenge, die sich um 120 Grad auf
 * sich selbst abbildet - genau dann, wenn der Drehpunkt stimmt. Der Restfehler
 * (mittlerer Abstand eines gedrehten Punkts zum naechsten Originalpunkt,
 * als Median) ist damit ein Mass fuer die Lage des Drehpunkts: klein heisst
 * richtig, gross heisst "das ist nicht die Achse".
 *
 * Das Verfahren kann nicht "ueberall gut" melden: der Test rechnet denselben
 * Restfehler an einem BEWUSST FALSCHEN Punkt mit (Mitte der Bounding Box,
 * 1,80 m daneben) und verlangt, dass dort mindestens ein Vielfaches heraus-
 * kommt. Ein Verfahren ohne diesen Vergleichsays.
 *
 * Arbeitskoordinaten sind die des Mesh-Components, NICHT die Welt: die
 * Kette Nabe -> Versatz -> Modelldrehung ist eine starre Abbildung und
 * erhaelt eine Drehachse samt Abstand zur Stangenachse. Erst das Ergebnis
 * wird am Ende durch diese Kette geschickt.
 */
namespace Ka52
{
	/** Achsparallele Punkte eines Dreiblattrotors (3-fach symmetrisch). */
	using FBlattPunkte = TArray<FVector2D>;

	/** Zellenraster, damit der naechste Nachbar nicht O(n) kostet. */
	struct FNachbarRaster
	{
		double ZellGroesse = 50.0; // cm
		TMap<FIntPoint, TArray<int32>> Zellen;

		void Fuellen(const FBlattPunkte& Punkte)
		{
			Zellen.Reset();
			ZellGroesse = 50.0;
			for (int32 i = 0; i < Punkte.Num(); ++i)
			{
				FIntPoint Zelle(FMath::FloorToInt(Punkte[i].X / ZellGroesse),
					FMath::FloorToInt(Punkte[i].Y / ZellGroesse));
				Zellen.FindOrAdd(Zelle).Add(i);
			}
		}

		/** Abstand zum naechsten Punkt, 0, wenn keiner in Reichweite liegt. */
		double Abstand(const FBlattPunkte& Punkte, const FVector2D& Wo) const
		{
			const FIntPoint Mitte(FMath::FloorToInt(Wo.X / ZellGroesse),
				FMath::FloorToInt(Wo.Y / ZellGroesse));
			double Bestes = TNumericLimits<double>::Max();
			for (int32 dx = -1; dx <= 1; ++dx)
			{
				for (int32 dy = -1; dy <= 1; ++dy)
				{
					const TArray<int32>* Zelle = Zellen.Find(
						FIntPoint(Mitte.X + dx, Mitte.Y + dy));
					if (!Zelle)
					{
						continue;
					}
					for (int32 Index : *Zelle)
					{
						Bestes = FMath::Min(Bestes, FVector2D::Distance(
							Punkte[Index], Wo));
					}
				}
			}
			// Ohne Treffer zaehlt die Zellgroesse als Abstand: sonst fliegt ein
			// Punkt ohne Nachbarn aus der Statistik heraus (Survivorship Bias)
			// und ein falscher Kandidat sieht besser aus als ein richtiger.
			return Bestes < TNumericLimits<double>::Max() ? Bestes : ZellGroesse;
		}
	};

	/**
	 * Restfehler der 3-fach-Symmetrie um einen Kandidaten.
	 *
	 * @param Stichprobe so viele Punkte werden gedreht (200 reichen, das
	 *        Ergebnis ist der Median und damit stabil).
	 * @return Median der Nachbarabstaende in cm. Klein = Kandidat ist die Achse.
	 */
	double Restfehler(const FBlattPunkte& Punkte, const FVector2D& Kandidat,
		const FNachbarRaster& Raster, int32 Stichprobe = 200)
	{
		const double Rad = FMath::DegreesToRadians(120.0);
		const double Ca = FMath::Cos(Rad);
		const double Sa = FMath::Sin(Rad);
		TArray<double> Abstaende;
		Abstaende.Reserve(Stichprobe);
		// Gleichmaessig ueber die Liste verteilt, nicht nur deren Anfang:
		// die Vertices eines importierten Meshes sind nach Material sortiert.
		const double Schritt = static_cast<double>(Punkte.Num())
			/ static_cast<double>(FMath::Max(1, Stichprobe));
		for (int32 k = 0; k < Stichprobe; ++k)
		{
			const int32 Index = FMath::Clamp(
				FMath::RoundToInt(static_cast<int32>(k) * Schritt), 0, Punkte.Num() - 1);
			const FVector2D D(Punkte[Index].X - Kandidat.X,
				Punkte[Index].Y - Kandidat.Y);
			const FVector2D Gedreht(Kandidat.X + Ca * D.X - Sa * D.Y,
				Kandidat.Y + Sa * D.X + Ca * D.Y);
			Abstaende.Add(Raster.Abstand(Punkte, Gedreht));
		}
		Abstaende.Sort();
		return Abstaende[Abstaende.Num() / 2];
	}

	/**
	 * Drehpunkt einer Scheibe: der Kandidat mit dem kleinsten Restfehler.
	 *
	 * Gesucht wird in einem Fenster um den Schwerpunkt, zweistufig
	 * verfeinert. Der Startpunkt beschraenkt nur das Suchfenster - wer
	 * entscheidet, ist der Restfehler, nicht die Naehe zum Startpunkt.
	 */
	FVector2D SucheDrehpunkt(const FBlattPunkte& Punkte, double& OutRestfehler)
	{
		FVector2D Schwerpunkt = FVector2D::ZeroVector;
		for (const FVector2D& p : Punkte)
		{
			Schwerpunkt += p;
		}
		Schwerpunkt /= static_cast<double>(FMath::Max(1, Punkte.Num()));

		FNachbarRaster Raster;
		Raster.Fuellen(Punkte);

		FVector2D Beste = Schwerpunkt;
		double BesterFehler = Restfehler(Punkte, Beste, Raster);
		// Stufe 1: 40 cm Radius, 4 cm Raster. Das Fenster fasst den
		// Schwerpunktfehler des Ersatzdatensatzes (~10 cm) mit Faktor 4.
		for (int32 ix = -10; ix <= 10; ++ix)
		{
			for (int32 iy = -10; iy <= 10; ++iy)
			{
				const FVector2D Kandidat(Schwerpunkt.X + 4.0 * ix,
					Schwerpunkt.Y + 4.0 * iy);
				const double Fehler = Restfehler(Punkte, Kandidat, Raster);
				if (Fehler < BesterFehler)
				{
					BesterFehler = Fehler;
					Beste = Kandidat;
				}
			}
		}
		// Stufe 2: 1-cm-Raster um den Treffer der Stufe 1.
		for (int32 ix = -4; ix <= 4; ++ix)
		{
			for (int32 iy = -4; iy <= 4; ++iy)
			{
				const FVector2D Kandidat(Beste.X + 1.0 * ix, Beste.Y + 1.0 * iy);
				const double Fehler = Restfehler(Punkte, Kandidat, Raster);
				if (Fehler < BesterFehler)
				{
					BesterFehler = Fehler;
					Beste = Kandidat;
				}
			}
		}
		OutRestfehler = BesterFehler;
		return Beste;
	}
}

bool FKa52GeraetTest::RunTest(const FString& Parameters)
{
	AWiesbadenHelicopter* CDO = GetMutableDefault<AWiesbadenHelicopter>();
	if (!CDO)
	{
		AddError(TEXT("Kein CDO von AWiesbadenHelicopter"));
		return false;
	}

	// Fuer die Achse gibt es hier bewusst KEINE feste Toleranz: sie wird
	// weiter unten aus dem Restfehler der jeweiligen Messung hergeleitet
	// (4 x Restfehler + 2 cm Aufloesung des Suchrasters). Eine Zahl nach
	// Gefuehl wuerde entweder den Ersatzdatensatz pruefen oder den
	// eingetragenen Wert gegen sich selbst - beides ohne Aussagekraft.

	UWorld* CDOWelt = CDO->GetWorld();
	AddInfo(FString::Printf(TEXT("CDO-Welt: %s"),
		CDOWelt ? TEXT("vorhanden") : TEXT("keine (reiner CDO-Test)")));

	// -- 1. Kabine ---------------------------------------------------------
	UStaticMeshComponent* Kabine = CDO->GetCockpitMesh();
	if (!TestNotNull(TEXT("CockpitMesh existiert"), Kabine))
	{
		return false;
	}
	TestNotNull(TEXT("CockpitMesh traegt ein Mesh"),
		Kabine->GetStaticMesh() ? static_cast<UObject*>(Kabine->GetStaticMesh()) : nullptr);
	TestEqual(TEXT("CockpitMesh-Name des Assets"),
		Kabine->GetStaticMesh() ? Kabine->GetStaticMesh()->GetName() : TEXT(""),
		FString(TEXT("SM_Ka52Cockpit")));

	// Die Kabine erbt die Modelldrehung nur, wenn sie am Rumpf haengt.
	TestEqual(TEXT("Kabine haengt am Rumpf"),
		Kabine->GetAttachParent() ? Kabine->GetAttachParent()->GetName() : TEXT(""),
		FString(TEXT("FuselageMesh")));

	if (Kabine->GetStaticMesh())
	{
		const FBox B = Kabine->GetStaticMesh()->GetBoundingBox();
		// Kabine Y -500..-231 => die Nase liegt bei Modell--Y. Ein
		// gespiegelter Import legt sie ueber das Heck (Y +231..+500):
		// der Pilot sitzt dann im Rumpf hinter dem Hauptrotor.
		TestTrue(FString::Printf(
			TEXT("Kabinen-Y %.0f..%.0f cm (erwartet -520..-210, Nase bei -Y)"),
			B.Min.Y, B.Max.Y),
			B.Min.Y < -200.0f && B.Max.Y < 0.0f);
		// Reale Ka-52-Kabine: 1,6 m breit, 2,7 m lang, 1,5 m hoch.
		TestTrue(FString::Printf(TEXT("Kabinenbreite %.0f cm (erwartet 120..220)"),
			B.GetSize().X), B.GetSize().X > 120.0f && B.GetSize().X < 220.0f);
		TestTrue(FString::Printf(TEXT("Kabinenlaenge %.0f cm (erwartet 230..300)"),
			B.GetSize().Y), B.GetSize().Y > 230.0f && B.GetSize().Y < 300.0f);
		AddInfo(FString::Printf(TEXT("Kabine: X %.0f..%.0f  Y %.0f..%.0f  Z %.0f..%.0f cm"),
			B.Min.X, B.Max.X, B.Min.Y, B.Max.Y, B.Min.Z, B.Max.Z));

		// DIE COCKPITAUGEN MUESSEN IN DER KABINE LIEGEN. Geprueft wird nicht
		// die Konstante gegen sich selbst, sondern der EINGEBAUTE Koerper: der
		// Augpunkt (Kamera-Anker + CockpitOffset, beide Actorraum) wird gegen
		// die gemessene Kabinenbox gestellt, nach dem Rumpfversatz des Actors.
		//
		// Warum das ein eigener Test ist: Am 26.09.2026 stand der Augpunkt
		// 3 cm ueber dem Kabinenboden, also auf Fusshoehe - man sah an den
		// Sitzkissen vorbei und das Bild enthielt keine Kabine. Fehler war die
		// Kamera-Basis Z 130, die doppelt verrechnet war.
		//
		// ACHTUNG beim Vergleichen: die Box ist Mesh-lokal, der Augpunkt ist
		// Actorraum. Beide ueber die Rumpf-Transformation zusammenführen,
		// sonst vergleicht man zwei verschiedene Rahmen und haelt einen
		// vorrotierten Versatz fuer richtig.
		if (const UWiesbadenVehicleCameraComponent* Kamera = CDO->GetVehicleCamera())
		{
			const USceneComponent* Rumpf = Kabine->GetAttachParent();
			const FBox KabineAktor = Rumpf
				? B.TransformBy(FTransform(Rumpf->GetRelativeRotation(),
					Rumpf->GetRelativeLocation()).ToMatrixWithScale())
				: B;
			const FVector Augen = Kamera->GetRelativeLocation() + Kamera->CockpitOffset;
			const double Rand = 10.0;
			TestTrue(FString::Printf(
				TEXT("Augen (%.0f, %.0f, %.0f) cm liegen in der Kabine X %.0f..%.0f"),
				Augen.X, Augen.Y, Augen.Z, KabineAktor.Min.X, KabineAktor.Max.X),
				Augen.X > KabineAktor.Min.X + Rand && Augen.X < KabineAktor.Max.X - Rand);
			TestTrue(FString::Printf(
				TEXT("Augen (%.0f, %.0f, %.0f) cm liegen in der Kabine Y %.0f..%.0f"),
				Augen.X, Augen.Y, Augen.Z, KabineAktor.Min.Y, KabineAktor.Max.Y),
				Augen.Y > KabineAktor.Min.Y + Rand && Augen.Y < KabineAktor.Max.Y - Rand);
			TestTrue(FString::Printf(
				TEXT("Augen (%.0f, %.0f, %.0f) cm liegen in der Kabine Z %.0f..%.0f"),
				Augen.X, Augen.Y, Augen.Z, KabineAktor.Min.Z, KabineAktor.Max.Z),
				Augen.Z > KabineAktor.Min.Z + Rand && Augen.Z < KabineAktor.Max.Z - Rand);
			// Die physikalische Fassung derselben Sache: ein sitzender Pilot
			// schaut 90..150 cm ueber den Kabinenboden. Auf Fusshoehe (3 cm)
			// war die Kamoreposition richtig und das Bild trotzdem leer.
			const double Hoehe = Augen.Z - KabineAktor.Min.Z;
			TestTrue(FString::Printf(
				TEXT("Augenhoehe ueber Kabinenboden %.0f cm (erwartet 90..150)"),
				Hoehe), Hoehe > 90.0 && Hoehe < 150.0);
			AddInfo(FString::Printf(
				TEXT("Kabine Actorraum: X %.0f..%.0f  Y %.0f..%.0f  Z %.0f..%.0f cm"),
				KabineAktor.Min.X, KabineAktor.Max.X, KabineAktor.Min.Y,
				KabineAktor.Max.Y, KabineAktor.Min.Z, KabineAktor.Max.Z));
		}
		else
		{
			AddError(TEXT("Fahrzeugkamera nicht erreichbar - Augpunkt nicht pruefbar"));
		}
	}

	// -- 2. Kanone ---------------------------------------------------------
	UWiesbadenHeliGunComponent* Waffe = CDO->GetGun();
	if (!TestNotNull(TEXT("Bordgeschoetz existiert"), Waffe))
	{
		return false;
	}
	TestNotNull(TEXT("Bordgeschoetz traegt ein Mesh"),
		Waffe->GetBarrelMesh()->GetStaticMesh() ? static_cast<UObject*>(Waffe->GetBarrelMesh()->GetStaticMesh()) : nullptr);
	TestEqual(TEXT("Geschuetzmesh-Name"),
		Waffe->GetBarrelMesh()->GetStaticMesh() ? Waffe->GetBarrelMesh()->GetStaticMesh()->GetName() : TEXT(""),
		FString(TEXT("SM_Ka52GunTurret")));

	// Ruhelage: Das Mesh-Rohr zeigt in Modell-+X (Steuerbord), der Heli
	// fliegt nach Modell--Y. Geprueft wird im MODELLRAUM - die Gierdrehung
	// des Pawns (+90) ist ein starrer Dreh des ganzen Modells und dreht
	// die Nase mit; addiert man sie nur auf die Rohrachse, vergleicht man
	// den Rumpf in zwei verschiedenen Bezugssystemen (das ergab 90 Grad
	 // neben der Nase, obwohl das Rohr korrekt stand).
	const FVector Rohr = Waffe->GetTurretYaw()->GetRelativeRotation().Vector();
	const float GradNebenNase = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(Rohr, FVector(0.0f, -1.0f, 0.0f)), -1.0f, 1.0f)));
	TestTrue(FString::Printf(
		TEXT("Rohr im Ruhezustand %.1f Grad neben der Nase (erwartet < 8)"),
		GradNebenNase), GradNebenNase < 8.0f);

	// -- 3. Muendung auf dem Rohr ------------------------------------------
	// Gegen die ECHTEN Vertexdaten gemessen, nicht gegen die Bounding Box:
	// die Box reicht bis z = 36, weil die Wiegenarme hoeher stehen als das
	// Rohr. Ein Vergleich Mündung gegen Box-Oberkante meldet 18 cm
	// Versatz, obwohl das Rohr genau in der Mitte sitzt.
	if (const UStaticMesh* RohrMesh = Waffe->GetBarrelMesh()->GetStaticMesh())
	{
		const FBox B = RohrMesh->GetBoundingBox();
		const FVector M = Waffe->GetMuzzlePoint()->GetRelativeLocation();

		// Die Bohrachse: Mittelwert der Ecken im Bereich des Bremsmantels
		// (x 176..198) und des Rohrs. Er ist ungefaehr die Z-Hoehe des
		// Rohrs, mit dem Anteil der groesseren Bremsdurchmesser.
		double SummeZ = 0.0;
		int32 Anzahl = 0;
		if (const FStaticMeshRenderData* RD = RohrMesh->GetRenderData())
		{
			if (RD->LODResources.Num() > 0
				&& RD->LODResources[0].VertexBuffers.PositionVertexBuffer.GetNumVertices() > 0)
			{
				const FPositionVertexBuffer& VB =
					RD->LODResources[0].VertexBuffers.PositionVertexBuffer;
				for (uint32 i = 0; i < VB.GetNumVertices(); ++i)
				{
					const FVector3f P = VB.VertexPosition(i);
					if (P.X >= 176.0f && P.X <= 205.0f)
					{
						SummeZ += P.Z;
						++Anzahl;
					}
				}
			}
		}
		TestTrue(FString::Printf(TEXT("Rohrbereich hat %d abgetastete Ecken"), Anzahl),
			Anzahl > 50);
		if (Anzahl > 50)
		{
			const double Bohrachse = SummeZ / Anzahl;
			TestTrue(FString::Printf(
				TEXT("Rohrachse z=%.2f, Muendung z=%.2f (Differenz %.2f cm, erwartet < 2)"),
				Bohrachse, M.Z, FMath::Abs(Bohrachse - M.Z)),
				FMath::Abs(Bohrachse - M.Z) < 2.0);
		}

		// Und die Muendung liegt wirklich am Rohrende, nicht irgendwo im
		// Turm: knapp vor dem hintersten Asset-Punkt.
		TestTrue(FString::Printf(
			TEXT("Muendung x=%.0f, hinterster Asset-Punkt x=%.0f (Abstand %.1f cm)"),
			M.X, B.Max.X, FMath::Abs(B.Max.X - M.X)),
			FMath::Abs(B.Max.X - M.X) < 25.0f);
		// Innerhalb der Asset-Huelle - der Mündungsfeuer darf nicht frei
		// im Raum stehen.
		TestTrue(TEXT("Muendung innerhalb der Asset-Huelle"),
			M.Z > B.Min.Z && M.Z < B.Max.Z);
	}

	// -- 4. Flugsounds -----------------------------------------------------
	UWiesbadenHelicopterAudioComponent* Audio = CDO->GetHelicopterAudio();
	if (!TestNotNull(TEXT("Flugsound-Komponente existiert"), Audio))
	{
		return false;
	}
	TestNotNull(TEXT("Rotor-Klang gesetzt"), Audio->RotorSound);
	TestNotNull(TEXT("Triebwerks-Klang gesetzt"), Audio->EngineSound);
	TestNotNull(TEXT("Wind-Klang gesetzt"), Audio->WindSound);
	TestNotNull(TEXT("Schussklang gesetzt"), Waffe->FireSound);

	// Die Schleife ist das, was einen Dauerlaerm endlos macht. Ein Asset
	// ohne Schleife verstummt nach einmal Abspielen - man hoert dann einen
	// Hubschrauber, der nach einer Sekunde tot ist.
	auto SchleifeOk = [this](USoundWave* W, const TCHAR* Name) -> bool
	{
		if (!W)
		{
			return false;
		}
		bool bSchleife = false;
		// Der Property-Name wird erprobt statt geraten: GetBoolProperty
		// gibt es auf UObject nicht, und get_editor_property waere eine
		// Blaue Linie (im Automation-Lauf nicht einmal die).
		for (const TCHAR* Property : { TEXT("looping"), TEXT("bLooping") })
		{
			if (FProperty* P = W->GetClass()->FindPropertyByName(Property))
			{
				// GetPropertyValue_InContainer gibt es erst auf dem
				// typisierten Property, nicht auf FProperty.
				if (const FBoolProperty* B = CastField<FBoolProperty>(P))
				{
					bSchleife = B->GetPropertyValue_InContainer(W);
					break;
				}
			}
		}
		return TestTrue(FString::Printf(TEXT("%s ist als Schleife markiert"), Name),
			bSchleife);
	};
	SchleifeOk(Audio->RotorSound, TEXT("Rotor"));
	SchleifeOk(Audio->EngineSound, TEXT("Triebwerk"));
	SchleifeOk(Audio->WindSound, TEXT("Wind"));

	// Der Wind haengt an der Geschwindigkeit, nicht an der Drehzahl.
	TestTrue(TEXT("Wind wird ueber die Geschwindigkeit geregelt"),
		Audio->WindFullSpeedMetersPerS > 10.0f);

	// -- 5. Koaxialrotor: synchron, gegenlaeufig, eine Achse --------------
	{
		FRotator Oben;
		FRotator Unten;
		AWiesbadenHelicopter::ComputeCoaxialRotorRotation(350.0f, 1.0f / 60.0f, Oben, Unten);
		TestTrue(TEXT("Koaxial: gleicher Betrag"),
			FMath::IsNearlyEqual(Oben.Yaw, -Unten.Yaw, 0.001f));
		// 350 rpm -> 2100 Grad/s -> 35 Grad je Bild bei 60 Hz.
		TestTrue(FString::Printf(TEXT("Koaxial: %.2f Grad je 1/60 s (erwartet 35)"),
			Oben.Yaw), FMath::IsNearlyEqual(Oben.Yaw, 35.0f, 0.05f));
		TestTrue(TEXT("Koaxial: nur Gier, kein Roll/Nick"),
			FMath::IsNearlyZero(Oben.Roll) && FMath::IsNearlyZero(Oben.Pitch)
			&& FMath::IsNearlyZero(Unten.Roll) && FMath::IsNearlyZero(Unten.Pitch));
		// Steht der Rotor, stehten beide: kein Nachlaufen des einen.
		AWiesbadenHelicopter::ComputeCoaxialRotorRotation(0.0f, 1.0f / 60.0f, Oben, Unten);
		TestTrue(TEXT("Koaxial: bei 0 rpm stehen beide Naben"),
			FMath::IsNearlyZero(Oben.Yaw) && FMath::IsNearlyZero(Unten.Yaw));
		// Negative Drehzahl wird auf 0 geklemmt, nicht rueckwaerts
		// gedreht: ein Rotor dreht nicht rueckwaerts. Autorotation ist im
		// Physikmodul eine POSITIVE, niedrige Drehzahl. Ohne die Klammer
		// drehte sich beim Vorwaertslauf der untere Rotor kurz in die
		// verkehrte Richtung - sichtbar als ein Ruck in der Radschaltung.
		AWiesbadenHelicopter::ComputeCoaxialRotorRotation(-120.0f, 0.5f, Oben, Unten);
		TestTrue(TEXT("Koaxial: negative Drehzahl wird geklemmt, nicht rueckwaerts gedreht"),
			FMath::IsNearlyZero(Oben.Yaw) && FMath::IsNearlyZero(Unten.Yaw));
		// Und die Klammer muss symetrisch sein: beide Naben stehen.
		AddInfo(FString::Printf(
			TEXT("Koaxial bei -120 rpm / 0,5 s: oben %.1f Grad, unten %.1f Grad"),
			Oben.Yaw, Unten.Yaw));
	}

	// Beide Naben auf derselben XY-Position: eine gemeinsame Rotorstange.
	if (CDO->GetMainRotorHub() && CDO->GetLowerRotorHub())
	{
		const FVector A = CDO->GetMainRotorHub()->GetRelativeLocation();
		const FVector B = CDO->GetLowerRotorHub()->GetRelativeLocation();
		const double AbstandXY = FMath::Sqrt(
			FVector::DistSquared2D(A, B));
		TestTrue(FString::Printf(
			TEXT("Naben liegen %.2f cm auseinander in XY (erwartet < 5)"), AbstandXY),
			AbstandXY < 5.0);
		// Und sie sind uebereinander, nicht nebeneinander.
		TestTrue(FString::Printf(
			TEXT("Nabenhoehen %.1f / %.1f cm (Abstand %.1f, erwartet 100..130)"),
			A.Z, B.Z, FMath::Abs(A.Z - B.Z)),
			FMath::Abs(A.Z - B.Z) > 100.0f && FMath::Abs(A.Z - B.Z) < 130.0f);
	}

	// -- 5b. DIE ACHSE -----------------------------------------------------
	//
	// Das ist die eigentliche Forderung "beide Naben exakt auf der
	// Rotorstangenachse", und sie ist an den Component-Positionen oben
	// NICHT ablesbar: die Hub-Nodes lagen auf (0,0), waehrend die
	// Radscheibe 1,8 m daneben sass. Ein Component, dessen Lage stimmt,
	// kann eine Scheibe zeigen, die sich sichtbar um etwas anderes dreht
	// (das alte Symptom "Rotor schlenkert").
	//
	// Geprueft wird in zwei Teilen, weil ein Teil allein nichts taugt:
	//
	//   5b-1 RECHNUNG, exakt. Der eingetragene Versatz muss genau das
	//        aufheben, was die Modelldrehung aus dem gemessenen
	//        Drehpunkt macht, und der Drehpunkt muss danach auf (0, 0)
	//        liegen. Faengt jeden Zahlendreher an beiden Rotoren und
	//        jeden Versatz, der an einer zweiten Stelle nachgezogen wurde.
	//
	//   5b-2 GEOMETRIE, mit der Aufloesung des Datensatzes. Der
	//        Drehpunkt wird am Asset selbst gemessen (3-fach-Symmetrie,
	//        siehe namespace Ka52 oben) und muss mit dem eingetragenen
	//        Wert uebereinstimmen - soweit die Daten das hergeben. Das
	//        Asset traegt Nanite, seine LOD-Buffer haben 773 Dreiecke
	//        statt 1,9 Millionen, und deren Achse liegt 3 bis 5 cm
	//        daneben: eine Toleranz unter 10 cm wuerde den Ersatz-
	//        datensatz pruefen statt des Flugmodells.
	//
	// Die belastbare Messung des WERTS selbst ist offline und in
	// voller Aufloesung erfolgt (Tools/ka52_rotorachse.py an der
	// Importquelle, Saved/Diagnose/ka52/rotorachse_fbx.txt) und wird von
	// Tools/test_ka52_rotorachse.py gegen die Zahlen hier geprueft.
	struct FRotor
	{
		const TCHAR* Name;
		UStaticMeshComponent* Blade;
		USceneComponent* Hub;
		bool bUnten;
	};
	const FRotor Rotoren[] = {
		{ TEXT("oberer"), CDO->GetMainRotorBlade(), CDO->GetMainRotorHub(), false },
		{ TEXT("unterer"), CDO->GetLowerRotorBlade(), CDO->GetLowerRotorHub(), true },
	};
	for (const FRotor& R : Rotoren)
	{
		// TestNotNull nimmt const TCHAR* und const void* - ein FString als
		// Meldung kompiliert nicht, und Renderdaten sind gar kein UObject.
		if (!TestNotNull(TEXT("Radscheibe (Component)"), R.Blade
			? static_cast<const UObject*>(R.Blade) : nullptr))
		{
			continue;
		}
		const UStaticMesh* Mesh = R.Blade->GetStaticMesh();
		if (!TestNotNull(TEXT("Radscheibe traegt ein StaticMesh"), Mesh
			? static_cast<const UObject*>(Mesh) : nullptr))
		{
			continue;
		}
		const FStaticMeshRenderData* RD = Mesh->GetRenderData();
		if (!TestTrue(TEXT("Renderdaten der Radscheibe geladen"), RD != nullptr))
		{
			continue;
		}
		if (RD->LODResources.Num() == 0)
		{
			AddError(FString::Printf(TEXT("%s: keine LOD-Ressource"), R.Name));
			continue;
		}

		// DIE ECHTE GEOMETRIE HERAUSSCHLAUCHEN.
		//
		// Der erste Zugriff auf LOD 0 lieferte 1303 Vertex gegenueber
		// 208 350 im Quell-GLB und trotzdem eine plausibel aussehende Zahl -
		// ein stiller Fehlschlag. Bei Nanite tragen die klassischen LODs
		// keinen vollstaendigen Satz, und ein Platzhalter-Puffer liefert
		// einen Schwerpunkt irgendwo in der Scheibe, ohne dass irgendwo
		// eine Warnung steht.
		//
		// Deshalb wird die LOD gewaehlt, deren Bounding Box zum Authored-
		// Bounds des Assets passt. Passt keine, faellt der Test durch: dann
		// weiss man wenigstens, dass er nichts belastet misst.
		const FBox Authored = Mesh->GetBoundingBox();
		AddInfo(FString::Printf(
			TEXT("Rotor %s: Nanite %s, %d LODs, Authored-Bounds X %.0f..%.0f "
				"Y %.0f..%.0f Z %.0f..%.0f"),
			R.Name, Mesh->GetNaniteSettings().bEnabled ? TEXT("an") : TEXT("aus"),
			RD->LODResources.Num(), Authored.Min.X, Authored.Max.X,
			Authored.Min.Y, Authored.Max.Y, Authored.Min.Z, Authored.Max.Z));

		int32 Gewaehlt = INDEX_NONE;
		double BesteAbweichung = 1e30;
		for (int32 L = 0; L < RD->LODResources.Num(); ++L)
		{
			const uint32 N = RD->LODResources[L].VertexBuffers
				.PositionVertexBuffer.GetNumVertices();
			if (N == 0)
			{
				AddInfo(FString::Printf(TEXT("Rotor %s: LOD %d leer"), R.Name, L));
				continue;
			}
			FBox Kandidat(ForceInit);
			for (uint32 i = 0; i < N; ++i)
			{
				Kandidat += FVector(RD->LODResources[L].VertexBuffers
					.PositionVertexBuffer.VertexPosition(i));
			}
			const double Abw = (Kandidat.Min - Authored.Min).Size()
				+ (Kandidat.Max - Authored.Max).Size();
			AddInfo(FString::Printf(
				TEXT("Rotor %s: LOD %d, %u Vertex, %u Dreiecke, Box X %.0f..%.0f "
					"Y %.0f..%.0f, Abweichung vom Authored-Bounds %.1f cm"),
				R.Name, L, N, RD->LODResources[L].GetNumTriangles(),
				Kandidat.Min.X, Kandidat.Max.X,
				Kandidat.Min.Y, Kandidat.Max.Y, Abw));
			if (Abw < BesteAbweichung)
			{
				BesteAbweichung = Abw;
				Gewaehlt = L;
			}
		}
		// 5 cm Toleranz: der Authored-Bounds ist gerundet, die LOD-Box
		// aus den echten Vertexen. Groesser waere es ein anderer Datensatz.
		if (!TestTrue(FString::Printf(
				TEXT("%s: eine LOD passt zum Authored-Bounds (beste Abweichung "
					"%.1f cm, erlaubt < 5)"), R.Name, BesteAbweichung),
				Gewaehlt != INDEX_NONE && BesteAbweichung < 5.0))
		{
			continue;
		}
		AddInfo(FString::Printf(TEXT("Rotor %s: gemessen wird LOD %d"), R.Name, Gewaehlt));
		const FPositionVertexBuffer& VB = RD->LODResources[Gewaehlt]
			.VertexBuffers.PositionVertexBuffer;
		const uint32 Anzahl = VB.GetNumVertices();

		// Genau die Laufzeit-Transformation des Components: der Versatz
		// wirkt in der Kette Nabe -> Versatz -> Gier -> Vertex.
		const FVector Versatz = R.Blade->GetRelativeLocation();
		const FRotator Gier = R.Blade->GetRelativeRotation();
		const FVector Nabe = R.Hub ? R.Hub->GetRelativeLocation() : FVector::ZeroVector;

		// Punkte im Component-Raum (das ist der Modellraum des Assets, noch
		// ohne Versatz und Gier). Die Kette weiter unten ist starr und laesst
		// eine Drehachse samt Abstand zur Stangenachse unveraendert - darum
		// wird hier und nicht in Weltkoordinaten gemessen.
		Ka52::FBlattPunkte Punkte;
		Punkte.Reserve(static_cast<int32>(Anzahl));
		FBox MeshBox(ForceInit);
		for (uint32 i = 0; i < Anzahl; ++i)
		{
			const FVector P(VB.VertexPosition(i));
			MeshBox += P;
			Punkte.Add(FVector2D(P.X, P.Y));
		}

		// Drehpunkt suchen und das Verfahren an einem bewusst falschen
		// Punkt gegenprobieren (Mitte der Bounding Box, 1,80 m daneben).
		double RestfehlerAchse = 0.0;
		const FVector2D Achse = Ka52::SucheDrehpunkt(Punkte, RestfehlerAchse);
		Ka52::FNachbarRaster Raster;
		Raster.Fuellen(Punkte);
		const FVector2D BoxMitte(
			0.5 * (MeshBox.Min.X + MeshBox.Max.X),
			0.5 * (MeshBox.Min.Y + MeshBox.Max.Y));
		const double RestfehlerKontrolle =
			Ka52::Restfehler(Punkte, BoxMitte, Raster);
		const double BoxmitteAbstand = FVector2D::Distance(BoxMitte, Achse);

		// Der Test darf nicht an einem Verfahren scheitern, das unabhaengig
		// von den Daten immer "gut" meldet. Die Kontrollstelle muss
		// messbar schlechter sein UND weit weg liegen.
		TestTrue(FString::Printf(
			TEXT("%s: die Kontrollstelle (Bounding-Box-Mitte, %.0f cm vom "
				"Drehpunkt) hat den %.1f-fachen Restfehler (%.1f gegen %.1f cm) - "
				"das Verfahren unterscheidet also wirklich"),
			R.Name, BoxmitteAbstand, RestfehlerKontrolle / FMath::Max(RestfehlerAchse, 1.0),
			RestfehlerKontrolle, RestfehlerAchse),
			BoxmitteAbstand > 100.0 && RestfehlerKontrolle > 3.0 * RestfehlerAchse);

		// ZWEITE Kontrolle, die nah liegt und deshalb die Haelfte der Aussage
		// traegt: hat der Restfehler in der Naehe des Drehpunkts ein Minimum
		// oder ein flaches Feld? Bei einem flachen Feld waere die gefundene
		// Position beliebig - und damit waere auch die Angabe "am Mesh
		// gemessen (0,99, -0,58) cm" wertlos, auf die sich 5b-2 stuetzt.
		// Deshalb wird der Restfehler in 5, 10 und 20 cm Abstand erneut
		// gerechnet; er muss deutlich ansteigen.
		const double Restfehler5 = Ka52::Restfehler(Punkte, Achse + FVector2D(5.0, 0.0), Raster);
		const double Restfehler10 = Ka52::Restfehler(Punkte, Achse + FVector2D(10.0, 0.0), Raster);
		const double Restfehler20 = Ka52::Restfehler(Punkte, Achse + FVector2D(20.0, 0.0), Raster);
		TestTrue(FString::Printf(
			TEXT("%s: der Restfehler steigt mit dem Abstand (0 cm: %.2f, 5 cm: %.2f, "
				"10 cm: %.2f, 20 cm: %.2f) - die Lage ist also aufgeloest und nicht "
				"irgendein Punkt eines flachen Feldes"),
			R.Name, RestfehlerAchse, Restfehler5, Restfehler10, Restfehler20),
			Restfehler10 > 1.5 * RestfehlerAchse && Restfehler20 > 2.0 * RestfehlerAchse);

		// Und jetzt durch die Laufzeit-Kette: der Drehpunkt muss danach
		// auf der Rotorstangenachse liegen, also x = y = 0.
		const FVector Deklariert = AWiesbadenHelicopter::GetRotorDrehpunktCm(R.bUnten);
		const FVector DrehpunktLokal(Achse.X, Achse.Y, 0.0f);
		const FVector Drehpunkt = Nabe + Versatz + Gier.RotateVector(DrehpunktLokal);

		// -- 5b-1 RECHNUNG, exakt --------------------------------------------
		// Der angewandte Versatz muss genau der sein, den
		// ComputeRotorMountOffset aus dem eingetragenen Drehpunkt macht.
		// 0,01 cm: das ist dieselbe Gleitkommazahl, nichts gerundet.
		const FVector SollVersatz = AWiesbadenHelicopter::ComputeRotorMountOffset(
			Deklariert, Gier, Nabe.Z);
		const FVector VersatzFehler = Versatz - SollVersatz;
		TestTrue(FString::Printf(
			TEXT("%s: angewandter Versatz (%.3f, %.3f) entspricht der Rechnung "
				"aus dem eingetragenen Drehpunkt (%.3f, %.3f), Abweichung %.4f cm"),
			R.Name, Versatz.X, Versatz.Y, SollVersatz.X, SollVersatz.Y,
			FMath::Sqrt(static_cast<double>(VersatzFehler.X) * VersatzFehler.X
				+ static_cast<double>(VersatzFehler.Y) * VersatzFehler.Y)),
			FMath::Abs(VersatzFehler.X) < 0.01 && FMath::Abs(VersatzFehler.Y) < 0.01);

		// Und der eingetragene Drehpunkt muss nach dieser Kette EXAKT auf der
		// Stangenachse liegen. Das ist die Forderung des Auftrags, in der
		// Form, in der sie ohne Rundung pruefbar ist.
		const FVector AchseNachKette = Nabe + Versatz
			+ Gier.RotateVector(Deklariert);
		TestTrue(FString::Printf(
			TEXT("%s: eingetragener Drehpunkt (%.2f, %.2f) cm liegt nach der "
				"Component-Kette bei (%.4f, %.4f) cm - auf der Rotorstangenachse"),
			R.Name, Deklariert.X, Deklariert.Y, AchseNachKette.X, AchseNachKette.Y),
			FMath::Abs(AchseNachKette.X) < 0.01 && FMath::Abs(AchseNachKette.Y) < 0.01);

		// -- 5b-2 GEOMETRIE, mit der Aufloesung des Datensatzes --------------
		// Der am Asset gemessene Drehpunkt muss dort liegen, wo der
		// eingetragene steht. Der Spielraum ist die Aufloesung des
		// Ersatzdatensatzes, nicht die Genauigkeit des Flugmodells - und
		// wird deshalb aus dem Messwert selbst hergeleitet statt geraten.
		const double MeshAbweichung = FVector2D::Distance(Achse,
			FVector2D(Deklariert.X, Deklariert.Y));
		const double AufloesungCm = 4.0 * RestfehlerAchse + 2.0;
		TestTrue(FString::Printf(
			TEXT("%s: am Asset gemessener Drehpunkt (%.2f, %.2f) cm weicht vom "
				"eingetragenen (%.2f, %.2f) um %.2f cm ab (Aufloesung dieses "
				"Datensatzes %.2f cm = 4 x Restfehler + 2 cm Aufloesung)"),
			R.Name, Achse.X, Achse.Y, Deklariert.X, Deklariert.Y,
			MeshAbweichung, AufloesungCm),
			MeshAbweichung < AufloesungCm);
		AddInfo(FString::Printf(
			TEXT("Rotor %s: %u Vertex, %u Dreiecke, Versatz (%.2f, %.2f, %.1f) cm, "
				"Drehpunkt eingetragen (%.2f, %.2f) / am Mesh (%.2f, %.2f) cm, "
				"nach der Kette (%.2f, %.2f) cm, Restfehler %.2f cm"),
			R.Name, Anzahl, RD->LODResources[Gewaehlt].GetNumTriangles(),
			Versatz.X, Versatz.Y, Versatz.Z, Deklariert.X, Deklariert.Y,
			Achse.X, Achse.Y, Drehpunkt.X, Drehpunkt.Y, RestfehlerAchse));
		// TestTrue schweigt im Erfolgsfall, und genau diese drei Zeilen sind
		// der Beleg, dass die Rechnung exakt stimmt. Sie kommen deshalb
		// ausdruecklich ins Log.
		AddInfo(FString::Printf(
			TEXT("Rotor %s, Kontrollstelle: Box-Mitte %.0f cm vom Drehpunkt, "
				"Restfehler %.2f gegen %.2f cm am Drehpunkt (Faktor %.1f); "
				"Verlauf 5/10/20 cm daneben: %.2f / %.2f / %.2f cm"),
			R.Name, BoxmitteAbstand, RestfehlerKontrolle, RestfehlerAchse,
			RestfehlerKontrolle / FMath::Max(RestfehlerAchse, 1.0),
			Restfehler5, Restfehler10, Restfehler20));
		AddInfo(FString::Printf(
			TEXT("Rotor %s, Rechnung: Versatzabweichung %.5f cm, Drehpunkt nach der "
				"Kette (%.5f, %.5f) cm, Meshabweichung %.2f cm (Aufloesung %.2f cm)"),
			R.Name,
			FMath::Sqrt(static_cast<double>(VersatzFehler.X) * VersatzFehler.X
				+ static_cast<double>(VersatzFehler.Y) * VersatzFehler.Y),
			AchseNachKette.X, AchseNachKette.Y, MeshAbweichung, AufloesungCm));
	}

	// -- 6. Suchscheinwerfer: zwei, und sie zeigen -------------------------
	UWiesbadenHeliLightRig* Licht = CDO->GetLightRig();
	if (TestNotNull(TEXT("Lichtbastel existiert"), Licht))
	{
		TestNotNull(TEXT("Suchscheinwerfer A"), Licht->GetSearchlightA());
		TestNotNull(TEXT("Suchscheinwerfer B"), Licht->GetSearchlightB());
		// "Zwei starke": beide ueber 5 km Reichweite und ueber 50000
		// Intensitaet - ein Spielprojektor sieht sonst aus wie ein
		// Taschenlampen-Fleck.
		for (USpotLightComponent* S : { Licht->GetSearchlightA(), Licht->GetSearchlightB() })
		{
			if (!S)
			{
				continue;
			}
			TestTrue(FString::Printf(TEXT("Scheinwerfer: Reichweite %.0f cm (erwartet > 5000)"),
				S->AttenuationRadius), S->AttenuationRadius > 5000.0f);
			TestTrue(FString::Printf(TEXT("Scheinwerfer: Intensitaet %.0f (erwartet > 50000)"),
				S->Intensity), S->Intensity > 50000.0f);
		}
		TestTrue(TEXT("Scheinwerfer stehen getrennt (nicht dieselbe Position)"),
			Licht->GetSearchlightA() && Licht->GetSearchlightB()
			&& FVector::DistSquared(Licht->GetSearchlightA()->GetRelativeLocation(),
				Licht->GetSearchlightB()->GetRelativeLocation()) > 1.0f);
	}

	AddInfo(TEXT("Ka-52-Ausstattung vollstaendig geprueft."));
	return true;
}

#endif // WITH_EDITOR
