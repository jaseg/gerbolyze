
import os
import subprocess
from pathlib import Path

def pytest_sessionstart(session):
    if 'PYTEST_XDIST_WORKER' in os.environ: # only run this on the controller
        return

    # Rebuild svg-flatten
    subprocess.run(['make', '-C', Path(__file__).parent.parent / 'svg-flatten', '-j'], check=True)

