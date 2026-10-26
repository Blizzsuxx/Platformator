#!/usr/bin/env python3
"""Sequential matched runtime-cleanup captures; never overwrite a prior run."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import sys
from types import SimpleNamespace

PROJECT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(PROJECT / 'benchmark'))
import sweep_scaling as sweep

MODES = ('quiet', 'mixed', 'wake_burst', 'connected', 'hierarchy', 'lifecycle')
CASES = {'broad_phase': (100, 1000), 'broad_phase_stress': (50, 300),
         'rigid_body_container': (100, 1000), 'add_and_remove': (10, 1000),
         'moving_add_and_remove': (10, 1000)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-runner', type=Path)
    parser.add_argument('--candidate-runner', type=Path)
    parser.add_argument('--baseline-probe', type=Path)
    parser.add_argument('--candidate-probe', type=Path)
    parser.add_argument('--stage', choices=('baseline', 'candidate', 'both'), default='both')
    parser.add_argument('--mode', choices=('validate', 'time'), default='validate')
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--workers', default='1,2,6')
    parser.add_argument('--cpus', default='0,2,4,6,8,10')
    parser.add_argument('--list-jobs', action='store_true')
    parser.add_argument('--repetitions', type=int)
    parser.add_argument('--only', help='Comma-separated workload/scenario names for focused runs')
    args = parser.parse_args()
    workers = sweep.parse_counts(args.workers)
    cpus = [int(x) for x in args.cpus.split(',')]
    if len(set(cpus)) != len(cpus) or min(cpus) < 0 or max(workers) > len(cpus):
        raise ValueError('Need distinct available CPU choices for requested workers')
    if not set(cpus) <= os.sched_getaffinity(0):
        raise ValueError('CPU choice is outside process affinity')
    repeats = args.repetitions or (1 if args.mode == 'validate' else 5)
    if repeats <= 0 or (args.mode == 'validate' and repeats != 1):
        raise ValueError('Invalid repetitions')
    only = set(args.only.split(',')) if args.only else None
    cases = [('probe', name, n, 0) for name in MODES for n in (100, 1000)]
    cases += [('runner', name, n, 0) for name, counts in CASES.items() for n in counts]
    cases += [('runner', 'persistent_contacts', n, r) for n in (10, 1000) for r in (0, 1, 2)]
    if only:
        if not only <= {case[1] for case in cases}:
            raise ValueError('Unknown workload filter')
        cases = [case for case in cases if case[1] in only]
    stages = ('baseline', 'candidate') if args.stage == 'both' else (args.stage,)
    jobs = [(kind, case, count, recipients, w, rep) for kind, case, count, recipients in cases
            for w in workers for rep in range(repeats)]
    random.Random(1).shuffle(jobs)
    schedule = [(stage, *job) for i, job in enumerate(jobs)
                for stage in (stages if i % 2 == 0 else stages[::-1])]
    metadata = {'arguments': {k: str(v) for k, v in vars(args).items()}, 'schedule': schedule,
                'driver_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'shuffle_seed': 1}
    if args.list_jobs:
        print(json.dumps(metadata, indent=2))
        return
    args.output_root.mkdir(parents=True, exist_ok=False)
    (args.output_root / 'schedule.json').write_text(json.dumps(metadata, indent=2) + '\n')
    groups = {}
    fingerprints = {}
    for i, (stage, kind, case, count, recipients, w, rep) in enumerate(schedule):
        binary = getattr(args, stage + '_' + kind)
        if binary is None or not binary.is_file():
            raise FileNotFoundError(f'Missing {stage} {kind}')
        group = f'{kind}-{case}-n{count}-r{recipients}-w{w}'
        folder = args.output_root / stage / group
        folder.mkdir(parents=True, exist_ok=True)
        output = folder / f'run-{rep}.csv'
        command = ['taskset', '-c', ','.join(map(str, cpus[:w])), str(binary.resolve())]
        if kind == 'probe':
            command += ['--workload', case, '--count', str(count)]
        else:
            command += ['--scenario', case, '--dt', str(1/120), '--repetitions', str(repeats),
                        '--run-index', str(rep)]
            spec = SimpleNamespace(scenario=case, scripted_endpoints=recipients, steady_count=1000,
                                   box_mix=None, circle_mix=None)
            command = sweep.apply_count_argument(command, spec, count)
        command += ['--workers', str(w), '--seed', '0', '--warmup-frames', '120',
                    '--measure-frames', '600', '--csv-output', str(output)]
        if args.mode == 'validate':
            command.append('--validate-invariants')
        environment = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')
        with output.with_suffix('.log').open('x') as stream:
            result = subprocess.run(command, cwd=binary.resolve().parent, env=environment,
                                    stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f'{group}/{stage} failed ({result.returncode}); see {output.with_suffix(".log")}')
        metrics = sweep.parse_benchmark_csv(output)
        manifest = sweep.load_manifest(output)
        build = manifest['build']
        if build['configuration'] != 'Release' or build['sanitizer'] != 'none' or build['work_probes'] or build['debug_tools']:
            raise ValueError('Invalid performance build')
        fingerprint = build['source_sha256']
        if fingerprints.setdefault(stage, fingerprint) != fingerprint:
            raise ValueError('Stage source changed during capture')
        expected_case = 'runtime_' + case if kind == 'probe' else case
        if (manifest['scenario'] != expected_case or manifest['workers'] != w or manifest['seed'] != 0
                or manifest['dt'] != 1/120 or manifest['warmup_steps'] != 120 or manifest['measured_steps'] != 600
                or manifest['timing_eligible'] != (args.mode == 'time')):
            raise ValueError('Capture metadata differs from job')
        entry = {'metrics': metrics, 'physical': manifest['physical_invariants'],
                 'parameters': manifest['resolved_parameters'], 'build': build}
        groups.setdefault(group, {}).setdefault(stage, []).append(entry)
        print(f'{i+1}/{len(schedule)} {stage} {group} rep={rep}', flush=True)
    for group, stages_data in groups.items():
        if len(stages_data) == 2:
            old, new = stages_data['baseline'][0], stages_data['candidate'][0]
            if old['parameters'] != new['parameters']:
                raise ValueError(f'Fixture parameter mismatch: {group}')
            keys = ('objects', 'awake', 'sleeping', 'contacts', 'destroyed') if group.startswith('probe-') else ('objects', 'colliders', 'bodies')
            for key in keys:
                if old['physical'].get(key) != new['physical'].get(key):
                    raise ValueError(f'Population/controlled contract mismatch: {group}/{key}')
    (args.output_root / 'results.json').write_text(json.dumps({'groups': groups, 'fingerprints': fingerprints}, indent=2)+'\n')
    checksums = {str(p.relative_to(args.output_root)): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in sorted(args.output_root.rglob('*')) if p.is_file()}
    (args.output_root / 'checksums.json').write_text(json.dumps(checksums, indent=2)+'\n')


if __name__ == '__main__':
    main()
