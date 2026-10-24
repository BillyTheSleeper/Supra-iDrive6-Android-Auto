#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Builds NbtAad_ALL_Texts.zip (<lang>.txt with "id=text" lines, same format as BMW's PluginsCoded apps)
# and NbtAad_ALL_Images.zip (<imageId>.png).
import os, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
LANGS = {"de": "Deutsch", "en-UK": "English (UK)", "en-US": "English (US)", "es-ES": "Espanol", "es-US": "Espanol (US)",
         "fr-FR": "Francais", "fr-CA": "Francais (CA)", "it": "Italiano", "pt": "Portugues", "ru": "Russian"}
TEXTS = {1: "Android Auto"}
with zipfile.ZipFile(os.path.join(HERE, "NbtAad_ALL_Texts.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    for lang, langname in LANGS.items():
        body = "100000=%s\n" % langname + "".join("%d=%s\n" % (k, v) for k, v in sorted(TEXTS.items()))
        z.writestr(lang + ".txt", body.encode("utf-8"))
icon = os.path.join(HERE, "..", "plugin", "res", "aa_row_icon_63x54.png")
with zipfile.ZipFile(os.path.join(HERE, "NbtAad_ALL_Images.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    z.write(icon, "55001.png")
print("wrote NbtAad_ALL_Texts.zip, NbtAad_ALL_Images.zip")
