#!/usr/bin/env python3
"""check.py -- verify the SSCDO#2 porting report against the studies.

usage (from doc/study/sscdo2/report/):
    python3 check.py              check the report
    python3 check.py --selftest   verify that the checker catches its mutations

Rules:
1. every \\misura{v}{src} / \\misurau{v}{unit}{src}: src is a study folder of
   ../ or `log`; v appears literally (U+2212 normalised to '-') in that
   folder's *.md files (build/ excluded) or in the session log;
2. every \\studio{folder} names an existing folder;
3. every folder listed in ../README.md is cited by \\studio or \\misura;
4. every required \\scheda, \\prova and \\questione id is present, and no
   \\scheda has an empty state argument.
"""
import os, re, sys, tempfile, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
STUDIES = os.path.dirname(HERE)
LOG = os.path.join(STUDIES, "..", "..", "..", "logs", "2026-09-29-sscdo2-ricognizione.md")

REQUIRED_SCHEDE = [
    "lmo-frequenza", "lmo-delta", "lmo-volume", "lmo-canali", "lmo-filtro",
    "delrm-distanza", "delrm-volume", "delrm-dinamica", "delrm-integratore", "delrm-dcblocker",
    "stunedrev-tempi", "stunedrev-ingresso-uscita", "stunedrev-sezioni", "stunedrev-memoria", "stunedrev-reset",
    "coro-uscita", "master",
]
REQUIRED_PROVE = [
    "lmo-cue", "lmo-glissando", "lmo-delta", "lmo-filtro",
    "delrm-distanza", "delrm-dinamica", "delrm-dcblocker",
    "stunedrev-facce", "stunedrev-ingresso",
]
REQUIRED_QUESTIONI = ["doppio-ingresso", "uscite-cue", "semi", "dcblocker", "filtro-lmo"]

def norm(s): return s.replace("−", "-")

def tex_sources(root):
    out = {}
    for f in sorted(os.listdir(root)):
        if f.endswith(".tex"):
            out[f] = open(os.path.join(root, f), encoding="utf-8").read()
    return out

def study_folders(studies):
    return sorted(d for d in os.listdir(studies)
                  if os.path.isdir(os.path.join(studies, d)) and d != "report" and not d.startswith("."))

def source_text(studies, log, src, cache={}):
    key = (studies, src)
    if key not in cache:
        if src == "log":
            cache[key] = norm(open(log, encoding="utf-8").read())
        else:
            parts = []
            for dp, dns, fns in os.walk(os.path.join(studies, src)):
                dns[:] = [d for d in dns if d != "build"]
                for fn in fns:
                    if fn.endswith(".md"):
                        parts.append(open(os.path.join(dp, fn), encoding="utf-8").read())
            cache[key] = norm("\n".join(parts))
    return cache[key]

def args(text, macro, n):
    """All argument tuples of \\macro{a}{b}... with n brace groups, at any nesting depth."""
    out = []
    for m in re.finditer(r"\\" + macro + r"(?![A-Za-z])", text):
        i, groups = m.end(), []
        while len(groups) < n:
            while i < len(text) and text[i] in " \t\n":
                i += 1
            if i >= len(text) or text[i] != "{":
                break
            depth, j = 0, i
            while j < len(text):
                if text[j] == "\\":
                    j += 2; continue
                if text[j] == "{": depth += 1
                elif text[j] == "}":
                    depth -= 1
                    if depth == 0: break
                j += 1
            groups.append(text[i + 1:j]); i = j + 1
        if len(groups) == n:
            out.append(tuple(groups))
    return out

def check(root, studies, log, required=True):
    errors, warnings = [], []
    tex = tex_sources(root)
    folders = study_folders(studies)
    cited = set()
    for f, t in tex.items():
        body = re.sub(r"(?<!\\)%.*", "", t)
        for v, src in args(body, "misura", 2) + [(a, c) for a, _, c in args(body, "misurau", 3)]:
            cited.add(src)
            if src != "log" and src not in folders:
                errors.append(f"{f}: \\misura{{{v}}}{{{src}}}: unknown source"); continue
            nv = norm(v)
            if not re.search(r"(?<![\d.])" + re.escape(nv) + r"(?![\d])", source_text(studies, log, src)):
                errors.append(f"{f}: \\misura{{{v}}}{{{src}}}: value not found in source")
            if len(v.strip("+-− ")) < 3:
                warnings.append(f"{f}: \\misura{{{v}}}{{{src}}}: short value, weak check")
        for (d,) in args(body, "studio", 1):
            cited.add(d)
            if d not in folders:
                errors.append(f"{f}: \\studio{{{d}}}: no such folder")
        for a in args(body, "scheda", 8):
            if not a[6].strip():
                errors.append(f"{f}: \\scheda{{{a[0]}}}: empty state")
            elif a[6].strip() not in (r"\deciso", r"\daprovare", r"\domanda"):
                errors.append(f"{f}: \\scheda{{{a[0]}}}: state must be \\deciso, \\daprovare or \\domanda")
    index = open(os.path.join(studies, "README.md"), encoding="utf-8").read()
    for d in re.findall(r"`([a-z0-9-]+)/`", index):
        if d in folders and d not in cited:
            errors.append(f"study {d}/ is in the index and never cited")
    if required:
        # the spec's structure: a list of states, and each machine drawn as a chain
        main = re.sub(r"(?<!\\)%.*", "", tex.get("sscdo2-porting.tex", ""))
        if "\\listastati" not in main:
            errors.append("missing \\listastati (the list of card states) in sscdo2-porting.tex")
        for f in ("parte2-lmo.tex", "parte2-delrm.tex", "parte2-stunedrev.tex"):
            if "\\begin{tikzpicture}" not in tex.get(f, ""):
                errors.append(f"{f}: missing the machine's chain figure (tikzpicture)")
        alltex = "\n".join(re.sub(r"(?<!\\)%.*", "", t) for t in tex.values())
        have = {m: {a[0] for a in args(alltex, m, n)} for m, n in (("scheda", 8), ("prova", 4), ("questione", 2))}
        for m, req in (("scheda", REQUIRED_SCHEDE), ("prova", REQUIRED_PROVE), ("questione", REQUIRED_QUESTIONI)):
            for i in req:
                if i not in have[m]:
                    errors.append(f"missing \\{m}{{{i}}}")
    return errors, warnings

