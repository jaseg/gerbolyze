#!/usr/bin/env python3

import multiprocessing as mp
import subprocess
import pathlib

import click
from tqdm import tqdm

def process_file(indir, outdir, inpath):
    outpath = outdir / inpath.relative_to(indir).with_suffix('.png')
    outpath.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['resvg', '--export-id', 'g-top-copper', '--width', '1000', inpath, outpath],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
 
@click.command()
@click.argument('indir', type=click.Path(exists=True, file_okay=False, dir_okay=True, path_type=pathlib.Path))
def export(indir):
    jobs = list(indir.glob('svg/**/*.svg'))
    with tqdm(total = len(jobs)) as tq:
        with mp.Pool() as pool:
            results = [ pool.apply_async(process_file, (indir / 'svg', indir / 'png', path), callback=lambda _res: tq.update(1)) for path in jobs ]
            results = [ res.get() for res in results ]

if __name__ == '__main__':
    export()
