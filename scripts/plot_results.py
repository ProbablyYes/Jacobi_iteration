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
    maximum_count = max(counts.values())
    workload = max((key for key, count in counts.items() if count == maximum_count),
                   key=lambda key: (int(key[0]), int(key[1])))
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
        width = 0.25
        figure, axes = plt.subplots(1, 2, figsize=(14, 4.8),
                                    gridspec_kw={"width_ratios": [1, 1.5]})
        axes[0].bar(x, compute, color="tab:blue", label="computation")
        axes[0].set_xticks(x, labels, rotation=20, ha="right", fontsize=8)
        axes[0].set_ylabel("Maximum rank time (s)")
        axes[0].set_title("Stencil computation")
        axes[0].grid(alpha=0.3, axis="y")
        axes[1].bar([value - width for value in x], issue, width,
                    label="halo issue")
        axes[1].bar(x, wait, width, label="exposed halo wait")
        axes[1].bar([value + width for value in x], reduction, width,
                    label="Allreduce")
        axes[1].set_xticks(x, labels, rotation=20, ha="right", fontsize=8)
        axes[1].set_ylabel("Maximum rank time (s)")
        axes[1].set_title("Communication and synchronization")
        axes[1].grid(alpha=0.3, axis="y")
        axes[1].legend()
        figure.suptitle("Independently reduced timing components (not additive)")
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


def plot_sensitivities(rows: list[dict[str, str]]) -> None:
    fixed = [row for row in rows if row["status"] == "fixed_iterations"
             and int(row["processes"]) == 4]
    summaries = median_rows(fixed, ("version", "size", "processes", "iterations"))
    sizes = sorted({int(item["size"]) for item in summaries})
    if len(sizes) >= 2:
        plt.figure(figsize=(7, 4.5))
        for version in ("mpi_blocking", "mpi_overlap"):
            values = sorted(
                (int(item["size"]), float(item["total_seconds"]))
                for item in summaries if item["version"] == version
            )
            if values:
                plt.plot([x for x, _ in values], [y for _, y in values], "o-",
                         label=version)
        plt.xscale("log", base=2)
        plt.yscale("log")
        plt.xlabel("Interior grid size N")
        plt.ylabel("Median time (s)")
        plt.title("Problem-size sensitivity: P=4")
        plt.grid(alpha=0.3, which="both")
        plt.legend()
        save_figure("size_sensitivity.png")

    matrix = median_rows(
        [row for row in rows if row["status"] == "fixed_iterations"],
        ("version", "size", "processes", "iterations"),
    )
    serial_by_size = {
        int(item["size"]): float(item["total_seconds"])
        for item in matrix if item["version"] == "serial"
    }
    matrix_sizes = sorted(serial_by_size)
    if len(matrix_sizes) >= 2:
        figure, axes = plt.subplots(1, 2, figsize=(11, 4.6))
        for version, linestyle in (("mpi_blocking", "-"), ("mpi_overlap", "--")):
            for size in matrix_sizes:
                values = sorted(
                    (int(item["processes"]),
                     serial_by_size[size] / float(item["total_seconds"]))
                    for item in matrix
                    if item["version"] == version and int(item["size"]) == size
                )
                if values:
                    label = f"{version.replace('mpi_', '')}, N={size}"
                    axes[0].plot([x for x, _ in values], [y for _, y in values],
                                 marker="o", linestyle=linestyle, label=label)
                    axes[1].plot([x for x, _ in values],
                                 [100.0 * y / x for x, y in values],
                                 marker="o", linestyle=linestyle, label=label)
        maximum_processes = max(int(item["processes"]) for item in matrix
                                if item["version"] != "serial")
        axes[0].plot([1, maximum_processes], [1, maximum_processes], "k:",
                     label="ideal")
        axes[1].axhline(100.0, color="black", linestyle=":", label="ideal")
        axes[0].set_ylabel("Speedup (serial / MPI)")
        axes[1].set_ylabel("Parallel efficiency (%)")
        for axis in axes:
            axis.set_xlabel("Processes")
            axis.grid(alpha=0.3)
            axis.legend(fontsize=8)
        figure.suptitle("Problem size and parallel scaling")
        save_figure("size_process_matrix.png")

    converged = [row for row in rows if row["status"] == "converged"]
    summaries = median_rows(converged, ("version", "size", "processes", "tolerance", "iterations"))
    tolerances = sorted({float(item["tolerance"]) for item in summaries}, reverse=True)
    if len(tolerances) >= 2:
        figure, axes = plt.subplots(1, 2, figsize=(10, 4.5))
        for version in ("mpi_blocking", "mpi_overlap"):
            values = sorted(
                ((float(item["tolerance"]), int(item["iterations"]),
                  float(item["total_seconds"]))
                 for item in summaries if item["version"] == version),
                reverse=True,
            )
            if values:
                labels = [f"{tol:.0e}" for tol, _, _ in values]
                axes[0].plot(labels, [iterations for _, iterations, _ in values],
                             "o-", label=version)
                axes[1].plot(labels, [seconds for _, _, seconds in values],
                             "o-", label=version)
        axes[0].set_title("Iterations to convergence")
        axes[0].set_ylabel("Iterations")
        axes[1].set_title("Runtime to convergence")
        axes[1].set_ylabel("Median time (s)")
        for axis in axes:
            axis.set_xlabel("Residual tolerance")
            axis.grid(alpha=0.3)
            axis.legend()
        figure.suptitle("Tolerance sensitivity")
        save_figure("tolerance_sensitivity.png")


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
    plot_sensitivities(raw)
    plot_convergence(convergence)
    plot_accuracy(accuracy)
    print(f"图表已写入 {FIGURES}")


if __name__ == "__main__":
    main()
