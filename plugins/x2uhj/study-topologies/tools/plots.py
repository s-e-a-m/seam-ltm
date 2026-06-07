"""Render the study figures from results.json into doc/figures/."""
import json, os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
RESULTS = os.path.join(HERE, "..", "results.json")
FIGDIR = os.path.join(HERE, "..", "doc", "figures")

def _load():
    with open(RESULTS) as fp:
        return json.load(fp)["topologies"]

def fixed_vs_perfs(topos):
    plt.figure(figsize=(9, 5))
    for name, e in topos.items():
        rates = sorted(float(r) for r in e["per_fs_error"])
        per = [e["per_fs_error"][f"{r:.1f}"] for r in rates]
        fix = [e["fixed_error"][f"{r:.1f}"] for r in rates]
        plt.plot([r/1000 for r in rates], per, "s-", label=f"{name} per-fs")
        plt.plot([r/1000 for r in rates], fix, "o--", label=f"{name} fixed")
    plt.xlabel("sample rate (kHz)"); plt.ylabel("max |phase diff - 90| (deg)")
    plt.title("Quadrature error: fixed coefficients versus per-rate design")
    plt.legend(fontsize=8); plt.grid(True, alpha=0.3); plt.tight_layout()
    plt.savefig(os.path.join(FIGDIR, "fixed_vs_perfs.png"), dpi=120); plt.close()

def pareto(topos):
    plt.figure(figsize=(7, 5))
    for name, e in topos.items():
        err48 = e["per_fs_error"]["48000.0"]
        plt.scatter(e["cost"], err48, s=60)
        plt.annotate(name, (e["cost"], err48), fontsize=8,
                     textcoords="offset points", xytext=(5, 5))
    plt.xlabel("multiplies per sample"); plt.ylabel("max error at 48 kHz (deg)")
    plt.title("Quality versus cost"); plt.grid(True, alpha=0.3); plt.tight_layout()
    plt.savefig(os.path.join(FIGDIR, "pareto_cost_quality.png"), dpi=120); plt.close()

def error_vs_order(topos):
    plt.figure(figsize=(8, 5))
    for name, e in topos.items():
        items = sorted((int(o), v) for o, v in e["error_vs_order"].items() if v is not None)
        if items:
            plt.plot([o for o, _ in items], [v for _, v in items], "o-", label=name)
    plt.xlabel("sections per path"); plt.ylabel("max error at 48 kHz (deg)")
    plt.title("Phase error versus order"); plt.legend(); plt.grid(True, alpha=0.3)
    plt.tight_layout(); plt.savefig(os.path.join(FIGDIR, "error_vs_order.png"), dpi=120); plt.close()

def group_delay(topos):
    plt.figure(figsize=(8, 5))
    for name, e in topos.items():
        gd = e["group_delay"]
        plt.semilogx(gd["freqs"], gd["gd_us"], "-", label=name)
    plt.xlabel("Hz"); plt.ylabel("differential group delay (us)")
    plt.title("Differential group delay between the two paths (48 kHz)")
    plt.legend(); plt.grid(True, which="both", alpha=0.3)
    plt.tight_layout(); plt.savefig(os.path.join(FIGDIR, "group_delay.png"), dpi=120); plt.close()

def main():
    os.makedirs(FIGDIR, exist_ok=True)
    topos = _load()
    fixed_vs_perfs(topos); pareto(topos); error_vs_order(topos); group_delay(topos)
    print("wrote figures to", os.path.abspath(FIGDIR))

if __name__ == "__main__":
    main()
