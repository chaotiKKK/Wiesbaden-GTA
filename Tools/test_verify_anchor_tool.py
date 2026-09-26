r"""Selbsttest: das Anker-Werkzeug darf nicht still bestehen.

Geprueft werden die drei Fehler, die dieses Werkzeug ueber Wochenlang
tarnten (Beleg: Saved/Diagnose/anchor_verify.txt mit einem wertlosen
Alkis24-Stand, der als Messung galt):

  1. der dokumentierte Aufruf nennt einen VOLLSTAENDIGEN Skriptpfad - ein
     relativer Pfad wird von der Engine gegen Engine/Binaries/Win64
     aufgeloest und liefert nur "Could not load Python file",
  2. das Ergebnis wird VOR dem Lauf geloescht, damit ein abgebrochener Lauf
     nicht das Ergebnis des Vortags als Messung stehen laesst,
  3. ein Lauf ohne Ergebnisdatei (oder mit umgeschriebenem Kartenpfad) wird
     als FEHLER gemeldet, nicht als Erfolg.

Der Test liest die Dateien als Text. Das ist hier richtig: geprueft wird der
Vertrag zwischen drei Beteiligten (Batchdatei, Python-Skript, Engine), und
der einzige Weg, ihn zu brechen, ist eine Aenderung an genau diesem Text.
Das Skript selbst laesst sich ohne die Engine nicht ausfuehren - es bindet
unreal.

    python -m unittest discover -s Tools -p "test_verify_anchor_tool.py"
"""

import re
import unittest
from pathlib import Path

WERKZEUGE = Path(__file__).resolve().parent
CMD = WERKZEUGE / "verify_anchor.cmd"
SKRIPT = WERKZEUGE / "verify_anchor_state.py"


def lies(pfad):
    return pfad.read_text(encoding="utf-8", errors="replace")


def aufruf_zeilen(text):
    """Nur die Zeilen, die etwas TATSACHLICH aufrufen.

    Batch kommentiert mit rem, Python mit dem Docstring. Beide erwaehnen gern
    die kaputte Form, um vor ihr zu warnen - wer das nicht unterscheidet,
    schlagt sich selbst mit dem eigenen Kommentar fehl (genau der erste
    Versuch dieses Tests: 'rem 1. ABSOLUTER SKRIPTPFAD. "-script=Tools/..."
    gelesen' ist eine Warnung, kein Aufruf).
    """
    zeilen = []
    for zeile in text.splitlines():
        if zeile.strip().lower().startswith("rem "):
            continue
        if "-run=pythonscript" in zeile or "-script=" in zeile:
            zeilen.append(zeile)
    return zeilen


def loese_variablen(text):
    """Set-Variablen eines Batch-Skripts aufloesen, wie die Engine es tut.

    "%SCRIPT%" ist kein relativer Pfad, sondern "%PROJ%\\Tools\\..." - und
    "%PROJ%" ist "%~dp0..", also der absolute Ordner ueber dem Werkzeug. Wer
    nur auf den Text schaut, haelt eine absolut gesetzte Variable fuer einen
    relativen Pfad und flaggt jeden korrekten Aufruf (der erste Versuch
    dieses Tests tat das).

    %~dp0 wird durch den echten Werkzeugordner ersetzt, damit am Ende ein
    Pfad zum Pruefen uebrig bleibt, den man auf der Platte nachschlagen kann.
    """
    werte = {"~dp0": str(WERKZEUGE), "ERRORLEVEL": "0", "WB": "0"}
    for name, wert in re.findall(r'^\s*set\s+"([^"]+)=(.*)"\s*$',
                                 text, re.MULTILINE | re.IGNORECASE):
        werte[name] = wert
    for _ in range(6):
        vorher = text
        # "~dp0" ist ein Batch-Sonderparameter OHNE abschliessendes % - als
        # eigene Ersetzung, sonst greift das Schema "%NAME%" hier nie.
        text = text.replace("%~dp0", str(WERKZEUGE))
        for name, wert in werte.items():
            text = text.replace("%%%s%%" % name, wert)
        if text == vorher:
            break
    return text


