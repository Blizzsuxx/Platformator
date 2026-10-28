#!/usr/bin/env python3
"""Retained production-ingress checks for uniform zoom and retained multi-cell pairs."""
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
OUT=HERE/'verification/edge-cases'
OUT.mkdir(parents=True,exist_ok=True)
GENERATOR={'name':Path(__file__).name,'revision':'1','source_hash':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}

def identity(slot,generation=1):return {'slot':str(slot),'generation':str(generation)}
def trace(name,domain,initial,ops,population):
    header={'type':'header','format':'platformator-spatial-trace','version':1,'domain':domain,
            'case_id':name,'seed':0,'units':'world','dt':1/120,'warmup_steps':0,'measure_steps':len(ops),
            'generator':GENERATOR,'parameters':{'population':population,'bounds_source':'provided-aabb' if domain=='collision' else 'sprite-transform'}}
    if domain=='visibility':header['parameters']['renderer']='none-replay'
    records=[header,{'type':'initial',**initial}]+[{'type':'step','index':i,'ops':step} for i,step in enumerate(ops)]
    path=OUT/(name+'.jsonl')
    path.write_text(''.join(json.dumps(r,separators=(',',':'))+'\n' for r in records))
    return path

view=lambda size:{'id':identity(99),'position':[0,0],'size':[size,size],'output':[1600,900]}
zoom=trace('uniform-zoom','visibility',{'sprites':[{'id':identity(1),'center':[5,5],'dimensions':[2,2],'scale':[1,1],'angle_degrees':0,'active':True}],'view':view(10)},
           [[{'op':'set_view','view':view(size)}] for size in [20,10]],1)
endpoint=lambda slot,bounds,generation=1:{'id':identity(slot,generation),'bounds':bounds,'group':'1','mask':'1','active':True}
migration=trace('retained-multicell-pair','collision',{'colliders':[endpoint(1,[-400,-400,400,400]),endpoint(2,[-350,-350,350,350])]},[
    [{'op':'update_bounds','id':identity(1),'bounds':[600,600,1400,1400]},
     {'op':'update_bounds','id':identity(2),'bounds':[650,650,1350,1350]}],
    [{'op':'set_filter','id':identity(2),'group':'1','mask':'0'}],
    [{'op':'set_filter','id':identity(2),'group':'1','mask':'1'}],
    [{'op':'remove','id':identity(1)}, {'op':'add','collider':endpoint(1,[600,600,1400,1400],2)}]],2)
runner=ROOT/'bin/cleanup-benchmark/platformator_benchmark_runner'
commands=[]
for workers in [1,6]:
    for path,modes in [(zoom,['linear','bvh','segmented']),(migration,['segmented','bvh'])]:
        for mode in modes:
            output=OUT/(path.stem+'-'+mode+'-w'+str(workers)+'.csv')
            command=[str(runner),'--spatial-trace',str(path),'--workers',str(workers),'--capture-mode','validate','--csv-output',str(output)]
            command += ['--render-culling',mode] if path==zoom else ['--broad-phase',mode,'--bvh-query-execution','parallel']
            completed=subprocess.run(command,capture_output=True,text=True)
            output.with_suffix('.log').write_text(completed.stdout+completed.stderr)
            assert completed.returncode==0,completed.stderr
            with output.with_suffix('.samples.csv').open() as stream: rows=list(csv.DictReader(stream))
            if path==zoom:
                assert all(r['changed_bounds_count']=='0' and r['query_count']=='1' for r in rows)
            else:
                assert [(int(r['pair_add_count']),int(r['pair_remove_count'])) for r in rows]==[(0,0),(0,1),(1,0),(1,1)]
                assert [int(r['candidate_pair_count']) for r in rows]==[1,0,1,1]
            commands.append(command)
(OUT/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
print('PASS: 10 production-ingress validations; zoom skips static bounds; multi-cell migration publishes exact net transitions, with one/six workers.')
