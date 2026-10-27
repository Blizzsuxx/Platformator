#!/usr/bin/env python3
"""Alternate matched baseline/candidate timing jobs; retain full raw outputs."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument('--baseline', required=True, type=Path)
p.add_argument('--candidate', required=True, type=Path)
p.add_argument('--out', required=True, type=Path)
p.add_argument('--workers', default='1,6')
p.add_argument('--single-cpus', default='0')
p.add_argument('--multi-cpus', default='0,2,4,6,8,10')
p.add_argument('--warmup', type=int, default=120)
p.add_argument('--steps', type=int, default=600)
p.add_argument('--repetitions', type=int, default=5)
a = p.parse_args()
base, cand, out = a.baseline.resolve(), a.candidate.resolve(), a.out.resolve()
out.mkdir(parents=True, exist_ok=False)
groups = [('broad_phase','100,1000'), ('broad_phase_stress','50,300'), ('narrow_phase','10,200'),
          ('rigid_body_container','100,1000'), ('persistent_contacts','10,1000'),
          ('add_and_remove','10,100,1000'), ('moving_add_and_remove','10,100,1000')]
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')
schedule = []
def execute(label, stage, command, source):
    print(stage, label, flush=True)
    directory = out/stage
    directory.mkdir(exist_ok=True)
    with (directory/(label+'.log')).open('w') as log:
        status = subprocess.run(command,cwd=source,env=env,stdout=log,stderr=subprocess.STDOUT).returncode
    schedule.append({'stage':stage,'label':label,'returncode':status,'command':command})
    (out/'schedule.json').write_text(json.dumps(schedule,indent=2)+'\n')
    if status: raise SystemExit(f'{stage} {label} failed ({status})')
index=0
for workers in map(int,a.workers.split(',')):
    cpu=a.single_cpus if workers == 1 else a.multi_cpus
    for scenario,counts in groups:
        label=f'{scenario}-w{workers}'
        stages=[('baseline',base),('candidate',cand)]
        if index % 2: stages.reverse()
        for stage,source in stages:
            d=out/stage
            command=['taskset','-c',cpu,sys.executable,str(source/'benchmark/sweep_scaling.py'),
                     '--runner',str(source/'bin/cleanup-benchmark/platformator_benchmark_runner'),
                     '--scenario',scenario,'--counts',counts,'--workers',str(workers),'--seed','0',
                     '--dt','0.008333333333333333','--warmup-frames',str(a.warmup),'--measure-frames',str(a.steps),
                     '--repetitions',str(a.repetitions),'--raw-output-dir',str(d/label),'--output',str(d/(label+'.csv'))]
            execute(label,stage,command,source)
        index+=1
    for count in (10,1000):
        for repetition in range(a.repetitions):
            label=f'pair-churn-n{count}-w{workers}-r{repetition}'
            stages=[('baseline',base),('candidate',cand)]
            if index % 2: stages.reverse()
            for stage,source in stages:
                command=['taskset','-c',cpu,str(source/'bin/cleanup-benchmark/platformator_dead_state_pair_churn_probe'),
                         str(count),str(workers),str(a.warmup),str(a.steps),'0',str(out/stage/(label+'.csv'))]
                execute(label,stage,command,source)
            index+=1
print('paired capture complete',len(schedule),flush=True)
