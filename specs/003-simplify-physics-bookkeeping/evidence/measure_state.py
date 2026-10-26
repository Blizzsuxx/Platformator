"""Measure declarations with each tree's flags and RSS directly from fresh runner children."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True)
p.add_argument('--output',type=Path,required=True);p.add_argument('--repetitions',type=int,default=5)
a=p.parse_args();source=a.source.resolve();a.output.mkdir(exist_ok=False,parents=True)
entry=next(x for x in json.loads((source/'bin/cleanup-debug/compile_commands.json').read_text())
           if x['file'].endswith('/src/physicsmanager.cpp'))
tokens=shlex.split(entry['command']);flags=[];skip=False
for token in tokens[1:]:
    if skip:skip=False;continue
    if token in ('-o','-MF','-MT','-MQ'):skip=True;continue
    if token in ('-c','-MD','-MMD') or token.endswith('.cpp'):continue
    flags.append(token)
with tempfile.TemporaryDirectory(prefix='physics-layout-') as d:
    d=Path(d);cpp=d/'layout.cpp';exe=d/'layout'
    cpp.write_text('#include "colliderpair.h"\n#include <platformator/rigidbody.h>\n#include <cstdio>\nint main(){printf("%zu %zu\\n",sizeof(ColliderPair),sizeof(Rigidbody));}\n')
    subprocess.run([tokens[0],*flags,'-I'+str(source/'src'),str(cpp),'-o',str(exe)],cwd=entry['directory'],check=True)
    pair,body=map(int,subprocess.check_output([str(exe)],text=True).split())
runner=source/'bin/cleanup-benchmark/platformator_benchmark_runner'
command=['taskset','-c','0',str(runner),'--scenario','persistent_contacts','--contact-pair-count','1000',
         '--scripted-endpoints','2','--workers','1','--warmup-frames','120','--measure-frames','600']
peaks=[]
for i in range(a.repetitions):
    with (a.output/f'run-{i}.log').open('w') as log:
        child=os.fork()
        if child==0:
            os.dup2(log.fileno(),1);os.dup2(log.fileno(),2)
            os.execvpe(command[0],command,dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software'))
        _,status,usage=os.wait4(child,0)
        assert os.waitstatus_to_exitcode(status)==0
        peaks.append(usage.ru_maxrss)
report={'source':str(source),'layout_compiler_flags':flags,'pair_bytes':pair,'body_bytes':body,
        'runner_command':command,'peak_rss_kib':peaks,'method':'wait4 per child, not inherited shell/compiler peak'}
(a.output/'measurements.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ('pair_bytes','body_bytes','peak_rss_kib')}))
