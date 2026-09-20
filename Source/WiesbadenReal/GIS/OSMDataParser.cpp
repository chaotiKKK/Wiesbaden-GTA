// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/OSMDataParser.h"

#include "WiesbadenReal.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "FastXml.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/**
	 * Fuegt einen Knoten ein und BEWAHRT dabei vorhandene Tags.
	 *
	 * Overpass liefert denselben Knoten mehrfach: einmal aus einer
	 * Knoten-Abfrage MIT Tags (z. B. highway=traffic_signals) und danach
	 * nochmals aus der Way-Rekursion OHNE Tags. Ein blindes Add() ueberschreibt
	 * den getaggten Eintrag.
	 *
	 * Gemessen an der Wiesbaden-Datei: 1.125.032 Knoten-Eintraege fuer 1.115.437
	 * eindeutige Knoten - 9.595 Duplikate. Von 2.310 Ampelknoten kamen dadurch
	 * nur DREI im Datensatz an, und die Stadt hatte keine einzige Ampel
	 * ("20213 Kreuzungen, 0 Ampeln"). Betroffen sind alle Knoten-Tags:
	 * Zebrastreifen, Stopp, Vorfahrt, Haltestellen, Strassenlaternen.
	 */
	void AddOrMergeNode(FOSMDataSet& OutDataSet, FOSMId Id, FOSMNode&& Node)
	{
		if (FOSMNode* Existing = OutDataSet.Nodes.Find(Id))
		{
			// Tags beider Vorkommen vereinen - welches zuerst kommt, ist nicht
			// garantiert.
			for (const TPair<FName, FString>& Tag : Node.Tags)
			{
				Existing->Tags.Add(Tag.Key, Tag.Value);
			}
			return;
		}

		OutDataSet.Nodes.Add(Id, MoveTemp(Node));
	}

	/**
	 * Fuegt einen Way ein und BEWAHRT dabei vorhandene Tags.
	 *
	 * Dieselbe Falle wie bei den Knoten, eine Ebene hoeher: Overpass liefert
	 * denselben Way mehrfach - einmal aus der eigentlichen Abfrage MIT allen
	 * Tags, danach nochmals als blosses Geruest (Id und Knotenliste, KEINE
	 * Tags), weil er Mitglied einer Relation ist. Ein blindes Add()
	 * ueberschreibt den getaggten Eintrag mit dem tagfreien, und damit
	 * verschwindet highway=* - der Way ist ab da keine Strasse mehr.
	 *
	 * Gemessen an der Wiesbaden-Datei: 225.479 Way-Eintraege fuer 223.434
	 * eindeutige Ways, also 2.045 Duplikate - und bei ALLEN 2.045 steht die
	 * tagfreie Kopie HINTEN. Davon waren 2.033 Strassen. Im Spiel fehlten
	 * dadurch 1.990 befahrbare Wege mit 118,6 km, darunter Stuecke der
	 * Rheinallee (2,29 km), der Mainzer Strasse (1,77 km), der Platter Strasse
	 * (1,07 km), des Konrad-Adenauer-Rings und des Kaiser-Friedrich-Rings.
	 *
	 * Aufgefallen ist es an einem einzigen Satz: "Platter Strasse
	 * stadtauswaerts fehlt in Spielwelt und Minikarte". Dass es in BEIDEN
	 * fehlte, war der Hinweis - die Minikarte zeichnet aus dem Netz, nicht aus
	 * der Geometrie, also fehlte es schon in den Daten.
	 */
	void AddOrMergeWay(FOSMDataSet& OutDataSet, FOSMId Id, FOSMWay&& Way)
	{
		if (FOSMWay* Existing = OutDataSet.Ways.Find(Id))
		{
			for (const TPair<FName, FString>& Tag : Way.Tags)
			{
				Existing->Tags.Add(Tag.Key, Tag.Value);
			}

			// Die laengere Knotenliste gewinnt. Ein Geruest-Eintrag traegt sie
			// zwar meist vollstaendig, aber ein abgeschnittenes Vorkommen darf
			// eine vollstaendige Liste nicht ersetzen.
			if (Way.NodeIds.Num() > Existing->NodeIds.Num())
			{
				Existing->NodeIds = MoveTemp(Way.NodeIds);
			}
			return;
		}

		OutDataSet.Ways.Add(Id, MoveTemp(Way));
	}

	/**
	 * FFastXml-Callback fuer OSM-XML.
	 *
	 * Das Format ist flach und zustandsbehaftet: <node>, <way> und <relation>
	 * enthalten <tag>, <nd> und <member> als Kinder. FFastXml liefert
	 * Element-Oeffnung, Attribute und Element-Schluss als separate Callbacks,
	 * ohne Verschachtelungsinformation - der aktuelle Kontext muss daher
	 * selbst verwaltet werden.
	 */
	class FOSMXmlCallback final : public IFastXmlCallback
	{
	public:
		explicit FOSMXmlCallback(FOSMDataSet& InDataSet)
			: DataSet(InDataSet)
		{
			// Vorreservierung anhand typischer Verhaeltnisse eines
			// Stadtextrakts. Verhindert ~20 Reallokationen der Hash-Tabellen
			// bei mehreren Hunderttausend Nodes.
			DataSet.Nodes.Reserve(500000);
			DataSet.Ways.Reserve(80000);
			DataSet.Relations.Reserve(4000);
		}

		int32 GetSkippedCount() const { return SkippedCount; }

		virtual bool ProcessXmlDeclaration(const TCHAR* /*ElementData*/, int32 /*XmlFileLineNumber*/) override
		{
			return true;
		}

		virtual bool ProcessComment(const TCHAR* /*Comment*/) override
		{
			return true;
		}

		virtual bool ProcessElement(const TCHAR* ElementName, const TCHAR* /*ElementData*/, int32 /*LineNumber*/) override
		{
			if (FCString::Strcmp(ElementName, TEXT("node")) == 0)
			{
				FlushCurrentElement();
				Context = EContext::Node;
				PendingNode = FOSMNode();
				bPendingIdValid = false;
				bPendingLatValid = false;
				bPendingLonValid = false;
			}
			else if (FCString::Strcmp(ElementName, TEXT("way")) == 0)
			{
				FlushCurrentElement();
				Context = EContext::Way;
				PendingWay = FOSMWay();
				bPendingIdValid = false;
			}
			else if (FCString::Strcmp(ElementName, TEXT("relation")) == 0)
			{
				FlushCurrentElement();
				Context = EContext::Relation;
				PendingRelation = FOSMRelation();
				bPendingIdValid = false;
			}
			else if (FCString::Strcmp(ElementName, TEXT("tag")) == 0)
			{
				SubContext = ESubContext::Tag;
				PendingTagKey = NAME_None;
				PendingTagValue.Reset();
			}
			else if (FCString::Strcmp(ElementName, TEXT("nd")) == 0)
			{
				SubContext = ESubContext::NodeRef;
			}
			else if (FCString::Strcmp(ElementName, TEXT("member")) == 0)
			{
				SubContext = ESubContext::Member;
				PendingMember = FOSMRelationMember();
			}
			else
			{
				// <osm>, <bounds>, <meta>, <note> und unbekannte Elemente
				// werden ignoriert, ohne den Kontext zu zerstoeren.
				SubContext = ESubContext::None;
			}

			return true;
		}

		virtual bool ProcessAttribute(const TCHAR* AttributeName, const TCHAR* AttributeValue) override
		{
			switch (SubContext)
			{
			case ESubContext::Tag:
				if (FCString::Strcmp(AttributeName, TEXT("k")) == 0)
				{
					PendingTagKey = FName(AttributeValue);
				}
				else if (FCString::Strcmp(AttributeName, TEXT("v")) == 0)
				{
					PendingTagValue = AttributeValue;
				}
				return true;

			case ESubContext::NodeRef:
				if (FCString::Strcmp(AttributeName, TEXT("ref")) == 0)
				{
					const FOSMId Ref = FCString::Atoi64(AttributeValue);
					if (Ref != 0 && Context == EContext::Way)
					{
						PendingWay.NodeIds.Add(Ref);
					}
				}
				return true;

			case ESubContext::Member:
				if (FCString::Strcmp(AttributeName, TEXT("type")) == 0)
				{
					if (FCString::Strcmp(AttributeValue, TEXT("node")) == 0)
					{
						PendingMember.Type = EOSMMemberType::Node;
					}
					else if (FCString::Strcmp(AttributeValue, TEXT("relation")) == 0)
					{
						PendingMember.Type = EOSMMemberType::Relation;
					}
					else
					{
						PendingMember.Type = EOSMMemberType::Way;
					}
				}
				else if (FCString::Strcmp(AttributeName, TEXT("ref")) == 0)
				{
					PendingMember.Ref = FCString::Atoi64(AttributeValue);
				}
				else if (FCString::Strcmp(AttributeName, TEXT("role")) == 0)
				{
					PendingMember.Role = FName(AttributeValue);
				}
				return true;

			case ESubContext::None:
			default:
				break;
			}

			// Attribute des umgebenden Elements (id, lat, lon).
			if (FCString::Strcmp(AttributeName, TEXT("id")) == 0)
			{
				const FOSMId Id = FCString::Atoi64(AttributeValue);
				if (Id != 0)
				{
					bPendingIdValid = true;
					switch (Context)
					{
					case EContext::Node:		PendingNode.Id = Id; break;
					case EContext::Way:			PendingWay.Id = Id; break;
					case EContext::Relation:	PendingRelation.Id = Id; break;
					default: break;
					}
				}
			}
			else if (Context == EContext::Node)
			{
				if (FCString::Strcmp(AttributeName, TEXT("lat")) == 0)
				{
					PendingNode.Location.Latitude = FCString::Atod(AttributeValue);
					bPendingLatValid = true;
				}
				else if (FCString::Strcmp(AttributeName, TEXT("lon")) == 0)
				{
					PendingNode.Location.Longitude = FCString::Atod(AttributeValue);
					bPendingLonValid = true;
				}
			}

			return true;
		}

		virtual bool ProcessClose(const TCHAR* Element) override
		{
			if (FCString::Strcmp(Element, TEXT("tag")) == 0)
			{
				CommitTag();
				SubContext = ESubContext::None;
			}
			else if (FCString::Strcmp(Element, TEXT("member")) == 0)
			{
				if (Context == EContext::Relation && PendingMember.Ref != 0)
				{
					PendingRelation.Members.Add(PendingMember);
				}
				SubContext = ESubContext::None;
			}
			else if (FCString::Strcmp(Element, TEXT("nd")) == 0)
			{
				SubContext = ESubContext::None;
			}
			else if (FCString::Strcmp(Element, TEXT("node")) == 0
				|| FCString::Strcmp(Element, TEXT("way")) == 0
				|| FCString::Strcmp(Element, TEXT("relation")) == 0)
			{
				FlushCurrentElement();
			}

			return true;
		}

		/** Uebertraegt ein noch offenes Element in den Datensatz. */
		void FlushCurrentElement()
		{
			switch (Context)
			{
			case EContext::Node:
				// Ein Node ohne gueltige id/lat/lon ist unbrauchbar. Das kommt
				// bei abgeschnittenen Downloads am Dateiende vor.
				if (bPendingIdValid && bPendingLatValid && bPendingLonValid && PendingNode.Location.IsValid())
				{
					// Tags bewahren: auch OSM-XML kann denselben Knoten mehrfach
					// enthalten (siehe AddOrMergeNode).
					const FOSMId PendingId = PendingNode.Id;
					AddOrMergeNode(DataSet, PendingId, MoveTemp(PendingNode));
				}
				else if (bPendingIdValid || bPendingLatValid || bPendingLonValid)
				{
					++SkippedCount;
				}
				break;

			case EContext::Way:
				// Ways mit weniger als 2 Referenzen haben keine Geometrie.
				//
				// Zusammenfuehren statt ersetzen - im XML-Weg gilt dieselbe
				// Doppelung wie im JSON-Weg (siehe AddOrMergeWay). Beide Wege
				// muessen es gleich machen, sonst haengt das Ergebnis vom
				// Dateiformat ab.
				if (bPendingIdValid && PendingWay.NodeIds.Num() >= 2)
				{
					AddOrMergeWay(DataSet, PendingWay.Id, MoveTemp(PendingWay));
				}
				else if (bPendingIdValid)
				{
					++SkippedCount;
				}
				break;

			case EContext::Relation:
				if (bPendingIdValid && PendingRelation.Members.Num() > 0)
				{
					DataSet.Relations.Add(PendingRelation.Id, MoveTemp(PendingRelation));
				}
				else if (bPendingIdValid)
				{
					++SkippedCount;
				}
				break;

			case EContext::None:
			default:
				break;
			}

			Context = EContext::None;
			SubContext = ESubContext::None;
			bPendingIdValid = false;
			bPendingLatValid = false;
			bPendingLonValid = false;
		}

	private:
		void CommitTag()
		{
			if (PendingTagKey.IsNone())
			{
				return;
			}

			switch (Context)
			{
			case EContext::Node:		PendingNode.Tags.Add(PendingTagKey, PendingTagValue); break;
			case EContext::Way:			PendingWay.Tags.Add(PendingTagKey, PendingTagValue); break;
			case EContext::Relation:	PendingRelation.Tags.Add(PendingTagKey, PendingTagValue); break;
			default: break;
			}

			PendingTagKey = NAME_None;
			PendingTagValue.Reset();
		}

		enum class EContext : uint8 { None, Node, Way, Relation };
		enum class ESubContext : uint8 { None, Tag, NodeRef, Member };

		FOSMDataSet& DataSet;

		EContext Context = EContext::None;
		ESubContext SubContext = ESubContext::None;

		FOSMNode PendingNode;
		FOSMWay PendingWay;
		FOSMRelation PendingRelation;
		FOSMRelationMember PendingMember;

		FName PendingTagKey;
		FString PendingTagValue;

		bool bPendingIdValid = false;
		bool bPendingLatValid = false;
		bool bPendingLonValid = false;

		int32 SkippedCount = 0;
	};

	/** Liest Tags aus einem Overpass-JSON-Element in eine TMap. */
	void ReadJsonTags(const TSharedPtr<FJsonObject>& Element, TMap<FName, FString>& OutTags)
	{
		const TSharedPtr<FJsonObject>* TagsObject = nullptr;
		if (!Element->TryGetObjectField(TEXT("tags"), TagsObject) || !TagsObject || !TagsObject->IsValid())
		{
			return;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*TagsObject)->Values)
		{
			if (!Pair.Value.IsValid())
			{
				continue;
			}

			FString Value;
			if (Pair.Value->TryGetString(Value))
			{
				OutTags.Add(FName(*Pair.Key), Value);
			}
		}
	}
}