class DokumentierterAufrufTest(unittest.TestCase):
    """Vertrag 1: was der Aufruf zeigt, muss laufen."""

    def setUp(self):
        self.cmd = lies(CMD)
        self.skrpt = lies(SKRIPT)
        self.cmd_aufgeloest = loese_variablen(self.cmd)

    def test_zeiger_auf_absolute_skriptpfade(self):
        # Jeder -script=Parameter muss AUFGELOEST absolut sein - nach dem
        # Ersetzen der %VARIABLEN%, denn so uebergibt ihn die Engine.
        for quelle, text in (("verify_anchor.cmd", self.cmd_aufgeloest),
                             ("verify_anchor_state.py", self.skrpt)):
            aufrufe = re.findall(r"-script=\"?([^\"\r\n]+)\"?",
                                 "\n".join(aufruf_zeilen(text)))
            self.assertTrue(aufrufe, "%s nennt ueberhaupt keinen Aufruf" % quelle)
            for aufruf in aufrufe:
                self.assertRegex(
                    aufruf, r"^[A-Za-z]:[\\/]",
                    "%s: -script=%s ist auch aufgeloest relativ. Die Engine "
                    "loest das gegen Engine\\Binaries\\Win64 auf und meldet "
                    "nur 'Could not load Python file' - der Lauf sieht aus, "
                    "als waere nichts passiert." % (quelle, aufruf))

    def test_kein_verbotener_relativer_aufruf_in_der_doku(self):
        """Die Form, die den Lauf gekillt hat, darf nicht mehr dastehen.

        Erlaubt ist die Nennung als Warnung ("NICHT so"), verbrochen ist
        dieselbe Form als Anleitung.
        """
        verboten = re.compile(r"^.*-script=Tools[/\\]verify_anchor_state\.py.*$")
        verdacht = [zeile for zeile in aufruf_zeilen(self.skrpt)
                    if verboten.match(zeile)
                    and "NICHT" not in zeile.upper()
                    and "ABSOLUT" not in zeile.upper()]
        self.assertEqual([], verdacht,
                         "Der dokumentierte Aufruf mit relativem Pfad ist "
                         "zurueckgekehrt: %r" % verdacht)

    def test_skriptpfad_im_cmd_ist_durchgaengig(self):
        """Der AUFGELOESTE Pfad muss auf die Datei zeigen, die es gibt.

        Ein Tippfehler im Pfad ist derselbe Fehler wie ein relativer Pfad -
        die Engine meldet ihn nur freundlicher ("Could not load Python
        file"), und der Lauf sieht vollstaendig aus. Diese Probe ist
        deshalb billig und stark: sie schlaegt bei jedem Tippfehler an.
        """
        aufruf = re.search(r'-script="?([^"\r\n]+)"?', self.cmd_aufgeloest)
        self.assertIsNotNone(aufruf, "verify_anchor.cmd ruft kein Skript auf")
        pfad = aufruf.group(1)
        self.assertTrue(
            Path(pfad).exists(),
            "-script zeigt auf %r, aber dort liegt keine Datei" % pfad)


class ErgebnisdateiTest(unittest.TestCase):
    """Vertrag 2: die Datei gehoert zu diesem Lauf oder zu keinem."""

    def setUp(self):
        self.cmd = lies(CMD)
        self.skrpt = lies(SKRIPT)

    def test_cmd_loescht_das_alte_ergebnis(self):
        self.assertRegex(
            self.cmd, r"del\s+/q\s+\"?%ERGEBNIS%\"?",
            "verify_anchor.cmd loescht das alte Ergebnis nicht. Bricht der "
            "Lauf ab, liegt der Vortag als Messung da - das ist genau der "
            "Fehler, der den Alkis24-Stand wochenlang echt aussehen liess.")

    def test_reihenfolge_loeschen_vor_lauf(self):
        # Gesucht ist der AUFRUF, nicht die Pfaddefinition in set "UE=...".
        lauf = self.cmd.index("-run=pythonscript")
        loeschen = self.cmd.index("del /q")
        self.assertLess(loeschen, lauf,
                        "Erst wird gelaufen und dann geloescht - dann "
                        "loescht der Lauf sein eigenes Ergebnis.")

    def test_skript_loescht_selbst(self):
        """Auch der Direktaufruf soll kein Ergebnis stehen lassen."""
        self.assertIn("os.remove(RESULT_FILE)", self.skrpt)

    def test_skrpt_schreibt_erst_nach_der_messung(self):
        """Das Schreiben kommt am Ende, nicht am Anfang."""
        schreiben = self.skrpt.index('with open(RESULT_FILE, "w"')
        messung = self.skrpt.index("LEERE Komponenten")
        self.assertLess(messung, schreiben)


