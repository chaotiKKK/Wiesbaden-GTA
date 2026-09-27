// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "World/WiesbadenCustomerFigures.h"

using namespace WiesbadenCustomerFigures;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomerFiguresPickTest,
	"WiesbadenReal.World.CustomerFigures.Pick",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCustomerFiguresPickTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("keine Figur: keine Wahl"), PickFigure(0, {}, 7), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("eine Figur: immer sie, auch nach sich selbst"), PickFigure(1, { 0 }, 7), 0);
	TestEqual(TEXT("eine Figur, erster Auftritt"), PickFigure(1, { INDEX_NONE }, 3), 0);

	// Zwei Figuren wechseln sich streng ab, gleich welcher Wurf.
	int32 Last = INDEX_NONE;
	for (uint32 Roll = 0; Roll < 40; ++Roll)
	{
		const int32 Next = PickFigure(2, { Last }, Roll * 2654435761u);
		TestTrue(FString::Printf(TEXT("Wurf %u: gueltig (%d)"), Roll, Next), Next == 0 || Next == 1);
		if (Last != INDEX_NONE)
		{
			TestNotEqual(FString::Printf(TEXT("Wurf %u: nicht zweimal dieselbe"), Roll), Next, Last);
		}
		Last = Next;
	}

	// Drei Figuren: nie die letzte, und jede kommt vor.
	TSet<int32> Seen;
	Last = 0;
	for (uint32 Roll = 0; Roll < 60; ++Roll)
	{
		const int32 Next = PickFigure(3, { Last }, Roll);
		TestNotEqual(FString::Printf(TEXT("drei, Wurf %u: nicht die letzte"), Roll), Next, Last);
		Seen.Add(Next);
		Last = Next;
	}
	TestEqual(TEXT("drei: alle kommen dran"), Seen.Num(), 3);

	// Gaeste: wer schon im Laden sitzt, kommt nicht noch einmal herein ...
	for (uint32 Roll = 0; Roll < 10; ++Roll)
	{
		TestEqual(FString::Printf(TEXT("frei ist nur Figur 2 (Wurf %u)"), Roll), PickFigure(3, { 0, 1 }, Roll), 2);
	}
	// ... ausser alle sitzen schon da: dann jede ausser der zuletzt gekommenen.
	for (uint32 Roll = 0; Roll < 10; ++Roll)
	{
		TestNotEqual(FString::Printf(TEXT("alle besetzt, nicht die letzte (Wurf %u)"), Roll), PickFigure(2, { 1, 0 }, Roll), 1);
	}
	// Ungueltige Eintraege stoeren nicht.
	TestEqual(TEXT("INDEX_NONE und fremde Indizes ignoriert"), PickFigure(2, { INDEX_NONE, 7, 1 }, 5), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomerFiguresNamesTest,
	"WiesbadenReal.World.CustomerFigures.Names",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCustomerFiguresNamesTest::RunTest(const FString& Parameters)
{
	const FString Base = RootPath();
	const TArray<FString> Names = NamesFromMeshPackages({
		Base + TEXT("/Mira/Meshes/SK_Mira"),
		Base + TEXT("/Iris/Meshes/SK_Iris"),
		Base + TEXT("/Iris/Meshes/SK_Iris"),              // doppelt
		Base + TEXT("/Nora/Meshes/SK_Iris"),              // Ordner und Mesh passen nicht
		Base + TEXT("/Lena/SK_Lena"),                     // nicht unter Meshes
		Base + TEXT("/Tom/Meshes/Alt/SK_Tom"),            // zu tief
		TEXT("/Game/Assets/People/Denno/Meshes/SK_Denno"),  // nicht unter Kunden
		Base + TEXT("/iris/Meshes/SK_Iris"),              // Gross-/Kleinschreibung
	});
	TestEqual(TEXT("nur die beiden gueltigen Figuren"), Names, TArray<FString>({ TEXT("Iris"), TEXT("Mira") }));

	TestEqual(TEXT("Mesh-Pfad"), MeshPath(TEXT("Mira")),
		FString(TEXT("/Game/Assets/People/Kunden/Mira/Meshes/SK_Mira.SK_Mira")));
	TestEqual(TEXT("Bewegungspfad"), AnimPath(TEXT("Mira"), ECustomerAnim::Sit),
		FString(TEXT("/Game/Assets/People/Kunden/Mira/Animations/A_Mira_Sit.A_Mira_Sit")));
	TestEqual(TEXT("Bewegungsnamen wie im Blender-Skript"),
		FString::Printf(TEXT("%s %s %s %s"), AnimName(ECustomerAnim::Idle), AnimName(ECustomerAnim::Walk),
			AnimName(ECustomerAnim::Wave), AnimName(ECustomerAnim::Sit)), FString(TEXT("Idle Walk Wave Sit")));

	// Eine leere Figur ist nie vollstaendig - der Kunde nimmt dann die Fussgaenger-Figur.
	TestFalse(TEXT("leere Figur unvollstaendig"), FWbCustomerFigure().IsComplete());
	TestNull(TEXT("leere Figur: keine Bewegung"), FWbCustomerFigure().Anim(ECustomerAnim::Walk));
	return true;
}
