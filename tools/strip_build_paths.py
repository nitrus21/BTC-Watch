"""Retire les chemins locaux inscrits dans le firmware par la macro __FILE__.

Les messages d'erreur du framework contiennent sinon le dossier utilisateur
de la machine de compilation (nom du compte, arborescence). Ce script est
appele par platformio.ini avant la compilation.
"""
import os

Import("env")  # noqa: F821 - fourni par PlatformIO

ROOTS = {
    "pio": env.subst("$PROJECT_PACKAGES_DIR"),  # noqa: F821
    "project": env.subst("$PROJECT_DIR"),  # noqa: F821
    "home": os.path.expanduser("~"),
}

flags = []
for name, root in ROOTS.items():
    if not root:
        continue
    for variant in sorted({root, root.replace("\\", "/"), root.replace("/", "\\")}):
        flags.append("-fmacro-prefix-map=%s=%s" % (variant, name))

env.Append(CCFLAGS=flags)  # noqa: F821