class EchterFehlschlagTest(unittest.TestCase):
    """Vertrag 3: Fehlschlag muss als Fehlschlag herauskommen."""

    def setUp(self):
        self.cmd = lies(CMD)
        self.skrpt = lies(SKRIPT)

    def test_exit_code_der_engine_wird_geprueft(self):
        self.assertRegex(
            self.cmd, r'if\s+not\s+"%WB%"\s*==\s*"0"',
            "Der Exit-Code der Engine wird nirgends geprueft. Sie endet bei "
            "Skriptfehlern mit 127 - genau so sieht ein gescheiterter Lauf "
            "aus, wenn niemand hinsieht.")

    def test_jeder_fehlerfall_hat_eigenen_code(self):
        """Jede Fehlermarke endet ungleich null - sonst ist 0 nicht lesbar."""
        for marke in (":kein_skript", ":engine_fehler", ":kein_ergebnis",
                      ":falsche_karte", ":keine_messung"):
            self.assertRegex(
                self.cmd, re.escape(marke) + r"\b[\s\S]*?exit /b [1-9]",
                "Die Fehlermarke %s gibt 0 zurueck - Fehler sind dann nicht "
                "von Erfolg unterscheidbar." % marke)

    def test_ergebnisdatei_wird_vor_aller_anderen_geprueft(self):
        """Die Datei entscheidet, nicht der Exit-Code.

        Gemessen: die Engine stuerzt beim Herunterfahren ab (Megascans-
        Plugin, dllmain_crt_process_detach), NACHDEM die Messung geschrieben
        ist. Wer den Exit-Code zum Massstab macht, meldet eine gute Messung
        als Fehlschlag. Umgekehrt darf ohne Datei nie Erfolg gemeldet werden.
        """
        self.assertIn('if exist "%ERGEBNIS%"', self.cmd)
        # Der Sprung auf die Pruefung steht VOR der Engine-Fehlerbehandlung.
        datei = self.cmd.index('if exist "%ERGEBNIS%"')
        enginefehler = self.cmd.index(":engine_fehler")
        self.assertLess(datei, enginefehler,
                        "Die Ergebnisdatei wird erst nach dem Exit-Code "
                        "beurteilt - damit schlaegt ein Shutdown-Absturz eine "
                        "gueltige Messung.")

    def test_abweichender_exit_code_wird_laut_gemeldet(self):
        self.assertIn("WARNUNG", self.cmd,
                      "Ein gueltiges Ergebnis mit ungewoehnlichem Exit-Code "
                      "muss beide Tatsachen nennen - nicht die eine "
                      "verschweigen.")

    def test_fehlende_ergebnisdatei_ist_ein_fehler(self):
        """Ohne Ergebnisdatei gibt es keinen Erfolg - und einen eigenen Code.

        Geprueft wird die Sprungmarken-Bauform: der Lauf verzweigt, und die
        Marke fuer den Fall "nichts geschrieben" endet ungleich null.
        """
        self.assertIn("goto :kein_ergebnis", self.cmd,
                      "Es gibt keinen Weg, auf dem ein Lauf ohne "
                      "Ergebnisdatei als Erfolg endet.")
        self.assertRegex(
            self.cmd, r":kein_ergebnis\b[\s\S]*?exit /b [1-9]",
            "Die Marke fuer die fehlende Ergebnisdatei gibt 0 zurueck. Genau "
            "das war der stille Durchfall: kein Skriptlauf, trotzdem "
            "'fertig'.")

    def test_kopfzeile_der_messung_wird_geprueft(self):
        self.assertIn("Karte geladen: /Game/Maps/", self.cmd,
                      "Das Skript prueft den Kartenpfad nicht. Ein durch "
                      "Git Bash umgeschriebenes /Game/... ergibt eine leere "
                      "Ebene und eine Zahl, die gemessen aussieht.")

    def test_messzeile_wird_geprueft(self):
        self.assertIn("LEERE Komponenten:", self.cmd,
                      "Ohne diese Zeile ist die Datei kein Messergebnis.")

    def test_erfolgsfall_gibt_null_zurueck(self):
        ausgaenge = re.findall(r"exit /b (\d+)", self.cmd)
        self.assertIn("0", ausgaenge, "Der Erfolgsfall gibt nichts zurueck.")
        # Fehler brauchen eigene Codes, sonst ist exit 0 nicht unterscheidbar.
        self.assertGreaterEqual(len(set(ausgaenge)), 3,
                                "Es gibt weniger als drei verschiedene "
                                "Exit-Codes - Fehlerfaelle sind nicht "
                                "unterscheidbar: %r" % ausgaenge)

    def test_logname_ist_je_lauf_eindeutig(self):
        """Kein fester Logname.

        Ein haengender Lauf haelt sein Log exklusiv. Mit festem Namen
        scheitert dann die Umleitung des naechsten Laufs und man liest
        "Der Prozess kann nicht auf die Datei zugreifen" - und schliesst
        daraus, die Messung sei gescheitert. Dieselbe Fehlerklasse, die
        dieses Werkzeug abstellen soll, sass hier im Werkzeug selbst.
        """
        self.assertIn("verify_anchor_%STAMP%.log", self.cmd)
        self.assertNotRegex(
            self.cmd, r'SET\s+"LOG=[^"]*verify_anchor\.log"',
            "Der Logname ist wieder fest - ein haengender Lauf blockiert dann "
            "den naechsten.")

    def test_kein_klammerblock_mit_text_in_klammern(self):
        """Bauform: keine "if ( ... )"-Bloecke.

        In einem Klammerblock beendet das ERSTE ungeschuetzte ")" den Block,
        auch eines, das in einem Text steht. Die erste Fassung dieses
        Skripts trug "echo ... errors." am Zeilenende und riss damit den
        ganzen Block auf ("'.' kann syntaktisch ... nicht verarbeitet
        werden") - der Lauf brach ab, statt zu pruefen.
        """
        bloecke = re.findall(r"^\s*if\s+[^\r\n]*\(", self.cmd,
                             re.MULTILINE | re.IGNORECASE)
        self.assertEqual([], bloecke,
                         "Klammerbloecke sind verboten, nutze goto: %r" % bloecke)

    def test_skrpt_bricht_ab_wenn_die_karte_nicht_geladen_ist(self):
        self.assertIn("Karte nicht geladen", self.skrpt)
        # Und zwar VOR dem Schreiben der Ergebnisdatei.
        abbruch = self.skrpt.index("Karte nicht geladen")
        schreiben = self.skrpt.index('with open(RESULT_FILE, "w"')
        self.assertLess(abbruch, schreiben,
                        "Der Abbruch kommt nach dem Schreiben - dann steht "
                        "doch wieder eine Zahl in der Datei.")



