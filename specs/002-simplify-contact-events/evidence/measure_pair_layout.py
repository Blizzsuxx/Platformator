"""Measure pair object layout using the recorded baseline declaration and current build flags."""
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

root=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='platformator-pair-layout-') as directory:
    directory=Path(directory)
    original=subprocess.check_output(['git','show','f67f6ba47f88f08ee2dd2aae1ea8eee940aa07d8:src/colliderpair.h'],cwd=root)
    (directory/'baseline_pair.h').write_bytes(original)
    entry=next(row for row in json.loads((root/'bin/cleanup-debug/compile_commands.json').read_text())
               if row['file'].endswith('/src/physicsmanager.cpp'))
    tokens=shlex.split(entry['command']); flags=[]; skip=False
    for token in tokens[1:]:
        if skip:skip=False;continue
        if token in ('-o','-MF','-MT','-MQ'):skip=True;continue
        if token in ('-c','-MD','-MMD'):continue
        if token.endswith('.cpp'):continue
        flags.append(token)
    result={}
    for label,header in [('baseline','baseline_pair.h'),('candidate','colliderpair.h')]:
        source=directory/(label+'.cpp');binary=directory/label
        source.write_text('#include "'+header+'"\n#include <cstdio>\nint main(){std::printf("%zu\\n",sizeof(ColliderPair));}\n')
        subprocess.run([tokens[0],*flags,'-I'+str(root/'src'),str(source),'-o',str(binary)],cwd=entry['directory'],check=True)
        result[label]=int(subprocess.check_output([str(binary)],text=True))
    result['bytes_removed_per_pair']=result['baseline']-result['candidate']
    print(json.dumps(result,indent=2))
