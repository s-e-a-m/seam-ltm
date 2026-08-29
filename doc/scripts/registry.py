#!/usr/bin/env python3
"""registry.py — lettura del registro dei plugin.

Una sola fonte, doc/plugins.toml, per il README e per il sito: senza, le due
copie divergono entro pochi mesi e nessuno se ne accorge finché non le legge
qualcuno.

tomllib è nella stdlib da Python 3.11 — un generatore di documentazione deve
poter girare su una macchina appena clonata, senza installare nulla.
"""
import pathlib
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]


def load():
    with open(ROOT / "doc" / "plugins.toml", "rb") as fh:
        data = tomllib.load(fh)
    return data["family"]


def plugins(families=None):
    for fam in families or load():
        for p in fam["plugin"]:
            yield fam, p
