#!/usr/bin/env python3
"""Capture driver; timing phases must run after builds/tests and without competing benchmark jobs."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('suite', choices=('calibration', 'engine', 'replay', 'controls', 'startup'))
parser.add_argument('mode', choices=('validate', 'performance', 'resource'))
args = parser.parse_args()
folder = HERE / args.suite
output = folder / {'validate':'validation','performance':'timing','resource':'resources'}[args.mode]
runner = ROOT / 'bin/cleanup-benchmark' / ('platformator_spatial_resource_runner' if args.mode == 'resource' else 'platformator_benchmark_runner')
command = [sys.executable, str(ROOT/'benchmark/sweep_spatial.py'), '--runner', str(runner),
           '--cases', str(folder/'cases/cases.json'), '--worker-counts', '1,6', '--available-workers', '6',
           '--repetitions', '5' if args.mode == 'performance' else '1', '--capture-mode', args.mode,
           '--bvh-maintenances', 'native-partial,full-rebuild' if args.suite == 'calibration' else 'native-partial',
           '--bvh-paddings', '0,2' if args.suite == 'calibration' else '0',
           '--bvh-query-executions', 'serial,parallel' if args.suite == 'calibration' else 'serial',
           '--output-dir', str(output)]
if args.suite == 'controls': command += ['--broad-phases', 'segmented', '--render-cullings', 'none']
if args.mode == 'performance': command += ['--validation-dir', str(folder/'validation')]
if args.mode == 'resource': command += ['--timing-dir', str(folder/'timing')]
environment = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software', SDL_AUDIODRIVER='dummy')
(folder/(args.mode+'-command.txt')).write_text('SDL_VIDEODRIVER=offscreen SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy '+shlex.join(command)+'\n')
started = time.time()
print('Starting', args.suite, args.mode, flush=True)
with (folder/(args.mode+'.log')).open('w') as log:
    result = subprocess.run(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
(folder/(args.mode+'-status.json')).write_text(json.dumps({'returncode':result.returncode, 'elapsed_seconds':time.time()-started, 'command':command},indent=2)+'\n')
print('Finished', args.suite, args.mode, 'status', result.returncode, 'seconds', round(time.time()-started,2), flush=True)
sys.exit(result.returncode)
