// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenBusLineFile.h"

#include "GIS/GeoCoordinateConverter.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbBusLineFile, Log, All);

namespace
{
	/** Einmal gelesen, fuer alle Aufrufer dieselbe Instanz (siehe Header). */
	TMap<FString, TSharedRef<WiesbadenBusLineFile::FLineRoute>> GRouteCache;
	TMap<FString, TSharedRef<WiesbadenBusLine::FBusSchedule>> GScheduleCache;

	/** Fassung der Datei: Aenderungszeit in Ticks; fehlende Datei -> 0. */
	int64 FileVersion(const FString& FullPath)
	{
		return IFileManager::Get().FileExists(*FullPath)
			? IFileManager::Get().GetTimeStamp(*FullPath).GetTicks()
			: 0;
	}

	void ReadPairs(const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FVector2D>& Out)
	{
		if (!Arr) { return; }
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
			if (V.IsValid() && V->TryGetArray(P) && P && P->Num() >= 2)
			{
				Out.Add(FVector2D((*P)[0]->AsNumber(), (*P)[1]->AsNumber()));
			}
		}
	}

	void ReadStrings(const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
	{
		if (!Arr) { return; }
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S)) { Out.Add(S); }
		}
	}

	WiesbadenBusLineFile::FLineFile ParseLineFile(const FString& FileName, const FString& FullPath)
	{
		WiesbadenBusLineFile::FLineFile F;
		F.FileName = FileName;
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *FullPath))
		{
			UE_LOG(LogWbBusLineFile, Warning, TEXT("Liniendatei nicht lesbar: %s"), *FullPath);
			return F;
		}
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
		{
			UE_LOG(LogWbBusLineFile, Warning, TEXT("Liniendatei kein gueltiges JSON: %s"), *FullPath);
			return F;
		}

		const TArray<TSharedPtr<FJsonValue>>* PathArr = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* StopArr = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* NameArr = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* MonArr = nullptr;
		Obj->TryGetArrayField(TEXT("path"), PathArr);
		Obj->TryGetArrayField(TEXT("stops"), StopArr);
		Obj->TryGetArrayField(TEXT("stop_names"), NameArr);
		Obj->TryGetArrayField(TEXT("monitor_stops"), MonArr);
		ReadPairs(PathArr, F.GeoPath);
		ReadPairs(StopArr, F.GeoStops);
		ReadStrings(NameArr, F.StopNames);
		ReadStrings(MonArr, F.MonitorStops);
		// "*" heisst: JEDE Halte der Linie bekommt eine Saeule (der Monitor stellt
		// je Halte zwei auf, eine pro Strassenseite). Ein Platzhalter statt einer
		// Liste mit 40 Namen: eine verlaengerte Linie bekommt damit von selbst
		// Saeulen an den neuen Halten statt still weniger als vorher.
		if (F.MonitorStops.Contains(TEXT("*")))
		{
			F.MonitorStops = F.StopNames;
		}

		Obj->TryGetStringField(TEXT("ref"), F.Ref);
		if (F.Ref.IsEmpty() && FileName.StartsWith(TEXT("line")))
		{
			// Notnagel statt leerer Anzeige: der Dateiname traegt die Liniennummer.
			F.Ref = FileName.Mid(4).Replace(TEXT(".json"), TEXT(""));
		}
		Obj->TryGetStringField(TEXT("to"), F.Destination);
		// `from`: der andere Endpunkt. Die Saeule auf der Gegenseite der Strasse
		// zeigt die Gegenrichtung und braucht deren Zieltext - ohne ihn stuende auf
		// beiden Tafeln dasselbe Ziel, und die eine Haelfte waere falsch.
		Obj->TryGetStringField(TEXT("from"), F.Origin);

		double Headway = 0.0;
		if (Obj->TryGetNumberField(TEXT("headway_seconds"), Headway) && Headway > 0.0)
		{
			F.HeadwaySeconds = Headway;
		}
		double Term = 0.0;
		if (Obj->TryGetNumberField(TEXT("terminus_dwell_seconds"), Term) && Term >= 0.0)
		{
			F.TerminusDwellSeconds = Term;
		}

		const TSharedPtr<FJsonObject>* Blinds = nullptr;
		if (Obj->TryGetObjectField(TEXT("blinds"), Blinds) && Blinds && Blinds->IsValid())
		{
			F.Blinds.Dir = TEXT("/Game/Vehicles/Bus/Blind");
			(*Blinds)->TryGetStringField(TEXT("dir"), F.Blinds.Dir);
			(*Blinds)->TryGetStringField(TEXT("forward"), F.Blinds.Forward);
			(*Blinds)->TryGetStringField(TEXT("backward"), F.Blinds.Backward);
			(*Blinds)->TryGetStringField(TEXT("line"), F.Blinds.Line);
		}

		F.bLoaded = true;
		UE_LOG(LogWbBusLineFile, Log,
			TEXT("Liniendatei %s gelesen: Linie %s, %d Wegpunkte, %d Halte, %d Namen, %d Monitorhalte, Takt %.0f s, Wendezeit %.0f s, Ziel '%s' / Gegenrichtung '%s'."),
			*FileName, *F.Ref, F.GeoPath.Num(), F.GeoStops.Num(), F.StopNames.Num(),
			F.MonitorStops.Num(), F.HeadwaySeconds, F.TerminusDwellSeconds, *F.Destination, *F.Origin);
		return F;
	}

	/** Geo -> Welt, Bogenlaengen und Halte-Bogenlaengen: genau einmal je Datei. */
	void ProjectRoute(WiesbadenBusLineFile::FLineRoute& Out, const UGeoCoordinateConverter& Converter)
	{
		const WiesbadenBusLineFile::FLineFile& F = Out.File;
		Out.WorldPath.Reset();
		Out.ArcCm.Reset();
		Out.Route.StopArcCm.Reset();
		Out.Route.TotalLengthCm = 0.0;
		for (const FVector2D& G : F.GeoPath)
		{
			const FGeoCoordinate C(G.Y, G.X, 0.0);
			const FVector Wld = Converter.GeoToUnrealGround(C);
			Out.WorldPath.Add(FVector(Wld.X, Wld.Y, 0.0));
		}
		if (Out.WorldPath.Num() < 2)
		{
			UE_LOG(LogWbBusLineFile, Warning, TEXT("Liniendatei %s: %d Wegpunkte - keine Route."),
				*F.FileName, Out.WorldPath.Num());
			return;
		}
		Out.ArcCm.Add(0.0);
		for (int32 i = 1; i < Out.WorldPath.Num(); ++i)
		{
			Out.ArcCm.Add(Out.ArcCm.Last() + FVector2D::Distance(
				FVector2D(Out.WorldPath[i - 1].X, Out.WorldPath[i - 1].Y),
				FVector2D(Out.WorldPath[i].X, Out.WorldPath[i].Y)));
		}
		Out.Route.TotalLengthCm = Out.ArcCm.Last();

		// Jede Halte auf den naechsten Pfadpunkt legen: ihre Bogenlaenge ist die
		// des projizierten Punktes. Ohne das waeren Halte und Fahrlinie zwei
		// verschiedene Geometrien (die OSM-Knoten liegen neben der Linie).
		for (const FVector2D& G : F.GeoStops)
		{
			const FGeoCoordinate C(G.Y, G.X, 0.0);
			const FVector Wld = Converter.GeoToUnrealGround(C);
			const FVector2D S(Wld.X, Wld.Y);
			int32 Best = 0;
			double BestD = TNumericLimits<double>::Max();
			for (int32 i = 0; i < Out.WorldPath.Num(); ++i)
			{
				const double D = FVector2D::DistSquared(FVector2D(Out.WorldPath[i].X, Out.WorldPath[i].Y), S);
				if (D < BestD) { BestD = D; Best = i; }
			}
			Out.Route.StopArcCm.Add(Out.ArcCm[Best]);
		}
		Out.Route.StopArcCm.Sort();
		if (Out.Route.StopArcCm.Num() < 2)
		{
			// Ohne mindestens zwei Halte gibt es keine Rundfahrt: Anfang und Ende.
			Out.Route.StopArcCm.Reset();
			Out.Route.StopArcCm.Add(0.0);
			Out.Route.StopArcCm.Add(Out.Route.TotalLengthCm);
		}
		UE_LOG(LogWbBusLineFile, Log,
			TEXT("Linie %s: Route %.2f km, %d Halte auf der Linie (Bogen %.0f..%.0f m)."),
			*F.Ref, Out.Route.TotalLengthCm / 100000.0, Out.Route.StopArcCm.Num(),
			Out.Route.StopArcCm.Num() > 0 ? Out.Route.StopArcCm[0] / 100.0 : 0.0,
			Out.Route.StopArcCm.Num() > 0 ? Out.Route.StopArcCm.Last() / 100.0 : 0.0);
	}
}

