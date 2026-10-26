"""Verify matched current schema-3 captures and retain run-level/tail/worker comparisons."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics
import sys

PROJECT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(PROJECT/'benchmark'))
import sweep_scaling as sweep


def fingerprint(source):
    files=[p for folder in ('src','include/platformator','cmake') for p in (source/folder).rglob('*') if p.is_file()]
    files += [source/'CMakeLists.txt',source/'vendored/SDL3_mixer-3.2.4/src/decoder_wav.c']
    text=''.join(str(p.relative_to(source))+':'+hashlib.sha256(p.read_bytes()).hexdigest()+'\n' for p in sorted(files))
    return hashlib.sha256(text.encode()).hexdigest()


def stage_data(root,stage,source):
    folder=root/stage/'time';checks=json.loads((folder/'checksums.json').read_text())
    for name,digest in checks.items():assert hashlib.sha256((folder/name).read_bytes()).hexdigest()==digest,(stage,name)
    expected=fingerprint(source);groups={};hashes=set()
    for directory in sorted(p for p in folder.iterdir() if p.is_dir()):
        records=[]
        for repeat in range(5):
            path=directory/f'run-{repeat}.csv';sweep.parse_benchmark_csv(path)
            manifest=sweep.load_manifest(path)
            assert manifest['schema_version']==3 and manifest['timing_eligible'] and manifest['complete_capture']
            assert manifest['warmup_steps']==120 and manifest['measured_steps']==600
            build=manifest['build'];assert build['configuration']=='Release' and build['sanitizer']=='none'
            assert not build['work_probes'] and not build['debug_tools']
            hashes.add(build['source_sha256']);assert build['source_sha256']==expected,(stage,'source drift',build['source_sha256'],expected)
            with path.open() as f:metrics={row['name']:row for row in csv.DictReader(f) if row['category']=='scope'}
            records.append((metrics,manifest))
        scopes={}
        for scope in sweep.SCOPES:
            means=[float(row[0][scope]['avg']) for row in records]
            scopes[scope]={'mean_ms':statistics.mean(means),'stdev_ms':statistics.stdev(means),
                           'run_means_ms':means,'run_min_ms':min(means),'run_max_ms':max(means)}
            scopes[scope]['worst_ms']=max(float(row[0][scope]['max']) for row in records)
            for field in ('median','p95','p99','max'):
                scopes[scope]['mean_'+field+'_ms']=statistics.mean(float(row[0][scope][field]) for row in records)
        groups[directory.name]={'scopes':scopes,'manifest':records[0][1]}
    assert len(groups)==36 and len(hashes)==1
    return groups,expected


def main():
    p=argparse.ArgumentParser();p.add_argument('--capture-root',type=Path,required=True)
    p.add_argument('--baseline-source',type=Path,required=True);p.add_argument('--candidate-source',type=Path,default=PROJECT)
    p.add_argument('--output',type=Path,default=Path(__file__).resolve().parent);a=p.parse_args()
    old,oldhash=stage_data(a.capture_root,'baseline',a.baseline_source)
    new,newhash=stage_data(a.capture_root,'candidate',a.candidate_source)
    assert old.keys()==new.keys();rows=[]
    for name in old:
        ma,mb=old[name]['manifest'],new[name]['manifest']
        for field in ('scenario','workers','seed','dt','resolved_parameters','boundaries','dependencies','machine'):
            assert ma[field]==mb[field],(name,field)
        for field in ('compiler','flags','configuration','sanitizer','work_probes','debug_tools'):
            assert ma['build'][field]==mb['build'][field],(name,field)
        before,after=old[name]['scopes']['complete_step'],new[name]['scopes']['complete_step']
        row={'capture':name,'change_percent':(after['mean_ms']/before['mean_ms']-1)*100,
             'run_ranges_overlap':max(before['run_min_ms'],after['run_min_ms'])<=min(before['run_max_ms'],after['run_max_ms']),
             'baseline':old[name]['scopes'],'candidate':new[name]['scopes']}
        workers=ma['workers'];serial=name.rsplit('-w',1)[0]+'-w1'
        row['workers']=workers
        for stage,data in (('baseline',old),('candidate',new)):
            row[stage+'_worker_ratio']=data[serial]['scopes']['complete_step']['mean_ms']/data[name]['scopes']['complete_step']['mean_ms']
        rows.append(row)
    report={'baseline_source_sha256':oldhash,'candidate_source_sha256':newhash,'runs_per_group':5,'groups':rows}
    (a.output/'performance-data.json').write_text(json.dumps(report,indent=2)+'\n')
    fields=['capture','baseline_us','candidate_us','change_percent','baseline_run_sd_us','candidate_run_sd_us',
            'baseline_p95_us','candidate_p95_us','baseline_p99_us','candidate_p99_us','baseline_max_us','candidate_max_us',
            'run_ranges_overlap','baseline_worker_ratio','candidate_worker_ratio']
    with (a.output/'performance-comparison.csv').open('w') as f:
        w=csv.DictWriter(f,fieldnames=fields);w.writeheader()
        for row in rows:
            x,y=row['baseline']['complete_step'],row['candidate']['complete_step']
            w.writerow(dict(zip(fields,[row['capture'],x['mean_ms']*1000,y['mean_ms']*1000,row['change_percent'],
                x['stdev_ms']*1000,y['stdev_ms']*1000,x['mean_p95_ms']*1000,y['mean_p95_ms']*1000,
                x['mean_p99_ms']*1000,y['mean_p99_ms']*1000,x['worst_ms']*1000,y['worst_ms']*1000,
                row['run_ranges_overlap'],row['baseline_worker_ratio'],row['candidate_worker_ratio']])))
    lines=['# Complete-step results','','Five independent processes/group,120 warmup/600 measured,seed0,dt1/120. '
           'Matched hardware/dependencies/build/affinity/boundaries, strict readers and raw checksums verified. '
           'Observed run-mean overlap is descriptive, not significance or proof of equivalence. Positive means slower.','',
           '| Case/population/recipients/workers | Baseline µs | Candidate µs | Change | Observed ranges overlap |',
           '| --- | ---: | ---: | ---: | --- |']
    for row in rows:
        lines.append(f"| {row['capture']} | {row['baseline']['complete_step']['mean_ms']*1000:.3f} | "
                     f"{row['candidate']['complete_step']['mean_ms']*1000:.3f} | {row['change_percent']:+.2f}% | "
                     f"{'yes' if row['run_ranges_overlap'] else 'no'} |")
        if row['change_percent']>0:print(row['capture'],f"{row['change_percent']:+.2f}%",'overlap',row['run_ranges_overlap'])
    (a.output/'performance-table.md').write_text('\n'.join(lines)+'\n')
    print('Verified36 matched groups/360 processes; source hashes',oldhash,newhash)

if __name__=='__main__':main()