FOSMParseResult UOSMDataParser::ParseFile(const FString& FilePath, FOSMDataSet& OutDataSet)
{
	FOSMParseResult Result;
	const double StartTime = FPlatformTime::Seconds();

	if (FilePath.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Leerer Dateipfad.");
		return Result;
	}

	if (!FPaths::FileExists(FilePath))
	{
		Result.ErrorMessage = FString::Printf(
			TEXT("Datei nicht gefunden: %s. OSM-Daten zuerst mit Tools/overpass_fetch.py holen."), *FilePath);
		UE_LOG(LogWbGIS, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		Result.ErrorMessage = FString::Printf(
			TEXT("Datei konnte nicht gelesen werden (Rechte oder Encoding): %s"), *FilePath);
		UE_LOG(LogWbGIS, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	if (FileContent.IsEmpty())
	{
		Result.ErrorMessage = FString::Printf(TEXT("Datei ist leer: %s"), *FilePath);
		UE_LOG(LogWbGIS, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	const FString Extension = FPaths::GetExtension(FilePath).ToLower();
	UE_LOG(LogWbGIS, Log, TEXT("Lade OSM-Daten aus %s (%.1f MB, Format '%s')."),
		*FilePath, FileContent.Len() / (1024.0 * 1024.0), *Extension);

	if (Extension == TEXT("json") || Extension == TEXT("geojson"))
	{
		Result = ParseJsonString(FileContent, OutDataSet);
	}
	else
	{
		Result = ParseXmlString(FileContent, OutDataSet);
	}

	Result.ParseDurationSeconds = FPlatformTime::Seconds() - StartTime;
	return Result;
}

FOSMParseResult UOSMDataParser::ParseXmlString(const FString& XmlContent, FOSMDataSet& OutDataSet)
{
	FOSMParseResult Result;
	const double StartTime = FPlatformTime::Seconds();

	OutDataSet.Reset();

	if (XmlContent.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Leerer XML-Inhalt.");
		return Result;
	}

	// FFastXml schreibt Null-Terminatoren direkt in den Puffer. Der Aufrufer
	// darf seinen String dadurch nicht verlieren, daher eine Arbeitskopie.
	TArray<TCHAR> WorkBuffer;
	WorkBuffer.Append(*XmlContent, XmlContent.Len() + 1);

	FOSMXmlCallback Callback(OutDataSet);

	FText ErrorText;
	int32 ErrorLine = 0;

	const bool bParsed = FFastXml::ParseXmlFile(
		&Callback,
		/*XmlFilePath=*/nullptr,
		WorkBuffer.GetData(),
		/*FeedbackContext=*/nullptr,
		/*bShowSlowTaskDialog=*/false,
		/*bShowCancelButton=*/false,
		ErrorText,
		ErrorLine);

	// Ein am Dateiende abgeschnittenes Element bleibt sonst offen und geht verloren.
	Callback.FlushCurrentElement();

	if (!bParsed)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = ErrorText.IsEmpty()
			? TEXT("Unbekannter XML-Parserfehler.")
			: ErrorText.ToString();
		Result.ErrorLineNumber = ErrorLine;
		UE_LOG(LogWbGIS, Error, TEXT("OSM-XML-Parserfehler in Zeile %d: %s"), ErrorLine, *Result.ErrorMessage);
		return Result;
	}

	OutDataSet.RecomputeBounds();

	Result.bSuccess = true;
	Result.NodeCount = OutDataSet.Nodes.Num();
	Result.WayCount = OutDataSet.Ways.Num();
	Result.RelationCount = OutDataSet.Relations.Num();
	Result.SkippedElementCount = Callback.GetSkippedCount();
	Result.ParseDurationSeconds = FPlatformTime::Seconds() - StartTime;

	if (Result.NodeCount == 0)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("XML war syntaktisch gueltig, enthielt aber keine Nodes. ")
			TEXT("Vermutlich eine Overpass-Fehlerseite oder eine leere Bounding-Box.");
		UE_LOG(LogWbGIS, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	UE_LOG(LogWbGIS, Log, TEXT("OSM-XML geparst: %s"), *Result.ToString());
	return Result;
}

FOSMParseResult UOSMDataParser::ParseJsonString(const FString& JsonContent, FOSMDataSet& OutDataSet)
{
	FOSMParseResult Result;
	const double StartTime = FPlatformTime::Seconds();

	OutDataSet.Reset();

	if (JsonContent.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Leerer JSON-Inhalt.");
		return Result;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonContent);

	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Result.ErrorMessage = TEXT("JSON konnte nicht deserialisiert werden. ")
			TEXT("Bei Overpass-Antworten ist das meist eine HTML-Fehlerseite (Rate-Limit, HTTP 429).");
		UE_LOG(LogWbGIS, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
	if (!Root->TryGetArrayField(TEXT("elements"), Elements) || !Elements)
	{
		Result.ErrorMessage = TEXT("JSON enthaelt kein 'elements'-Array - kein gueltiges Overpass-Ergebnis.");
		UE_LOG(LogWbGIS, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	OutDataSet.Nodes.Reserve(Elements->Num());

	for (const TSharedPtr<FJsonValue>& ElementValue : *Elements)
	{
		if (!ElementValue.IsValid())
		{
			++Result.SkippedElementCount;
			continue;
		}

		const TSharedPtr<FJsonObject>* ElementObjectPtr = nullptr;
		if (!ElementValue->TryGetObject(ElementObjectPtr) || !ElementObjectPtr || !ElementObjectPtr->IsValid())
		{
			++Result.SkippedElementCount;
			continue;
		}

		const TSharedPtr<FJsonObject>& Element = *ElementObjectPtr;

		FString ElementType;
		if (!Element->TryGetStringField(TEXT("type"), ElementType))
		{
			++Result.SkippedElementCount;
			continue;
		}

		// JSON-Zahlen sind double. OSM-IDs liegen bei ~1.2e10 und sind damit
		// in double exakt darstellbar (< 2^53), der Cast ist also verlustfrei.
		double IdAsDouble = 0.0;
		if (!Element->TryGetNumberField(TEXT("id"), IdAsDouble))
		{
			++Result.SkippedElementCount;
			continue;
		}
		const FOSMId Id = static_cast<FOSMId>(IdAsDouble);

		if (ElementType == TEXT("node"))
		{
			double Lat = 0.0;
			double Lon = 0.0;
			if (!Element->TryGetNumberField(TEXT("lat"), Lat) || !Element->TryGetNumberField(TEXT("lon"), Lon))
			{
				++Result.SkippedElementCount;
				continue;
			}

			FOSMNode Node(Id, Lon, Lat);
			if (!Node.Location.IsValid())
			{
				++Result.SkippedElementCount;
				continue;
			}

			ReadJsonTags(Element, Node.Tags);
			AddOrMergeNode(OutDataSet, Id, MoveTemp(Node));
		}
		else if (ElementType == TEXT("way"))
		{
			FOSMWay Way;
			Way.Id = Id;

			const TArray<TSharedPtr<FJsonValue>>* NodeRefs = nullptr;
			if (Element->TryGetArrayField(TEXT("nodes"), NodeRefs) && NodeRefs)
			{
				Way.NodeIds.Reserve(NodeRefs->Num());
				for (const TSharedPtr<FJsonValue>& RefValue : *NodeRefs)
				{
					double RefAsDouble = 0.0;
					if (RefValue.IsValid() && RefValue->TryGetNumber(RefAsDouble))
					{
						Way.NodeIds.Add(static_cast<FOSMId>(RefAsDouble));
					}
				}
			}

			if (Way.NodeIds.Num() < 2)
			{
				++Result.SkippedElementCount;
				continue;
			}

			ReadJsonTags(Element, Way.Tags);
			AddOrMergeWay(OutDataSet, Id, MoveTemp(Way));
		}
		else if (ElementType == TEXT("relation"))
		{
			FOSMRelation Relation;
			Relation.Id = Id;

			const TArray<TSharedPtr<FJsonValue>>* Members = nullptr;
			if (Element->TryGetArrayField(TEXT("members"), Members) && Members)
			{
				Relation.Members.Reserve(Members->Num());
				for (const TSharedPtr<FJsonValue>& MemberValue : *Members)
				{
					const TSharedPtr<FJsonObject>* MemberObjectPtr = nullptr;
					if (!MemberValue.IsValid() || !MemberValue->TryGetObject(MemberObjectPtr)
						|| !MemberObjectPtr || !MemberObjectPtr->IsValid())
					{
						continue;
					}

					const TSharedPtr<FJsonObject>& MemberObject = *MemberObjectPtr;

					FOSMRelationMember Member;

					double RefAsDouble = 0.0;
					if (!MemberObject->TryGetNumberField(TEXT("ref"), RefAsDouble))
					{
						continue;
					}
					Member.Ref = static_cast<FOSMId>(RefAsDouble);

					FString MemberType;
					MemberObject->TryGetStringField(TEXT("type"), MemberType);
					if (MemberType == TEXT("node"))
					{
						Member.Type = EOSMMemberType::Node;
					}
					else if (MemberType == TEXT("relation"))
					{
						Member.Type = EOSMMemberType::Relation;
					}
					else
					{
						Member.Type = EOSMMemberType::Way;
					}

					FString Role;
					if (MemberObject->TryGetStringField(TEXT("role"), Role))
					{
						Member.Role = FName(*Role);
					}

					Relation.Members.Add(Member);
				}
			}

			if (Relation.Members.Num() == 0)
			{
				++Result.SkippedElementCount;
				continue;
			}

			ReadJsonTags(Element, Relation.Tags);
			OutDataSet.Relations.Add(Id, MoveTemp(Relation));
		}
		else
		{
			// "area" und "count" liefert Overpass bei bestimmten Abfragen mit;
			// beides ist fuer die Geometrieerzeugung irrelevant.
			++Result.SkippedElementCount;
		}
	}

	OutDataSet.RecomputeBounds();

	Result.bSuccess = OutDataSet.Nodes.Num() > 0;
	Result.NodeCount = OutDataSet.Nodes.Num();
	Result.WayCount = OutDataSet.Ways.Num();
	Result.RelationCount = OutDataSet.Relations.Num();
	Result.ParseDurationSeconds = FPlatformTime::Seconds() - StartTime;

	if (!Result.bSuccess)
	{
		Result.ErrorMessage = TEXT("Overpass-JSON enthielt keine Nodes.");
		UE_LOG(LogWbGIS, Error, TEXT("%s"), *Result.ErrorMessage);
	}
	else
	{
		UE_LOG(LogWbGIS, Log, TEXT("Overpass-JSON geparst: %s"), *Result.ToString());
	}

	return Result;
}

FString UOSMDataParser::BuildOverpassQuery(const FGeoBounds& Bounds, int32 TimeoutSeconds)
{
	// Overpass-QL. Wichtige Details:
	//  - [out:json] ist kompakter und schneller zu parsen als XML.
	//  - Die BBox wird global im Settings-Block gesetzt, damit sie fuer alle
	//    Statements gilt und nicht je Statement wiederholt werden muss.
	//  - "out body" + ">;" + "out skel qt" ist das Standardidiom, um
	//    referenzierte Nodes von Ways mitzuliefern. Ohne diesen Recurse-Schritt
	//    fehlen sonst alle Geometrien.
	//  - "qt" sortiert nach Quadtile und beschleunigt die Serverantwort.
	//  - KEINE relation[type=route]-Abfrage: der Recurse ">;" zoege die gesamten
	//    Mitglieds-Geometrien der Linien in die Antwort (eine Bus-/Zuglinie, die
	//    Wiesbaden beruehrt, bringt ihre komplette Strecke bis Barcelona mit) -
	//    das sprengt die BBox und den Terrain-Crop. Route-Relations werden von
	//    der Pipeline nicht konsumiert (nur type=restriction).
	const FString BBox = Bounds.ToOverpassBBox();

	return FString::Printf(TEXT(
		"[out:json][timeout:%d][bbox:%s];\n"
		"(\n"
		"  way[\"highway\"];\n"
		"  way[\"building\"];\n"
		"  way[\"building:part\"];\n"
		"  way[\"landuse\"];\n"
		"  way[\"natural\"];\n"
		"  way[\"waterway\"];\n"
		"  way[\"barrier\"];\n"
		"  way[\"railway\"];\n"
		"  way[\"bridge\"];\n"
		"  way[\"tunnel\"];\n"
		"  way[\"leisure\"];\n"
		"  way[\"amenity\"];\n"
		"  node[\"highway\"~\"traffic_signals|crossing|stop|give_way|bus_stop|street_lamp|turning_circle\"];\n"
		"  node[\"amenity\"~\"fuel|parking|restaurant|cafe|bank|pharmacy|hospital|police|fire_station\"];\n"
		"  node[\"shop\"];\n"
		"  node[\"public_transport\"];\n"
		"  node[\"natural\"=\"tree\"];\n"
		"  relation[\"type\"=\"multipolygon\"][\"building\"];\n"
		"  relation[\"type\"=\"restriction\"];\n"
		");\n"
		"out body;\n"
		">;\n"
		"out skel qt;\n"),
		FMath::Clamp(TimeoutSeconds, 30, 1800),
		*BBox);
}

void UOSMDataParser::FetchFromOverpassAsync(
	const FGeoBounds& Bounds,
	FOnOverpassQueryComplete OnComplete,
	const FString& OverpassEndpoint)
{
	if (!Bounds.IsValid())
	{
		FOSMParseResult Failure;
		Failure.ErrorMessage = TEXT("Ungueltige Bounding-Box fuer Overpass-Abfrage.");
		UE_LOG(LogWbGIS, Error, TEXT("%s"), *Failure.ErrorMessage);
		OnComplete.ExecuteIfBound(Failure, nullptr);
		return;
	}

	if (PendingRequest.IsValid())
	{
		FOSMParseResult Failure;
		Failure.ErrorMessage = TEXT("Es laeuft bereits eine Overpass-Abfrage. ")
			TEXT("Parallele Abfragen an die oeffentliche API loesen ein Rate-Limit aus.");
		UE_LOG(LogWbGIS, Warning, TEXT("%s"), *Failure.ErrorMessage);
		OnComplete.ExecuteIfBound(Failure, nullptr);
		return;
	}

	const FString Query = BuildOverpassQuery(Bounds);

	UE_LOG(LogWbGIS, Log, TEXT("Overpass-Abfrage an %s fuer BBox %s (%d Zeichen Query)."),
		*OverpassEndpoint, *Bounds.ToOverpassBBox(), Query.Len());

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(OverpassEndpoint);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/x-www-form-urlencoded"));

	// Die Overpass-Nutzungsbedingungen verlangen einen identifizierbaren
	// User-Agent. Anonyme Massenabfragen werden geblockt.
	Request->SetHeader(TEXT("User-Agent"), TEXT("WiesbadenReal/1.0 (UE5 GIS-Pipeline)"));

	Request->SetContentAsString(FString::Printf(TEXT("data=%s"), *FGenericPlatformHttp::UrlEncode(Query)));

	// Grosse Stadtabfragen laufen serverseitig mehrere Minuten.
	Request->SetTimeout(900.0f);

	TWeakObjectPtr<UOSMDataParser> WeakThis(this);

	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, OnComplete](FHttpRequestPtr /*Req*/, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			UOSMDataParser* Self = WeakThis.Get();
			if (!Self)
			{
				// Der Parser wurde waehrend des Downloads garbage-collected -
				// etwa bei einem Level-Wechsel. Kein Fehler, nur Abbruch.
				UE_LOG(LogWbGIS, Verbose, TEXT("Overpass-Antwort verworfen: Parser existiert nicht mehr."));
				return;
			}

			Self->PendingRequest.Reset();

			FOSMParseResult Result;

			if (!bConnectedSuccessfully || !Response.IsValid())
			{
				Result.ErrorMessage = TEXT("Overpass-Abfrage fehlgeschlagen: keine Verbindung oder Timeout.");
				UE_LOG(LogWbGIS, Error, TEXT("%s"), *Result.ErrorMessage);
				OnComplete.ExecuteIfBound(Result, nullptr);
				return;
			}

			const int32 ResponseCode = Response->GetResponseCode();
			if (ResponseCode != 200)
			{
				Result.ErrorMessage = FString::Printf(
					TEXT("Overpass antwortete mit HTTP %d. 429 = Rate-Limit (spaeter erneut versuchen), ")
					TEXT("504 = serverseitiger Timeout (Bounding-Box verkleinern)."),
					ResponseCode);
				UE_LOG(LogWbGIS, Error, TEXT("%s"), *Result.ErrorMessage);
				OnComplete.ExecuteIfBound(Result, nullptr);
				return;
			}

			const FString Content = Response->GetContentAsString();
			UE_LOG(LogWbGIS, Log, TEXT("Overpass-Antwort empfangen: %.1f MB."), Content.Len() / (1024.0 * 1024.0));

			TSharedPtr<FOSMDataSet> DataSet = MakeShared<FOSMDataSet>();
			Result = Self->ParseJsonString(Content, *DataSet);

			OnComplete.ExecuteIfBound(Result, Result.bSuccess ? DataSet : nullptr);
		});

	if (!Request->ProcessRequest())
	{
		FOSMParseResult Failure;
		Failure.ErrorMessage = TEXT("HTTP-Anfrage konnte nicht gestartet werden.");
		UE_LOG(LogWbGIS, Error, TEXT("%s"), *Failure.ErrorMessage);
		OnComplete.ExecuteIfBound(Failure, nullptr);
		return;
	}

	PendingRequest = Request;
}

void UOSMDataParser::CancelPendingRequest()
{
	if (PendingRequest.IsValid())
	{
		UE_LOG(LogWbGIS, Log, TEXT("Overpass-Abfrage abgebrochen."));
		PendingRequest->CancelRequest();
		PendingRequest.Reset();
	}
}