def selftest():
    tmp = tempfile.mkdtemp()
    try:
        studies = os.path.join(tmp, "sscdo2"); root = os.path.join(studies, "report")
        os.makedirs(os.path.join(studies, "demo")); os.makedirs(root)
        open(os.path.join(studies, "demo", "README.md"), "w").write("delay 2113 samples, notch −12.8 dB\n")
        open(os.path.join(studies, "README.md"), "w").write("| x | `demo/` |\n")
        log = os.path.join(tmp, "log.md"); open(log, "w").write("cue at 97.44 Hz\n")
        def run(tex):
            open(os.path.join(root, "a.tex"), "w").write(tex)
            return check(root, studies, log, required=False)[0]
        cases = [
            ("good", r"\misura{2113}{demo} \misura{-12.8}{demo} \misura{97.44}{log} \studio{demo}", 0),
            ("minus normalised", r"\misura{−12.8}{demo}", 0),
            ("MUTATION value", r"\misura{2114}{demo}", 1),
            ("MUTATION source", r"\misura{97.44}{demo} \studio{demo}", 1),
            ("MUTATION folder", r"\misura{2113}{demo} \studio{nodemo}", 1),
            ("MUTATION uncited", r"\misura{97.44}{log}", 1),
            ("MUTATION empty state", r"\misura{2113}{demo}\scheda{x}{a}{b}{c}{d}{e}{}{g}", 1),
            ("MUTATION missing leading digit", r"\misura{113}{demo} \studio{demo}", 1),
            ("MUTATION bad state", r"\misura{2113}{demo}\scheda{x}{a}{b}{c}{d}{e}{TODO}{g}", 1),
        ]
        # required ids must be found through nested braces
        open(os.path.join(root, "a.tex"), "w").write(
            r"\studio{demo}\scheda{deep}{a}{b}{c}{\texttt{x\textasciitilde{} y} \emph{\misura{2113}{demo}}}{e}{\deciso}{g}")
        found = {a[0] for a in args(open(os.path.join(root, "a.tex")).read(), "scheda", 8)}
        good = found == {"deep"}
        print(f"  [{'ok' if good else 'FAIL'}] nested braces: found {sorted(found)}")
        ok_nested = good
        ok = True
        for name, tex, want in cases:
            got = len(run(tex))
            good = (got == 0) if want == 0 else (got >= 1)
            print(f"  [{'ok' if good else 'FAIL'}] {name}: {got} error(s)")
            ok &= good
        # a required card present only in a comment must be reported missing
        global REQUIRED_SCHEDE, REQUIRED_PROVE, REQUIRED_QUESTIONI
        saved = (REQUIRED_SCHEDE, REQUIRED_PROVE, REQUIRED_QUESTIONI)
        REQUIRED_SCHEDE, REQUIRED_PROVE, REQUIRED_QUESTIONI = ["x"], [], []
        open(os.path.join(root, "a.tex"), "w").write("\\studio{demo}\n% \\scheda{x}{a}{b}{c}{d}{e}{\\deciso}{g}\n")
        errs = check(root, studies, log, required=True)[0]
        REQUIRED_SCHEDE, REQUIRED_PROVE, REQUIRED_QUESTIONI = saved
        good = any("missing \\scheda{x}" in e for e in errs)
        print(f"  [{'ok' if good else 'FAIL'}] MUTATION commented card: {len(errs)} error(s)")
        return ok and ok_nested and good
    finally:
        shutil.rmtree(tmp)

if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(0 if selftest() else 1)
    errors, warnings = check(HERE, STUDIES, LOG)
    for w in warnings: print("warning:", w)
    for e in errors: print("ERROR:", e)
    print("CHECK OK" if not errors else f"CHECK FAILED ({len(errors)} errors)")
    sys.exit(1 if errors else 0)
