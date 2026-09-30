// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Misc/ConfigCacheIni.h"
#include "Missions/WiesbadenParcours.h"
#include "Vehicles/WiesbadenVehiclePhysics.h"

namespace
{
	// Faehrt die Bewertung geradlinig von A nach B (10-cm-Schritte, 0,01 s je Schritt).
	void Fahre(FWbParcoursBewertung& B, FVector2D Von, FVector2D Bis, float Kmh, float Kurs = 0.0f, bool bHandbremse = false,
		float Grip = 1.0f)
	{
		const int32 Schritte = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(Von, Bis) / 10.0f));
		for (int32 I = 1; I <= Schritte; ++I)
		{
			FWbParcoursProbe P;
			P.PosCm = FMath::Lerp(Von, Bis, static_cast<float>(I) / Schritte);
			P.KursGrad = Kurs;
			P.Kmh = Kmh;
			P.bHandbremse = bHandbremse;
			P.DtSekunden = 0.01f;
			P.BelagsGrip = Grip;
			B.Schritt(P);
		}
	}

	void Halte(FWbParcoursBewertung& B, FVector2D Pos, float Kurs = 0.0f)
	{
		FWbParcoursProbe P;
		P.PosCm = Pos;
		P.KursGrad = Kurs;
		P.Kmh = 0.0f;
		P.DtSekunden = 0.01f;
		B.Schritt(P);
	}

	// Start und Slalom auf der richtigen Seite (Kegel 1 links, dann abwechselnd);
	// endet auf der Mittellinie hinter dem letzten Kegel, gibt dessen X zurueck.
	float Slalom(FWbParcoursBewertung& B)
	{
		const FWbParcoursLayout& L = B.GetLayout();
		Fahre(B, FVector2D(-500, 0), FVector2D(1000, 0), 30.0f);
		float X = 1000.0f;
		for (int32 I = 0; I < L.SlalomKegel.Num(); ++I)
		{
			const float Y = (I % 2 == 0) ? -200.0f : 200.0f;
			Fahre(B, FVector2D(X, 0), FVector2D(L.SlalomKegel[I].X, Y), 30.0f);
			X = L.SlalomKegel[I].X;
			Fahre(B, FVector2D(X, Y), FVector2D(X + 700.0f, 0), 30.0f);
			X += 700.0f;
		}
		return X;
	}

	// Sauberer Lauf mit gesetzten Posen: Slalom auf der richtigen Seite, 50 km/h
	// an der Linie, Halt mitten in der Box, Handbremswende, zurueck ins Ziel.
	void SaubereRunde(FWbParcoursBewertung& B, bool bHandbremse, float HaltX)
	{
		const FWbParcoursLayout& L = B.GetLayout();
		const float X = Slalom(B);
		Fahre(B, FVector2D(X, 0), FVector2D(L.BremslinieX + 100.0f, 0), 50.0f);
		Fahre(B, FVector2D(L.BremslinieX + 100.0f, 0), FVector2D(HaltX, 0), 20.0f);
		Halte(B, FVector2D(HaltX, 0));
		Fahre(B, FVector2D(HaltX, 0), FVector2D(21000, 0), 35.0f);
		Fahre(B, FVector2D(21000, 0), FVector2D(21000, -600), 20.0f, 180.0f, bHandbremse);
		Fahre(B, FVector2D(21000, -600), FVector2D(-300, -1200), 60.0f, 180.0f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParcoursBewertungTest,
	"WiesbadenReal.Missions.Parcours.Bewertung",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FParcoursBewertungTest::RunTest(const FString& Parameters)
{
	const FWbParcoursLayout L = FWbParcoursLayout::Standard();
	TestEqual(TEXT("Fuenf Slalomkegel"), L.SlalomKegel.Num(), 5);
	TestEqual(TEXT("Alle Kegel: Slalom + Tor + Linie + Box + Wende"), L.AlleKegel().Num(), 17);

	// -- Sauberer Lauf -------------------------------------------------------
	{
		FWbParcoursBewertung B(L);
		SaubereRunde(B, true, 18100.0f);
		const FWbParcoursErgebnis E = B.GetErgebnis();
		TestTrue(TEXT("Sauber: im Ziel"), E.bImZiel);
		TestEqual(TEXT("Sauber: kein Kegel"), E.KegelGetroffen, 0);
		TestEqual(TEXT("Sauber: kein Torfehler"), E.TorFehler, 0);
		TestTrue(TEXT("Sauber: in der Box angehalten"), E.bAngehalten && E.StoppAbweichungM == 0.0f);
		TestTrue(TEXT("Sauber: Handbremse genutzt"), E.bHandbremseGenutzt);
		TestEqual(TEXT("Sauber: keine Strafe"), E.StrafSekunden, 0.0f);
		TestEqual(TEXT("Sauber: Sauberkeit 100"), E.Sauberkeit, 100);
		TestTrue(TEXT("Sauber: Zeit gezaehlt"), E.FahrzeitSekunden > 10.0f);
	}

	// -- Keine Uhr vor der Startlinie, kein Start neben dem Tor ---------------
	{
		FWbParcoursBewertung B(L);
		Fahre(B, FVector2D(-800, 0), FVector2D(-100, 0), 30.0f);
		TestTrue(TEXT("Vor der Linie: bereit"), B.GetAbschnitt() == EWbParcoursAbschnitt::Bereit);
		Fahre(B, FVector2D(-100, 900), FVector2D(300, 900), 30.0f);
		TestTrue(TEXT("Neben dem Starttor: kein Start"), B.GetAbschnitt() == EWbParcoursAbschnitt::Bereit);
		TestEqual(TEXT("... und keine Zeit"), B.GetFahrzeit(), 0.0f);
	}

	// -- Falsche Slalomseite und Kegelkontakt --------------------------------
	{
		FWbParcoursBewertung B(L);
		Fahre(B, FVector2D(-500, 0), FVector2D(1500, 0), 30.0f);
		// Kegel 1 muss links (Y < 0) umfahren werden - rechts vorbei ist ein Torfehler.
		Fahre(B, FVector2D(1500, 250), FVector2D(3000, 250), 30.0f);
		TestEqual(TEXT("Rechts am ersten Kegel: Torfehler"), B.GetErgebnis().TorFehler, 1);
		TestEqual(TEXT("... 5 s Strafe"), B.GetStrafSekunden(), FWbParcoursBewertung::StrafeTor);
		// Mitten ueber Kegel 2 (X 4000): umgefahren, genau einmal.
		Fahre(B, FVector2D(3000, 0), FVector2D(4600, 0), 30.0f);
		const TArray<int32> Neu = B.HoleNeuUmgefahrene();
		TestTrue(TEXT("Kegel 2 umgefahren"), Neu.Num() == 1 && Neu[0] == 1);
		TestTrue(TEXT("... und nur einmal gezaehlt"), B.HoleNeuUmgefahrene().Num() == 0 && B.GetErgebnis().KegelGetroffen == 1);
		// Seitlich 1 m am Kegel vorbei (Wagen 80 cm halb breit): keine Beruehrung.
		FWbParcoursBewertung C(L);
		Fahre(C, FVector2D(-500, 0), FVector2D(1500, 0), 30.0f);
		Fahre(C, FVector2D(1500, -200), FVector2D(3000, -200), 30.0f);
		TestEqual(TEXT("1 m Luft: kein Kegel"), C.GetErgebnis().KegelGetroffen, 0);
	}

	// -- Stoppbox: 3 m zu weit kostet 3 s ------------------------------------
	{
		FWbParcoursBewertung B(L);
		SaubereRunde(B, true, L.BoxBisX + 300.0f);
		const FWbParcoursErgebnis E = B.GetErgebnis();
		TestTrue(FString::Printf(TEXT("3 m hinter der Box (%.1f m)"), E.StoppAbweichungM),
			FMath::IsNearlyEqual(E.StoppAbweichungM, 3.0f, 0.05f));
		TestTrue(TEXT("... 3 s Strafe"), FMath::IsNearlyEqual(E.StrafSekunden, 3.0f, 0.05f));
		TestTrue(TEXT("... nicht mehr ganz sauber"), E.Sauberkeit < 100);
	}

	// -- Wende ohne Handbremse ------------------------------------------------
	{
		FWbParcoursBewertung B(L);
		SaubereRunde(B, false, 18100.0f);
		const FWbParcoursErgebnis E = B.GetErgebnis();
		TestFalse(TEXT("Ohne Handbremse erkannt"), E.bHandbremseGenutzt);
		TestEqual(TEXT("... 3 s Strafe"), E.StrafSekunden, FWbParcoursBewertung::StrafeOhneHandbremse);
		TestTrue(TEXT("... trotzdem im Ziel"), E.bImZiel);
	}

	// -- Zu langsam an der Bremslinie ----------------------------------------
	{
		FWbParcoursBewertung B(L);
		const float X = Slalom(B);
		Fahre(B, FVector2D(X, 0), FVector2D(L.BremslinieX + 200.0f, 0), 25.0f);
		TestTrue(TEXT("25 km/h an der Linie: zu langsam"), B.GetErgebnis().KmhAnBremslinie == 25.0f
			&& B.GetStrafSekunden() == FWbParcoursBewertung::StrafeZuLangsam);
	}

	// -- Zurueck und erneut vorbei: ein Slalomkegel zaehlt nur einmal ---------
	{
		FWbParcoursBewertung B(L);
		Fahre(B, FVector2D(-500, 0), FVector2D(1500, 0), 30.0f);
		Fahre(B, FVector2D(1500, 250), FVector2D(3000, 250), 30.0f);    // rechts vorbei: Fehler
		Fahre(B, FVector2D(3000, 250), FVector2D(1500, 250), 10.0f);    // zurueckgesetzt
		Fahre(B, FVector2D(1500, 250), FVector2D(3000, 250), 30.0f);    // noch einmal rechts
		TestEqual(TEXT("Derselbe Kegel zaehlt nur einmal"), B.GetErgebnis().TorFehler, 1);
	}

	// -- Sauberkeit folgt dem GEWERTETEN Layout, nicht dem Standard ------------
	{
		FWbParcoursLayout Streng = L;
		Streng.MindestKmhAnBremslinie = 50.0f;
		FWbParcoursBewertung B(Streng);
		const float X = Slalom(B);
		Fahre(B, FVector2D(X, 0), FVector2D(Streng.BremslinieX + 200.0f, 0), 45.0f);
		const FWbParcoursErgebnis E = B.GetErgebnis();
		TestTrue(TEXT("45 km/h bei Mindestens 50: Strafe"), E.bZuLangsam && E.StrafSekunden == FWbParcoursBewertung::StrafeZuLangsam);
		FWbParcoursErgebnis Nur = E;
		Nur.bAngehalten = true;   // nur die Linie bewerten
		Nur.bHandbremseGenutzt = true;
		TestEqual(TEXT("... und dieselbe Sauberkeitsminderung"), FWbParcoursBewertung::BerechneSauberkeit(Nur), 90);
	}

	// -- Regen-Variante: nur, wenn die GANZE Runde nass war --------------------
	{
		FWbParcoursBewertung B(L);
		Fahre(B, FVector2D(-500, 0), FVector2D(-100, 0), 30.0f);   // vor dem Start trocken: zaehlt nicht
		Fahre(B, FVector2D(-100, 0), FVector2D(1500, 0), 30.0f, 0.0f, false, 0.65f);
		TestTrue(TEXT("Nass ab dem Start: Regenwertung"), B.IstRegen() && B.GetErgebnis().bRegen);
		Fahre(B, FVector2D(1500, 0), FVector2D(1600, 0), 30.0f, 0.0f, false, 0.9f);
		Fahre(B, FVector2D(1600, 0), FVector2D(1700, 0), 30.0f, 0.0f, false, 0.65f);
		TestFalse(TEXT("Wird die Strasse unterwegs trocken: trockene Wertung"), B.IstRegen());
		TestEqual(TEXT("... das Ergebnis traegt das Maximum der Runde"), B.GetErgebnis().BelagsGripMax, 0.9f);
	}
	{
		FWbParcoursBewertung B(L);
		Fahre(B, FVector2D(-500, 0), FVector2D(1500, 0), 30.0f, 0.0f, false, 0.79f);
		TestFalse(TEXT("Schnee (Grip 0,79) ist keine Regenwertung"), B.IstRegen());
	}

	// -- Medaillen ------------------------------------------------------------
	TestEqual(TEXT("Gold braucht Sauberkeit"), FWbParcoursBewertung::BerechneMedaille(44.0f, 80), FString(TEXT("Silber")));
	TestEqual(TEXT("Gold"), FWbParcoursBewertung::BerechneMedaille(44.0f, 100), FString(TEXT("Gold")));
	TestEqual(TEXT("Bronze"), FWbParcoursBewertung::BerechneMedaille(60.0f, 100), FString(TEXT("Bronze")));
	TestEqual(TEXT("Zu langsam: keine Medaille"), FWbParcoursBewertung::BerechneMedaille(90.0f, 100), FString(TEXT("ohne Medaille")));
	using FB = FWbParcoursBewertung;
	TestTrue(TEXT("Regengrenzen liegen hinter den trockenen"), FB::GoldSekundenRegen > FB::GoldSekunden
		&& FB::SilberSekundenRegen > FB::SilberSekunden && FB::BronzeSekundenRegen > FB::BronzeSekunden);
	const float ZwischenGold = 0.5f * (FB::GoldSekunden + FB::GoldSekundenRegen);
	TestEqual(TEXT("Zwischen den Goldgrenzen: trocken Silber"), FB::BerechneMedaille(ZwischenGold, 100, false), FString(TEXT("Silber")));
	TestEqual(TEXT("... im Regen Gold"), FB::BerechneMedaille(ZwischenGold, 100, true), FString(TEXT("Gold")));
	TestEqual(TEXT("Regen-Gold braucht auch Sauberkeit"), FB::BerechneMedaille(ZwischenGold, 80, true), FString(TEXT("Silber")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParcoursBestzeitTest,
	"WiesbadenReal.Missions.Parcours.Bestzeit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FParcoursBestzeitTest::RunTest(const FString& Parameters)
{
	// Eigene Ini im Speicher - die Spieler-Einstellungen bleiben unberuehrt.
	FConfigFile Ini;
	TestEqual(TEXT("Anfangs keine Bestzeit"), FWbParcoursBestzeit::Lesen(Ini, false), 0.0f);
	TestTrue(TEXT("Die erste Zeit ist Bestzeit"), FWbParcoursBestzeit::Eintragen(Ini, false, 50.0f));
	TestFalse(TEXT("Langsamer: keine Bestzeit"), FWbParcoursBestzeit::Eintragen(Ini, false, 55.0f));
	TestFalse(TEXT("Gleich schnell: keine Bestzeit"), FWbParcoursBestzeit::Eintragen(Ini, false, 50.0f));
	TestTrue(TEXT("Schneller: neue Bestzeit"), FWbParcoursBestzeit::Eintragen(Ini, false, 48.5f));
	TestEqual(TEXT("... und sie steht drin"), FWbParcoursBestzeit::Lesen(Ini, false), 48.5f);
	TestEqual(TEXT("Regen hat eine eigene Bestzeit"), FWbParcoursBestzeit::Lesen(Ini, true), 0.0f);
	TestTrue(TEXT("Regen: erste Zeit ist Bestzeit"), FWbParcoursBestzeit::Eintragen(Ini, true, 60.0f));
	TestEqual(TEXT("... die trockene bleibt"), FWbParcoursBestzeit::Lesen(Ini, false), 48.5f);
	TestFalse(TEXT("Keine Zeit (0 s) traegt nichts ein"), FWbParcoursBestzeit::Eintragen(Ini, true, 0.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParcoursFahrerTest,
	"WiesbadenReal.Missions.Parcours.FahrerRunde",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die ganze Runde mit der ECHTEN Fahrphysik des Kaefers: der Parcours-Fahrer
 * steuert, das Einspurmodell faehrt, die Pose wird wie im Spiel integriert.
 * Beweist, dass der Parcours mit dem Wagen sauber fahrbar ist (Slalom-Abstand,
 * Bremsweg zur Box, Handbremswende) - und liefert die Referenzzeit.
 */
bool FParcoursFahrerTest::RunTest(const FString& Parameters)
{
	// Eine Runde mit der echten Kaefer-Physik; die Pose wird wie im Spiel integriert.
	auto Runde = [](bool bFehler, float Grip, FString& Info)
	{
		const FWbParcoursLayout L = FWbParcoursLayout::Standard();
		FWbParcoursBewertung B(L);
		FWbParcoursFahrer Fahrer(L);
		Fahrer.bFehlerMachen = bFehler;
		FWiesbadenVehiclePhysics V;
		V.Reset();
		Fahrer.MaxLenkGrad = V.MaxSteerAngleDeg;
		Fahrer.LenkAbfallMS = V.SteerFalloffSpeedMetersPerS;

		FVector2D Pos(-1000.0f, 0.0f);
		float KursGrad = 0.0f;
		constexpr float Dt = 1.0f / 60.0f;
		float MaxSchwimm = 0.0f;
		FWiesbadenVehiclePhysicsOutput Out;
		for (int32 Schritt = 0; Schritt < 60 * 240 && !B.IstFertig(); ++Schritt)
		{
			FWbParcoursProbe P;
			P.PosCm = Pos;
			P.KursGrad = FMath::UnwindDegrees(KursGrad);
			P.Kmh = FMath::Abs(V.SpeedMetersPerS) * 3.6f;
			P.DtSekunden = Dt;
			P.BelagsGrip = Grip;
			const FWbParcoursSteuerung S = Fahrer.Steuern(P, B.GetAbschnitt());
			P.bHandbremse = S.bHandbremse;
			B.Schritt(P);

			FWiesbadenVehiclePhysicsInput In;
			In.Throttle = S.Gas;
			In.Brake = S.Bremse;
			In.Steering = S.Lenkung;
			In.bHandbrake = S.bHandbremse;
			In.SurfaceGripScale = Grip;
			V.Tick(In, Dt, Out);
			const float Rad = FMath::DegreesToRadians(KursGrad);
			const FVector2D Vor(FMath::Cos(Rad), FMath::Sin(Rad));
			const FVector2D Rechts(-FMath::Sin(Rad), FMath::Cos(Rad));
			Pos += (Vor * Out.ForwardSpeedMetersPerS + Rechts * Out.LateralVelocityMetersPerS) * (100.0f * Dt);
			KursGrad += FMath::RadiansToDegrees(Out.YawRateRadPerS) * Dt;
			MaxSchwimm = FMath::Max(MaxSchwimm, FMath::Abs(Out.SlipAngleDeg));
		}
		const FWbParcoursErgebnis E = B.GetErgebnis();
		Info = FString::Printf(TEXT("Parcours-Fahrer%s%s: %s, Fahrzeit %.1f s, Strafe %.0f s, Kegel %d, Tore %d, Linie %.0f km/h, Halt %.1f m neben der Box, Handbremse %d, Wendezone verfehlt %d, Sauberkeit %d, %s, max Schwimmwinkel %.0f Grad"),
			bFehler ? TEXT(" (mit Fehlern)") : TEXT(""), E.bRegen ? TEXT(" (Regen)") : TEXT(""), WbParcoursAbschnittName(B.GetAbschnitt()), E.FahrzeitSekunden,
			E.StrafSekunden, E.KegelGetroffen, E.TorFehler, E.KmhAnBremslinie, E.StoppAbweichungM,
			E.bHandbremseGenutzt ? 1 : 0, E.bWendezoneVerfehlt ? 1 : 0, E.Sauberkeit, *E.Medaille, MaxSchwimm);
		return E;
	};

	FString Info;
	const FWbParcoursErgebnis E = Runde(false, 1.0f, Info);
	AddInfo(Info);
	TestTrue(TEXT("Fahrer erreicht das Ziel"), E.bImZiel);
	TestEqual(TEXT("Fahrer beruehrt keinen Kegel"), E.KegelGetroffen, 0);
	TestEqual(TEXT("Fahrer: kein Torfehler"), E.TorFehler, 0);
	TestTrue(TEXT("Fahrer haelt in der Stoppbox"), E.bAngehalten && E.StoppAbweichungM == 0.0f);
	TestTrue(TEXT("Fahrer wendet mit Handbremse in der Zone"), E.bHandbremseGenutzt && !E.bWendezoneVerfehlt);
	TestEqual(TEXT("Fahrer: Sauberkeit 100"), E.Sauberkeit, 100);
	TestTrue(TEXT("Referenzzeit ergibt Gold"), E.Medaille == TEXT("Gold"));

	// Mit Absicht-Fehlern: die Wertung muss sie finden, die Runde endet trotzdem.
	const FWbParcoursErgebnis F = Runde(true, 1.0f, Info);
	AddInfo(Info);
	TestTrue(TEXT("Fehlerlauf: im Ziel"), F.bImZiel);
	TestTrue(TEXT("Fehlerlauf: Kegel 2 umgefahren"), F.KegelGetroffen >= 1);
	TestTrue(TEXT("Fehlerlauf: zu langsam an der Linie"), F.KmhAnBremslinie < FWbParcoursLayout::Standard().MindestKmhAnBremslinie);
	TestTrue(TEXT("Fehlerlauf: Strafe und Sauberkeit unter 100"), F.StrafSekunden > 0.0f && F.Sauberkeit < 100);
	TestFalse(TEXT("Fehlerlauf: kein Gold"), F.Medaille == TEXT("Gold"));

	// Nasse Fahrbahn (Grip 0,65, kalibriert): sauber fahrbar, Regenwertung, und
	// die Referenzzeit ist Gold nur mit den Regen-Grenzen.
	const FWbParcoursErgebnis R = Runde(false, 0.65f, Info);
	AddInfo(Info);
	TestTrue(TEXT("Regen: im Ziel, Regenwertung"), R.bImZiel && R.bRegen);
	TestEqual(TEXT("Regen: kein Kegel"), R.KegelGetroffen, 0);
	TestEqual(TEXT("Regen: kein Torfehler"), R.TorFehler, 0);
	TestTrue(TEXT("Regen: ueber 40 km/h an der Linie, Halt in der Box"),
		!R.bZuLangsam && R.bAngehalten && R.StoppAbweichungM == 0.0f);
	TestTrue(TEXT("Regen: Handbremswende in der Zone"), R.bHandbremseGenutzt && !R.bWendezoneVerfehlt);
	TestEqual(TEXT("Regen: Sauberkeit 100"), R.Sauberkeit, 100);
	TestTrue(TEXT("Regen: Referenzzeit ergibt Gold"), R.Medaille == TEXT("Gold"));
	TestTrue(TEXT("Regen ist langsamer als trocken"), R.FahrzeitSekunden > E.FahrzeitSekunden);
	TestFalse(TEXT("... mit den trockenen Grenzen waere es kein Gold"),
		FWbParcoursBewertung::BerechneMedaille(R.GesamtSekunden, R.Sauberkeit, false) == TEXT("Gold"));
	return true;
}
