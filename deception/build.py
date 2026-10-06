#!/usr/bin/env python3
"""python deception/build.py [NINJA ARGS]: configure and build the GC 2.7 frontend reconstruction, the oracle and the
hybrid (see deception/configure.py)."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, "deception/configure.py"], cwd=ROOT, check=True)
sys.exit(subprocess.run(["ninja", "-f", "build/deception/build.ninja", *sys.argv[1:]], cwd=ROOT).returncode)
