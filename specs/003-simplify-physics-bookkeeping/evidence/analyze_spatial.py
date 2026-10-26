"""Verify and summarize the bounded convergence profiling/timing evidence."""
import csv
import hashlib
import json
from pathlib import Path
import statistics as stats
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(PROJECT / 'benchmark'))
import sweep_scaling as sweep
from capture_cleanup import compare
ROOT = PROJECT / 'bin/physics-bookkeeping-20261026'
OUT = Path(__file__).resolve().parent

def spread(values):
    return {'mean': stats.mean(values), 'sd': stats.stdev(values) if len(values)>1 else 0,
            'min': min(values), 'max': max(values), 'runs': values}

def metrics(path):
    sweep.parse_benchmark_csv(path)
    with path.open() as f: rows=list(csv.DictReader(f))
    return {row['name']: row for row in rows}

def main():
    report = {'raw_root': str(ROOT), 'verification': {}, 'timing': {}, 'stat': {}, 'cache': {}, 'profile': {}}
    for phase in ('profile', 'stat', 'cache', 'validate', 'time'):
        checks=json.loads((ROOT/phase/'checksums.json').read_text())
        for name,digest in checks.items(): assert hashlib.sha256((ROOT/phase/name).read_bytes()).hexdigest()==digest, (phase,name)
        report['verification'][phase]={'verified_files':len(checks), 'processes':len(list((ROOT/phase).rglob('*.manifest.json')))}
    for stage in ('baseline','candidate'):
        base=ROOT/'validate'/stage
        a=json.loads((base/'broad_phase_stress-n50-w1-steps6000/run-0.manifest.json').read_text())
        b=json.loads((base/'broad_phase_stress-n50-w6-steps6000/run-0.manifest.json').read_text())
        compare(a['physical_invariants'],b['physical_invariants'])
    report['verification']['extended_stress_worker_comparisons']=2
    usage=json.loads((ROOT/'time/provenance.json').read_text())['child_usage']
    for folder in sorted((ROOT/'time/baseline').iterdir()):
        name=folder.name; group={}
        for stage in ('baseline','candidate'):
            paths=[ROOT/'time'/stage/name/f'run-{i}.csv' for i in range(10)]
            records=[metrics(p) for p in paths]
            scopes={scope:{field:spread([float(r[scope][field]) for r in records])
                           for field in ('avg','median','p95','p99','max')} for scope in sweep.SCOPES}
            counters={c:{'mean':stats.mean(float(r[c]['avg']) for r in records),
                         'max':max(float(r[c]['max']) for r in records)} for c in sweep.COUNTERS}
            for c in ('candidate_pair_count','pending_narrow_phase_pair_count','active_collision_count','awake_dynamic_body_count','collision_enter_event_count','collision_exit_event_count'):
                assert counters[c]['max']==0,(name,c)
            uses=[u for u in usage if u['capture'].startswith(stage+'/'+name+'/')]
            group[stage]={'scopes':scopes,'counters':counters,'child_usage':{field:spread([u[field] for u in uses]) for field in ('ru_utime','ru_stime','ru_nvcsw','ru_nivcsw')}}
        before=group['baseline']['scopes']['complete_step']['avg']['runs'];after=group['candidate']['scopes']['complete_step']['avg']['runs']
        group['mean_change_percent']=(stats.mean(after)/stats.mean(before)-1)*100
        group['paired_change_percent']=spread([(b/a-1)*100 for a,b in zip(before,after)])
        report['timing'][name]=group
    for folder in sorted((ROOT/'stat/baseline').iterdir()):
        name=folder.name;group={}
        for stage in ('baseline','candidate'):
            records=[]
            for path in sorted((ROOT/'stat'/stage/name).glob('*.stat.csv')):
                record={}
                for row in csv.reader(path.read_text().splitlines()):
                    if not row or row[0].startswith('#'):continue
                    assert len(row)>4 and float(row[4])==100, (path,row)
                    record[row[2]]=float(row[0])
                records.append(record)
            group[stage]={key:spread([r[key] for r in records]) for key in records[0]}
            group[stage]['IPC']=spread([r['instructions:u']/r['cycles:u'] for r in records])
            group[stage]['cycles_per_task_ns']=spread([r['cycles:u']/(r['task-clock:u']*1e6) for r in records])
        group['change_percent']={key:(group['candidate'][key]['mean']/group['baseline'][key]['mean']-1)*100 for key in ('cycles:u','instructions:u','task-clock:u','branches:u','branch-misses:u')}
        report['stat'][name]=group
    for folder in sorted((ROOT/'cache/baseline').iterdir()):
        name=folder.name;group={}
        for stage in ('baseline','candidate'):
            records=[]
            for path in sorted((ROOT/'cache'/stage/name).glob('*.stat.csv')):
                record={}
                for row in csv.reader(path.read_text().splitlines()):
                    if not row or row[0].startswith('#'):continue
                    assert float(row[4])==100,(path,row)
                    record[row[2]]=float(row[0])
                records.append(record)
            group[stage]={key:spread([r[key] for r in records]) for key in records[0]}
        group['change_percent']={key:(group['candidate'][key]['mean']/group['baseline'][key]['mean']-1)*100 for key in group['baseline']}
        report['cache'][name]=group
    for path in sorted((ROOT/'profile').rglob('*.perf')):
        compact=path.with_suffix('.compact.txt')
        command=['perf','report','-i',str(path),'--stdio','--no-children','--call-graph','none','--percent-limit','0.5','--sort','dso,symbol']
        if compact.exists():
            result=subprocess.CompletedProcess(command,0,compact.read_text(),'retained compact report; raw profile checksum verified')
        else:
            result=subprocess.run(command,capture_output=True,text=True,check=True)
            compact.write_text(result.stdout)
        report['profile'][str(path.relative_to(ROOT))]={'command':command,'compact_report':str(compact.relative_to(ROOT)),
                                                        'output':result.stdout,'stderr':result.stderr}
    (OUT/'spatial-results.json').write_text(json.dumps(report,indent=2)+'\n')
    for name,group in report['timing'].items():
        a=group['baseline']['scopes']['complete_step']['avg'];b=group['candidate']['scopes']['complete_step']['avg']
        print(name, 'mean_us',round(a['mean']*1000,3),round(b['mean']*1000,3),'delta%',round(group['mean_change_percent'],2),'pair range%',round(group['paired_change_percent']['min'],2),round(group['paired_change_percent']['max'],2))
    for name,group in report['stat'].items():print('STAT',name,{k:round(v,2) for k,v in group['change_percent'].items()})
    for name,group in report['cache'].items():print('CACHE',name,{k:round(v,2) for k,v in group['change_percent'].items()})
    print('VERIFIED',report['verification'])

if __name__=='__main__': main()
