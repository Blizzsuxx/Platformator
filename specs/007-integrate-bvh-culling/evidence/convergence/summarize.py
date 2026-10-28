#!/usr/bin/env python3
"""Build paired tables and a coverage ledger from the final eligible capture bundles."""
import csv
import json
from pathlib import Path
import statistics
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT/'benchmark'))
from sweep_spatial import case_method_profiles, image_qualification_matches
from generate_spatial_traces import derive_operation_counts

SOURCE = 'f1c6298f8b6e7baaa3d4374236a9650e5345a2567f210dbfa3efcb3197b0aba1'
MEAN = '.avg.across_process_mean'

def read_csv(path):
    with path.open(newline='') as stream:
        return list(csv.DictReader(stream))

def write_csv(name, rows):
    with (HERE/name).open('w',newline='') as stream:
        columns=list(dict.fromkeys(k for row in rows for k in row))
        writer=csv.DictWriter(stream,fieldnames=columns)
        writer.writeheader();writer.writerows(rows)

def trace_metadata(suite, case):
    """Return honest requested knobs and observed per-step counts for immutable traces."""
    parameters=dict(case['parameters'])
    operation_counts=''
    if case['domain'] in {'collision','visibility'}:
        trace_path=HERE/suite/'cases'/case['trace']
        records=[json.loads(line) for line in trace_path.read_text().splitlines()]
        counts=derive_operation_counts(case['domain'],records[1],records[2:])
        operation_counts=json.dumps(counts,sort_keys=True,separators=(',',':'))
        parameters.pop('operation_counts',None)
        parameters.pop('filter_change_count',None)
        parameters.pop('coalesced_write_count',None)
        if 'changed_count' in parameters:
            parameters['requested_changed_endpoint_count']=parameters.pop('changed_count')
        if 'changed_percent' in parameters:
            parameters['changed_percent_semantics']='requested workload knob; observed counts are in operation_counts'
    return parameters,operation_counts

def ledger(suite,mode):
    directory=HERE/suite/mode
    records=json.loads((directory/'eligibility.json').read_text())
    assert records and all(r['eligible'] and r['source_sha256']==SOURCE for r in records), (suite,mode)
    for record in records:
        manifest=json.loads((directory/record['manifest']).read_text())
        assert manifest['build']['source_sha256']==SOURCE
        if suite in ['engine','startup'] and mode=='validation' and manifest['sample_counts']['actual_render']:
            assert image_qualification_matches(manifest)
            assert manifest['validation']['image']['frames']==(1 if suite=='startup' else 720)
    return records

def key(row):
    return row['case_id'],str(row['workers']),row['broad_phase'],row['render_culling']

def summary(suite):
    rows=read_csv(HERE/suite/'timing/summary.csv')
    assert all(r['eligible_process_count']=='5' and r['requested_process_count']=='5' and r['source_sha256']==SOURCE for r in rows)
    return {key(row):row for row in rows}

coverage=[]
counts={}
case_data_by_suite={}
for suite in ['calibration','engine','replay','controls','startup']:
    modes=['validation','timing'] + ([] if suite in ['calibration','startup'] else ['resources'])
    entries={mode:ledger(suite,mode) for mode in modes}
    counts[suite]={mode:len(records) for mode,records in entries.items()}
    cases=json.loads((HERE/suite/'cases/cases.json').read_text())['cases']
    case_by_id={case['case_id']:case for case in cases}
    case_data_by_suite[suite]=case_by_id
    for case in cases:
        reported_parameters,operation_counts=trace_metadata(suite,case)
        profiles=case_method_profiles(case,None,['segmented'] if suite=='controls' else ['segmented','bvh'],
                    ['none'] if suite=='controls' else ['none','linear','bvh','segmented'],
                    ['native-partial','full-rebuild'] if suite=='calibration' else ['native-partial'],
                    [0.0,2.0] if suite=='calibration' else [0.0],
                    ['serial','parallel'] if suite=='calibration' else ['serial'])
        expected=len(profiles)*2
        row={'suite':suite,'case_id':case['case_id'],'case_sha256':case['sha256'],
             'scenario':case.get('scenario','replay'),'domain':case['domain'],
             'parameters':json.dumps(reported_parameters,sort_keys=True),
             'operation_counts':operation_counts,'workers':'1,6',
             'qualification_status':'historical_only',
             'current_qualification_reason':('historical source; replay input lacks recorded operation_counts metadata'
                 if case['domain'] in {'collision','visibility'} else
                 'historical source; engine input lacks a recorded runtime scenario'),
             'expected_configurations':expected,'source_sha256':SOURCE,
             'image_qualification':('same-frame pixels/order, 1 frame' if suite=='startup' else 'same-frame pixels/order, 720 frames') if case.get('render') else 'inapplicable'}
        for mode,records in entries.items():
            captured=[r for r in records if r['case_id']==case['case_id']]
            expected_captures=expected*(5 if mode=='timing' else 1)
            assert len(captured)==expected_captures,(suite,case['case_id'],mode,len(captured),expected_captures)
            assert all(r['case_sha256']==case['sha256'] for r in captured)
            row[mode+'_eligible']=len(captured)
        coverage.append(row)
