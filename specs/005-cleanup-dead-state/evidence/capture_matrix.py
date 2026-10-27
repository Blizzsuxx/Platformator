#!/usr/bin/env python3
"""Capture matching schema-3 sweeps and validated pair churn for feature 005."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument('--source', required=True, type=Path)
p.add_argument('--out', required=True, type=Path)
p.add_argument('--mode', required=True, choices=['validate', 'time'])
p.add_argument('--workers', default='1,6')
p.add_argument('--single-cpus', default='0')
p.add_argument('--multi-cpus', default='0,2,4,6,8,10')
p.add_argument('--warmup', type=int, default=120)
p.add_argument('--steps', type=int, default=600)
p.add_argument('--repetitions', type=int, default=5)
a = p.parse_args()
source = a.source.resolve()
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=False)
runner = source/'bin/cleanup-benchmark/platformator_benchmark_runner'
probe = source/'bin/cleanup-benchmark/platformator_dead_state_pair_churn_probe'
sweep = source/'benchmark/sweep_scaling.py'
groups = [('broad_phase','100,1000'), ('broad_phase_stress','50,300'),
          ('narrow_phase','10,200'), ('rigid_body_container','100,1000'),
          ('persistent_contacts','10,1000'), ('add_and_remove','10,100,1000'),
          ('moving_add_and_remove','10,100,1000')]
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')
entries = []
for workers in map(int, a.workers.split(',')):
    cpu = a.single_cpus if workers == 1 else a.multi_cpus
    for scenario, counts in groups:
        label = f'{scenario}-w{workers}'
        command = ['taskset','-c',cpu,sys.executable,str(sweep),'--runner',str(runner),
                   '--scenario',scenario,'--counts',counts,'--workers',str(workers),'--seed','0',
                   '--dt','0.008333333333333333','--warmup-frames',str(a.warmup),'--measure-frames',str(a.steps),
                   '--repetitions',str(1 if a.mode == 'validate' else a.repetitions),
                   '--raw-output-dir',str(out/label),'--output',str(out/(label+'.csv'))]
        if a.mode == 'validate': command.append('--validate-invariants')
        print(label, flush=True)
        with (out/(label+'.log')).open('w') as log:
            status = subprocess.run(command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT).returncode
        entries.append({'label':label,'status':status,'command':command})
        (out/'schedule.json').write_text(json.dumps(entries,indent=2)+'\n')
        if status: raise SystemExit(f'{label} failed ({status}); see log')
    for count in (10,1000):
        for repetition in range(1 if a.mode == 'validate' else a.repetitions):
            label = f'pair-churn-n{count}-w{workers}-r{repetition}'
            command = ['taskset','-c',cpu,str(probe),str(count),str(workers),str(a.warmup),str(a.steps),
                       '1' if a.mode == 'validate' else '0',str(out/(label+'.csv'))]
            print(label, flush=True)
            with (out/(label+'.log')).open('w') as log:
                status = subprocess.run(command,cwd=source,env=env,stdout=log,stderr=subprocess.STDOUT).returncode
            entries.append({'label':label,'status':status,'command':command})
            (out/'schedule.json').write_text(json.dumps(entries,indent=2)+'\n')
            if status: raise SystemExit(f'{label} failed ({status}); see log')
print('capture complete',len(entries),flush=True)
