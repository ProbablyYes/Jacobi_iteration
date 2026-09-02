#!/usr/bin/env python3
"""从真实 CSV 结果生成课程报告图表。"""

from __future__ import annotations

import csv
import math
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results"
FIGURES = RESULTS / "figures"


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def median_rows(rows: list[dict[str, str]], keys: tuple[str, ...]) -> list[dict[str, object]]:
    groups: dict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        groups[tuple(row[key] for key in keys)].append(row)
    output: list[dict[str, object]] = []
    numeric = (
        "total_seconds",
        "compute_seconds",
        "halo_issue_seconds",
        "halo_wait_seconds",
        "reduction_seconds",
        "residual",
        "relative_l2_error",
    )
    for key, values in groups.items():
        summary: dict[str, object] = dict(zip(keys, key))
        for field in numeric:
            summary[field] = statistics.median(float(value[field]) for value in values)
        output.append(summary)
    return output


def save_figure(name: str) -> None:
    plt.tight_layout()
    plt.savefig(FIGURES / name, dpi=180, bbox_inches="tight")
    plt.close()


def plot_scaling(rows: list[dict[str, str]]) -> None:
    summaries = median_rows(rows, ("version", "size", "processes", "iterations"))
    if not summaries:
        return
    counts = defaultdict(int)
    for item in summaries:
        counts[(item["size"], item["iterations"])] += 1
    workload = max(counts, key=counts.get)
    selected = [item for item in summaries if (item["size"], item["iterations"]) == workload]
    serial_times = [float(item["total_seconds"]) for item in selected if item["version"] == "serial"]
    if not serial_times:
        return
    baseline = statistics.median(serial_times)

    plt.figure(figsize=(7, 4.5))
    for version in ("mpi_blocking", "mpi_overlap"):
        values = sorted(
            (int(item["processes"]), float(item["total_seconds"]))
            for item in selected if item["version"] == version
        )
        if values:
            plt.plot([x for x, _ in values], [y for _, y in values], "o-", label=version)
    plt.axhline(baseline, color="gray", linestyle="--", label="serial")
    plt.xlabel("Processes")
    plt.ylabel("Median time (s)")
    plt.title(f"Strong scaling: N={workload[0]}, iterations={workload[1]}")
    plt.grid(alpha=0.3)
    plt.legend()
    save_figure("runtime_vs_processes.png")

    plt.figure(figsize=(7, 4.5))
    maximum_processes = 1
    for version in ("mpi_blocking", "mpi_overlap"):
        values = sorted(
            (int(item["processes"]), baseline / float(item["total_seconds"]))
            for item in selected if item["version"] == version
        )
        if values:
            maximum_processes = max(maximum_processes, max(x for x, _ in values))
            plt.plot([x for x, _ in values], [y for _, y in values], "o-", label=version)
    plt.plot([1, maximum_processes], [1, maximum_processes], "k--", label="ideal")
    plt.xlabel("Processes")
    plt.ylabel("Speedup (serial / MPI)")
    plt.title("Speedup and ideal linear scaling")
    plt.grid(alpha=0.3)
    plt.legend()
    save_figure("speedup.png")

    plt.figure(figsize=(7, 4.5))
    for version in ("mpi_blocking", "mpi_overlap"):
        values = sorted(
            (int(item["processes"]),
             baseline / (float(item["total_seconds"]) * int(item["processes"])))
            for item in selected if item["version"] == version
        )
        if values:
            plt.plot([x for x, _ in values], [100.0 * y for _, y in values],
                     "o-", label=version)
    plt.axhline(100.0, color="black", linestyle="--", label="ideal")
    plt.xlabel("Processes")
    plt.ylabel("Parallel efficiency (%)")
    plt.title("Parallel efficiency")
    plt.grid(alpha=0.3)
    plt.legend()
    save_figure("efficiency.png")

    mpi = sorted((
        (int(item["processes"]), item)
        for item in selected if item["version"] in {"mpi_blocking", "mpi_overlap"}
    ), key=lambda pair: (pair[0], str(pair[1]["version"])))
    if mpi:
        labels = [f'{item["version"].replace("mpi_", "")}\nP={p}' for p, item in mpi]
        compute = [float(item["compute_seconds"]) for _, item in mpi]
        issue = [float(item["halo_issue_seconds"]) for _, item in mpi]
        wait = [float(item["halo_wait_seconds"]) for _, item in mpi]
        reduction = [float(item["reduction_seconds"]) for _, item in mpi]
        x = list(range(len(mpi)))
        plt.figure(figsize=(max(8, len(mpi) * 0.9), 4.8))
        plt.bar(x, compute, label="computation")
        plt.bar(x, issue, bottom=compute, label="halo issue")
        lower = [a + b for a, b in zip(compute, issue)]
        plt.bar(x, wait, bottom=lower, label="exposed halo wait")
        lower = [a + b for a, b in zip(lower, wait)]
        plt.bar(x, reduction, bottom=lower, label="Allreduce")
        plt.xticks(x, labels)
        plt.ylabel("Maximum rank time (s)")
        plt.title("Measured time components")
        plt.legend()
        save_figure("time_breakdown.png")

    paired: dict[int, dict[str, dict[str, object]]] = defaultdict(dict)
    for item in selected:
        if item["version"] in {"mpi_blocking", "mpi_overlap"}:
            paired[int(item["processes"])][str(item["version"])] = item
    complete = [(processes, versions) for processes, versions in sorted(paired.items())
                if len(versions) == 2]
    if complete:
        processes = [item[0] for item in complete]
        blocking_total = [float(item[1]["mpi_blocking"]["total_seconds"]) for item in complete]
        overlap_total = [float(item[1]["mpi_overlap"]["total_seconds"]) for item in complete]
        blocking_wait = [float(item[1]["mpi_blocking"]["halo_wait_seconds"]) for item in complete]
        overlap_wait = [float(item[1]["mpi_overlap"]["halo_wait_seconds"]) for item in complete]
        x = list(range(len(processes)))
        width = 0.36
        figure, axes = plt.subplots(1, 2, figsize=(10, 4.5))
        axes[0].bar([value - width / 2 for value in x], blocking_total, width,
                    label="blocking")
        axes[0].bar([value + width / 2 for value in x], overlap_total, width,
                    label="overlap")
        axes[0].set_title("Total runtime")
        axes[0].set_ylabel("Median time (s)")
        axes[1].bar([value - width / 2 for value in x], blocking_wait, width,
                    label="blocking")
        axes[1].bar([value + width / 2 for value in x], overlap_wait, width,
                    label="overlap")
        axes[1].set_title("Exposed halo wait")
        axes[1].set_ylabel("Maximum rank time (s)")
        for axis in axes:
            axis.set_xticks(x, [str(value) for value in processes])
            axis.set_xlabel("Processes")
            axis.grid(alpha=0.3, axis="y")
            axis.legend()
        figure.suptitle("Blocking vs communication-computation overlap")
        save_figure("blocking_vs_overlap.png")


