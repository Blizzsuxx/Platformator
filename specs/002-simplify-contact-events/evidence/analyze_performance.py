"""Compare equivalent schema-2/3 timing boundaries without relabeling event counters."""
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import statistics as stats

ROOT = Path(__file__).resolve().parents[3]
EVIDENCE = Path(__file__).resolve().parent
CAPTURES = ROOT / 'bin/contact-events-20261025'
SCOPES = ('complete_step', 'physics', 'cleanup', 'broad_phase', 'narrow_phase', 'resolve_collisions')


def read_stage(stage):
    folder = CAPTURES / stage / 'time'
    checksums = json.loads((folder / 'checksums.json').read_text())
    for name, digest in checksums.items():
        assert hashlib.sha256((folder / name).read_bytes()).hexdigest() == digest, (stage, name)
    result = {}
    hashes = set()
    for directory in sorted(path for path in folder.iterdir() if path.is_dir()):
        paths = sorted(directory.glob('run-*.csv'))
        paths = [path for path in paths if '.samples.' not in path.name]
        assert len(paths) == 5, (stage, directory)
        records = []
        for path in paths:
            manifest = json.loads(path.with_suffix('.manifest.json').read_text())
            assert manifest['schema_version'] == (2 if stage == 'baseline' else 3)
            assert manifest['timing_eligible'] and manifest['validation_mode'] == 'boundary-only'
            assert manifest['complete_capture'] and manifest['measured_steps'] == 600
            assert manifest['warmup_steps'] == 120 and manifest['repetitions'] == 5
            build = manifest['build']
            assert build['configuration'] == 'Release' and build['sanitizer'] == 'none'
            assert not build['work_probes'] and not build['debug_tools']
            hashes.add(build['source_sha256'])
            command = json.loads(path.with_suffix('.log').read_text().splitlines()[0])
            assert command[:3] == ['taskset', '-c', '0' if manifest['workers'] == 1 else '0,2,4,6,8,10']
            with path.open() as handle:
                metrics = {row['name']: row for row in csv.DictReader(handle) if row['category'] == 'scope'}
            records.append((metrics, manifest))
        summary = {}
        for scope in SCOPES:
            means = [float(record[0][scope]['avg']) for record in records]
            summary[scope] = {'mean_ms': stats.mean(means), 'stdev_ms': stats.stdev(means),
                              'run_min_ms': min(means), 'run_max_ms': max(means), 'run_means_ms': means,
                              'mean_p95_ms': stats.mean(float(record[0][scope]['p95']) for record in records),
                              'mean_p99_ms': stats.mean(float(record[0][scope]['p99']) for record in records)}
        result[directory.name] = {'scopes': summary, 'manifest': records[0][1]}
    assert len(result) == 54 and len(hashes) == 1, (stage, len(result), hashes)
    return result, hashes.pop()