write_csv('coverage.csv',coverage)
(HERE/'capture-counts.json').write_text(json.dumps(counts,indent=2)+'\n')

startup=[]
for row in summary('startup').values():
    setup=float(row['initialization.total_ms.across_process_mean'])
    first_step=float(row['scope.complete_step'+MEAN])
    render=row['scope.render'+MEAN]
    startup.append({'case_id':row['case_id'],'base_case_id':row['case_id'].removeprefix('cold-'),
        'workers':row['workers'],'broad_phase':row['broad_phase'],'render_culling':row['render_culling'],
        'source_sha256':SOURCE,'setup_ms':setup,'first_complete_step_ms':first_step,'first_render_ms':render,
        'cold_operational_ms':setup+first_step+(float(render) if render else 0),
        'first_spatial_update_ms':row['scope.spatial_update'+MEAN],
        'first_render_culling_ms':row['scope.render_culling'+MEAN],
        'setup_process_stdev_ms':row['initialization.total_ms.across_process_stdev'],
        'first_complete_step_process_stdev_ms':row['scope.complete_step.avg.across_process_stdev'],
        'first_render_process_stdev_ms':row['scope.render.avg.across_process_stdev']})
write_csv('cold-startup.csv',startup)

comparisons=[]
for suite in ['replay','engine']:
    rows=summary(suite)
    for identity,current in rows.items():
        case,workers,broad,culling=identity
        if suite=='replay':
            base_method='segmented' if current['domain']=='collision' else 'linear'
            if current['domain']=='collision':
                if broad!='bvh':continue
                reference=rows[(case,workers,'segmented','none')];method='bvh';scope='spatial_replay'
            else:
                if culling not in ['bvh','segmented']:continue
                reference=rows[(case,workers,'segmented','linear')];method=culling;scope='spatial_replay'
        elif current['scenario'] not in ['sprite_visibility','spatial_combined']:
            if broad!='bvh':continue
            reference=rows[(case,workers,'segmented','none')];method='bvh';base_method='segmented';scope='complete_step'
        else:
            if culling not in ['bvh','segmented']:continue
            reference=rows[(case,workers,broad,'linear')];method=culling;base_method='linear';scope='render'
        mean=float(current['scope.'+scope+MEAN]);base=float(reference['scope.'+scope+MEAN])
        params=json.loads(current['parameters'])
        source_case=case_data_by_suite[suite][case]
        reported_parameters,operation_counts=trace_metadata(suite,source_case)
        observed_counts=json.loads(operation_counts) if operation_counts else None
        unchanged=(all(item['changed_endpoint_count']==0 for item in observed_counts['per_step'])
                   if observed_counts else params.get('variant')=='static')
        comparisons.append({'suite':suite,'case_id':case,'workers':workers,'physics':broad,
                'method':method,'reference':base_method,'scope':scope,
                'static':unchanged,'parameters':json.dumps(reported_parameters,sort_keys=True),
                'operation_counts':operation_counts,'method_ms':mean,'reference_ms':base,'ratio':mean/base,
                'method_process_stdev_ms':current['scope.'+scope+'.avg.across_process_stdev'],
                'reference_process_stdev_ms':reference['scope.'+scope+'.avg.across_process_stdev'],
                'method_mean_p95_ms':current['scope.'+scope+'.p95.across_process_mean'],
                'method_mean_p99_ms':current['scope.'+scope+'.p99.across_process_mean'],
                'method_mean_max_ms':current['scope.'+scope+'.max.across_process_mean'],
                'method_initialization_ms':current['initialization.total_ms.across_process_mean'],
                'reference_initialization_ms':reference['initialization.total_ms.across_process_mean']})
write_csv('paired-comparisons.csv',comparisons)

