"""Measure the runner child directly; inherited shell/compiler high-water marks are excluded."""
import json
import os
from pathlib import Path

root=Path(__file__).resolve().parents[3]
runner=root/'bin/cleanup-benchmark/platformator_benchmark_runner'
command=[str(runner),'--scenario','persistent_contacts','--contact-pair-count','1000',
         '--scripted-endpoints','2','--workers','1','--warmup-frames','120','--measure-frames','600']
child=os.fork()
if child==0:
    os.execv(command[0],command)
_,status,usage=os.wait4(child,0)
exitcode=os.waitstatus_to_exitcode(status)
print(json.dumps({'runner_peak_rss_kib':usage.ru_maxrss,'exit_code':exitcode,'method':'wait4 per child'}))
raise SystemExit(exitcode)
