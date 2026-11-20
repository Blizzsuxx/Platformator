import statistics as stats


def collect_diagnostics(run, report, save):
    for case in ("single_container", "eight_containers"):
        _, rows = run(case, "graph", 0)
        latest = rows[-300:]
        constraints = sum(row["writable_constraints"] for row in rows)
        report["topology"][case] = {
            "frames": len(rows),
            "frames_majority_island": sum(2 * row["largest_island"] > row["writable_constraints"] for row in rows),
            "largest_constraint_fraction_weighted": sum(row["largest_island"] for row in rows) / constraints,
            "largest_constraint_fraction_last_300": sum(row["largest_island"] for row in latest) /
                                                    sum(row["writable_constraints"] for row in latest),
            "six_worker_ideal_iteration_speedup_equal_constraint_cost":
                constraints / sum(max(row["largest_island"], row["writable_constraints"] / 6) for row in rows),
            "medians_last_300": {key: stats.median(row[key] for row in latest) for key in (
                "constraints", "largest_island", "writable_constraints", "greedy_colors", "greedy_overflow",
                "largest_color", "ordered_levels", "largest_level")},
        }
        save()
    for case in ("trigger_validation", "single_container"):
        stock, _ = run(case, "serial", 0, validation=True)
        checked, _ = run(case, "short", 0, validation=True)
        results = {key: stock[key] == checked[key] for key in ("snapshot_sha256", "counter_sha256")}
        report["validation"][case] = results
        save()
        assert all(results.values()), (case, results)
