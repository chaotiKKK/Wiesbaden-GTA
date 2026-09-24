// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/WiesbadenDennoShop.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoShopCutTest,
	"WiesbadenReal.World.DennoShop.FacadeCut",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoShopCutTest::RunTest(const FString& Parameters)
{
	// Der Ausschnitt oeffnet GENAU die Erdgeschossfront von Sedanplatz 5:
	// die ganze Ladenbreite, aber weder das Obergeschoss noch die Nachbarhaeuser
	// (Nr. 3 und 7 schliessen in derselben Flucht an).
	const FVector Half = AWiesbadenDennoShop::CutHalfExtentCm();
	TestTrue(TEXT("Ausschnitt reicht ueber die 13,6 m breite Ladenfront"), Half.X * 2.0 >= 1360.0);
	TestTrue(TEXT("Ausschnitt bleibt in der 14,8 m breiten Hausfront"), Half.X * 2.0 <= 1480.0);
	TestTrue(TEXT("Ausschnitt endet im Erdgeschoss (<= 3,5 m hoch)"), Half.Z * 2.0 <= 350.0);

	// Schraege Front (wie die echte, ~5 Grad aus Nord): Kastenachsen folgen ihr.
	const FVector Centre(1000.0, 2000.0, 11000.0);
	const FVector2D AxisU = FVector2D(-0.094, -0.996).GetSafeNormal();   // nach Norden (UE -Y)
	auto Along = [&](double U, double N, double Z)
	{
		const FVector2D Nrm(-AxisU.Y, AxisU.X);
		return Centre + FVector(AxisU.X * U + Nrm.X * N, AxisU.Y * U + Nrm.Y * N, Z);
	};
	TestTrue(TEXT("Mitte der Front ist offen"), AWiesbadenDennoShop::IsInsideCut(Centre, Centre, AxisU, Half));
	TestTrue(TEXT("Nordende der Ladenfront ist offen"),
		AWiesbadenDennoShop::IsInsideCut(Along(Half.X - 10.0, 0.0, 0.0), Centre, AxisU, Half));
	TestFalse(TEXT("Nachbarhaus Nr. 7 (weiter noerdlich in derselben Flucht) bleibt zu"),
		AWiesbadenDennoShop::IsInsideCut(Along(Half.X + 50.0, 0.0, 0.0), Centre, AxisU, Half));
	TestFalse(TEXT("Obergeschoss bleibt zu"),
		AWiesbadenDennoShop::IsInsideCut(Along(0.0, 0.0, Half.Z + 30.0), Centre, AxisU, Half));
	TestFalse(TEXT("Rueckwaertige Waende weiter im Haus bleiben zu"),
		AWiesbadenDennoShop::IsInsideCut(Along(0.0, 400.0, 0.0), Centre, AxisU, Half));
	// Achsenfehler-Falle: mit Weltachsen statt Frontachsen laege ein Punkt am
	// Nordende einer SCHRAEGEN Front ausserhalb eines achsparallelen Kastens.
	TestTrue(TEXT("Schraege Front: Nordecke 2 cm vor der Wand noch offen"),
		AWiesbadenDennoShop::IsInsideCut(Along(Half.X - 5.0, -2.0, 0.0), Centre, AxisU, Half));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoShopBuildDecisionTest,
	"WiesbadenReal.World.DennoShop.BuildDecision",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoShopBuildDecisionTest::RunTest(const FString& Parameters)
{
	// Ohne gefundene Hauswand entsteht KEIN Laden - frueher baute er nach der
	// Wartezeit auf der OSM-Linie und schwebte auf Karten ohne das Haus im Leeren.
	const double Wait = AWiesbadenDennoShop::WallWaitSeconds;
	TestTrue(TEXT("Wand gefunden -> bauen"),
		AWiesbadenDennoShop::DecideBuild(true, 0.0) == EDennoShopBuild::Build);
	TestTrue(TEXT("Wand gefunden, auch spaet -> bauen"),
		AWiesbadenDennoShop::DecideBuild(true, Wait + 100.0) == EDennoShopBuild::Build);
	TestTrue(TEXT("Keine Wand, Zelle streamt noch -> warten"),
		AWiesbadenDennoShop::DecideBuild(false, Wait - 1.0) == EDennoShopBuild::Wait);
	TestTrue(TEXT("Keine Wand nach der Wartezeit -> aufgeben, KEIN Laden"),
		AWiesbadenDennoShop::DecideBuild(false, Wait) == EDennoShopBuild::GiveUp);

	// Nur ein Treffer auf der Frontlinie ist die Hauswand.
	const FVector Mid(1000.0, 2000.0, 0.0);
	const FVector Out(-1.0, 0.0, 0.0);
	TestTrue(TEXT("Treffer 40 cm vor der OSM-Linie ist die Wand"),
		AWiesbadenDennoShop::IsPlausibleWall(Mid + Out * 40.0, Mid, Out));
	TestTrue(TEXT("Treffer 1 m hinter der OSM-Linie (zurueckgesetzte Wand) zaehlt"),
		AWiesbadenDennoShop::IsPlausibleWall(Mid - Out * 100.0, Mid, Out));
	TestFalse(TEXT("Schildmast 4 m vor dem Haus ist keine Wand"),
		AWiesbadenDennoShop::IsPlausibleWall(Mid + Out * 400.0, Mid, Out));
	TestTrue(TEXT("Seitlicher Versatz entlang der Front spielt keine Rolle"),
		AWiesbadenDennoShop::IsPlausibleWall(Mid + FVector(0.0, 500.0, 0.0), Mid, Out));
	return true;
}

namespace
{
	TSharedPtr<FJsonObject> LoadShopJson(FAutomationTestBase& Test)
	{
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/denno_shop.json"));
		FString Text;
		if (!Test.TestTrue(FString::Printf(TEXT("%s lesbar"), *Path), FFileHelper::LoadFileToString(Text, *Path)))
		{
			return nullptr;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		Test.TestTrue(TEXT("denno_shop.json ist gueltiges JSON"),
			FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid());
		return Root;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoShopSharedDimsTest,
	"WiesbadenReal.World.DennoShop.SharedDims",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoShopSharedDimsTest::RunTest(const FString& Parameters)
{
	// Tools/denno_shop.json ist die einzige Quelle: build_denno_shop.py baut
	// den Laden daraus, create_facade_cut_materials.py den Ausschnitt-Code.
	// Die C++-Konstanten muessen dazu passen - sonst schneidet der Ausschnitt
	// neben der Ladenfront oder Denno steht in der Wand.
	const TSharedPtr<FJsonObject> Root = LoadShopJson(*this);
	if (!Root.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Laden = Root->GetObjectField(TEXT("laden"));
	const TSharedPtr<FJsonObject> Cut = Root->GetObjectField(TEXT("ausschnitt"));
	const TSharedPtr<FJsonObject> Denno = Root->GetObjectField(TEXT("denno"));

	const double HalfWidthM = Laden->GetNumberField(TEXT("halbe_breite_m"));
	const double OverM = Laden->GetNumberField(TEXT("front_ueberstand_m"));
	const double FasciaTopM = Laden->GetNumberField(TEXT("schild_oben_m"));
	TestEqual(TEXT("Ausschnittbreite = Ladenbreite + Front-Ueberstand (Python W2+OVER)"),
		AWiesbadenDennoShop::ShopHalfWidthCm, (HalfWidthM + OverM) * 100.0, 0.01);
	TestEqual(TEXT("Ausschnitt-Oberkante knapp unter dem Schildband"),
		AWiesbadenDennoShop::CutTopCm,
		FasciaTopM * 100.0 - Cut->GetNumberField(TEXT("oben_unter_schild_cm")), 0.01);
	TestEqual(TEXT("Ausschnitt-Unterkante"),
		AWiesbadenDennoShop::CutBottomCm, Cut->GetNumberField(TEXT("unten_cm")), 0.01);
	TestEqual(TEXT("Ausschnitt quer zur Wand"),
		AWiesbadenDennoShop::CutHalfDepthCm, Cut->GetNumberField(TEXT("halbe_tiefe_cm")), 0.01);
	TestEqual(TEXT("Denno x"), AWiesbadenDennoShop::DennoXCm, Denno->GetNumberField(TEXT("x_cm")), 0.01);
	TestEqual(TEXT("Denno y"), AWiesbadenDennoShop::DennoYCm, Denno->GetNumberField(TEXT("y_cm")), 0.01);
	TestEqual(TEXT("Denno Gier"), static_cast<double>(AWiesbadenDennoShop::DennoYawDeg),
		Denno->GetNumberField(TEXT("gier_grad")), 0.01);

	// Denno steht im Cafe (Nordhaelfte, x > 0) und im Raum, nicht in einer Wand.
	TestTrue(TEXT("Denno steht in der Cafe-Haelfte"),
		AWiesbadenDennoShop::DennoXCm > 50.0 && AWiesbadenDennoShop::DennoXCm < HalfWidthM * 100.0 - 50.0);
	TestTrue(TEXT("Denno steht im Raum, nicht in Front- oder Rueckwand"),
		AWiesbadenDennoShop::DennoYCm > 50.0
		&& AWiesbadenDennoShop::DennoYCm < Laden->GetNumberField(TEXT("tiefe_m")) * 100.0 - 50.0);
	return true;
}

#if WITH_EDITORONLY_DATA
namespace
{
	/** Knoten-Fingerabdruck: Typ + die Werte, die das Aussehen bestimmen. */
	FString ExpressionSignature(const UMaterialExpression* E)
	{
		FString Sig = E->GetClass()->GetName();
		if (const UMaterialExpressionConstant* C = Cast<UMaterialExpressionConstant>(E))
		{
			Sig += FString::Printf(TEXT(":%.5f"), C->R);
		}
		else if (const UMaterialExpressionConstant3Vector* V = Cast<UMaterialExpressionConstant3Vector>(E))
		{
			Sig += TEXT(":") + V->Constant.ToString();
		}
		else if (const UMaterialExpressionScalarParameter* S = Cast<UMaterialExpressionScalarParameter>(E))
		{
			Sig += FString::Printf(TEXT(":%s=%.5f"), *S->ParameterName.ToString(), S->DefaultValue);
		}
		else if (const UMaterialExpressionVectorParameter* P = Cast<UMaterialExpressionVectorParameter>(E))
		{
			Sig += TEXT(":") + P->ParameterName.ToString() + TEXT("=") + P->DefaultValue.ToString();
		}
		else if (const UMaterialExpressionTextureBase* T = Cast<UMaterialExpressionTextureBase>(E))
		{
			Sig += TEXT(":") + GetPathNameSafe(T->Texture);
		}
		else if (const UMaterialExpressionMaterialFunctionCall* F = Cast<UMaterialExpressionMaterialFunctionCall>(E))
		{
			Sig += TEXT(":") + GetPathNameSafe(F->MaterialFunction);
		}
		else if (const UMaterialExpressionCustom* X = Cast<UMaterialExpressionCustom>(E))
		{
			Sig += TEXT(":") + X->Code;
		}
		return Sig;
	}

	/** Ist dieser Knoten einer der fuenf, die create_facade_cut_materials.py anhaengt? */
	bool IsCutAddition(const UMaterialExpression* E)
	{
		if (E->IsA<UMaterialExpressionWorldPosition>())
		{
			return true;
		}
		if (const UMaterialExpressionVectorParameter* P = Cast<UMaterialExpressionVectorParameter>(E))
		{
			const FString N = P->ParameterName.ToString();
			return N == TEXT("ShopCenter") || N == TEXT("ShopAxisU") || N == TEXT("ShopHalf");
		}
		if (const UMaterialExpressionCustom* X = Cast<UMaterialExpressionCustom>(E))
		{
			return X->Description == TEXT("LadenAusschnitt");
		}
		return false;
	}

	TArray<FString> Fingerprint(const UMaterial* M, bool bSkipCutAdditions)
	{
		TArray<FString> Sigs;
		for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
		{
			if (E && !(bSkipCutAdditions && IsCutAddition(E)))
			{
				Sigs.Add(ExpressionSignature(E));
			}
		}
		Sigs.Sort();
		return Sigs;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoShopCutMaterialsTest,
	"WiesbadenReal.World.DennoShop.CutMaterials",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoShopCutMaterialsTest::RunTest(const FString& Parameters)
{
	// Die Ausschnitt-Materialien sind KOPIEN der Stadtfassaden. Aendert jemand
	// ein Original (Fenster, Fensterlicht, Farbe) und ruft
	// Tools/create_facade_cut_materials.py nicht erneut, zeigte Sedanplatz 5
	// still eine veraltete Fassade. Dieser Test macht das sichtbar: jede Kopie
	// muss - bis auf die fuenf Ausschnitt-Knoten - Knoten fuer Knoten dem
	// Original gleichen und den Code aus Tools/denno_shop.json tragen.
	const TSharedPtr<FJsonObject> Root = LoadShopJson(*this);
	if (!Root.IsValid())
	{
		return false;
	}
	const FString Hlsl = Root->GetStringField(TEXT("ausschnitt_hlsl"));
	static const TCHAR* Names[] = { TEXT("Putz"), TEXT("Beton"), TEXT("Glas"),
		TEXT("Backstein"), TEXT("Sandstein"), TEXT("Fachwerk") };
	auto LoadMat = [](const FString& Path) { return LoadObject<UMaterial>(nullptr, *Path); };
	for (const TCHAR* Name : Names)
	{
		const FString OrigPath = FString::Printf(TEXT("/Game/Materials/City/M_WbFacade_%s.M_WbFacade_%s"), Name, Name);
		const FString CutPath = FString::Printf(TEXT("/Game/Materials/City/Cut/M_WbFacade_%s_Cut.M_WbFacade_%s_Cut"), Name, Name);
		const UMaterial* Orig = LoadMat(OrigPath);
		const UMaterial* Cut = LoadMat(CutPath);
		if (!TestNotNull(*OrigPath, Orig) || !TestNotNull(*CutPath, Cut))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s ist Masked"), Name), Cut->BlendMode == BLEND_Masked);
		int32 Additions = 0;
		bool bCodeMatches = false;
		for (const TObjectPtr<UMaterialExpression>& E : Cut->GetExpressions())
		{
			if (E && IsCutAddition(E))
			{
				++Additions;
				if (const UMaterialExpressionCustom* X = Cast<UMaterialExpressionCustom>(E))
				{
					bCodeMatches = X->Code.TrimStartAndEnd() == Hlsl.TrimStartAndEnd();
				}
			}
		}
		TestEqual(FString::Printf(TEXT("%s: genau fuenf Ausschnitt-Knoten"), Name), Additions, 5);
		TestTrue(FString::Printf(TEXT("%s: Ausschnitt-Code = Tools/denno_shop.json"), Name), bCodeMatches);
		TestTrue(FString::Printf(TEXT("%s_Cut ist aktuell (sonst Tools/create_facade_cut_materials.py erneut ausfuehren)"), Name),
			Fingerprint(Orig, false) == Fingerprint(Cut, true));
	}
	// Empfindlichkeit des Vergleichs: eine Kopie gegen das FALSCHE Original
	// muss als abweichend auffallen - sonst prueft der Fingerabdruck nichts.
	const UMaterial* Putz = LoadMat(TEXT("/Game/Materials/City/M_WbFacade_Putz.M_WbFacade_Putz"));
	const UMaterial* BetonCut = LoadMat(TEXT("/Game/Materials/City/Cut/M_WbFacade_Beton_Cut.M_WbFacade_Beton_Cut"));
	if (Putz && BetonCut)
	{
		TestFalse(TEXT("Gegenprobe: Beton-Kopie gleicht NICHT dem Putz-Original"),
			Fingerprint(Putz, false) == Fingerprint(BetonCut, true));
	}
	return true;
}
#endif
