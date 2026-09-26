// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
//
// Der Plasmacutter (Dead-Space-Prinzip): drehbare Schnittebene, echtes
// Trennen vorbereiteter Stuecke. Genau zwei Fehlerarten fallen hier auf,
// die im Bild aergerlich waeren:
//
//  1. Die Schnittebene dreht sich nicht in Rasten (oder laeuft aus dem
//     Kreis) - das Mausrad wuerde die Kante unmerklich verrutschen.
//  2. Ein Stueck faellt, obwohl die Kante es nur gestriffen hat - oder das
//     falsche faellt, wenn die Ebene von der anderen Seite kommt.

#include "Misc/AutomationTest.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

#include "Weapons/WiesbadenCutMath.h"
#include "World/WiesbadenCuttable.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCutMathTest,
	"WiesbadenReal.Weapons.Schnittgeometrie",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCutMathTest::RunTest(const FString& Parameters)
{
	const FVector Aim = FVector(1.0, 0.0, 0.0);

	// Normale: Einheitslaenge, senkrecht zum Blick - bei jedem Rastwinkel.
	for (float Angle : { 0.0f, 45.0f, 90.0f, 359.0f })
	{
		const FVector N = WiesbadenCutMath::CutPlaneNormal(Aim, Angle);
		TestTrue(FString::Printf(TEXT("Laenge 1 (%.0f Grad)"), Angle),
			FMath::IsNearlyEqual(N.Size(), 1.0f, 0.01f));
		TestTrue(FString::Printf(TEXT("Senkrecht zum Blick (%.0f Grad)"), Angle),
			FMath::IsNearlyZero(FVector::DotProduct(N, Aim), 0.01f));
	}

	// 0 Grad: Kante senkrecht (Ebene enthaelt Blick und Hoehe); die Normale
	// zeigt zur Seite. 90 Grad: Kante waagerecht, Normale nach oben.
	const FVector N0 = WiesbadenCutMath::CutPlaneNormal(Aim, 0.0f);
	const FVector N90 = WiesbadenCutMath::CutPlaneNormal(Aim, 90.0f);
	TestTrue(TEXT("0 Grad: Normale zur Seite"),
		N0.Equals(FVector(0.0, 1.0, 0.0), 0.01f));
	TestTrue(TEXT("90 Grad: Normale nach oben"),
		N90.Equals(FVector(0.0, 0.0, 1.0), 0.01f));

	// Degenerierter Blick: keine Nullnormale.
	const FVector Weird = WiesbadenCutMath::CutPlaneNormal(FVector::ZeroVector, 30.0f);
	TestTrue(TEXT("Ohne Blick trotzdem Einheitsnormale"),
		FMath::IsNearlyEqual(Weird.Size(), 1.0f, 0.01f));

	// Abstand und Fall-Entscheidung: Ebene durch Z 50, Normale nach oben.
	const FVector PlanePoint(0.0, 0.0, 50.0);
	const FVector Up(0.0, 0.0, 1.0);
	TestTrue(TEXT("Darunter: negativer Abstand"),
		WiesbadenCutMath::SignedDistanceToPlane(FVector(0.0, 0.0, 25.0), PlanePoint, Up) < 0.0f);
	TestTrue(TEXT("Darueber: positiver Abstand"),
		WiesbadenCutMath::SignedDistanceToPlane(FVector(0.0, 0.0, 75.0), PlanePoint, Up) > 0.0f);
	TestTrue(TEXT("Unteres Stueck faellt ab"),
		WiesbadenCutMath::ShouldDetach(FVector(0.0, 0.0, 25.0), PlanePoint, Up));
	TestFalse(TEXT("Oberes Stueck bleibt stehen"),
		WiesbadenCutMath::ShouldDetach(FVector(0.0, 0.0, 75.0), PlanePoint, Up));

	// Rasten drehen und bleiben im Kreis 0..<360, auch rueckwaerts und bei
	// Spruengen um mehrere Rasten (starkes Scrollen).
	TestTrue(TEXT("Vorwaerts 350 + 15 = 5"),
		FMath::IsNearlyEqual(WiesbadenCutMath::RotateCutPlane(350.0f, 15.0f), 5.0f, 0.01f));
	TestTrue(TEXT("Rueckwaerts 0 - 15 = 345"),
		FMath::IsNearlyEqual(WiesbadenCutMath::RotateCutPlane(0.0f, -15.0f), 345.0f, 0.01f));
	TestTrue(TEXT("Sprung 355 + 10 = 5"),
		FMath::IsNearlyEqual(WiesbadenCutMath::RotateCutPlane(355.0f, 10.0f), 5.0f, 0.01f));
	TestTrue(TEXT("Rastweite 15 bleibt Rastweite"),
		FMath::IsNearlyEqual(WiesbadenCutMath::RotateCutPlane(100.0f, 15.0f), 115.0f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCuttableTest,
	"WiesbadenReal.Weapons.PlasmacutterTrennt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCuttableTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	AWiesbadenCuttable* Cuttable = World->SpawnActor<AWiesbadenCuttable>();
	if (!TestNotNull(TEXT("Trenn-Objekt gespawnt"), Cuttable))
	{
		World->DestroyWorld(false);
		return false;
	}

	// Die vorbereiteten Stuecke liegen uebereinander (unten Z 25, oben Z 75).
	const FVector Below = Cuttable->GetPieceBelow()->GetComponentLocation();
	const FVector Above = Cuttable->GetPieceAbove()->GetComponentLocation();
	TestTrue(FString::Printf(TEXT("Stuecke uebereinander (%.0f unter %.0f)"),
		Below.Z, Above.Z), Above.Z > Below.Z);

	// 1. Nur gestriffen: beide Stuecke liegen auf der stehenden Seite.
	TestFalse(TEXT("Streifen trennt nichts"),
		Cuttable->ApplyCut(FVector(0.0, 0.0, -200.0), FVector(0.0, 0.0, 1.0)));
	TestFalse(TEXT("Ohne Schnitt nicht als getrennt markiert"), Cuttable->IsCut());

	// 2. Schnitt mit der Normale nach oben: das UNTERE Stueck faellt ab.
	TestTrue(TEXT("Schnitt von unten laesst das untere Stueck fallen"),
		Cuttable->ApplyCut(FVector(0.0, 0.0, 50.0), FVector(0.0, 0.0, 1.0)));
	TestTrue(TEXT("Als getrennt markiert"), Cuttable->IsCut());
	TestTrue(TEXT("Das untere Stueck ist abgefallen"),
		Cuttable->GetFallenPiece() == Cuttable->GetPieceBelow());

	// 3. Ein zweiter Schnitt aendert nichts mehr.
	TestFalse(TEXT("Zweiter Schnitt abgewiesen"),
		Cuttable->ApplyCut(FVector(0.0, 0.0, 50.0), FVector(0.0, 0.0, 1.0)));

	// 4. Von der anderen Seite faellt das OBERE Stueck (eigenes Objekt).
	AWiesbadenCuttable* Second = World->SpawnActor<AWiesbadenCuttable>();
	if (!TestNotNull(TEXT("Zweites Trenn-Objekt"), Second))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestTrue(TEXT("Schnitt von oben laesst das obere Stueck fallen"),
		Second->ApplyCut(FVector(0.0, 0.0, 50.0), FVector(0.0, 0.0, -1.0)));
	TestTrue(TEXT("Das obere Stueck ist abgefallen"),
		Second->GetFallenPiece() == Second->GetPieceAbove());

	World->DestroyWorld(false);
	return true;
}

// Der Cutdown: was der Plasmacutter im fertigen Spiel liefern muss. Die
// beiden Tests oben pruefen die Rechenwege; dieser hier prueft den ganzen
// Ablauf am gespawnten Objekt - und zwar an den DREI Dingen, die im Bild
// sofort auffielen, als sie fehlten:
//
//  1. Die Stuecke tragen die gebauten Cutpiece-Meshes (56 x 56 x 57,5 cm
//     bzw. 72 cm hoch), nicht die Engine-Wuerfel. Der Wuerfel ist 100 cm
//     und damit sichtbar zu gross - der Rueckfall darf nicht stillschweigend
//     das Bild bestimmen.
//  2. Die Glutkante sitzt an der Schnittflaeche des ABGEFALLENEN Stuecks
//     (nicht am Actor, nicht am stehenden) und leuchtet mit voller Stärke.
//  3. Sie klingt ueber EmberSeconds ab und ist danach aus - Licht UND
//     Fläche, sonst leuchtet die Kante unbegrenzt weiter.
//
// Die Prüfung laeuft ohne Physik (UWorld::CreateWorld ohne Szene): es zählt
// das Fallen über die Komponenten, nicht über eine Simulation. Steckenbleiben
// oder falsches Stück fällt hier auf, echte Höhenmessung nicht.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCuttableCutdownTest,
	"WiesbadenReal.Weapons.PlasmacutterCutdown",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCuttableCutdownTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	AWiesbadenCuttable* Cuttable = World->SpawnActor<AWiesbadenCuttable>();
	if (!TestNotNull(TEXT("Trenn-Objekt gespawnt"), Cuttable))
	{
		World->DestroyWorld(false);
		return false;
	}

	UStaticMeshComponent* Below = Cuttable->GetPieceBelow();
	UStaticMeshComponent* Above = Cuttable->GetPieceAbove();

	// 1. Meshes. Massstab ist das Mesh selbst: sobald die Assets importiert
	//    sind, müssen beide Stuecke massstaeblich sein. Fehlen die uassets
	//    (git-ignoriert, auf einem frischen Klon nicht da), gilt der
	//    Wuerfelrueckfall als erlaubt - das wird als Warnung gemeldet und
	//    nicht stillschweigend akzeptiert.
	// TestNotNull braucht einen echten Zeigertyp - deshalb der Cast, der
	// Bedingungsausdruck allein laesst die Template-Ableitung scheitern.
	TestNotNull(TEXT("Unteres Stueck traegt ein Mesh"),
		static_cast<UStaticMesh*>(Below ? Below->GetStaticMesh() : nullptr));
	TestNotNull(TEXT("Oberes Stueck traegt ein Mesh"),
		static_cast<UStaticMesh*>(Above ? Above->GetStaticMesh() : nullptr));
	if (!Below || !Above || !Below->GetStaticMesh() || !Above->GetStaticMesh())
	{
		// Ohne Mesh ist auch der Cutdown nicht pruefbar - und ein Actor
		// ohne Mesh waere im Spiel unsichtbar, das ist der echte Fehler.
		World->DestroyWorld(false);
		return false;
	}

	const bool AssetsDa = Below->GetStaticMesh()->GetPathName().Contains(TEXT("Cutpiece_Unten"))
		&& Above->GetStaticMesh()->GetPathName().Contains(TEXT("Cutpiece_Oben"));

	if (AssetsDa)
	{
		TestTrue(TEXT("Unteres Stueck nutzt das gebaute Cutpiece-Mesh"),
			Below->GetStaticMesh() && Below->GetStaticMesh()->GetPathName().Contains(TEXT("Cutpiece_Unten")));
		TestTrue(TEXT("Oberes Stueck nutzt das gebaute Cutpiece-Mesh"),
			Above->GetStaticMesh() && Above->GetStaticMesh()->GetPathName().Contains(TEXT("Cutpiece_Oben")));

		// Mass aus der gebauten Box (halb Kantenlänge, also x2):
		// Unten 56 x 56 x 57,5, Oben 56 x 56 x 72, Glut 52 x 52 x 2 cm.
		const FVector UntenBox = Below->GetStaticMesh()->GetBounds().BoxExtent * 2.0f;
		const FVector ObenBox = Above->GetStaticMesh()->GetBounds().BoxExtent * 2.0f;
		TestTrue(FString::Printf(TEXT("Unten 56 x 56 x 57,5 cm (gemessen %.1f x %.1f x %.1f)"),
			UntenBox.X, UntenBox.Y, UntenBox.Z),
			FMath::IsNearlyEqual(UntenBox.X, 56.0, 0.5)
			&& FMath::IsNearlyEqual(UntenBox.Y, 56.0, 0.5)
			&& FMath::IsNearlyEqual(UntenBox.Z, 57.5, 0.5));
		TestTrue(FString::Printf(TEXT("Oben 56 x 56 x 72 cm (gemessen %.1f x %.1f x %.1f)"),
			ObenBox.X, ObenBox.Y, ObenBox.Z),
			FMath::IsNearlyEqual(ObenBox.X, 56.0, 0.5)
			&& FMath::IsNearlyEqual(ObenBox.Y, 56.0, 0.5)
			&& FMath::IsNearlyEqual(ObenBox.Z, 72.0, 0.5));

		// Ursprung in der Stueckmitte -> keine Skalierung noetig. Eine
		// Skalierung auf 0,5 (der alte Wuerfel-Zweig) wuerde die
		// gebauten Masse halbieren.
		TestTrue(TEXT("Unten unskaliert (Blender bringt das Mass mit)"),
			Below->GetRelativeScale3D().Equals(FVector::OneVector, 0.01f));
		TestTrue(TEXT("Oben unskaliert"),
			Above->GetRelativeScale3D().Equals(FVector::OneVector, 0.01f));
	}
	else
	{
		AddWarning(TEXT("Cutpiece-uassets fehlen (Content/Waffen/Cutpieces leer) - "
			"geprueft wurde der Engine-Wuerfelrueckfall. Import: Tools\\import_cutpieces.cmd"));
	}

	// Vor dem Schnitt darf nichts leuchten.
	TestFalse(TEXT("Vor dem Schnitt glimmt nichts"), Cuttable->IsEmberGlowing());
	TestFalse(TEXT("Glutkante vor dem Schnitt unsichtbar"),
		Cuttable->GetEmberFace() && Cuttable->GetEmberFace()->IsVisible());

	// 2. Schnitt: das untere Stueck faellt, die Glutkante wandert mit.
	const FVector Schnitt(0.0, 0.0, 50.0);
	TestTrue(TEXT("Schnitt trennt das untere Stueck ab"),
		Cuttable->ApplyCut(Schnitt, FVector(0.0, 0.0, 1.0)));
	TestTrue(TEXT("Das untere Stueck ist das abgefallene"),
		Cuttable->GetFallenPiece() == Below);

	// Vorher-Zustand merken: die Restzeit startet bei EmberSeconds.
	const float Start = Cuttable->GetEmberRemaining();
	TestTrue(FString::Printf(TEXT("Glimmzeit startet bei EmberSeconds (%.2f s)"), Start),
		FMath::IsNearlyEqual(Start, Cuttable->EmberSeconds, 0.01f));
	TestTrue(TEXT("Nach dem Schnitt glimmt die Kante"), Cuttable->IsEmberGlowing());

	// Die Flaeche und das Licht muessen am ABGEFALLENEN Stueck haengen.
	// Am Actor haengend leuchtet die Kante mitten in der Luft.
	TestTrue(TEXT("Glutflaeche haengt am abgefallenen Stueck"),
		Cuttable->GetEmberFace()
		&& Cuttable->GetEmberFace()->GetAttachParent() == Below);
	TestTrue(TEXT("Glutlicht haengt am abgefallenen Stueck"),
		Cuttable->GetEmberLight()
		&& Cuttable->GetEmberLight()->GetAttachParent() == Below);
	TestTrue(TEXT("Glutflaeche sitzt auf der Schnitthoehe"),
		Cuttable->GetEmberFace()
		&& FMath::IsNearlyEqual(Cuttable->GetEmberFace()->GetComponentLocation().Z, 50.0, 1.0));
	TestTrue(TEXT("Glutflaeche sichtbar"), Cuttable->GetEmberFace()->IsVisible());
	TestTrue(TEXT("Glutlicht mit voller Staerke"),
		Cuttable->GetEmberLight()->Intensity > 0.0f);
	TestTrue(TEXT("Glutlicht warm eingefaerbt"),
		Cuttable->GetEmberLight()->GetLightColor().R > Cuttable->GetEmberLight()->GetLightColor().B);

	// 3. Abklingen: nach der halben Zeit noch etwa halbe Staerke, am Ende aus -
	//    Licht und Flaeche. Monoton, damit die Kante nicht aufblitzt.
	//    Zwei Schritte a 25 % der Glimmzeit = die Haelfte ist um.
	const float StartIntensitaet = Cuttable->GetEmberLight()->Intensity;
	float Vorher = Cuttable->GetEmberLight()->Intensity;
	for (int32 Schritt = 0; Schritt < 2; ++Schritt)
	{
		Cuttable->Tick(Cuttable->EmberSeconds * 0.25f);
		const float Jetzt = Cuttable->GetEmberLight()->Intensity;
		TestTrue(FString::Printf(TEXT("Staerke faellt monoton (Schritt %d)"), Schritt), Jetzt <= Vorher);
		// Lichtstaerke und Restzeit muessen zusammenpassen.
		TestTrue(TEXT("Glutlicht folgt der Restzeit"),
			FMath::IsNearlyEqual(Jetzt,
				Cuttable->EmberIntensity * (Cuttable->GetEmberRemaining() / Cuttable->EmberSeconds), 1.0f));
		Vorher = Jetzt;
	}
	TestTrue(TEXT("Nach der Haelfte noch glueht die Kante"), Cuttable->IsEmberGlowing());
	TestTrue(TEXT("Nach der Haelfte etwa halbe Staerke"),
		Vorher < StartIntensitaet * 0.75f && Vorher > StartIntensitaet * 0.25f);

	// Ueber die Restzeit hinaus: aus, und zwar beides.
	Cuttable->Tick(Cuttable->EmberSeconds);
	TestFalse(TEXT("Nach EmberSeconds ist die Kante aus"), Cuttable->IsEmberGlowing());
	TestTrue(TEXT("Restzeit bei null"), FMath::IsNearlyZero(Cuttable->GetEmberRemaining(), 0.001f));
	TestTrue(TEXT("Glutlicht endgueltig aus (Staerke 0)"),
		FMath::IsNearlyZero(Cuttable->GetEmberLight()->Intensity, 0.01f));
	TestTrue(TEXT("Glutlicht unsichtbar geschaltet"), !Cuttable->GetEmberLight()->IsVisible());
	TestTrue(TEXT("Glutflaeche unsichtbar geschaltet"), !Cuttable->GetEmberFace()->IsVisible());

	// Und es bleibt aus: weiteres Ticken darf nichts neu anzunden.
	Cuttable->Tick(Cuttable->EmberSeconds);
	TestFalse(TEXT("Bleibt aus"), Cuttable->GetEmberLight()->IsVisible());

	World->DestroyWorld(false);
	return true;
}
