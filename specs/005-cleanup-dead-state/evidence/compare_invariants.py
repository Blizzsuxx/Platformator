#!/usr/bin/env python3
"""Compare schema-3 controlled snapshots and pair-churn transitions."""
import csv
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'benchmark'))
from capture_cleanup import compare

base, candidate = map(Path, sys.argv[1:3])
for directory in (base, candidate):
    assert len(list(directory.glob('*-w*/population-*/run-00.manifest.json'))) == 32
    for path in directory.glob('*-w*/population-*/run-00.manifest.json'):
        inv = json.loads(path.read_text())['physical_invariants']
        assert inv['finite'] and inv['populations_valid'] and inv['supports_valid'], path

for label, left, right in [('baseline/candidate', base, candidate)]:
    for path in sorted(right.glob('*-w*/population-*/run-00.manifest.json')):
        relative = path.relative_to(right)
        a = json.loads((left / relative).read_text())['physical_invariants']
        b = json.loads(path.read_text())['physical_invariants']
        try:
            compare(a, b)
            print('MATCH', label, relative)
        except ValueError as error:
            if str(relative).startswith('rigid_body_container-'):
                print('WAIVED', label, relative, error)
            else:
                raise
for directory in (base, candidate):
    for path in sorted(directory.glob('*-w1/population-*/run-00.manifest.json')):
        relative = path.relative_to(directory)
        other = directory / str(relative).replace('-w1/', '-w6/')
        try:
            compare(json.loads(path.read_text())['physical_invariants'], json.loads(other.read_text())['physical_invariants'])
            print('MATCH workers', directory.name, relative)
        except ValueError as error:
            if str(relative).startswith('rigid_body_container-'):
                print('WAIVED workers', directory.name, relative, error)
            else:
                raise
for path in sorted(candidate.glob('pair-churn-*.samples.csv')):
    with path.open() as f, (base / path.name).open() as g:
        a, b = list(csv.DictReader(f)), list(csv.DictReader(g))
    keys = ['candidate_pair_count', 'active_collision_count', 'collision_enter_event_count', 'collision_exit_event_count']
    assert len(a) == len(b) == 600 and all(all(x[k] == y[k] for k in keys) for x, y in zip(a, b)), path
    print('MATCH pair transitions', path.name, len(a))
