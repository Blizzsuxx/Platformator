"""Matched sorted/unsorted Release comparisons; physical trajectories may differ."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import random
import statistics
import subprocess
import sys

PROJECT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(PROJECT/'benchmark'))
import sweep_scaling as sweep

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--phase',choices=('validate','time','followup'),required=True)
    args=parser.parse_args();root=args.root.resolve();out=root/args.phase;out.mkdir(exist_ok=False)
    cases=[('rigid_body_container',count,workers,600) for count in (100,1000) for workers in (1,6)]
    cases += [('rigid_body_container',1000,workers,6000) for workers in (1,6)]
    cases += [('broad_phase',100,6,600)]
    if args.phase=='validate':cases=[('rigid_body_container',1000,workers,600) for workers in (1,6)]
    if args.phase=='followup':cases=[('rigid_body_container',count,1,600) for count in (100,1000)]
    is_timing=args.phase in ('time','followup')
    repeats=5 if is_timing else 1
    jobs=[(*case,rep) for case in cases for rep in range(repeats)];random.Random(27).shuffle(jobs)
    schedule=[(stage,*job) for i,job in enumerate(jobs) for stage in (('sorted','unsorted') if i%2==0 else ('unsorted','sorted'))]
    provenance={'phase':args.phase,'seed':27,'schedule':schedule,'commands':[],'runner_hashes':{},'note':'No perf; extended trajectories distinct; no physical trajectory equality required'}
    for stage in ('sorted','unsorted'):
        exe=root/stage/'platformator_benchmark_runner';provenance['runner_hashes'][stage]=hashlib.sha256(exe.read_bytes()).hexdigest()
    manifests={};groups={}
    for i,(stage,case,count,workers,frames,rep) in enumerate(schedule):
        name=f'{case}-n{count}-w{workers}-steps{frames}';folder=out/stage/name;folder.mkdir(parents=True,exist_ok=True);path=folder/f'run-{rep}.csv'
        cmd=['taskset','-c','0' if workers==1 else '0,2,4,6,8,10',str(root/stage/'platformator_benchmark_runner'),
             '--scenario',case,'--workers',str(workers),'--seed','0','--dt',str(1/120),'--warmup-frames','120',
             '--measure-frames',str(frames),'--repetitions',str(repeats),'--run-index',str(rep),'--csv-output',str(path)]
        if case=='rigid_body_container':cmd+=['--box-count',str(count//2),'--circle-count',str(count-count//2)]
        else:cmd+=['--broad-count',str(count)]
        if args.phase=='validate':cmd+=['--validate-invariants']
        libpath=str(root/stage)+':'+str(root/stage/'tbb');env=dict(os.environ,LD_LIBRARY_PATH=libpath,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
        provenance['commands'].append({'command':cmd,'LD_LIBRARY_PATH':libpath})
        with path.with_suffix('.log').open('x') as stream:
            stream.write(json.dumps(cmd)+'\n');stream.flush();subprocess.run(cmd,env=env,stdout=stream,stderr=subprocess.STDOUT,check=True)
        sweep.parse_benchmark_csv(path);m=sweep.load_manifest(path);build=m['build']
        assert build['source_sha256']=={'sorted':'b87bc7a61c539405ff6e77fc8f481a3fd2c6ad3510f2b089ca4c475dd27367db','unsorted':'cc6ce0456970f439e78ae6e664a7ab90066a12b8c2155c14a47483b68d6c1588'}[stage]
        assert build['configuration']=='Release' and build['sanitizer']=='none' and not build['work_probes'] and not build['debug_tools']
        assert m['measured_steps']==frames and m['warmup_steps']==120 and m['workers']==workers and m['timing_eligible']==is_timing
        key=(case,count,workers,frames,rep)
        if key in manifests:
            old=manifests[key]
            for field in ('scenario','workers','seed','dt','resolved_parameters','boundaries','dependencies','machine'):assert m[field]==old[field],(key,field)
            for field in ('compiler','flags','configuration','sanitizer','work_probes','debug_tools'):assert build[field]==old['build'][field],(key,field)
        else:manifests[key]=m
        with path.open() as stream:rows={row['name']:row for row in csv.DictReader(stream)}
        groups.setdefault(name,{}).setdefault(stage,[]).append({'run_index':rep,'metrics':rows,'physical_invariants':m['physical_invariants']})
        print(f'{args.phase} {i+1}/{len(schedule)} {stage} {name} r{rep}',flush=True)
    (out/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    (out/'results.json').write_text(json.dumps(groups,indent=2)+'\n')
    checks={str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.rglob('*')) if p.is_file()}
    (out/'checksums.json').write_text(json.dumps(checks,indent=2)+'\n')
    if is_timing:
        for name,group in groups.items():
            values={stage:[float(run['metrics']['complete_step']['avg']) for run in group[stage]] for stage in ('sorted','unsorted')}
            print(name,statistics.mean(values['sorted']),statistics.mean(values['unsorted']),(statistics.mean(values['unsorted'])/statistics.mean(values['sorted'])-1)*100)
    print('COMPLETED',args.phase,len(schedule),flush=True)

if __name__=='__main__':main()
