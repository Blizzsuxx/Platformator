import csv,hashlib,json,os,random,statistics,subprocess,sys
from pathlib import Path
repo=Path('/home/user/Projects/Platformator')
sys.path.insert(0,str(repo/'benchmark'))
import sweep_scaling as sweep
out=repo/'benchmark/results/sdl-update-9c4f3f6/paired-churn'
out.mkdir(exist_ok=False)
build=repo/'bin/cleanup-benchmark'
old_dir=Path('/tmp/platformator-sdl-update/old-runtime')
old_dir.mkdir(exist_ok=True)
old_libraries={'libSDL3.so.0':'libSDL3.so.0.4.2','libSDL3_mixer.so.0':'libSDL3_mixer.so.0.2.0',
               'libSDL3_image.so.0':'libSDL3_image.so.0.4.0','libSDL3_ttf.so.0':'libSDL3_ttf.so.0.1.0'}
for soname,filename in old_libraries.items():
    (old_dir/soname).symlink_to(build/filename)
order=[]
rng=random.Random(2)
for repetition in range(5):
    variants=['old','new'];rng.shuffle(variants)
    order.extend((variant,repetition) for variant in variants)
results={'old':[],'new':[]}
for variant,repetition in order:
    folder=out/variant;folder.mkdir(exist_ok=True)
    target=folder/f'run-{repetition:02d}.csv'
    command=['taskset','-c','0',str(build/'platformator_benchmark_runner'),'--scenario','add_and_remove',
             '--steady-count','1000','--churn-count','1000','--workers','1','--seed','0','--warmup-frames','120',
             '--measure-frames','600','--dt',str(1/120),'--repetitions','5','--run-index',str(repetition),'--csv-output',str(target)]
    env=dict(os.environ)
    env['LD_LIBRARY_PATH']=str(old_dir if variant=='old' else build)+':'+str(build)
    print(variant,repetition,flush=True)
    with target.with_suffix('.log').open('w') as log:subprocess.run(command,env=env,check=True,stdout=log,stderr=subprocess.STDOUT)
    data=sweep.parse_benchmark_csv(target);manifest=sweep.load_manifest(target)
    expected='3.4.2' if variant=='old' else '3.4.18'
    assert manifest['dependencies']['SDL']==expected,manifest['dependencies']
    assert manifest['dependencies']['SDL_mixer']==('3.2.0' if variant=='old' else '3.2.4')
    assert manifest['dependencies']['SDL_image']==('3.4.0' if variant=='old' else '3.4.8')
    assert manifest['dependencies']['SDL_ttf']==('3.1.0' if variant=='old' else '3.2.2')
    results[variant].append(data['scope.complete_step.avg'])
summary={'scenario':'add_and_remove','churn':1000,'steady':1000,'workers':1,'seed':0,'warmup':120,'steps':600,'dt':1/120,
         'order':order,'method':'Same current runner and headers; interleaved retained old/new Release SDL runtime libraries. Version functions verify loading. No rebuild between variants.',
         'old_library_sha256':{filename:hashlib.sha256((build/filename).read_bytes()).hexdigest() for filename in old_libraries.values()},
         'samples_ms':results,'statistics':{variant:{'mean_ms':statistics.mean(values),'stdev_ms':statistics.stdev(values),
                                                    'min_ms':min(values),'max_ms':max(values)} for variant,values in results.items()}}
summary['difference_percent']=100*(summary['statistics']['new']['mean_ms']/summary['statistics']['old']['mean_ms']-1)
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary['statistics']),flush=True)
print('new vs old %:',summary['difference_percent'],flush=True)
