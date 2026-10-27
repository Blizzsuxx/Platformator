#!/usr/bin/env python3
"""Alternate extra paired persistent-contact processes to investigate timing drift."""
import json
import os
from pathlib import Path
import subprocess
import sys

base, cand, out = map(Path, sys.argv[1:4])
out.mkdir(parents=True, exist_ok=False)
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')
schedule=[]
for workers, cpus in ((1,'0'),(6,'0,2,4,6,8,10')):
    for rep in range(5):
        order=[('baseline',base),('candidate',cand)]
        if rep % 2: order.reverse()
        for label, source in order:
            name=f'{label}-w{workers}-r{rep}'
            command=['taskset','-c',cpus,str(source/'bin/cleanup-benchmark/platformator_benchmark_runner'), '--scenario','persistent_contacts','--contact-pair-count','1000','--scripted-endpoints','2','--workers',str(workers),'--seed','0','--dt','0.008333333333333333','--warmup-frames','120','--measure-frames','600','--repetitions','5','--run-index',str(rep),'--csv-output',str(out/(name+'.csv'))]
            with (out/(name+'.log')).open('w') as log:
                status=subprocess.run(command,cwd=source,env=env,stdout=log,stderr=subprocess.STDOUT).returncode
            schedule.append({'label':name,'command':command,'returncode':status})
            (out/'schedule.json').write_text(json.dumps(schedule,indent=2)+'\n')
            if status: raise SystemExit(f'{name} failed ({status})')
