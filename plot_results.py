"""Create report charts from experiment_results.csv, timing_results.csv and simulation_trace.csv.
Requires matplotlib: python -m pip install matplotlib
"""
import csv
from pathlib import Path
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parent

def read_csv(name):
    path = ROOT / name
    if not path.exists():
        print(f"Skip {name}: file not found (run the experiment/menu first).")
        return []
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))

rows = read_csv("experiment_results.csv")
if rows:
    strategies = ["First Fit", "Best Fit", "Worst Fit", "Smart"]
    for mode in ("file", "program"):
        subset = [r for r in rows if r["mode"] == mode]
        if not subset:
            continue
        labels = sorted({(r["pattern"], int(r["pool_kb"])) for r in subset})
        x = list(range(len(labels)))
        width = 0.19
        for j, strategy in enumerate(strategies):
            vals = []
            for pattern, pool_kb in labels:
                data = [float(r["avg_fragmentation_pct"]) for r in subset
                        if r["pattern"] == pattern and int(r["pool_kb"]) == pool_kb
                        and r["strategy"] == strategy]
                vals.append(sum(data) / len(data) if data else 0)
            plt.bar([v + (j - 1.5) * width for v in x], vals, width, label=strategy)
        plt.xticks(x, [f"{p}\n{pool:,} KB" for p, pool in labels])
        plt.ylabel("Average external fragmentation (%)")
        plt.title(f"Average fragmentation by {mode}-profile pattern (30 seeds)")
        plt.legend()
        plt.gcf().set_size_inches(12, 6)
        plt.tight_layout()
        plt.savefig(ROOT / f"fragmentation_{mode}_profiles.png", dpi=160)
        plt.close()

timing = read_csv("timing_results.csv")
if timing:
    strategies = ["First Fit", "Best Fit", "Worst Fit", "Smart"]
    for strategy in strategies:
        subset = [r for r in timing if r["strategy"] == strategy]
        subset.sort(key=lambda r: int(r["processes"]))
        plt.plot([int(r["processes"]) for r in subset],
                 [float(r["average_elapsed_seconds"]) for r in subset], marker="o", label=strategy)
    plt.xlabel("Number of processes")
    plt.ylabel("Average elapsed time (seconds, 3 runs)")
    plt.title("Runtime versus workload size")
    plt.legend()
    plt.tight_layout()
    plt.savefig(ROOT / "timing_by_process_count.png", dpi=160)
    plt.close()

trace = read_csv("simulation_trace.csv")
if trace:
    plt.plot([int(r["tick"]) for r in trace],
             [float(r["external_fragmentation_pct"]) for r in trace], marker=".")
    plt.xlabel("Simulation tick")
    plt.ylabel("External fragmentation (%)")
    plt.title("Fragmentation after each simulation tick")
    plt.tight_layout()
    plt.savefig(ROOT / "fragmentation_trace.png", dpi=160)
    plt.close()

print("Chart generation complete. PNG files are saved in the project folder.")
