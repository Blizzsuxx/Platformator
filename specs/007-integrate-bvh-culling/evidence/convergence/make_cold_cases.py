#!/usr/bin/env python3
"""Create first-frame variants of the same bounded engine cases, without modifying primary inputs."""
import hashlib
import json
from pathlib import Path
import sys
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
sys.path.insert(0,str(ROOT/'benchmark'))
from generate_spatial_traces import engine_case
base=json.loads((HERE/'engine/cases/cases.json').read_text())
cases=[engine_case('cold-'+c['case_id'],c['scenario'],c['parameters'],c['render'],c['seed'],0,1,c['dt']) for c in base['cases']]
output=HERE/'startup/cases'
output.mkdir(parents=True,exist_ok=True)
manifest={'format':'platformator-spatial-cases','version':1,
          'generator':{'name':Path(__file__).name,'revision':'1','source_hash':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                       'fixture_generator_sha256':hashlib.sha256((ROOT/'benchmark/generate_spatial_traces.py').read_bytes()).hexdigest(),
                       'transformation':'same engine fixtures/seed/settings, cold- prefix, zero warmup, one measured first frame'},'cases':cases}
(output/'cases.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
print('Wrote',len(cases),'cold first-frame cases')