def plot_convergence(rows: list[dict[str, str]]) -> None:
    if not rows:
        return
    plt.figure(figsize=(7, 4.5))
    plt.semilogy([int(row["iteration"]) for row in rows],
                 [float(row["residual"]) for row in rows])
    plt.xlabel("Iteration")
    plt.ylabel("Residual (L-infinity)")
    plt.title("Jacobi convergence history")
    plt.grid(alpha=0.3)
    save_figure("convergence.png")


def plot_accuracy(rows: list[dict[str, str]]) -> None:
    if len(rows) < 2:
        return
    rows = sorted(rows, key=lambda row: int(row["size"]))
    sizes = [int(row["size"]) for row in rows]
    errors = [float(row["relative_l2_error"]) for row in rows]
    orders = [math.log2(errors[i - 1] / errors[i]) for i in range(1, len(errors))]
    plt.figure(figsize=(7, 4.5))
    plt.loglog(sizes, errors, "o-", base=2, label="measured error")
    reference = [errors[0] * (sizes[0] / size) ** 2 for size in sizes]
    plt.loglog(sizes, reference, "k--", base=2,
               label=f"second-order reference; p={statistics.mean(orders):.3f}")
    plt.xlabel("Interior grid size N")
    plt.ylabel("Relative L2 error")
    plt.title("Grid refinement accuracy")
    plt.grid(alpha=0.3, which="both")
    plt.legend()
    save_figure("grid_refinement.png")


def main() -> None:
    FIGURES.mkdir(parents=True, exist_ok=True)
    raw = read_csv(RESULTS / "raw.csv")
    accuracy = read_csv(RESULTS / "accuracy.csv")
    convergence = read_csv(RESULTS / "convergence.csv")
    if not raw and not accuracy and not convergence:
        raise SystemExit("没有实验数据；请先运行 make benchmark 和 make accuracy")
    plot_scaling(raw)
    plot_convergence(convergence)
    plot_accuracy(accuracy)
    print(f"图表已写入 {FIGURES}")


if __name__ == "__main__":
    main()
