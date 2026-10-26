"""Fresh matched follow-ups for the larger/affected primary positive deltas; originals remain untouched."""
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

PROJECT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(PROJECT/'benchmark'))
import sweep_scaling as sweep
parser=argparse.ArgumentParser();parser.add_argument('--variant',choices=('initial','final'),default='initial');args=parser.parse_args()
ROOT=PROJECT/('bin/physics-bookkeeping-20261025/followup-time' if args.variant=='initial' else 'bin/physics-bookkeeping-20261025/followup-final')
ROOT.mkdir(exist_ok=False)
CASES=[('rigid_body_container',100,0,6),('add_and_remove',1000,0,1),
       ('broad_phase_stress',300,0,1),('moving_add_and_remove',1000,0,1),
       ('moving_add_and_remove',1000,0,6),('persistent_contacts',1000,2,6)]
if args.variant=='final':
    CASES=[('persistent_contacts',1000,1,1),('rigid_body_container',1000,0,1),
           ('moving_add_and_remove',1000,0,6),('broad_phase_stress',50,0,1),
           ('broad_phase_stress',50,0,6),('broad_phase',100,0,6)]
jobs=[(*case,repeat) for case in CASES for repeat in range(5)]
random.Random(2).shuffle(jobs)
schedule=[(stage,*job) for index,job in enumerate(jobs)
          for stage in (('baseline','candidate') if index%2==0 else ('candidate','baseline'))]
(ROOT/'schedule.json').write_text(json.dumps({'shuffle_seed':2,'schedule':schedule},indent=2)+'\n')
for index,(stage,case,count,recipients,workers,repeat) in enumerate(schedule):
    source=PROJECT/'bin/physics-bookkeeping-20261025/baseline-source' if stage=='baseline' else PROJECT
    folder=ROOT/stage/f'{case}-n{count}-r{recipients}-w{workers}';folder.mkdir(exist_ok=True,parents=True)
    path=folder/f'run-{repeat}.csv'
    cmd=['taskset','-c','0' if workers==1 else '0,2,4,6,8,10',str(source/'bin/cleanup-benchmark/platformator_benchmark_runner'),
         '--scenario',case,'--workers',str(workers),'--seed','0','--warmup-frames','120','--measure-frames','600',
         '--dt',str(1/120),'--repetitions','5','--run-index',str(repeat),'--csv-output',str(path)]
    cmd=sweep.apply_count_argument(cmd,SimpleNamespace(scenario=case,scripted_endpoints=recipients,steady_count=1000,box_mix=None,circle_mix=None),count)
    with path.with_suffix('.log').open('x') as log:
        log.write(json.dumps(cmd)+'\n');log.flush()
        subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software'),check=True)
    sweep.parse_benchmark_csv(path)
    print(f'Follow-up {index+1}/60 {stage} {folder.name} r{repeat}',flush=True)
checks={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(ROOT.rglob('*')) if p.is_file()}
(ROOT/'checksums.json').write_text(json.dumps(checks,indent=2)+'\n')
print('COMPLETED60 matched follow-up processes',flush=True)
