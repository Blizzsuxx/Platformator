import json,pathlib,shlex,subprocess
for stage,source in [('baseline','/tmp/platformator-006-baseline/source'),('candidate','/home/user/Projects/Platformator')]:
 build=pathlib.Path(source,'bin/production')
 entry=next(x for x in json.loads((build/'compile_commands.json').read_text()) if x['file'].endswith('/examples/mario/mario_camera_rig.cpp'))
 args=shlex.split(entry['command']);args[args.index('-c')+1]='/tmp/platformator-006-baseline/mario-roundtrip.cpp'
 obj=f'/tmp/platformator-006-baseline/{stage}-mario-roundtrip.o';args[args.index('-o')+1]=obj
 subprocess.run(args,cwd=entry['directory'],check=True)
 line=subprocess.check_output(['ninja','-C',str(build),'-t','commands','mario_example'],text=True).splitlines()[-1]
 args=shlex.split(line)[2:];args=args[:args.index('&&')]
 args=[x for x in args if not x.startswith('-Wl,--dependency-file=')]
 args=[obj if x.endswith('mario_example.dir/main.cpp.o') else x for x in args]
 exe=f'/tmp/platformator-006-baseline/{stage}-mario-roundtrip';args[args.index('-o')+1]=exe
 subprocess.run(args,cwd=build,check=True)
 result=subprocess.run([exe,str(pathlib.Path(source,'examples/mario/level1.scene')),str(build/'assets')],cwd=build,check=True,capture_output=True,text=True)
 print(stage,result.stdout.strip())
 pathlib.Path(f'/tmp/platformator-006-baseline/{stage}-mario-roundtrip.log').write_text(result.stdout)
