// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/WiesbadenDennoShop.h"
#include "World/WiesbadenDeliveryCustomer.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Engine/Texture2D.h"
#endif

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoShopIdleTest,
	"WiesbadenReal.World.DennoShop.IdleMotion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoShopIdleTest::RunTest(const FString& Parameters)
{
	// Denno atmet und verlagert das Gewicht, ohne Skelett: der Actor dreht und
	// skaliert die Figur um ihren Ursprung an den Fuessen. Lebendig heisst hier:
	// der Kopf bewegt sich sichtbar (Zentimeter), die Fuesse bleiben stehen, und
	// nichts springt von einem Bild zum naechsten.
	auto At = [](double Seconds, const FVector& PointCm)
	{
		const FDennoIdlePose Pose = AWiesbadenDennoShop::ComputeDennoIdle(Seconds);
		return FTransform(Pose.Rotation, FVector::ZeroVector, Pose.Scale).TransformPosition(PointCm);
	};
	const FVector Head(0.0, 0.0, 160.0);
	const FVector Toe(8.0, 12.0, 0.0);   // Fussspitze neben der Drehachse
	const double Frame = 1.0 / 60.0;
	double MaxHead = 0.0, MaxToe = 0.0, MaxHeadStep = 0.0, MaxWidth = 0.0, MaxYaw = 0.0;
	for (double T = 0.0; T < 120.0; T += Frame)
	{
		const FDennoIdlePose Pose = AWiesbadenDennoShop::ComputeDennoIdle(T);
		MaxHead = FMath::Max(MaxHead, FVector::Dist(At(T, Head), Head));
		MaxToe = FMath::Max(MaxToe, FVector::Dist(At(T, Toe), Toe));
		MaxHeadStep = FMath::Max(MaxHeadStep, FVector::Dist(At(T + Frame, Head), At(T, Head)));
		MaxWidth = FMath::Max(MaxWidth, Pose.Scale.X - 1.0);
		MaxYaw = FMath::Max(MaxYaw, FMath::Abs(Pose.Rotation.Yaw));
		TestTrue(TEXT("Nie schmaler als die Grundstellung"), Pose.Scale.X >= 1.0 - 1e-9 && Pose.Scale.Z >= 1.0 - 1e-9);
	}
	TestTrue(FString::Printf(TEXT("Kopf bewegt sich sichtbar (max %.2f cm >= 1 cm)"), MaxHead), MaxHead >= 1.0);
	TestTrue(FString::Printf(TEXT("... aber dezent (max %.2f cm <= 4 cm)"), MaxHead), MaxHead <= 4.0);
	TestTrue(FString::Printf(TEXT("Fuesse bleiben stehen (max %.2f cm < 1,5 cm)"), MaxToe), MaxToe < 1.5);
	TestTrue(FString::Printf(TEXT("Kein Sprung zwischen zwei Bildern (max %.3f cm)"), MaxHeadStep), MaxHeadStep < 0.05);
	TestTrue(TEXT("Brustkorb weitet sich (Atem erreicht sein Maximum)"),
		MaxWidth > AWiesbadenDennoShop::BreathWidth * 0.99);
	TestTrue(TEXT("Umschauen bleibt im Rahmen"), MaxYaw <= AWiesbadenDennoShop::LookAroundDeg + 1e-9);

	// Atem im Takt: eine Atemperiode spaeter ist der Brustkorb wieder gleich weit.
	const double P = AWiesbadenDennoShop::BreathPeriodSeconds;
	for (double T : { 0.7, 13.1, 55.5 })
	{
		TestEqual(FString::Printf(TEXT("Atem wiederholt sich nach %.1f s"), P),
			AWiesbadenDennoShop::ComputeDennoIdle(T).Scale.X,
			AWiesbadenDennoShop::ComputeDennoIdle(T + P).Scale.X, 1e-9);
	}
	// Grundstellung zu Beginn eines Atemzugs: ausgeatmet.
	TestEqual(TEXT("t = 0: ausgeatmet"), AWiesbadenDennoShop::ComputeDennoIdle(0.0).Scale.X, 1.0, 1e-9);
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

// -- Asset-Hygiene des Laden-Ordners ------------------------------------------
// Der erste Import legte 50 Dateien / 13,4 MB ab: jedes GLB bekam seinen
// eigenen Materialordner (Messing, Anthrazit, Weiss je dreifach), Tripo-
// Texturnamen mit '+' und 2048er-Texturen fuer eine Figur, die nur hinter Glas
// steht. Tools/import_denno_shop.py raeumt das heute auf; diese Regeln halten
// es fest, damit ein kuenftiger Import es nicht still zuruecktraegt.
namespace DennoAssetHygiene
{
	const TCHAR* const Root = TEXT("/Game/Buildings/DennoShop");
	/** Groesste erlaubte Texturkante: Denno ist im naechsten Blick ~560 px hoch. */
	constexpr int32 MaxTextureEdge = 512;

	struct FAssetInfo
	{
		FString Folder;          // relativ zum Laden-Ordner, z.B. "Textures"
		FString Name;
		FString Class;           // StaticMesh, Material, Texture2D, ...
		int32 TextureEdge = 0;   // groesste Kantenlaenge in px (nur Texturen)
	};

	/** Zielordner je Asset-Art; leer = gehoert nicht in den Laden-Ordner. */
	FString FolderFor(const FString& Class)
	{
		if (Class == TEXT("StaticMesh") || Class == TEXT("SkeletalMesh") || Class == TEXT("Skeleton")) { return TEXT("Meshes"); }
		if (Class == TEXT("AnimSequence")) { return TEXT("Animations"); }
		if (Class.StartsWith(TEXT("Material"))) { return TEXT("Materials"); }
		if (Class.StartsWith(TEXT("Texture"))) { return TEXT("Textures"); }
		return FString();
	}

	/** Namenspraefix je Asset-Art (Skelett und Skelett-Mesh: SK_, Bewegung: A_). */
	FString PrefixFor(const FString& Class)
	{
		if (Class == TEXT("StaticMesh")) { return TEXT("SM_"); }
		if (Class == TEXT("SkeletalMesh") || Class == TEXT("Skeleton")) { return TEXT("SK_"); }
		if (Class == TEXT("AnimSequence")) { return TEXT("A_"); }
		return Class.StartsWith(TEXT("Material")) ? TEXT("M_") : TEXT("T_");
	}

	/** Nur ASCII-Buchstaben, Ziffern und '_' - kein '+', Leerzeichen, Umlaut. */
	bool IsCleanName(const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return false;
		}
		for (const TCHAR C : Name)
		{
			const bool bOk = (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_';
			if (!bOk)
			{
				return false;
			}
		}
		return true;
	}

	/** Alle Verstoesse, je einer pro Zeile (leer = sauber). Datenrein, testbar. */
	TArray<FString> FindIssues(const TArray<FAssetInfo>& Assets)
	{
		TMap<FString, int32> NameCount;
		for (const FAssetInfo& A : Assets)
		{
			++NameCount.FindOrAdd(A.Name);
		}
		TArray<FString> Issues;
		for (const FAssetInfo& A : Assets)
		{
			const FString Where = A.Folder.IsEmpty() ? A.Name : A.Folder + TEXT("/") + A.Name;
			if (!IsCleanName(A.Name))
			{
				Issues.Add(TEXT("Sonderzeichen im Namen: ") + Where);
			}
			if (NameCount[A.Name] > 1)
			{
				Issues.Add(FString::Printf(TEXT("doppelt (%dx): %s"), NameCount[A.Name], *Where));
			}
			const FString Target = FolderFor(A.Class);
			if (Target.IsEmpty())
			{
				Issues.Add(FString::Printf(TEXT("unerwartete Asset-Art %s: %s"), *A.Class, *Where));
				continue;
			}
			if (A.Folder != Target)
			{
				Issues.Add(FString::Printf(TEXT("gehoert nach %s/: %s"), *Target, *Where));
			}
			if (!A.Name.StartsWith(PrefixFor(A.Class), ESearchCase::CaseSensitive))
			{
				Issues.Add(FString::Printf(TEXT("Praefix %s fehlt: %s"), *PrefixFor(A.Class), *Where));
			}
			if (Target == TEXT("Textures") && A.TextureEdge > MaxTextureEdge)
			{
				Issues.Add(FString::Printf(TEXT("Textur zu gross (%d px > %d): %s"), A.TextureEdge, MaxTextureEdge, *Where));
			}
		}
		return Issues;
	}

	bool AnyIssueContains(const TArray<FString>& Issues, const TCHAR* Needle)
	{
		return Issues.ContainsByPredicate([Needle](const FString& S) { return S.Contains(Needle); });
	}

#if WITH_EDITOR
	/** Alle Assets unter Folder (echter Ordner, Asset-Registry), Texturkanten gemessen. */
	TArray<FAssetInfo> ScanFolder(const TCHAR* Folder, FAutomationTestBase& Test)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		Registry.ScanPathsSynchronous({ FString(Folder) }, true);
		TArray<FAssetData> Found;
		Registry.GetAssetsByPath(FName(Folder), Found, true);

		TArray<FAssetInfo> Assets;
		const FString RootPrefix = FString(Folder) + TEXT("/");
		for (const FAssetData& Data : Found)
		{
			FAssetInfo Info;
			const FString Path = Data.PackagePath.ToString();
			Info.Folder = Path.StartsWith(RootPrefix) ? Path.RightChop(RootPrefix.Len()) : FString();
			Info.Name = Data.AssetName.ToString();
			Info.Class = Data.AssetClassPath.GetAssetName().ToString();
			if (FolderFor(Info.Class) == TEXT("Textures"))
			{
				if (const UTexture2D* Texture = Cast<UTexture2D>(Data.GetAsset()))
				{
#if WITH_EDITORONLY_DATA
					Info.TextureEdge = static_cast<int32>(FMath::Max(Texture->Source.GetSizeX(), Texture->Source.GetSizeY()));
#else
					Info.TextureEdge = FMath::Max(Texture->GetSizeX(), Texture->GetSizeY());
#endif
				}
				Test.AddInfo(FString::Printf(TEXT("Textur %s: %d px"), *Info.Name, Info.TextureEdge));
			}
			Assets.Add(Info);
		}
		return Assets;
	}
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoShopAssetRulesTest,
	"WiesbadenReal.World.DennoShop.AssetRules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoShopAssetRulesTest::RunTest(const FString& Parameters)
{
	using namespace DennoAssetHygiene;
	// Ein sauberer Stand wie nach Tools/import_denno_shop.py: keine Meldung.
	const TArray<FAssetInfo> Clean = {
		{ TEXT("Meshes"), TEXT("SM_Denno"), TEXT("StaticMesh") },
		{ TEXT("Materials"), TEXT("M_Denno_Brass"), TEXT("Material") },
		{ TEXT("Materials"), TEXT("M_Denno_Part0"), TEXT("MaterialInstanceConstant") },
		{ TEXT("Textures"), TEXT("T_Denno_Part0"), TEXT("Texture2D"), 512 },
		{ TEXT("Textures"), TEXT("T_Denno_Part5"), TEXT("Texture2D"), 128 },
		// Eine animierte Figur (Iris): Skelett-Mesh, Skelett, Bewegung.
		{ TEXT("Meshes"), TEXT("SK_Iris"), TEXT("SkeletalMesh") },
		{ TEXT("Meshes"), TEXT("SK_Iris_Skeleton"), TEXT("Skeleton") },
		{ TEXT("Animations"), TEXT("A_Iris_Walk"), TEXT("AnimSequence") } };
	const TArray<FString> CleanIssues = FindIssues(Clean);
	TestEqual(TEXT("Sauberer Stand: keine Meldung"), CleanIssues.Num(), 0);
	for (const FString& Issue : CleanIssues)
	{
		AddError(TEXT("Falschmeldung: ") + Issue);
	}

	// Jede Regel einzeln gegen den ALTEN Stand (Befund 24.09.2026). Der Verstoss
	// steht nie an Index 0, damit eine Pruefung, die nur das erste Asset ansieht,
	// hier auffaellt.
	auto WithOne = [&Clean](const FAssetInfo& Bad)
	{
		TArray<FAssetInfo> A = Clean;
		A.Insert(Bad, 2);
		return A;
	};
	const TArray<FString> Plus = FindIssues(WithOne(
		{ TEXT("Textures"), TEXT("T_denno+figure+3d+model_tripo_part_0_basecolor"), TEXT("Texture2D"), 512 }));
	TestTrue(TEXT("Alter Tripo-Name mit '+' faellt auf"), AnyIssueContains(Plus, TEXT("Sonderzeichen")));
	TestEqual(TEXT("... und nur er"), Plus.Num(), 1);

	TArray<FAssetInfo> Tripled = Clean;
	Tripled.Add({ TEXT("Materials"), TEXT("M_Denno_Brass"), TEXT("Material") });
	Tripled.Add({ TEXT("Materials"), TEXT("M_Denno_Brass"), TEXT("Material") });
	const TArray<FString> Dup = FindIssues(Tripled);
	TestTrue(TEXT("Dreifaches Messing faellt auf"), AnyIssueContains(Dup, TEXT("doppelt (3x)")));
	TestEqual(TEXT("... alle drei Exemplare gemeldet, sonst nichts"), Dup.Num(), 3);

	const TArray<FString> Big = FindIssues(WithOne(
		{ TEXT("Textures"), TEXT("T_Denno_Part1"), TEXT("Texture2D"), 2048 }));
	TestTrue(TEXT("2048er-Textur faellt auf"), AnyIssueContains(Big, TEXT("Textur zu gross (2048")));
	TestEqual(TEXT("... und nur sie"), Big.Num(), 1);

	const TArray<FString> Folder = FindIssues(WithOne(
		{ TEXT("denno_shop_cafe/Materials"), TEXT("M_Denno_Oak"), TEXT("Material") }));
	TestTrue(TEXT("Alter Import-Ordner je GLB faellt auf"), AnyIssueContains(Folder, TEXT("gehoert nach Materials/")));
	TestEqual(TEXT("... und nur er"), Folder.Num(), 1);

	const TArray<FString> Prefix = FindIssues(WithOne(
		{ TEXT("Materials"), TEXT("tripo_part_0_material"), TEXT("Material") }));
	TestTrue(TEXT("Material ohne M_ faellt auf"), AnyIssueContains(Prefix, TEXT("Praefix M_")));
	TestEqual(TEXT("... und nur es"), Prefix.Num(), 1);

	const TArray<FString> Stray = FindIssues(WithOne(
		{ TEXT("Meshes"), TEXT("SM_DennoOld"), TEXT("ObjectRedirector") }));
	TestTrue(TEXT("Umleitung (fremde Asset-Art) faellt auf"), AnyIssueContains(Stray, TEXT("unerwartete Asset-Art")));
	TestEqual(TEXT("... und nur sie"), Stray.Num(), 1);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoShopAssetHygieneTest,
	"WiesbadenReal.World.DennoShop.AssetHygiene",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoShopAssetHygieneTest::RunTest(const FString& Parameters)
{
	using namespace DennoAssetHygiene;
	// Der ECHTE Laden-Ordner nach denselben Regeln wie AssetRules. Schlaegt er
	// fehl: Tools/import_denno_shop.py erneut ausfuehren (er loescht den Ordner
	// und legt ihn sauber neu an), bzw. die Blender-Skripte anpassen.
	const TArray<FAssetInfo> Assets = ScanFolder(Root, *this);

	// Die fuenf Meshes, die AWiesbadenDennoShop laedt, muessen da sein.
	static const TCHAR* Meshes[] = { TEXT("SM_DennoShop_Shell"), TEXT("SM_DennoShop_Cafe"),
		TEXT("SM_DennoShop_Salon"), TEXT("SM_DennoShop_Glass"), TEXT("SM_Denno") };
	for (const TCHAR* Mesh : Meshes)
	{
		TestTrue(FString::Printf(TEXT("Meshes/%s vorhanden"), Mesh), Assets.ContainsByPredicate(
			[Mesh](const FAssetInfo& A) { return A.Folder == TEXT("Meshes") && A.Name == Mesh; }));
	}
	TestTrue(TEXT("Texturgroessen gelesen (Denno hat Texturen)"), Assets.ContainsByPredicate(
		[](const FAssetInfo& A) { return FolderFor(A.Class) == TEXT("Textures") && A.TextureEdge > 0; }));

	const TArray<FString> Issues = FindIssues(Assets);
	for (const FString& Issue : Issues)
	{
		AddError(TEXT("Laden-Ordner: ") + Issue);
	}
	TestEqual(FString::Printf(TEXT("%s: %d Assets ohne Verstoss"), Root, Assets.Num()), Issues.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoCustomerFiguresAssetsTest,
	"WiesbadenReal.World.DennoShop.CustomerFigures",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoCustomerFiguresAssetsTest::RunTest(const FString& Parameters)
{
	using namespace DennoAssetHygiene;
	// Die Kundenfiguren (Lieferkunden und Ladengaeste), die das Spiel selbst
	// findet - jede nach denselben Ordnerregeln wie Dennos Laden. Schlaegt er
	// fehl: Tools/Blender/build_customer_figure.py und Tools/import_tripo_figure.py
	// (WB_FIGUR=kunden) erneut ausfuehren.
	const TArray<FString> Names = WiesbadenCustomerFigures::FindNames();
	AddInfo(FString::Printf(TEXT("Kundenfiguren: %s"), *FString::Join(Names, TEXT(", "))));
	TestTrue(FString::Printf(TEXT("mindestens zwei Kundenfiguren zum Abwechseln (%d)"), Names.Num()), Names.Num() >= 2);
	TestTrue(TEXT("Iris ist dabei"), Names.Contains(TEXT("Iris")));
	TestEqual(TEXT("jede gefundene Figur ist vollstaendig"), WiesbadenCustomerFigures::LoadAll().Num(), Names.Num());

	for (const FString& Name : Names)
	{
		const FString Folder = FString(WiesbadenCustomerFigures::RootPath()) / Name;
		for (const FString& Issue : FindIssues(ScanFolder(*Folder, *this)))
		{
			AddError(FString::Printf(TEXT("%s-Ordner: %s"), *Name, *Issue));
		}

		// Das Skelett: Knochen, die AWiesbadenDeliveryCustomer und die Bewegungen
		// brauchen, in der Grundhaltung dort, wo eine 1,60-1,80-m-Figur sie hat.
		const FWbCustomerFigure Figure = WiesbadenCustomerFigures::Load(Name);
		const USkeletalMesh* Mesh = Figure.Mesh;
		if (!TestNotNull(*FString::Printf(TEXT("SK_%s"), *Name), Mesh)
			|| !TestNotNull(*FString::Printf(TEXT("%s: Skelett"), *Name), Mesh->GetSkeleton()))
		{
			continue;
		}
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		auto BoneZ = [&Ref](const TCHAR* Bone) -> FVector
		{
			const int32 Index = Ref.FindBoneIndex(FName(Bone));
			return Index == INDEX_NONE ? FVector(NAN) : FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Index).GetLocation();
		};
		for (const TCHAR* Bone : { TEXT("Root"), TEXT("Hips"), TEXT("Head"), TEXT("UpperArm_L"), TEXT("UpperArm_R"),
			TEXT("LowerArm_R"), TEXT("Hand_R"), TEXT("Thigh_L"), TEXT("Shin_L"), TEXT("Foot_L"), TEXT("Foot_R") })
		{
			TestTrue(FString::Printf(TEXT("%s: Knochen %s"), *Name, Bone), Ref.FindBoneIndex(FName(Bone)) != INDEX_NONE);
		}
		TestTrue(FString::Printf(TEXT("%s: Wurzel am Boden (%.0f cm)"), *Name, BoneZ(TEXT("Root")).Z),
			FMath::Abs(BoneZ(TEXT("Root")).Z) < 1.0);
		TestTrue(FString::Printf(TEXT("%s: Kopfansatz 1,30-1,55 m (%.0f cm)"), *Name, BoneZ(TEXT("Head")).Z),
			BoneZ(TEXT("Head")).Z > 130.0 && BoneZ(TEXT("Head")).Z < 155.0);
		TestTrue(FString::Printf(TEXT("%s: Knoechel knapp ueber dem Boden (%.0f cm)"), *Name, BoneZ(TEXT("Foot_L")).Z),
			BoneZ(TEXT("Foot_L")).Z > 2.0 && BoneZ(TEXT("Foot_L")).Z < 14.0);
		TestTrue(FString::Printf(TEXT("%s: Arme haengen (Hand unter der Schulter, auf Hueftenhoehe)"), *Name),
			BoneZ(TEXT("Hand_R")).Z < BoneZ(TEXT("UpperArm_R")).Z - 40.0 && BoneZ(TEXT("Hand_R")).Z < BoneZ(TEXT("Hips")).Z + 20.0);
		const FVector Shoulders = BoneZ(TEXT("UpperArm_L")) - BoneZ(TEXT("UpperArm_R"));
		// Unreal: X vorn, Y RECHTS - wer nach +X blickt, hat die linke Schulter bei -Y.
		TestTrue(FString::Printf(TEXT("%s: Blick nach +X, linke Schulter links (-Y) (%.0f / %.0f cm)"), *Name, Shoulders.Y, Shoulders.X),
			Shoulders.Y < -30.0 && FMath::Abs(Shoulders.X) < 10.0);

		// Die Bewegungen: auf DIESEM Skelett, Schleifenlaengen wie gebaut - alle
		// Figuren gleich, damit das Spiel sie gleich behandeln kann.
		const struct { ECustomerAnim Anim; double Seconds; } Expected[] = {
			{ ECustomerAnim::Idle, WiesbadenDennoDelivery::CustomerIdleLoopSeconds }, { ECustomerAnim::Walk, 1.0 },
			{ ECustomerAnim::Wave, 2.4 }, { ECustomerAnim::Sit, 4.2 } };
		for (const auto& E : Expected)
		{
			const TCHAR* Kind = WiesbadenCustomerFigures::AnimName(E.Anim);
			const UAnimSequence* Seq = Figure.Anim(E.Anim);
			if (!TestNotNull(*FString::Printf(TEXT("A_%s_%s"), *Name, Kind), Seq))
			{
				continue;
			}
			TestTrue(FString::Printf(TEXT("A_%s_%s auf dem eigenen Skelett"), *Name, Kind), Seq->GetSkeleton() == Mesh->GetSkeleton());
			TestTrue(FString::Printf(TEXT("A_%s_%s dauert %.1f s (%.2f)"), *Name, Kind, E.Seconds, Seq->GetPlayLength()),
				FMath::IsNearlyEqual(Seq->GetPlayLength(), E.Seconds, 0.05));
		}
		TestTrue(FString::Printf(TEXT("%s: vollstaendig"), *Name), Figure.IsComplete());
	}
	TestTrue(TEXT("Winken kuerzer als die Pause nach dem Dank"), 2.4 <= WiesbadenDennoDelivery::CustomerThankPauseSeconds);
	TestEqual(TEXT("Kundinnen atmen in Dennos Takt"), WiesbadenDennoDelivery::CustomerIdleLoopSeconds,
		AWiesbadenDennoShop::BreathPeriodSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoSkeletalFigureTest,
	"WiesbadenReal.World.DennoShop.DennoSkeletal",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoSkeletalFigureTest::RunTest(const FString& Parameters)
{
	using namespace DennoAssetHygiene;
	// Denno mit Skelett (Tools/Blender/rig_denno.py, Tools/import_tripo_figure.py
	// WB_FIGUR=Denno) nach denselben Ordnerregeln wie Dennos Laden.
	const TArray<FAssetInfo> Assets = ScanFolder(TEXT("/Game/Assets/People/Denno"), *this);
	for (const FString& Issue : FindIssues(Assets))
	{
		AddError(TEXT("Denno-Ordner: ") + Issue);
	}
	const USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, AWiesbadenDennoShop::DennoMeshPath());
	if (!TestNotNull(TEXT("SK_Denno"), Mesh) || !TestNotNull(TEXT("Skelett"), Mesh->GetSkeleton()))
	{
		return false;
	}
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	auto Bone = [&Ref](const TCHAR* Name) -> FVector
	{
		const int32 Index = Ref.FindBoneIndex(FName(Name));
		return Index == INDEX_NONE ? FVector(NAN) : FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Index).GetLocation();
	};
	// Das Augenlid fuers Zwinkern sitzt vorn im Gesicht, auf Augenhoehe, rechts.
	const FVector Eye = Bone(TEXT("Eye_R"));
	TestTrue(TEXT("Augenlid-Knochen Eye_R vorhanden"), Ref.FindBoneIndex(TEXT("Eye_R")) != INDEX_NONE);
	TestTrue(FString::Printf(TEXT("Auge auf 1,50-1,65 m (%.0f cm)"), Eye.Z), Eye.Z > 150.0 && Eye.Z < 165.0);
	TestTrue(FString::Printf(TEXT("Auge vorn im Gesicht (X %.0f cm)"), Eye.X), Eye.X > 3.0);
	TestTrue(FString::Printf(TEXT("rechtes Auge (Unreal: rechts = +Y, %.1f cm)"), Eye.Y), Eye.Y > 1.5);
	// Huefte anatomisch (Schritt vorgegeben, nicht gesucht): 0,80-0,95 m.
	TestTrue(FString::Printf(TEXT("Huefte 0,80-0,95 m (%.0f cm)"), Bone(TEXT("Hips")).Z),
		Bone(TEXT("Hips")).Z > 80.0 && Bone(TEXT("Hips")).Z < 95.0);

	// Alle Bewegungen des Arbeitsplans, auf diesem Skelett.
	for (int32 I = 0; I < static_cast<int32>(EDennoAnim::Count); ++I)
	{
		const TCHAR* Path = AWiesbadenDennoShop::DennoAnimPath(static_cast<EDennoAnim>(I));
		const UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, Path);
		if (!TestNotNull(FString::Printf(TEXT("Bewegung %s"), Path), Seq))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s auf dem Denno-Skelett"), Path), Seq->GetSkeleton() == Mesh->GetSkeleton());
		TestTrue(FString::Printf(TEXT("%s dauert 0,5-5 s (%.2f)"), Path, Seq->GetPlayLength()),
			Seq->GetPlayLength() > 0.5 && Seq->GetPlayLength() < 5.0);
	}
	// Die Zeitpunkte, an denen C++ Paket und Tasse loslaesst, liegen IN der Bewegung.
	const UAnimSequence* Handover = LoadObject<UAnimSequence>(nullptr, AWiesbadenDennoShop::DennoAnimPath(EDennoAnim::Handover));
	const UAnimSequence* Serve = LoadObject<UAnimSequence>(nullptr, AWiesbadenDennoShop::DennoAnimPath(EDennoAnim::Serve));
	if (Handover && Serve)
	{
		TestTrue(TEXT("Paket wird waehrend der Uebergabe losgelassen"),
			WiesbadenDennoWork::HandoverReleaseSeconds < Handover->GetPlayLength());
		TestTrue(TEXT("Tasse wird waehrend des Servierens abgestellt"),
			WiesbadenDennoWork::ServeReleaseSeconds < Serve->GetPlayLength());
	}
	return true;
}
#endif