FString WiesbadenBusLineFile::LineFilePath(const FString& FileName)
{
	return FPaths::ProjectDir() / TEXT("Data/Raw/Bus") / FileName;
}

TSharedRef<const WiesbadenBusLineFile::FLineRoute> WiesbadenBusLineFile::ReadLine(
	const FString& FileName, const UGeoCoordinateConverter& Converter)
{
	const FString FullPath = LineFilePath(FileName);
	const FGeoCoordinate Origin = Converter.GetOrigin();
	const FString Key = FString::Printf(TEXT("%s|%lld|%.6f|%.6f"),
		*FileName, FileVersion(FullPath), Origin.Latitude, Origin.Longitude);
	if (const TSharedRef<FLineRoute>* Found = GRouteCache.Find(Key))
	{
		return *Found;
	}

	TSharedRef<FLineRoute> Entry = MakeShared<FLineRoute>();
	Entry->File = ParseLineFile(FileName, FullPath);
	if (Entry->File.bLoaded && Converter.IsInitialized())
	{
		ProjectRoute(*Entry, Converter);
	}
	else if (Entry->File.bLoaded)
	{
		// Ohne Georeferenz gibt es keine Weltkoordinaten; der Aufrufer merkt das an
		// der leeren Route (statt stillschweigend bei (0,0,0) zu bauen).
		UE_LOG(LogWbBusLineFile, Warning,
			TEXT("Liniendatei %s gelesen, aber der Georeferenz-Konverter ist nicht initialisiert - keine Route."),
			*FileName);
	}
	GRouteCache.Add(Key, Entry);
	return Entry;
}

