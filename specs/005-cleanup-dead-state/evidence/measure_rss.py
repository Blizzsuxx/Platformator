#!/usr/bin/env python3
"""Measure per-child peak RSS for the validated pair-churn fixture, not timing."""
import json
import os
from pathlib import Path
import sys

baseline, candidate, output = map(Path, sys.argv[1:4])
output.mkdir(parents=True, exist_ok=False)
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')
measurements = []
for name, source in [('baseline', baseline), ('candidate', candidate)]:
    for workers, cpus in [(1, '0'), (6, '0,2,4,6,8,10')]:
        label = f'{name}-w{workers}'
        command = ['taskset', '-c', cpus, str(source / 'bin/cleanup-benchmark/platformator_dead_state_pair_churn_probe'), '1000', str(workers), '120', '600', '1', str(output / f'{label}.csv')]
        pid = os.fork()
        if pid == 0:
            fd = os.open(output / f'{label}.log', os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o644)
            os.dup2(fd, 1); os.dup2(fd, 2); os.close(fd)
            os.chdir(source)
            os.execvpe(command[0], command, env)
        _, status, usage = os.wait4(pid, 0)
        result = {'label': label, 'command': command, 'exit': os.waitstatus_to_exitcode(status), 'peak_rss_kib': usage.ru_maxrss}
        measurements.append(result)
        (output / 'results.json').write_text(json.dumps(measurements, indent=2) + '\n')
        if result['exit']:
            raise SystemExit(f'{label} failed: {result["exit"]}')
