#!/usr/bin/env python3
"""Verify current bundle checksums, immutable identities and qualification links after captures finish."""
import hashlib
import json
from pathlib import Path
import sys
import time
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
sys.path.insert(0,str(ROOT/'benchmark'))
from sweep_spatial import validation_key,image_qualification_matches
SOURCE='f1c6298f8b6e7baaa3d4374236a9650e5345a2567f210dbfa3efcb3197b0aba1'
started=time.time()
files=0;byte_count=0;stage_counts={}

def sha(path):
    global files,byte_count
    digest=hashlib.sha256()
    with path.open('rb') as stream:
        while chunk:=stream.read(1024*1024):digest.update(chunk);byte_count+=len(chunk)
    files+=1
    return digest.hexdigest()

for suite in ['calibration','engine','replay','controls','startup']:
    validation_keys=set();performance_keys=set()
    modes=['validation','timing']+([] if suite in ['calibration','startup'] else ['resources'])
    for mode in modes:
        directory=HERE/suite/mode
        for line in (directory/'checksums.sha256').read_text().splitlines():
            expected,name=line.split('  ',1)
            assert sha(directory/name)==expected,(suite,mode,name)
        records=json.loads((directory/'eligibility.json').read_text())
        assert records and all(r['eligible'] for r in records),(suite,mode)
        stage_counts[suite+'/'+mode]=len(records)
        for record in records:
            manifest=json.loads((directory/record['manifest']).read_text())
            assert record['source_sha256']==manifest['build']['source_sha256']==SOURCE
            assert manifest['schema_version']==4 and manifest['complete_capture'] is True
            key=validation_key(manifest)
            if mode=='validation':
                assert manifest['capture_mode']=='validate' and manifest['validation']['status']=='passed'
                assert manifest['timing_eligible'] is False
                if manifest.get('sample_counts',{}).get('actual_render',0)>0:
                    assert image_qualification_matches(manifest)
                validation_keys.add(key)
            elif mode=='timing':
                assert manifest['capture_mode']=='performance' and manifest['timing_eligible'] is True
                assert key in validation_keys,(suite,'unqualified timing',record['capture'])
                assert manifest['build']['resource_hooks'] is False
                performance_keys.add(key)
            else:
                assert manifest['capture_mode']=='resource' and manifest['timing_eligible'] is False
                assert manifest['resources']['status']=='complete' and manifest['build']['resource_hooks'] is True
                assert key in performance_keys,(suite,'unpaired resource',record['capture'])
result={'status':'passed','source_sha256':SOURCE,'verified_checksum_files':files,'verified_bytes':byte_count,
        'stage_counts':stage_counts,'elapsed_seconds':time.time()-started}
(HERE/'INTEGRITY.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
