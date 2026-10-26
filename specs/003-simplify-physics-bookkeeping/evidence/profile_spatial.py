"""Bounded perf profiles and separate matched spatial-control measurements.

Run phases separately; profiles/stat timings are never performance comparisons.
Uses the unmodified preserved runners and existing strict schema-3 readers.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import resource
import subprocess
import sys
from types import SimpleNamespace

PROJECT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(PROJECT / 'benchmark'))
import sweep_scaling as sweep
from capture_cleanup import compare

CASES = [('broad_phase', 100, 6), ('broad_phase_stress', 50, 6), ('broad_phase_stress', 50, 1)]
SOURCES = {'baseline': PROJECT / 'bin/physics-bookkeeping-20261025/baseline-source', 'candidate': PROJECT}
HASHES = {'baseline': '5ca76cf38d163c353254d4f74f286888be050a32f1c88b7215c25b44faff1031',
          'candidate': 'b87bc7a61c539405ff6e77fc8f481a3fd2c6ad3510f2b089ca4c475dd27367db'}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase', choices=('profile', 'stat', 'cache', 'validate', 'time'), required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--resume', action='store_true', help='Reuse completed captures after a harness-only interruption')
    args = parser.parse_args()
    output = args.root.resolve() / args.phase
    output.mkdir(exist_ok=args.resume)
    # Extended trajectories are new matched controls, not the original 600-step workload.
    lengths = [60000] if args.phase == 'profile' else [6000] if args.phase in ('stat', 'cache', 'validate') else [600, 6000]
    repeats = {'profile': 1, 'stat': 5, 'cache': 5, 'validate': 1, 'time': 10}[args.phase]
    jobs = [(case, count, workers, frames, repeat) for case, count, workers in CASES
            for frames in lengths for repeat in range(repeats)]
    random.Random(26).shuffle(jobs)
    schedule = [(stage, *job) for i, job in enumerate(jobs)
                for stage in (('baseline', 'candidate') if i % 2 == 0 else ('candidate', 'baseline'))]
    metadata = {'phase': args.phase, 'shuffle_seed': 26, 'schedule': schedule,
                'instrumented_perf': args.phase in ('profile', 'stat', 'cache'),
                'performance_comparison_eligible': args.phase == 'time',
                'warmup': 120, 'sources': {k: str(v) for k, v in SOURCES.items()},
                'expected_source_hashes': HASHES, 'commands': [], 'child_usage': []}
    records = {}
    for i, (stage, case, count, workers, frames, repeat) in enumerate(schedule):
        folder = output / stage / f'{case}-n{count}-w{workers}-steps{frames}'
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / f'run-{repeat}.csv'
        runner = SOURCES[stage] / 'bin/cleanup-benchmark/platformator_benchmark_runner'
        cpus = '0' if workers == 1 else '0,2,4,6,8,10'
        command = [str(runner), '--scenario', case, '--workers', str(workers), '--seed', '0',
                   '--warmup-frames', '120', '--measure-frames', str(frames), '--dt', str(1/120),
                   '--repetitions', str(repeats), '--run-index', str(repeat), '--csv-output', str(path)]
        command = sweep.apply_count_argument(command, SimpleNamespace(scenario=case, scripted_endpoints=0,
                                                 steady_count=1000, box_mix=None, circle_mix=None), count)
        if args.phase == 'validate': command.append('--validate-invariants')
        if args.phase == 'profile':
            command = ['perf', 'record', '-e', 'cycles:u', '-F', '499', '--call-graph', 'dwarf,8192',
                       '-o', str(path.with_suffix('.perf')), '--', *command]
        if args.phase in ('stat', 'cache'):
            command = ['perf', 'stat', '-x', ',', '-o', str(path.with_suffix('.stat.csv')),
                       '-e', ('instructions:u,L1-icache-load-misses:u,L1-dcache-load-misses:u,cache-misses:u' if args.phase == 'cache' else 'task-clock,cycles:u,instructions:u,branches:u,branch-misses:u,context-switches,cpu-migrations'),
                       '--', *command]
        command = ['taskset', '-c', cpus, *command]
        metadata['commands'].append(command)
        usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
        if not (args.resume and path.with_suffix('.manifest.json').exists()):
            with path.with_suffix('.log').open('x') as log:
                log.write(json.dumps(command) + '\n'); log.flush()
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True,
                               env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software'))
        usage_after = resource.getrusage(resource.RUSAGE_CHILDREN)
        metadata['child_usage'].append({'capture': str(path.relative_to(output)), **{field: getattr(usage_after, field) - getattr(usage_before, field) for field in ('ru_utime', 'ru_stime', 'ru_nvcsw', 'ru_nivcsw')}})
        sweep.parse_benchmark_csv(path)
        manifest = sweep.load_manifest(path)
        assert manifest['build']['source_sha256'] == HASHES[stage]
        assert manifest['build']['configuration'] == 'Release' and manifest['build']['sanitizer'] == 'none'
        assert not manifest['build']['debug_tools'] and not manifest['build']['work_probes']
        assert manifest['workers'] == workers and manifest['warmup_steps'] == 120 and manifest['measured_steps'] == frames
        assert manifest['timing_eligible'] == (args.phase != 'validate')
        key = (case, count, workers, frames, repeat)
        previous = records.get(key)
        if previous:
            for field in ('scenario', 'workers', 'seed', 'dt', 'resolved_parameters', 'boundaries', 'dependencies', 'machine'):
                assert manifest[field] == previous[field], (key, field)
            for field in ('compiler', 'flags', 'configuration', 'sanitizer', 'work_probes', 'debug_tools'):
                assert manifest['build'][field] == previous['build'][field], (key, field)
            if args.phase == 'validate':
                compare(manifest['physical_invariants'], previous['physical_invariants'])
            else:
                assert manifest['physical_invariants'] == previous['physical_invariants']
        else: records[key] = manifest
        if args.phase == 'profile':
            for suffix, options in (('.self.txt', ['--no-children']), ('.stacks.txt', ['--children'])):
                if args.resume and path.with_suffix(suffix).exists(): continue
                with path.with_suffix(suffix).open('x') as report:
                    subprocess.run(['perf', 'report', '-i', str(path.with_suffix('.perf')), '--stdio',
                                    '--percent-limit', '0.5', *options], stdout=report, stderr=subprocess.STDOUT, check=True)
        print(f'{args.phase} {i+1}/{len(schedule)} {stage} {folder.name} r{repeat}', flush=True)
    (output / 'provenance.json').write_text(json.dumps(metadata, indent=2) + '\n')
    checks = {str(p.relative_to(output)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in sorted(output.rglob('*')) if p.is_file()}
    (output / 'checksums.json').write_text(json.dumps(checks, indent=2) + '\n')
    print('COMPLETED', args.phase, len(schedule), 'matched processes', flush=True)

if __name__ == '__main__': main()
