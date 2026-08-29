#!/usr/bin/env python3
"""publish.py — porta le schede dei plugin sul sito SEAM.

Uso: publish.py <percorso-del-sito>

La fonte è doc/plugins.toml, la stessa che genera le tabelle del README: una
sola fonte, così sito e repo non possono divergere.

Build, SDK e installazione restano nel README, accanto al codice: invecchiano
col codice, e sul sito diventerebbero false senza che nessuno se ne accorga.
"""
import datetime
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import registry

if len(sys.argv) < 2:
    sys.exit("uso: publish.py <percorso-del-sito>")

site = pathlib.Path(sys.argv[1])
root = registry.ROOT
scripts = root / "doc" / "scripts"

if not site.is_dir():
    sys.exit(f"publish: sito non trovato: {site}")

rev = subprocess.run(["git", "-C", str(root), "rev-parse", "--short", "HEAD"],
                     capture_output=True, text=True).stdout.strip()
today = datetime.date.today().isoformat()
families = registry.load()

# gli screenshot sono fotografie di finestre: restano file, non diventano inline
assets = site / "assets" / "seam-ltm" / "img"
assets.mkdir(parents=True, exist_ok=True)
copied = 0
for _fam, p in registry.plugins(families):
    src = root / "docs" / "img" / p["screenshot"]
    if src.exists():
        shutil.copy2(src, assets / p["screenshot"])
        copied += 1

out = ["---", 'title: "SEAM-LTM — Plugin Suite"', "permalink: /seam-ltm/", "toc: true",
       "generated_from: seam-ltm", f"generated_rev: {rev}", f"generated_at: {today}",
       "---", "",
       "<!-- GENERATO — non modificare qui: la fonte è doc/plugins.toml di seam-ltm -->"]
out += [
    "Sixteen VST3 plugins for sustained electroacoustic music, built directly on the Steinberg VST3 SDK — no JUCE, no frameworks.",
    "They fall into three families: converters that move a signal from one spatial format to another, generators that produce the test signals a room is measured with, and the measurement and processing tools that listen to the result.",
    "",
    "Several of them are the C++ counterpart of an algorithm that also lives in Faust: where that is the case, the entry links the library source.",
    "",
    "Building the suite, the VST3 SDK and the installation paths are documented in the [repository README](https://github.com/s-e-a-m/seam-ltm) — they belong next to the code, where they cannot quietly go stale.",
    "",
    f"Generated from [github.com/s-e-a-m/seam-ltm](https://github.com/s-e-a-m/seam-ltm) at `{rev}`.",
    "",
]

for fam in families:
    out.append(f"## {fam['title']}")
    out.append("")
    for p in fam["plugin"]:
        out.append(f"### {p['name']}")
        out.append("")
        # prima cosa fa, poi com'e fatto: l'immagine fra il titolo e l'I/O
        # allontanava il nome del plugin dall'informazione che lo qualifica
        out.append(f"**{p['io']}**")
        out.append("")
        out.append(p["description"].strip())
        out.append("")
        out.append(f'<img src="/assets/seam-ltm/img/{p["screenshot"]}" alt="{p["name"]}" class="ltm-shot">')
        if p.get("faust"):
            lib = p["faust"]
            url = f"https://github.com/s-e-a-m/faust-libraries/blob/master/src/{lib}"
            out.append("")
            out.append(f"Faust counterpart: [`{lib}`]({url}).")
        out.append("")

coll = site / "_ltm"
coll.mkdir(parents=True, exist_ok=True)
(coll / "index.md").write_text("\n".join(out))

nav = ["ltm:", '  - title: "Plugin Suite"', "    url: /seam-ltm/", "    children:"]
for fam in families:
    slug = "".join(c if c.isalnum() else "-" for c in fam["title"].lower()).strip("-")
    nav.append(f'      - title: "{fam["title"]}"')
    nav.append(f"        url: /seam-ltm/#{slug}")
subprocess.run(["python3", str(scripts / "navblock.py"),
                str(site / "_data" / "navigation.yml"), "ltm"],
               input="\n".join(nav), text=True, check=True)

n = sum(len(f["plugin"]) for f in families)
print(f"  publish seam-ltm: {n} plugin, {len(families)} famiglie, {copied} screenshot")
