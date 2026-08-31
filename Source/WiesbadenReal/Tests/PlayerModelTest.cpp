// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_EDITOR

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerModelTest,
	"WiesbadenReal.People.Spielermodell",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die Spielerfigur "Sebbo" auf die drei Fehler pruefen, die beim Bau
 * tatsaechlich aufgetreten sind.
 *
 * Alle drei waren im Editor nicht als Fehler sichtbar - das Modell lud, die
 * Materialien waren gesetzt, kein Protokolleintrag:
 *
 *   1. GROESSE. Der erste Durchlauf lieferte 0,70 m statt 1,78 m, weil die
 *      Beine in Objektkoordinaten angesetzt wurden, der Huftquerschnitt aber
 *      in Weltkoordinaten gemessen war. Ein Zwerg im Spiel, nichts im Log.
 *
 *   2. AUSRICHTUNG. Blenders X wird Unreals X - der Scan schaut in Blender
 *      nach -Y und stand deshalb QUER zur Laufrichtung. Auch das faellt an
 *      keiner Zahl auf ausser dieser: in Blickrichtung muss die groessere
 *      Ausdehnung stehen, weil die Figur eine Kettensaege vor sich her traegt.
 *
 *   3. URSPRUNG. Er liegt zwischen den Fuessen, nicht in der Koerpermitte.
 *      Liegt er falsch, steckt die Figur bis zur Huefte im Asphalt - derselbe
 *      Fehler wie beim Kaefer.
 */
bool FPlayerModelTest::RunTest(const FString& Parameters)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Assets/People/SM_Sebbo.SM_Sebbo"));

	if (!Mesh)
	{
		// Kein Fehlschlag: wer das Projekt frisch auscheckt, hat das Modell
		// noch nicht gebaut. Der Hinweis sagt, womit.
		AddInfo(TEXT("SM_Sebbo fehlt - Tools/Blender/build_sebbo.py und "
			"Tools/import_sebbo.py laufen lassen. Pruefung uebersprungen."));
		return true;
	}

	const FBox Box = Mesh->GetBoundingBox();
	const FVector Size = Box.GetSize();

	// -- Groesse -------------------------------------------------------------
	//
	// 177,7 cm nach Bauplan. Die Spanne laesst Spielraum fuer Aenderungen am
	// Beinprofil, schlaegt aber bei einem verlorenen Massstabsfaktor an -
	// Zentimeter statt Meter waeren Faktor 100.
	TestTrue(FString::Printf(TEXT("Koerpergroesse %.1f cm liegt zwischen 170 und 190"), Size.Z),
		Size.Z > 170.0 && Size.Z < 190.0);

	// -- Ursprung ------------------------------------------------------------
	//
	// Die Sohlen stehen auf z = 0. Zwei Zentimeter Spielraum fuer die
	// Rundung beim FBX-Weg.
	TestTrue(FString::Printf(TEXT("Sohle bei z=%.2f cm, erwartet 0"), Box.Min.Z),
		FMath::Abs(Box.Min.Z) < 2.0);

	// -- Ausrichtung ---------------------------------------------------------
	//
	// X ist in Unreal die Blickrichtung. Die Kettensaege ragt nach vorn, also
	// muss X deutlich groesser sein als Y (Schulterbreite). Stand die Figur
	// quer, war es genau umgekehrt: 41,3 auf X, 70,3 auf Y.
	TestTrue(FString::Printf(
		TEXT("Blickrichtung: %.1f cm auf X gegen %.1f cm auf Y"), Size.X, Size.Y),
		Size.X > Size.Y * 1.2);

	// -- Materialien ---------------------------------------------------------
	//
	// Drei Schlitze: Fotoscan, Hose, Stiefel. Ein leerer Schlitz zeichnet in
	// Unreal kommentarlos das graue Schachbrett.
	const TArray<FStaticMaterial>& Slots = Mesh->GetStaticMaterials();
	TestEqual(TEXT("Drei Materialschlitze"), Slots.Num(), 3);

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FName SlotName = Slots[i].MaterialSlotName;
		TestTrue(FString::Printf(TEXT("Schlitz %d ('%s') hat ein Material"),
			i, *SlotName.ToString()), Slots[i].MaterialInterface != nullptr);
	}

	// -- Detailstufen --------------------------------------------------------
	//
	// EditorStaticMeshLibrary.set_lods meldet Fehlschlag als RUECKGABEWERT und
	// wirft keinen Fehler. Im Kommandozeilenbetrieb liefert sie -1 und tut
	// nichts; wer das nicht prueft, glaubt an Stufen, die es nicht gibt.
	TestTrue(FString::Printf(TEXT("Mehr als eine Detailstufe (%d)"), Mesh->GetNumLODs()),
		Mesh->GetNumLODs() > 1);

	return true;
}

#endif // WITH_EDITOR