resources=[]
for suite in ['replay','engine','controls']:
    directory=HERE/suite/'resources'
    for record in json.loads((directory/'eligibility.json').read_text()):
        manifest=json.loads((directory/record['manifest']).read_text())
        resource=manifest['resources']
        row={field:record[field] for field in ['case_id','workers','broad_phase','render_culling','source_sha256']}
        row.update(suite=suite,process_peak_rss_bytes=resource['process_peak_rss_bytes'])
        for domain,stats in resource['domains'].items():
            for metric in ['initial_owned_capacity_bytes','final_owned_capacity_bytes','peak_owned_capacity_bytes','peak_live_bytes','allocated_bytes','allocation_count']:
                row[domain+'.'+metric]=stats[metric]
        resources.append(row)
write_csv('resources.csv',resources)

controls=summary('controls')
control_records=json.loads((HERE/'controls/timing/eligibility.json').read_text())
control_manifests={}
for record in control_records:
    control_manifests.setdefault((record['case_id'],str(record['workers'])),json.loads((HERE/'controls/timing'/record['manifest']).read_text()))
baselines=[]
for workers in [1,6]:
    for baseline in (HERE.parent/'baseline'/f'timing-pinned-{workers}').glob('*.csv'):
        for old in read_csv(baseline):
            scenario=old['scenario'];n=old['requested_count'];endpoints=old['scripted_endpoints']
            if scenario=='broad_phase':case='engine-broad-n'+n
            elif scenario=='broad_phase_stress':case='engine-broad-stress-n'+n
            elif scenario=='narrow_phase':case='engine-narrow-n'+n
            elif scenario=='rigid_body_container':case='engine-container-n'+n
            elif scenario in ['add_and_remove','moving_add_and_remove']:case='engine-'+scenario+'-churn'+n
            else:case='engine-persistent-pairs'+n+'-scripted'+endpoints
            current=controls[(case,str(workers),'segmented','none')]
            old_manifest=json.loads(next((baseline.parent/old['raw_directory']).glob('*.manifest.json')).read_text())
            new_manifest=control_manifests[(case,str(workers))]
            for parameter,expected in old_manifest['resolved_parameters'].items():
                if parameter != 'seed_policy':
                    assert new_manifest['input']['parameters'].get(parameter)==expected,(case,parameter,expected,new_manifest['input']['parameters'].get(parameter))
            foundation=HERE.parent/'foundation-control-captures/timing'/f'workers-{workers}'/baseline.name
            earlier=next(r for r in read_csv(foundation) if r['requested_count']==n and r['scripted_endpoints']==endpoints)
            assert old['seed']==earlier['seed']=='0' and old['repetitions']==earlier['repetitions']=='5'
            for scope in ['complete_step','physics','cleanup','broad_phase','narrow_phase','resolve_collisions']:
                new=float(current['scope.'+scope+MEAN]);old_value=float(old[scope+'_avg_ms']);foundation_value=float(earlier[scope+'_avg_ms'])
                baselines.append({'case_id':case,'workers':workers,'scope':scope,'seed':0,'processes':5,
                        't002_ms':old_value,'t017_ms':foundation_value,'current_ms':new,'current_source_sha256':SOURCE,
                        'current_over_t002':new/old_value if old_value else '',
                        'current_over_t017':new/foundation_value if foundation_value else '',
                        'current_process_stdev_ms':current['scope.'+scope+'.avg.across_process_stdev'],
                        't002_process_stdev_ms':old[scope+'_run_stdev_ms'],
                        't017_process_stdev_ms':earlier[scope+'_run_stdev_ms']})
assert len(baselines)==40*6
write_csv('same-method-controls.csv',baselines)

lines=['# Historical derived results','',f'Source: `{SOURCE}`. These retained ratios describe that source only; they do not qualify the current correction. Ratios below 1 favor the named method. Figures are means of five fresh-process means.','',
       '| Suite/domain | Comparison | Workers | Pairs | Wins | Median ratio |',
       '|---|---|---:|---:|---:|---:|']
groups={}
for row in comparisons:
    domain='collision' if row['scope']=='spatial_replay' and row['reference']=='segmented' else 'visibility' if row['scope']=='spatial_replay' else row['scope']
    groups.setdefault((row['suite']+'/'+domain,row['method']+'/'+row['reference'],row['workers']),[]).append(row['ratio'])
for (domain,comparison,workers),ratios in sorted(groups.items()):
    lines.append(f'| {domain} | {comparison} | {workers} | {len(ratios)} | {sum(v<1 for v in ratios)} | {statistics.median(ratios):.4f} |')
(HERE/'DERIVED.md').write_text('\n'.join(lines)+'\n')
print(json.dumps(counts,indent=2));print('\n'.join(lines))
