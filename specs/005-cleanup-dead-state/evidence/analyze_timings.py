#!/usr/bin/env python3
"""Generate per-cell comparisons from the feature's completed matched timing capture."""
import csv
import json
import statistics
import sys
from pathlib import Path

root, output = map(Path, sys.argv[1:3])
scopes = ('complete_step', 'broad_phase', 'narrow_phase', 'cleanup')
fields = ['scenario', 'population', 'workers', 'scope', 'baseline_avg_ms', 'candidate_avg_ms', 'baseline_p95_ms', 'candidate_p95_ms', 'baseline_run_stdev_ms', 'candidate_run_stdev_ms', 'candidate_vs_baseline_pct', 'baseline_candidate_pairs', 'candidate_candidate_pairs', 'baseline_contacts', 'candidate_contacts']
rows = []
def row(scenario, population, workers, scope, a, b, ac, bc):
    baseline, candidate = a[0], b[0]
    rows.append({'scenario':scenario, 'population':population, 'workers':workers, 'scope':scope,
      'baseline_avg_ms':baseline, 'candidate_avg_ms':candidate, 'baseline_p95_ms':a[1], 'candidate_p95_ms':b[1],
      'baseline_run_stdev_ms':a[2], 'candidate_run_stdev_ms':b[2],
      'candidate_vs_baseline_pct': 100*(candidate/baseline-1) if baseline else '',
      'baseline_candidate_pairs':ac[0], 'candidate_candidate_pairs':bc[0], 'baseline_contacts':ac[1], 'candidate_contacts':bc[1]})
for summary in sorted((root/'baseline').glob('*-w[16].csv')):
    with summary.open() as f: baseline=list(csv.DictReader(f))
    with (root/'candidate'/summary.name).open() as f: candidate=list(csv.DictReader(f))
    assert len(baseline)==len(candidate)
    for a,b in zip(baseline,candidate):
        assert a['requested_count']==b['requested_count'] and a['workers']==b['workers'] and a['repetitions']==b['repetitions']=='5' and a['timing_eligible']==b['timing_eligible']=='True'
        ac=[a['candidate_pair_count_avg'],a['active_collision_count_avg']];bc=[b['candidate_pair_count_avg'],b['active_collision_count_avg']]
        for scope in scopes:
            values=lambda x:[float(x[f'{scope}_avg_ms']),float(x[f'{scope}_p95_ms']),float(x[f'{scope}_run_stdev_ms'])]
            row(a['scenario'],a['requested_count'],a['workers'],scope,values(a),values(b),ac,bc)
for count in (10,1000):
  for workers in (1,6):
    runs=[]
    for stage in ('baseline','candidate'):
      group=[]
      for repetition in range(5):
        p=root/stage/f'pair-churn-n{count}-w{workers}-r{repetition}.csv'
        manifest=json.loads(p.with_suffix('.manifest.json').read_text())
        assert manifest['timing_eligible'] and manifest['measured_steps']==600
        with p.open() as f: metrics={r['name']:r for r in csv.DictReader(f)}
        group.append(metrics)
      runs.append(group)
    for scope in scopes:
      values=lambda group:[statistics.mean(float(r[scope]['avg']) for r in group),statistics.mean(float(r[scope]['p95']) for r in group),statistics.stdev(float(r[scope]['avg']) for r in group)]
      counter=lambda group:[statistics.mean(float(r[k]['avg']) for r in group) for k in ('candidate_pair_count','active_collision_count')]
      row('pair_churn',count,workers,scope,values(runs[0]),values(runs[1]),counter(runs[0]),counter(runs[1]))
assert len(rows)==144, len(rows)
with output.open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader();writer.writerows(rows)