TSharedRef<const WiesbadenBusLine::FBusSchedule> WiesbadenBusLineFile::ReadSchedule(const FString& ScheduleFile)
{
	const FString FullPath = LineFilePath(ScheduleFile);
	const FString Key = FString::Printf(TEXT("%s|%lld"), *ScheduleFile, FileVersion(FullPath));
	if (const TSharedRef<WiesbadenBusLine::FBusSchedule>* Found = GScheduleCache.Find(Key))
	{
		return *Found;
	}

	TSharedRef<WiesbadenBusLine::FBusSchedule> Schedule = MakeShared<WiesbadenBusLine::FBusSchedule>();
	if (ScheduleFile.IsEmpty())
	{
		GScheduleCache.Add(Key, Schedule);
		return Schedule;
	}
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *FullPath))
	{
		UE_LOG(LogWbBusLineFile, Warning, TEXT("Fahrplandatei nicht lesbar (Dauerbetrieb bleibt): %s"), *FullPath);
		GScheduleCache.Add(Key, Schedule);
		return Schedule;
	}
	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
	{
		UE_LOG(LogWbBusLineFile, Warning, TEXT("Fahrplandatei kein gueltiges JSON: %s"), *FullPath);
		GScheduleCache.Add(Key, Schedule);
		return Schedule;
	}
	double DayS = 86400.0;
	Obj->TryGetNumberField(TEXT("day_seconds"), DayS);
	Schedule->DaySeconds = (DayS > 0.0) ? DayS : 86400.0;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Obj->TryGetArrayField(TEXT("weekday"), Arr))
	{
		TArray<FString> Times;
		ReadStrings(Arr, Times);
		for (const FString& HHMM : Times)
		{
			int32 Colon = INDEX_NONE;
			if (HHMM.FindChar(TEXT(':'), Colon))
			{
				const int32 H = FCString::Atoi(*HHMM.Left(Colon));
				const int32 M = FCString::Atoi(*HHMM.Mid(Colon + 1));
				Schedule->DepartureSeconds.Add((double)H * 3600.0 + (double)M * 60.0);
			}
		}
	}
	Schedule->DepartureSeconds.Sort();
	UE_LOG(LogWbBusLineFile, Log, TEXT("Fahrplandatei %s gelesen: %d Abfahrten, Tag %.0f s."),
		*ScheduleFile, Schedule->DepartureSeconds.Num(), Schedule->DaySeconds);
	GScheduleCache.Add(Key, Schedule);
	return Schedule;
}

void WiesbadenBusLineFile::ClearCache()
{
	GRouteCache.Reset();
	GScheduleCache.Reset();
}
