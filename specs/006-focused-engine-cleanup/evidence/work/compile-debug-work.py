import json,pathlib,shlex,subprocess
for stage,source in [('baseline','/tmp/platformator-006-baseline/source'),('candidate','/home/user/Projects/Platformator')]:
 build=pathlib.Path(source,'bin/cleanup-debug')
 data=json.loads((build/'compile_commands.json').read_text())
 entry=next(x for x in data if x['file'].endswith('/src/debugdraw.cpp'))
 args=shlex.split(entry['command']);args[args.index('-c')+1]='/tmp/platformator-006-baseline/debug-growth-work.cpp'
 obj=f'/tmp/platformator-006-baseline/{stage}-debug-growth.o';args[args.index('-o')+1]=obj
 subprocess.run(args,cwd=entry['directory'],check=True)
 line=subprocess.check_output(['ninja','-C',str(build),'-t','commands','platformator_physics_bookkeeping_test'],text=True).splitlines()[-1]
 args=shlex.split(line)[2:-2]
 args=[x for x in args if not x.startswith('-Wl,--dependency-file=')]
 args=[obj if x.endswith('tests/physics_bookkeeping_test.cpp.o') else x for x in args]
 args[args.index('-o')+1]=f'/tmp/platformator-006-baseline/{stage}-debug-growth'
 subprocess.run(args,cwd=build,check=True)
 result=subprocess.run([f'/tmp/platformator-006-baseline/{stage}-debug-growth'],cwd=build,check=True,capture_output=True,text=True)
 print(stage,result.stdout.strip())
 pathlib.Path(f'/tmp/platformator-006-baseline/{stage}-debug-growth.log').write_text(result.stdout)
