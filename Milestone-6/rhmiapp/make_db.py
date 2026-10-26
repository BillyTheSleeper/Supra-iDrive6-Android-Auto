#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Builds NbtAad_ALL_Texts.zip (<lang>.txt with "id=text" lines, same format as BMW's PluginsCoded apps)
# and NbtAad_ALL_Images.zip (<imageId>.png).
import os, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
LANGS = {"de": "Deutsch", "en-UK": "English (UK)", "en-US": "English (US)", "es-ES": "Espanol", "es-US": "Espanol (US)",
         "fr-FR": "Francais", "fr-CA": "Francais (CA)", "it": "Italiano", "pt": "Portugues", "ru": "Russian"}
TEXTS = {1: "Android Auto", 2: "Android Auto: Connecting...",   # 2 = entry text while nbt-aad is connecting
         # Android Auto menu (hmiState 10): row 1 changes with the daemon state (10 / 13 / 14)
         10: "Start Android Auto", 11: "Force Stop", 12: "Restart", 13: "Connecting...", 14: "Open Android Auto"}
# Per-language overrides (ASCII only, like the language names above).
LANG_TEXTS = {
    "de": {2: "Android Auto: Verbinden...", 10: "Android Auto starten", 11: "Beenden erzwingen", 12: "Neu starten",
           13: "Verbinden...", 14: "Android Auto oeffnen"},
    "es-ES": {2: "Android Auto: Conectando...", 10: "Iniciar Android Auto", 11: "Forzar cierre", 12: "Reiniciar",
              13: "Conectando...", 14: "Abrir Android Auto"},
    "fr-FR": {2: "Android Auto : Connexion...", 10: "Demarrer Android Auto", 11: "Forcer l'arret", 12: "Redemarrer",
              13: "Connexion...", 14: "Ouvrir Android Auto"},
    "it": {2: "Android Auto: Connessione...", 10: "Avvia Android Auto", 11: "Arresto forzato", 12: "Riavvia",
           13: "Connessione...", 14: "Apri Android Auto"},
    "pt": {2: "Android Auto: A ligar...", 10: "Iniciar Android Auto", 11: "Forcar paragem", 12: "Reiniciar",
           13: "A ligar...", 14: "Abrir Android Auto"},
}
LANG_TEXTS["es-US"] = LANG_TEXTS["es-ES"]
LANG_TEXTS["fr-CA"] = LANG_TEXTS["fr-FR"]
with zipfile.ZipFile(os.path.join(HERE, "NbtAad_ALL_Texts.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    for lang, langname in LANGS.items():
        texts = dict(TEXTS)
        texts.update(LANG_TEXTS.get(lang, {}))
        body = "100000=%s\n" % langname + "".join("%d=%s\n" % (k, v) for k, v in sorted(texts.items()))
        z.writestr(lang + ".txt", body.encode("utf-8"))
icon = os.path.join(HERE, "..", "plugin", "res", "aa_row_icon_63x54.png")
preview = os.path.join(HERE, "..", "plugin", "res", "aa_content_preview_549x484.png")
with zipfile.ZipFile(os.path.join(HERE, "NbtAad_ALL_Images.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    z.write(icon, "55001.png")                      # 63x54 row icon (imageIdModel 4)
    z.write(preview, "55002.png")                   # 549x484 content preview (imageIdModel 12, StatePreview 13)
    for f in range(8):                              # 55010..55017: entry-row "connecting" spinner frames (rhmiapp-6)
        z.write(os.path.join(HERE, "..", "plugin", "res", "spinner", "spin_%d.png" % f), "%d.png" % (55010 + f))
print("wrote NbtAad_ALL_Texts.zip, NbtAad_ALL_Images.zip")
