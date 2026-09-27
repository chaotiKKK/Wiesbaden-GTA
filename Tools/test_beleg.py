r"""Selbsttest der BELEG-Regel: loeschen und danach NACHPRUEFEN.

WAS HIER EINE FEHLERKLASSE IST UND WAS SIE ANRICHTET
--------------------------------------------------
Ein Beleg ist eine Datei, aus der spaeter ein Messwert oder ein Gate-Ergebnis
gelesen wird - eine Log, ein Ergebnisprotokoll, ein Screenshot. Die Regel
lautet: sein Wegsein wird nicht geglaubt, es wird geprueft. Wer sie bricht,
bekommt kein rotes Gate, sondern ein GRUENES ohne Messung:

    Remove-Item $Log -ErrorAction SilentlyContinue     # loescht vielleicht
    ...
    $n = @(Select-String -Path $Log -Pattern "...").Count   # zaehlt die alte
    if ($n -ge 8) { "gruen" }                               # Log und sagt gruen

Zwei Loeschfehler sind in diesem Projekt NACHGEMESSEN (27.09.2026): das
Read-only-Flag am Ordner-Eintrag und der offene Handle eines haengenden
Editors (die Datei ist die lebende Saved\Logs\WiesbadenReal.log). Der
Testfall dafuer steckt deshalb in diesem Test: eine gesperrte Datei, gegen
die alles nichts kann, muss einen Abbruch und NICHT ein Weiterlaufen ergeben.

Diese Datei hat zwei Teile: die Helfer selbst (echte Dateien, echte
Fehler) und die neun Werkzeugstellen, die auf die Regel umgestellt wurden
(Quelltext, weil dort nur der Vertrag zwischen Skript und Beleg zaehlt).

    python -m unittest discover -s Tools -p "test_beleg.py"
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

WERKZEUGE = Path(__file__).resolve().parent
PROJEKT = WERKZEUGE.parent
sys.path.insert(0, str(WERKZEUGE))

import beleg  # noqa: E402  (Pfad oben gesetzt)

POWERSHELL = "powershell"
BELEG_PS1 = WERKZEUGE / "beleg.ps1"


def lies(pfad):
    return Path(pfad).read_text(encoding="utf-8", errors="replace")


# ---------------------------------------------------------------------------
#  1. Der Python-Helfer
# ---------------------------------------------------------------------------

class BelegPyTest(unittest.TestCase):
    """loesche_beleg / ist_frisch / beleg_hinweis gegen echte Dateien."""

    def setUp(self):
        self.wurzel = Path(tempfile.mkdtemp(prefix="beleg_py_"))
        self.addCleanup(shutil.rmtree, self.wurzel, ignore_errors=True)
        self.pfad = self.wurzel / "release_build.log"

    def schreibe(self, inhalt="x"):
        self.pfad.write_text(inhalt, encoding="utf-8")
        return self.pfad

    def test_loescht_eine_normale_datei(self):
        self.schreibe()
        self.assertTrue(beleg.loesche_beleg(str(self.pfad)))
        self.assertFalse(self.pfad.exists())

    def test_fehlende_datei_ist_kein_fehler(self):
        # Ein Beleg, den es nie gab, ist Wegsein - kein Anlass zum Abbruch.
        self.assertTrue(beleg.loesche_beleg(str(self.pfad)))
        self.assertTrue(beleg.loesche_beleg(""))
        self.assertTrue(beleg.loesche_beleg(None))

    def test_loescht_einen_ordner(self):
        ordner = self.wurzel / "Package_previous"
        (ordner / "Windows").mkdir(parents=True)
        (ordner / "Windows" / "WiesbadenReal.exe").write_bytes(b"MZ")
        self.assertTrue(beleg.loesche_beleg(str(ordner), ordner=True))
        self.assertFalse(ordner.exists())

    @unittest.skipUnless(os.name == "nt", "Read-only-Flag ist ein Windows-Thema")
    def test_raeumt_das_read_only_flag_vom_ordner_eintrag(self):
        # DER nachgemessene Fall: das Flag steckt am Ordner-Eintrag, nicht am
        # Inhalt. Ein Remove-Item -Force scheitert daran, os.remove auch - der
        # Helfer muss es aufloesen, sonst bricht der Lauf ab, obwohl nichts
        # blockiert ist.
        self.schreibe()
        os.chmod(str(self.pfad), 0o444)
        self.assertTrue(beleg.loesche_beleg(str(self.pfad)))
        self.assertFalse(self.pfad.exists())

    def test_wirft_statt_zu_quieten_wenn_gar_nicht_geht(self):
        # Der Stillfall, der die ganze Regel traegt: die Datei laesst sich
        # nicht loeschen. Ein Skript, das danach weiterlaeuft, liest ihre
        # ZAHLEN als Ergebnis des neuen Laufs. Also: BelegFehler.
        self.schreibe()
        with mock.patch.object(beleg.os, "remove", lambda *a, **k: None), \
                mock.patch.object(beleg.os, "chmod", lambda *a, **k: None):
            with self.assertRaises(beleg.BelegFehler) as gef:
                beleg.loesche_beleg(str(self.pfad))
        # Die Meldung muss den Weg zur Loesung nennen, nicht nur den Zustand.
        self.assertIn(self.pfad.name, str(gef.exception))

    def test_belegfehler_ist_ein_eigener_typ(self):
        # Die Aufrufer unterscheiden diesen Abbruch von einem Lauf-Fehler.
        self.assertTrue(issubclass(beleg.BelegFehler, RuntimeError))
        self.assertIsNot(beleg.BelegFehler, OSError)

    # -- Frische ------------------------------------------------------------

    def test_ist_frisch_bei_fehlender_datei_ist_false(self):
        # Der Fall, in dem ein Lauf gar nichts geschrieben hat. "Fehlt" darf
        # nicht wie "unbekannt" durchrutschen.
        self.assertFalse(beleg.ist_frisch(str(self.pfad), time.time()))
        self.assertFalse(beleg.ist_frisch("", time.time()))
        self.assertFalse(beleg.ist_frisch(None, None))

    def test_ist_frisch_unterscheidet_alt_von_neu(self):
        # "Alt" heisst: VOR dem Sitzungsbeginn geschrieben. Also muss `ab`
        # nach dem Zeitstempel liegen, nicht davor.
        self.schreibe()
        alt = time.time() - 3600
        os.utime(str(self.pfad), (alt, alt))
        self.assertFalse(beleg.ist_frisch(str(self.pfad), time.time()))
        self.assertTrue(beleg.ist_frisch(str(self.pfad), alt - 60))

    def test_ist_frisch_nimmt_datetime_und_floats(self):
        import datetime
        self.schreibe()
        jetzt = datetime.datetime.now()
        geschrieben = jetzt.timestamp() - 5
        os.utime(str(self.pfad), (geschrieben, geschrieben))
        self.assertTrue(beleg.ist_frisch(str(self.pfad), jetzt - datetime.timedelta(seconds=60)))
        self.assertTrue(beleg.ist_frisch(str(self.pfad), geschrieben - 60))
        self.assertFalse(beleg.ist_frisch(str(self.pfad), jetzt))

    def test_ist_frisch_ohne_zeitpunkt_prueft_nur_die_existenz(self):
        self.schreibe()
        self.assertTrue(beleg.ist_frisch(str(self.pfad), None))

    def test_toleranz_ist_klein_genug_um_alt_als_alt_zu_sehen(self):
        # Die Toleranz existiert wegen sekundengenauer Dateizeitstempel. Sie
        # darf nicht so gross werden, dass eine alte Log durchrutscht.
        self.assertLessEqual(beleg.TOLERANZ_S, 5.0)

    # -- Hinweis ------------------------------------------------------------

    def test_hinweis_bei_fehlender_datei_sagt_es(self):
        text = beleg.beleg_hinweis(str(self.pfad))
        self.assertIn("fehlt", text)

    def test_hinweis_nennt_den_alter(self):
        import datetime
        self.schreibe()
        alt = time.time() - 7200
        os.utime(str(self.pfad), (alt, alt))
        text = beleg.beleg_hinweis(str(self.pfad))
        self.assertIn("AELTER", text)
        self.assertIn(datetime.datetime.fromtimestamp(alt).strftime("%Y-%m-%d"), text)


# ---------------------------------------------------------------------------
#  2. Der PowerShell-Helfer (echter Aufruf, echte Datei)
# ---------------------------------------------------------------------------

class BelegPs1Test(unittest.TestCase):
    """Remove-Beleg muss im Kindprozess Exit 3 liefern, wenn es nicht geht.

    `exit 3` beendet den Kindprozess - genau deshalb laeuft der Aufruf in
    einem eigenen powershell.exe statt im Testprozess. Sonst wuerde der Test
    selbst abbrechen.
    """

    def setUp(self):
        self.wurzel = Path(tempfile.mkdtemp(prefix="beleg_ps1_"))
        self.addCleanup(shutil.rmtree, self.wurzel, ignore_errors=True)

    def kind(self, rumpf):
        skript = self.wurzel / "kind.ps1"
        skript.write_text(
            '. "%s"\n%s\n' % (str(BELEG_PS1).replace("\\", "\\\\"), rumpf),
            encoding="utf-8")
        lauf = subprocess.run(
            [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass",
             "-File", str(skript)],
            capture_output=True, text=True, timeout=180)
        return lauf

    def test_loescht_eine_normale_datei(self):
        ziel = self.wurzel / "wb_flight_a.log"
        ziel.write_text("alt", encoding="utf-8")
        lauf = self.kind('Remove-Beleg "%s"\nWrite-Host "WEG"'
                         % str(ziel).replace("\\", "\\\\"))
        self.assertEqual(lauf.returncode, 0, lauf.stdout + lauf.stderr)
        self.assertFalse(ziel.exists())
        self.assertIn("WEG", lauf.stdout)

    def test_raeumt_read_only_und_beendet_normale_laeufe_nicht(self):
        ziel = self.wurzel / "release_build.log"
        ziel.write_text("alt", encoding="utf-8")
        os.chmod(str(ziel), 0o444)
        lauf = self.kind('Remove-Beleg "%s"\nWrite-Host "WEG"'
                         % str(ziel).replace("\\", "\\\\"))
        self.assertEqual(lauf.returncode, 0, lauf.stdout + lauf.stderr)
        self.assertFalse(ziel.exists())

    def test_offener_handle_bricht_mit_exit_3_ab(self):
        # DER zweite nachgemessene Fall. FileShare::None heisst: niemand
        # sonst darf die Datei anfassen - loeschen kann sie jetzt niemand.
        # Remove-Beleg muss das als Abbruch melden, nicht als Erfolg.
        ziel = self.wurzel / "WiesbadenReal.log"
        ziel.write_text("beleg", encoding="utf-8")
        lauf = self.kind(
            '$strom = [System.IO.File]::Open("%s", "Open", "ReadWrite", "None")\n'
            'Remove-Beleg "%s"\n'
            '$strom.Dispose()\n'
            'Write-Host "WEITERGELAUFEN - das darf nicht passieren"\n'
            % (str(ziel).replace("\\", "\\\\"), str(ziel).replace("\\", "\\\\")))
        self.assertEqual(lauf.returncode, 3, lauf.stdout + lauf.stderr)
        self.assertNotIn("WEITERGELAUFEN", lauf.stdout)
        self.assertIn("cleanup_unreal_processes", lauf.stdout)
        ziel.unlink(missing_ok=True)

    def test_test_frisch_unterscheidet_alt_von_neu(self):
        ziel = self.wurzel / "frisch.txt"
        ziel.write_text("x", encoding="utf-8")
        weg = self.wurzel / "nie_geschrieben.txt"
        alt = time.time() - 3600
        os.utime(str(ziel), (alt, alt))
        lauf = self.kind(
            '$jetzt = Get-Date\n'
            '$frueher = (Get-Date).AddHours(-1)\n'
            'Write-Host ("ALT: " + (Test-Frisch "%s" $jetzt))\n'
            'Write-Host ("NEU: " + (Test-Frisch "%s" $frueher))\n'
            'Write-Host ("WEG: " + (Test-Frisch "%s" $frueher))\n'
            % (str(ziel).replace("\\", "\\\\"), str(ziel).replace("\\", "\\\\"),
               str(weg).replace("\\", "\\\\")))
        self.assertEqual(lauf.returncode, 0, lauf.stdout + lauf.stderr)
        self.assertIn("ALT: False", lauf.stdout)
        self.assertIn("NEU: True", lauf.stdout)
        self.assertIn("WEG: False", lauf.stdout)


# ---------------------------------------------------------------------------
#  3. Die aufgeraeumten Werkzeugstellen
# ---------------------------------------------------------------------------

class StillGeloeschtTest(unittest.TestCase):
    """Kein Skript loescht mehr still und rechnet danach mit Wegsein.

    Geprueft wird der QUELLTEXT: eine stille Loeschung ist kein Fehler im
    Lauf, sondern eine Absichtserklaerung im Skript - man kann sie nur am
    Text sehen.
    """

    # (Datei, Zeile, die NICHT (mehr) dastehen darf, Zeile, die dastehen MUSS)
    STELLEN = [
        ("Tools/flight_check.ps1",
         "Remove-Item $Log -ErrorAction SilentlyContinue",
         "Remove-Beleg $Log"),
        ("Tools/build_release.ps1",
         "Remove-Item $BuildLog -ErrorAction SilentlyContinue",
         "Remove-Beleg $BuildLog"),
        ("Tools/build_release.ps1",
         "Remove-Item $PrevPackageDir -Recurse -Force -ErrorAction SilentlyContinue",
         "Remove-Beleg $PrevPackageDir"),
    ]

    def test_die_drei_bekannten_stellen_sind_umgestellt(self):
        for datei, verboten, noetig in self.STELLEN:
            text = lies(PROJEKT / datei)
            with self.subTest(datei=datei, zeile=verboten):
                self.assertNotIn(verboten, text)
                self.assertIn(noetig, text)

    def test_build_release_loescht_auch_die_testlog_geprueft(self):
        # Am 27.09.2026 aus dem Nachbar-Branch feature/gates-rauchtest-beleg
        # uebernommen: Gate 2 hatte dieselbe blinde Loeschung der Testlog.
        # Deren Loeschung ist die EINZIGE stille Remove-Item-Stelle, die in
        # dieser Datei noch uebrig ist - und jede davon wird unmittelbar
        # nachgeprueft.
        text = lies(PROJEKT / "Tools/build_release.ps1")
        zeilen = text.splitlines()
        still = [n for n, z in enumerate(zeilen)
                 if "Remove-Item" in z and "SilentlyContinue" in z
                 and not z.strip().startswith("#")]
        self.assertTrue(still, "Gate 2 loescht die Testlog nicht mehr - unerwartet")
        for n in still:
            with self.subTest(zeile=zeilen[n]):
                self.assertIn("$TestLog", zeilen[n])
                self.assertIn("-Force", zeilen[n],
                              "ohne -Force bleibt eine schreibgeschuetzte Datei "
                              "liegen - das Loeschen waere wieder blind")
                danach = [z.strip() for z in zeilen[n:n + 5] if z.strip()]
                self.assertTrue(
                    any(z.startswith("if (Test-Path $TestLog)")
                        for z in danach[1:]),
                    "direkt nach dem Loeschen muss die Nachpruefung stehen: %r" % (danach,))
        # Die Frische-Messung, ohne die Gate 2 im neuen Test verlangt wird.
        self.assertIn("$testLogStart = Get-Date", text)
        self.assertIn("if (-not $testLogFresh)", text)

    # -- cmd: del laeuft nach "Zugriff verweigert" weiter --------------------

    CMD_STELLEN = [
        ("Tools/verify_anchor.cmd", 'del /q "%ERGEBNIS%"', 8),
        ("Tools/import_ka52_audio.cmd", 'del /q "%RPT%"', 4),
        ("Tools/import_ka52_cockpit.cmd", 'del /q "%RPT%"', 4),
        ("Tools/ka52_wait_build.cmd", 'del /q "%RPT%"', 3),
    ]

    def test_cmd_prueft_das_weg_seinen(self):
        for datei, loeschzeile, code in self.CMD_STELLEN:
            zeilen = lies(PROJEKT / datei).splitlines()
            with self.subTest(datei=datei):
                self.assertIn(loeschzeile, "\n".join(zeilen))
                stelle = [i for i, z in enumerate(zeilen) if loeschzeile in z]
                self.assertEqual(len(stelle), 1, "genau eine Loeschung erwartet")
                # Das Fenster ist breit genug fuer den Kommentar, der die
                # Regel begruendet - aber der erste NICHT-Kommentar danach
                # muss die if-exist-Pruefung sein.
                danach = [z.strip() for z in zeilen[stelle[0] + 1:stelle[0] + 22]
                          if z.strip() and not z.strip().lower().startswith("rem")]
                self.assertTrue(
                    danach and danach[0].lower().startswith("if ")
                    and "exist" in danach[0].lower(),
                    "direkt nach dem del muss eine if-exist-Pruefung stehen, "
                    "sonst gilt das Wegsein als gegeben: %r" % (danach,))
                self.assertIn("exit /b %d" % code,
                              "\n".join(zeilen[stelle[0]:stelle[0] + 30]))

    def test_verify_anchor_braucht_den_eigenen_exitcode(self):
        # 0/2..7 sind dort belegt. Ein doppelt vergebener Code wuerde die
        # Auswertung der Aufrufer (Gate 5) auf eine falsche Spur schicken.
        text = lies(PROJEKT / "Tools/verify_anchor.cmd")
        codes = [int(z.split("exit /b")[1].split()[0].strip())
                 for z in text.splitlines() if "exit /b" in z]
        self.assertEqual(sorted(codes), [0, 0, 2, 3, 4, 5, 6, 7, 8])

    def test_keine_klammerbloecke_in_den_cmd_werkenzeugen(self):
        # Hausregel (test_verify_anchor_tool.py): in Batch beendet das erste
        # ungeschuetzte ")" auch einen Textblock den Block - die Sprungmarke
        # ist die einzige Form, die nicht an einer Zeile zerbricht.
        for datei in ("Tools/verify_anchor.cmd", "Tools/import_ka52_audio.cmd",
                      "Tools/import_ka52_cockpit.cmd"):
            with self.subTest(datei=datei):
                bloecke = re.findall(r"^\s*if\s+[^\r\n]*\(",
                                     lies(PROJEKT / datei), re.MULTILINE | re.I)
                self.assertEqual(bloecke, [], "nutze goto statt Klammerblock")

    def test_jede_sprungmarke_ist_erreichbar(self):
        # Ein Tippfehler im goto-Ziel laeuft in cmd bis zum Ende durch und
        # meldet genau das, was das Skript eigentlich verhindern soll.
        paare = [
            ("Tools/verify_anchor.cmd", [("ergebnis_weg", 8)]),
            ("Tools/import_ka52_audio.cmd", [("bericht_weg", 4), ("log_weg", 4)]),
            ("Tools/import_ka52_cockpit.cmd", [("bericht_weg", 4), ("log_weg", 4)]),
            ("Tools/ka52_wait_build.cmd", [("beleg_weg", 3)]),
        ]
        for datei, marken in paare:
            zeilen = lies(PROJEKT / datei).splitlines()
            vorhanden = {z.strip()[1:].lower() for z in zeilen
                         if z.strip().startswith(":")}
            for marke, code in marken:
                with self.subTest(datei=datei, marke=marke):
                    self.assertIn(marke, vorhanden)
                    sprung = [z for z in zeilen
                              if ("goto :" + marke) in z.strip().lower()]
                    self.assertTrue(sprung, "kein goto :%s" % marke)
                    danach = zeilen[zeilen.index(sprung[0]) + 1:]
                    ende = next((i for i, z in enumerate(danach)
                                 if z.strip().lower().startswith("exit /b")), None)
                    self.assertIsNotNone(ende, "kein Abbruch nach dem Sprung")
                    self.assertEqual(danach[ende].split("exit /b")[1].split()[0].strip(),
                                     str(code))

    # -- Python ------------------------------------------------------------

    def test_bake_abnahme_loescht_geprueft_und_misst_die_frische(self):
        text = lies(PROJEKT / "Tools/bake_abnahme.py")
        self.assertIn("loesche_beleg(LOG)", text)
        self.assertIn("except BelegFehler", text)
        self.assertIn("ist_frisch(LOG, start)", text)
        # Das alte Muster, wortwoertlich - including des "pass", das in
        # Python das SilentlyContinue ist.
        self.assertNotIn("os.remove(LOG)", text)
        self.assertNotIn("except OSError:\n            pass", text)

    def test_medien_wartet_nur_auf_die_laufende_sitzung(self):
        text = lies(PROJEKT / "Tools/medien.py")
        anfang = text.index("def warte_auf_zeile")
        ende = text.index("def aufnahme", anfang)
        rumpf = text[anfang:ende]
        self.assertIn("ist_frisch", rumpf)
        # Sonst ist die Bedingung nach 0,5 s erfuellt, sobald die Startzeile
        # aus dem vorletzten Lauf noch in der Datei steht.
        self.assertIn("if os.path.isfile(log) and ist_frisch(log, grenze)", rumpf)

    def test_sweep_map_zoom_prueft_das_weg_seinen(self):
        text = lies(PROJEKT / "sweep_map_zoom.sh")
        stelle = text.index('rm -f "$SHOT"')
        danach = text[stelle:stelle + 700]
        self.assertIn('if [ -e "$SHOT" ]', danach)
        self.assertIn("exit 1", danach)


# ---------------------------------------------------------------------------
#  4. Wache: keine NEUE stelle dieser Art
# ---------------------------------------------------------------------------

class KeineNeueStilleLoeschungTest(unittest.TestCase):
    """In keinem anderen verfolgten .ps1 darf stille geloescht werden.

    Drei Dateien sind ausgenommen, jede aus einem benannten Grund - die
    Liste waechst nicht still:

      smoke_test.ps1     die stille Loeschung sitzt in Remove-Beleg() und wird
                        im selben Block nachgeprueft (am 27.09.2026 aus dem
                        Nachbar-Branch uebernommen; test_rauchtest_beleg.py
                        sichert genau das ab)
      engine_run_lock.ps1  zwei Stellen, davon eine die VORLAGE (loeschen und
                        danach CreateNew beweisen); die andere ist fremde WIP
      build_release.ps1  eine Stelle ($TestLog, Gate 2), aus demselben Branch;
                        im vorigen Test einzeln abgesichert
    """

    AUSGENOMMEN = {"Tools/smoke_test.ps1", "Tools/engine_run_lock.ps1",
                   "Tools/build_release.ps1"}

    def verfolgte_ps1(self):
        # HEAD, NICHT der Index: in diesem Baum wird geteilt gearbeitet, und
        # `git ls-files` zeigt auch, was ein anderer Thread gerade vorgemerkt
        # hat. Am 27.09.2026 schlug dieser Wächter genau daran fehl - mitten
        # im Lauf, ohne dass sich eine der geprueften Dateien geaendert
        # haette. Der Wächter soll aussagen "in keinem COMMITETEN .ps1", und
        # das ist HEAD. Neu eingecheckte Dateien fasst er im naechsten Lauf.
        roh = subprocess.run(["git", "ls-tree", "-r", "--name-only", "HEAD"],
                             cwd=PROJEKT, capture_output=True, text=True,
                             check=True).stdout
        return [z.strip().replace("\\", "/") for z in roh.splitlines()
                if z.strip().endswith(".ps1")]

    def test_keine_neue_stelle(self):
        treffer = []
        for pfad in self.verfolgte_ps1():
            if pfad in self.AUSGENOMMEN:
                continue
            for nummer, zeile in enumerate(lies(PROJEKT / pfad).splitlines(), 1):
                if "Remove-Item" in zeile and "SilentlyContinue" in zeile:
                    treffer.append("%s:%d %s" % (pfad, nummer, zeile.strip()))
        self.assertEqual(treffer, [],
                         "stille Loeschung auf einer Datei, aus der gelesen "
                         "wird - entweder Remove-Beleg (Tools\\beleg.ps1) oder "
                         "eine Begruendung, warum das hier harmlos ist:\n"
                         + "\n".join(treffer))

    def test_kein_python_verschluckt_einen_loeschfehler_still(self):
        # "except OSError: pass" ist das SilentlyContinue in Python. Gesucht
        # ist nur die relevante Form: ein Loeschaufruf, dessen Fehler
        # direkt danach geschluckt wird. Ein "except OSError: pass" um eine
        # getsize-Messung herum ist etwas anderes (nicht messbar ist nicht
        # "leer") und bleibt erlaubt.
        rumpf = re.compile(
            r"(?:os\.remove|os\.unlink|shutil\.rmtree)\s*\([^)]*\)"
            r"[^\n]*\n[ \t]*except[ \t]+[A-Za-z]*Error[^\n]*:[ \t]*\n[ \t]*pass\b")
        treffer = []
        for pfad in ("Tools/bake_abnahme.py", "Tools/medien.py",
                     "Tools/verify_anchor_state.py", "Tools/list_turning_plates.py",
                     "Tools/collect_materials.py", "Tools/import_ka52_cockpit.py"):
            for treffer_re in rumpf.finditer(lies(PROJEKT / pfad)):
                treffer.append("%s: %r" % (pfad, treffer_re.group(0)))
        self.assertEqual(treffer, [], "\n".join(treffer))


if __name__ == "__main__":
    unittest.main()
