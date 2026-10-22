import csv
import hashlib
import json
import math
from pathlib import Path
import sys
import tarfile
import tempfile
from types import SimpleNamespace

repo = Path('/home/user/Projects/Platformator')
sys.path.insert(0, str(repo / 'benchmark'))
import sweep_scaling as sweep
import capture_cleanup as capture
root = repo / 'benchmark/results/sdl-update-9c4f3f6'
old = repo / 'benchmark/results/cpp-cleanup'
report = json.loads((root / 'invariants.json').read_text())
rows = list(csv.DictReader((root / 'summary.csv').open()))
expected_versions = {'SDL': '3.4.18', 'SDL_mixer': '3.2.4', 'SDL_image': '3.4.8', 'SDL_ttf': '3.2.2'}
assert len(rows) == 36 and report['passed']
actual = {}
for archive in sorted((root / 'runs').glob('*.tar.gz')):
    with tempfile.TemporaryDirectory() as temporary:
        with tarfile.open(archive) as source:
            source.extractall(temporary, filter='data')
        paths = [path for path in Path(temporary).rglob('*.csv') if not path.name.endswith('.samples.csv')]
        assert len(paths) == 15
        for path in paths:
            metrics = sweep.parse_benchmark_csv(path)
            manifest = sweep.load_manifest(path)
            assert manifest['build']['source_sha256'] == report['source_sha256']
            assert all(manifest['dependencies'][name] == version for name, version in expected_versions.items())
            assert manifest['timing_eligible'] and manifest['validation_mode'] == 'boundary-only'
            assert manifest['repetitions'] == 5 and manifest['warmup_steps'] == 120 and manifest['measured_steps'] == 600
            assert manifest['seed'] == 0 and math.isclose(manifest['dt'], 1 / 120, rel_tol=1e-12)
            count = int(path.parent.name.split('-')[1])
            key = (manifest['scenario'], count, manifest['workers'])
            actual.setdefault(key, []).append((manifest['repetition_index'], metrics))
assert len(actual) == 36 and sum(map(len, actual.values())) == 180
for row in rows:
    key = (row['scenario'], int(row['requested_count']), int(row['workers']))
    runs = actual[key]
    assert sorted(index for index, _ in runs) == list(range(5))
    expected = sweep.build_result_row(SimpleNamespace(scenario=key[0], workers=key[2], seed=0, steady_count=1000,
                                                     validate_invariants=False), key[1], [metrics for _, metrics in runs])
    for field, value in expected.items():
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            assert math.isclose(float(row[field]), value, rel_tol=1e-12, abs_tol=1e-12), (key, field)
        else:
            assert row[field] == str(value), (key, field)
print('Verified 180 raw runs, 36 aggregates, five unique repetitions, complete raw/schema/provenance and actual updated SDL runtime versions.', flush=True)
comparisons = 0
for archive in sorted((root / 'validation').glob('*.tar.gz')):
    with tarfile.open(archive) as current, tarfile.open(old / 'validation' / archive.name) as previous:
        names = [name for name in current.getnames() if name.endswith('.manifest.json')]
        assert len(names) == 3
        for name in names:
            a = json.load(current.extractfile(name))
            b = json.load(previous.extractfile(name))
            capture.compare(b['physical_invariants'], a['physical_invariants'])
            comparisons += 1
assert comparisons == 36
print('All 36 old/new final physical snapshots agree within the existing declared tolerances (identities/supports/sleep/features exact).', flush=True)
with tempfile.TemporaryDirectory() as temporary:
    with tarfile.open(root / 'source.tar.gz') as source:
        source.extractall(temporary, filter='data')
    base = Path(temporary)
    paths = sorted(list(base.glob('src/**/*')) + list(base.glob('include/platformator/**/*')) + list(base.glob('cmake/**/*'))
                   + [base / 'CMakeLists.txt', base / 'vendored/SDL3_mixer-3.2.4/src/decoder_wav.c'])
    text = ''.join(f'{path.relative_to(base).as_posix()}:{hashlib.sha256(path.read_bytes()).hexdigest()}\n'
                   for path in paths if path.is_file())
    assert hashlib.sha256(text.encode()).hexdigest() == report['source_sha256']
print('Extracted source fingerprint matches the binary/invariant/timing provenance.', flush=True)
