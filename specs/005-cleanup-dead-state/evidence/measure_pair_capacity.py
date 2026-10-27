#!/usr/bin/env python3
"""Build an isolated, instrumented baseline Grid; never use this executable for timing."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

source, out = map(lambda p: Path(p).resolve(), sys.argv[1:3])
out.mkdir(parents=True, exist_ok=False)
build = source / 'bin/cleanup-benchmark'
text = (source / 'src/grid.cpp').read_text()
text = '#include <cstdio>\n' + text
for statement, label in [('pairSlots.push_back(pair);', 'pair'), ('freePairSlots.push_back(pair->pairSlot);', 'free')]:
    replacement = statement + f'''\n        {{ static size_t lastCapacity = 0; auto &v = {'pairSlots' if label == 'pair' else 'freePairSlots'}; if (v.capacity() != lastCapacity) {{ lastCapacity = v.capacity(); std::fprintf(stderr, "CAPACITY {label} size=%zu capacity=%zu candidates=%zu\\n", v.size(), v.capacity(), candidateCollisions.size()); }} }}'''
    assert text.count(statement) == 1
    text = text.replace(statement, replacement)
modified = out / 'grid.cpp'
modified.write_text(text)
db = json.loads((build / 'compile_commands.json').read_text())
cmd = shlex.split(next(x['command'] for x in db if x['file'].endswith('/src/grid.cpp')))
cmd[cmd.index('-o')+1] = str(out / 'grid.cpp.o')
cmd[-1] = str(modified)
subprocess.run(cmd, cwd=build, check=True)
archive = out / 'libplatformator-instrumented.a'
shutil.copyfile(build / 'libplatformator.a', archive)
subprocess.run(['ar', 'r', str(archive), str(out / 'grid.cpp.o')], check=True)
link = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'platformator_dead_state_pair_churn_probe'], text=True).splitlines()[-1]
args = shlex.split(link)[2:-2]
args = [x for x in args if not x.startswith('-Wl,--dependency-file=')]
args[args.index('libplatformator.a')] = str(archive)
args[args.index('-o')+1] = str(out / 'pair-capacity-probe')
subprocess.run(args, cwd=build, check=True)
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')
for count in (10, 1000):
    for workers, cpus in ((1, '0'), (6, '0,2,4,6,8,10')):
        label = f'n{count}-w{workers}'
        with (out / f'{label}.log').open('w') as log:
            subprocess.run(['taskset', '-c', cpus, str(out / 'pair-capacity-probe'), str(count), str(workers), '120', '600', '1', str(out / f'{label}.csv')], cwd=source, env=env, stdout=log, stderr=log, check=True)
print('capacity logs:', out)
