#!/usr/bin/env python3
"""render-readme.py — riscrive le tabelle dei plugin nel README dal registro.

Sostituisce il blocco fra i marker BEGIN/END. Fuori dai marker il README resta
scritto a mano.
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import registry

BEGIN = "<!-- BEGIN plugins (generato da doc/plugins.toml — non modificare a mano) -->"
END = "<!-- END plugins -->"

families = registry.load()
out = [BEGIN, ""]

for fam in families:
    out.append(f"### {fam['title']}")
    out.append("")
    out.append("| Plugin | I/O | Description |")
    out.append("|---|---|---|")
    for p in fam["plugin"]:
        desc = p["description"].strip().replace("\n", " ")
        out.append(f"| **{p['name']}** | {p['io']} | {desc} |")
    out.append("")

out.append("### Screenshots")
out.append("")
out.append("All sixteen windows, photographed after the UI standard landed, in the order")
out.append("of the three families above.")
out.append("")

names = [p["name"] for _f, p in registry.plugins(families)]
shots = {p["name"]: p["screenshot"] for _f, p in registry.plugins(families)}
for i in range(0, len(names), 4):
    row = names[i:i + 4]
    out.append("| " + " | ".join(row) + " |")
    out.append("|" + ":---:|" * len(row))
    out.append("| " + " | ".join(f"![{n}](docs/img/{shots[n]})" for n in row) + " |")
    out.append("")

out.append(END)

readme = registry.ROOT / "README.md"
text = readme.read_text()
if BEGIN in text and END in text:
    head = text.split(BEGIN)[0]
    tail = text.split(END, 1)[1].lstrip("\n")
    text = head + "\n".join(out) + "\n\n" + tail
else:
    sys.exit("render-readme: marker BEGIN/END assenti nel README")

readme.write_text(text)
print(f"  README: {len(names)} plugin in {len(families)} famiglie")