def main():
    old, old_hash = read_stage('baseline')
    new, new_hash = read_stage('candidate')
    assert old.keys() == new.keys()
    rows = []
    for name in old:
        a, b = old[name], new[name]
        ma, mb = a['manifest'], b['manifest']
        for key in ('scenario', 'workers', 'seed', 'dt', 'resolved_parameters', 'boundaries', 'dependencies', 'machine'):
            assert ma[key] == mb[key], (name, key)
        for key in ('compiler', 'flags', 'configuration', 'sanitizer', 'work_probes', 'debug_tools'):
            assert ma['build'][key] == mb['build'][key], (name, key)
        before, after = a['scopes']['complete_step'], b['scopes']['complete_step']
        change = (after['mean_ms'] / before['mean_ms'] - 1) * 100
        overlap = max(before['run_min_ms'], after['run_min_ms']) <= min(before['run_max_ms'], after['run_max_ms'])
        row = {'capture': name, 'change_percent': change, 'run_ranges_overlap': overlap,
               'baseline': a['scopes'], 'candidate': b['scopes']}
        rows.append(row)
    report = {'baseline_source_sha256': old_hash, 'candidate_source_sha256': new_hash,
              'runs_per_group': 5, 'groups': rows}
    (EVIDENCE / 'performance-data.json').write_text(json.dumps(report, indent=2) + '\n')
    with (EVIDENCE / 'performance-comparison.csv').open('w') as handle:
        writer = csv.writer(handle)
        writer.writerow(['capture', 'baseline_complete_step_ms', 'candidate_complete_step_ms', 'change_percent',
                         'baseline_run_stdev_ms', 'candidate_run_stdev_ms', 'baseline_mean_p95_ms',
                         'candidate_mean_p95_ms', 'baseline_mean_p99_ms', 'candidate_mean_p99_ms', 'run_ranges_overlap'])
        for row in rows:
            a, b = row['baseline']['complete_step'], row['candidate']['complete_step']
            writer.writerow([row['capture'], a['mean_ms'], b['mean_ms'], row['change_percent'], a['stdev_ms'], b['stdev_ms'],
                             a['mean_p95_ms'], b['mean_p95_ms'], a['mean_p99_ms'], b['mean_p99_ms'], row['run_ranges_overlap']])
    lines = ['# Complete-step comparison', '',
             'Five independent process runs per group; 120 warmup and 600 measured steps; seed 0; dt 1/120. '
             'Same hardware, compiler/flags, dependencies, physical workload, worker affinity and scope boundaries verified. '
             'Both capture sets and raw checksums verified. Positive change means slower. Overlap describes observed '
             'run-mean ranges, not a significance test. Timing scopes are compared; changed event-counter semantics are not.', '',
             '| Workload / pairs or population / recipients / workers | Baseline µs | Candidate µs | Change | Run ranges overlap |',
             '| --- | ---: | ---: | ---: | --- |']
    for row in rows:
        lines.append(f"| {row['capture']} | {row['baseline']['complete_step']['mean_ms']*1000:.3f} | "
                     f"{row['candidate']['complete_step']['mean_ms']*1000:.3f} | {row['change_percent']:+.1f}% | "
                     f"{'yes' if row['run_ranges_overlap'] else 'no'} |")
    (EVIDENCE / 'performance-table.md').write_text('\n'.join(lines) + '\n')
    os.environ['MPLCONFIGDIR'] = str(CAPTURES / 'matplotlib-config')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    figure, axes = plt.subplots(2, 2, figsize=(9, 6), sharex=True)
    for ri, workers in enumerate((1, 6)):
        for ci, recipients in enumerate((1, 2)):
            axis = axes[ri, ci]
            for dataset, label, color, marker in ((old, 'Baseline with Stay', '#666666', 's'),
                                                   (new, 'Contact transitions', '#0072B2', 'o')):
                points = [dataset[f'persistent_contacts-n{count}-r{recipients}-w{workers}']['scopes']['complete_step']
                          for count in (10, 100, 1000)]
                axis.errorbar((10, 100, 1000), [p['mean_ms']*1000 for p in points],
                              yerr=[p['stdev_ms']*1000 for p in points], label=label, color=color, marker=marker,
                              capsize=3, linewidth=1.6)
            axis.set_xscale('log'); axis.set_yscale('log')
            axis.set_title(f'{recipients} scripted endpoint(s), {workers} worker(s)', fontsize=10)
            axis.set_xticks((10, 100, 1000), labels=('10', '100', '1000'))
            axis.grid(True, alpha=.2)
            if ci == 0: axis.set_ylabel('Complete step (µs)')
            if ri == 1: axis.set_xlabel('Persistent confirmed pairs')
    handles, labels = axes[0, 0].get_legend_handles_labels()
    figure.legend(handles, labels, loc='lower center', bbox_to_anchor=(.5, .025), ncol=2, frameon=False)
    figure.suptitle('Unchanged contacts: complete-step cost', fontsize=13)
    figure.text(.5, .012, 'Five independent runs; error bars: standard deviation of run means; fixed CPU affinity.',
                ha='center', fontsize=8)
    figure.tight_layout(rect=(0, .09, 1, .96))
    figure.savefig(EVIDENCE / 'persistent-contact-performance.png', dpi=180)
    figure.savefig(EVIDENCE / 'persistent-contact-performance.svg')
    for row in rows:
        if row['change_percent'] > 0 or 'persistent_contacts-n1000' in row['capture']:
            print(f"{row['capture']}: {row['change_percent']:+.2f}%, overlap={row['run_ranges_overlap']}")
    print('Compared 54 groups / 540 independent processes; source hashes', old_hash, new_hash)


if __name__ == '__main__':
    main()
