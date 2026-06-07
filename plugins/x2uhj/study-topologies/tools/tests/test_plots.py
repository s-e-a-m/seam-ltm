import subprocess, sys, os
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIGDIR = os.path.join(HERE, "..", "doc", "figures")

def test_plots_emit_files():
    subprocess.run([sys.executable, "plots.py"], cwd=HERE, check=True)
    for fn in ("fixed_vs_perfs.png", "pareto_cost_quality.png",
               "error_vs_order.png", "group_delay.png"):
        assert os.path.exists(os.path.join(FIGDIR, fn))