class MesskarteAlsArgumentTest(unittest.TestCase):
    """Vertrag 4: eine andere Karte ohne Eingriff in Config/DefaultEngine.ini.

    Anlass: zwei Karten hintereinander messen wollen. Der Umweg ueber die ini
    hat zwei Fehler - die Standardkarte ist waehrend der Messung falsch (ein
    Abbruch laesst sie auf der fremden Karte stehen), und das Ergebnis der
    zweiten Karte ueberschreibt das der ersten. Der Kartenname gehoert darum
    als ARGUMENT her, und der Paketpfad wird in der Batch-Datei gebaut, weil
    Git Bash "/Game/..." auf der Kommandozeile umschreibt.
    """

    def setUp(self):
        self.cmd = lies(CMD)
        self.skrpt = lies(SKRIPT)

    def test_blosser_name_wird_zum_paketpfad(self):
        self.assertIn(r'set "WBMAP=/Game/Maps/WiesbadenCity_%WBMAP%"', self.cmd,
                      "Ein blosser Kartenname wird nicht zum Paketpfad "
                      "ergaenzt - das Skript wuerde stattdessen eine Ebene "
                      "namens 'Alkis31' suchen und abbrechen.")

    def test_vollstaendiger_pfad_bleibt_unveraendert(self):
        # Wer schon "/Game/..." uebergibt, darf es nicht doppelt bekommen.
        self.assertIn(r'findstr /C:"/Game/"', self.cmd,
                      "Es wird nicht geprueft, ob das Argument schon ein "
                      "Paketpfad ist - dann entstuende /Game/Maps/"
                      "WiesbadenCity_/Game/Maps/...")

    def test_wb_map_kommt_nur_aus_dem_argument(self):
        """Die Umleitung darf nicht wieder auf der Kommandozeile landen.

        WB_MAP aus Git Bash zu setzen ist der dokumentierte Absturz: der
        Pfad wird zu "C:/Program Files/Git/Game/...", load_level liefert
        False, und der Lauf misst die leere Ebene /Temp/Untitled_0.
        """
        zeilen = re.findall(r'set "WB_MAP=[^\r\n]*"', self.cmd)
        self.assertEqual(1, len(zeilen),
                         "WB_MAP wird %d mal gesetzt: %r" % (len(zeilen), zeilen))
        self.assertIn('set "WB_MAP=%WBMAP%"', zeilen[0],
                      "WB_MAP wird nicht aus dem Argument gesetzt.")

    def test_ergebnisdatei_ist_je_karte_eigenes(self):
        self.assertIn(r'set "ERGEBNIS=%PROJ%\Saved\Diagnose'
                      r'\anchor_verify%WBSUFFIX%.txt"', self.cmd,
                      "Der Ergebnisname traegt den Kartennamen nicht - eine "
                      "zweite Messung ueberschreibt die erste und der "
                      "Vergleich verliert eine Seite.")
        self.assertRegex(self.cmd, r'set "WBSUFFIX=_%%~nxf"',
                         "Der Namenszusatz kommt nicht aus dem Kartennamen.")

    def test_standardlauf_behaelt_den_bekannten_dateinamen(self):
        """Ohne Argument bleibt der Name, den die Altlaeufe geschrieben haben."""
        self.assertIn('set "WBSUFFIX="', self.cmd,
                      "Der Standardlauf schreibt nicht mehr nach "
                      "anchor_verify.txt - damit brechen die vorhandenen "
                      "Auswertungen, die auf diesen Namen zeigen.")
        self.assertIn("goto :ohne_argument", self.cmd)

    def test_doku_zeigt_den_aufruf_mit_argument(self):
        self.assertIn(r"Tools\verify_anchor.cmd Alkis31", self.cmd,
                      "Der Aufruf mit Argument steht nicht in der Doku - dann "
                      "bleibt nur der ini-Umweg.")


    def test_kein_prozentparameter_im_kommentar(self):
        """rem schuetzt nicht vor %~-Parametern.

        cmd expandiert Batch-Parameter auch in Kommentarzeilen und bricht mit
        "Die folgende Verwendung des Pfadoperators zur Ersetzung eines
        Batchparameters ist ungueltig" ab - der Lauf endet, bevor die Engine
        ueberhaupt startet. Im Kommentar gehoert deshalb %% statt %.
        """
        # "%%~" ist die korrekte Schreibweise im Kommentar (cmd gibt dann
        # "%~nxf" als Text aus) - verboten ist nur das einzelne "%~".
        zeilen = [z for z in self.cmd.splitlines()
                  if z.strip().lower().startswith("rem ")
                  and re.search(r"(?<!%)%~", z)]
        self.assertEqual(
            [], zeilen,
            "Kommentar mit einem Batch-Parameter (Prozent, Tilde) - cmd "
            "expandiert ihn und bricht den Lauf ab: " + repr(zeilen))

    def test_namenszusatz_geht_auch_an_das_skript(self):
        """Batch und Skript muessen denselben Dateinamen erwarten.

        Gemessen: die Batch-Datei suchte "anchor_verify_<Karte>.txt", das
        Skript schrieb unveraendert nach "anchor_verify.txt" - der Lauf meldete
        "keine Ergebnisdatei", obwohl 30 Sekunden lang gemessen wurde. Der
        Zusatz braucht darum einen zweiten Weg: WB_SUFFIX.
        """
        self.assertIn('set "WB_SUFFIX=%WBSUFFIX%"', self.cmd,
                      "Die Batch-Datei reicht den Namenszusatz nicht an das "
                      "Skript weiter - beide erwarten dann verschiedene Dateien.")
        self.assertIn('os.environ.get("WB_SUFFIX"', self.skrpt,
                      "Das Skript kennt den Zusatz nicht und schreibt immer "
                      "nach anchor_verify.txt.")
        # Und es filtert: der Wert darf keinen Pfad bilden.
        self.assertIn('if c.isalnum() or c == "_"', self.skrpt,
                      "Der Namenszusatz wird ungeprueft uebernommen.")

if __name__ == "__main__":
    unittest.main()
