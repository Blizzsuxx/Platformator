"""Reproduce contact-event baseline/candidate with matching workload and timing boundaries."""
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

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'benchmark'))
import sweep_scaling as sweep
from capture_cleanup import CASES, compare


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--stage', required=True, choices=('baseline', 'candidate'))
    parser.add_argument('--mode', required=True, choices=('validate', 'time'))
    args = parser.parse_args()
    destination = ROOT / 'bin/contact-events-20261025' / args.stage / args.mode
    destination.mkdir(parents=True, exist_ok=True)
    runner = ROOT / 'bin/cleanup-benchmark/platformator_benchmark_runner'
    cases = [(case, count, 0) for case, counts in CASES.items() for count in counts]
    cases += [('persistent_contacts', count, recipients) for count in (10, 100, 1000) for recipients in (0, 1, 2)]
    jobs = [(case, count, recipients, workers, repetition) for case, count, recipients in cases
            for workers in (1, 6) for repetition in range(1 if args.mode == 'validate' else 5)]
    random.Random(1).shuffle(jobs)
    groups = {}
    for index, (case, count, recipients, workers, repetition) in enumerate(jobs):
        name = f'{case}-n{count}-r{recipients}-w{workers}'
        directory = destination / name
        directory.mkdir(exist_ok=True)
        path = directory / f'run-{repetition}.csv'
        if path.exists():
            raise FileExistsError(path)
        spec = SimpleNamespace(scenario=case, scripted_endpoints=recipients, steady_count=1000,
                               box_mix=None, circle_mix=None)
        command = ['taskset', '-c', '0' if workers == 1 else '0,2,4,6,8,10', str(runner),
                   '--scenario', case, '--workers', str(workers), '--seed', '0',
                   '--warmup-frames', '120', '--measure-frames', '600', '--dt', str(1/120),
                   '--repetitions', '1' if args.mode == 'validate' else '5', '--run-index', str(repetition),
                   '--csv-output', str(path)]
        command = sweep.apply_count_argument(command, spec, count)
        if args.mode == 'validate':
            command.append('--validate-invariants')
        environment = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')
        with path.with_suffix('.log').open('w') as log:
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
        groups.setdefault(name, []).append((metrics, manifest))
        print(f'{args.stage}/{args.mode} {index+1}/{len(jobs)}: {name} repeat={repetition}', flush=True)
    if args.mode == 'validate':
        for case, count, recipients in cases:
            base = f'{case}-n{count}-r{recipients}'
            compare(groups[base+'-w1'][0][1]['physical_invariants'], groups[base+'-w6'][0][1]['physical_invariants'])
            if args.stage == 'candidate':
                for workers in (1, 6):
                    old = ROOT/'bin/contact-events-20261025/baseline/validate'/f'{base}-w{workers}'/'run-0.manifest.json'
                    compare(json.loads(old.read_text())['physical_invariants'], groups[f'{base}-w{workers}'][0][1]['physical_invariants'])
    else:
        rows = []
        for name, records in sorted(groups.items()):
            manifest = records[0][1]
            spec = SimpleNamespace(scenario=manifest['scenario'], workers=manifest['workers'], seed=0,
                                   steady_count=1000, validate_invariants=False,
                                   scripted_endpoints=manifest['resolved_parameters'].get('scripted_endpoints',0))
            count = int(name.split('-n')[1].split('-')[0])
            row = sweep.build_result_row(spec, count, [record[0] for record in records])
            row['capture'] = name
            rows.append(row)
        with (destination/'aggregate.csv').open('w') as output:
            writer=csv.DictWriter(output, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    checksums = {str(p.relative_to(destination)): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in sorted(destination.rglob('*')) if p.is_file()}
    (destination/'checksums.json').write_text(json.dumps(checksums, indent=2)+'\n')
    print('COMPLETED', destination, flush=True)

if __name__ == '__main__':
    main()
