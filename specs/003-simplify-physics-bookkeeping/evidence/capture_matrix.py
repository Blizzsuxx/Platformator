"""Capture matched schema-3 physics-bookkeeping workloads without changing runner semantics."""
import argparse
import csv
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
from capture_cleanup import compare

CASES = {'broad_phase': (100, 1000), 'broad_phase_stress': (50, 300),
         'narrow_phase': (10, 200), 'rigid_body_container': (100, 1000),
         'add_and_remove': (10, 1000), 'moving_add_and_remove': (10, 1000)}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('validate', 'time'), required=True)
    parser.add_argument('--stage', choices=('baseline', 'candidate', 'both'), required=True)
    parser.add_argument('--baseline-runner', type=Path, required=True)
    parser.add_argument('--candidate-runner', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--multi-workers', type=int, default=6)
    parser.add_argument('--one-cpus', default='0')
    parser.add_argument('--multi-cpus', default='0,2,4,6,8,10')
    args = parser.parse_args()
    assert args.multi_workers > 1
    stages = ('baseline', 'candidate') if args.stage == 'both' else (args.stage,)
    runners = {'baseline': args.baseline_runner.resolve(), 'candidate': args.candidate_runner.resolve()}
    cases = [(case, count, 0) for case, counts in CASES.items() for count in counts]
    cases += [('persistent_contacts', count, recipients) for count in (10, 1000) for recipients in (0, 1, 2)]
    repeats = 1 if args.mode == 'validate' else 5
    jobs = [(case, count, recipients, workers, repeat) for case, count, recipients in cases
            for workers in (1, args.multi_workers) for repeat in range(repeats)]
    random.Random(1).shuffle(jobs)
    # Alternate stage order for each matched job; preserve this schedule, never overwrite captures.
    schedule = [(stage, *job) for index, job in enumerate(jobs)
                for stage in (stages if index % 2 == 0 else stages[::-1])]
    args.output_root.mkdir(parents=True, exist_ok=True)
    schedule_path = args.output_root / f'{args.stage}-{args.mode}-schedule.json'
    with schedule_path.open('x') as stream:
        json.dump({'shuffle_seed': 1, 'arguments': {k: str(v) for k, v in vars(args).items()},
                   'schedule': schedule}, stream, indent=2)
        stream.write('\n')
    groups = {stage: {} for stage in stages}
    fingerprints = {stage: set() for stage in stages}
    for index, (stage, case, count, recipients, workers, repeat) in enumerate(schedule):
        name = f'{case}-n{count}-r{recipients}-w{workers}'
        folder = args.output_root / stage / args.mode / name
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / f'run-{repeat}.csv'
        for suffix in ('.csv', '.samples.csv', '.log', '.manifest.json'):
            if path.with_suffix(suffix).exists():
                raise FileExistsError(path.with_suffix(suffix))
        spec = SimpleNamespace(scenario=case, scripted_endpoints=recipients, steady_count=1000,
                               box_mix=None, circle_mix=None)
        command = ['taskset', '-c', args.one_cpus if workers == 1 else args.multi_cpus,
                   str(runners[stage]), '--scenario', case, '--workers', str(workers), '--seed', '0',
                   '--warmup-frames', '120', '--measure-frames', '600', '--dt', str(1/120),
                   '--repetitions', str(repeats), '--run-index', str(repeat), '--csv-output', str(path)]
        command = sweep.apply_count_argument(command, spec, count)
        if args.mode == 'validate':
            command.append('--validate-invariants')
        environment = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')
        with path.with_suffix('.log').open('x') as log:
            log.write(json.dumps(command) + '\n'); log.flush()
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, env=environment, check=True)
        metrics = sweep.parse_benchmark_csv(path)
        manifest = sweep.load_manifest(path)
        build = manifest['build']
        assert build['configuration'] == 'Release' and build['sanitizer'] == 'none'
        assert not build['work_probes'] and not build['debug_tools']
        assert manifest['workers'] == workers and manifest['seed'] == 0
        assert manifest['measured_steps'] == 600 and manifest['warmup_steps'] == 120
        assert manifest['timing_eligible'] == (args.mode == 'time')
        assert manifest['schema_version'] == 3 and manifest['complete_capture']
        fingerprints[stage].add(build['source_sha256'])
        groups[stage].setdefault(name, []).append((metrics, manifest))
        if stage == 'candidate':
            oldpath = args.output_root/'baseline'/args.mode/name/f'run-{repeat}.manifest.json'
            if oldpath.exists():
                old = json.loads(oldpath.read_text())
                for field in ('scenario', 'workers', 'seed', 'dt', 'resolved_parameters', 'boundaries', 'dependencies', 'machine'):
                    assert manifest[field] == old[field], (name, field)
        print(f'{args.mode} {index+1}/{len(schedule)} {stage}: {name} repeat={repeat}', flush=True)
    for stage in stages:
        assert len(groups[stage]) == 36 and len(fingerprints[stage]) == 1
        for records in groups[stage].values():
            assert len(records) == repeats
        if stage == 'candidate':
            for name in groups[stage]:
                for repeat in range(repeats):
                    relative = Path(args.mode)/name/f'run-{repeat}.manifest.json'
                    old = json.loads((args.output_root/'baseline'/relative).read_text())
                    new = json.loads((args.output_root/'candidate'/relative).read_text())
                    for field in ('scenario', 'workers', 'seed', 'dt', 'resolved_parameters', 'boundaries', 'dependencies', 'machine'):
                        assert old[field] == new[field], (name, field)
                    for field in ('compiler', 'flags', 'configuration', 'sanitizer', 'work_probes', 'debug_tools'):
                        assert old['build'][field] == new['build'][field], (name, field)
        if args.mode == 'validate':
            for case, count, recipients in cases:
                name = f'{case}-n{count}-r{recipients}'
                compare(groups[stage][name+'-w1'][0][1]['physical_invariants'],
                        groups[stage][name+f'-w{args.multi_workers}'][0][1]['physical_invariants'])
            if stage == 'candidate':
                for name, records in groups[stage].items():
                    old = json.loads((args.output_root/'baseline'/'validate'/name/'run-0.manifest.json').read_text())
                    compare(old['physical_invariants'], records[0][1]['physical_invariants'])
        else:
            rows = []
            for name, records in sorted(groups[stage].items()):
                manifest = records[0][1]
                spec = SimpleNamespace(scenario=manifest['scenario'], workers=manifest['workers'], seed=0,
                                       steady_count=1000, validate_invariants=False,
                                       scripted_endpoints=manifest['resolved_parameters'].get('scripted_endpoints', 0))
                count = int(name.split('-n')[1].split('-')[0])
                row = sweep.build_result_row(spec, count, [record[0] for record in records]); row['capture'] = name
                rows.append(row)
            with (args.output_root/stage/args.mode/'aggregate.csv').open('x') as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
        folder = args.output_root/stage/args.mode
        checksums = {str(p.relative_to(folder)): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in sorted(folder.rglob('*')) if p.is_file()}
        with (folder/'checksums.json').open('x') as stream:
            json.dump(checksums, stream, indent=2);stream.write('\n')
    print('COMPLETED', args.stage, args.mode, fingerprints, flush=True)

if __name__ == '__main__':
    main()
