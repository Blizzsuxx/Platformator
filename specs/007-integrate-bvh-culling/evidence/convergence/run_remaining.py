#!/usr/bin/env python3
"""Run remaining qualified timings and resources sequentially; abort on any ineligible stage."""
from pathlib import Path
import subprocess
import sys
HERE=Path(__file__).resolve().parent
for mode in ['performance','resource']:
    for suite in ['engine','replay','controls']:
        subprocess.run([sys.executable,str(HERE/'capture.py'),suite,mode],check=True)
