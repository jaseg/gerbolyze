#!/usr/bin/env python3

import itertools
import pathlib

import click

from gerbolyze.protoboard import ProtoBoard

common_defs = '''
empty = Empty(copper=False);
ground = Empty(copper=True);

tht = THTCircles();
tht50 = THTCircles(pad_dia=1.0, drill=0.6, pitch=1.27);

smd100 = SMDPads(1.27, 2.54);
smd100r = SMDPads(2.54, 1.27);
smd950 = SMDPads(0.95, 2.5);
smd950r = SMDPads(2.5, 0.95);
smd800 = SMDPads(0.80, 2.0);
smd800r = SMDPads(2.0, 0.80);
smd650 = SMDPads(0.65, 2.0);
smd650r = SMDPads(2.0, 0.65);
smd500 = SMDPads(0.5, 2.0);
smd500r = SMDPads(2.0, 0.5);
'''


def tht_normal_pitch100mil(size, mounting_holes=None):
    return ProtoBoard(common_defs, 'tht', mounting_holes, border=2).generate(*size)

def tht_pitch_50mil(size, mounting_holes=None):
    return ProtoBoard(common_defs, 'tht50', mounting_holes, border=2).generate(*size)

def tht_mixed_pitch(size, mounting_holes=None):
    w, h = size
    f = max(1.27*5, min(30, h*0.3))
    return ProtoBoard(common_defs, f'tht50@{f}mm / tht', mounting_holes, border=2).generate(*size)

smd_basic = {
        'smd100': 'smd_soic_100mil',
        'smd950': 'smd_sot_950um',
        'smd800': 'smd_sop_800um',
        'smd650': 'smd_sot_650um',
        'smd500': 'smd_sop_500um' }

#lengths_large = [15, 20, 25, 30, 35, 40, 45, 50, 60, 70, 80, 90, 100, 120, 150, 160, 180, 200, 250, 300]
lengths_large = [30, 40, 50, 60, 80, 100, 120, 150, 160]
sizes_large = list(itertools.combinations(lengths_large, 2))

lengths_small = [15, 20, 25, 30, 40, 50, 60, 80, 100]
sizes_small = list(itertools.combinations(lengths_small, 2))

lengths_medium = lengths_large
sizes_medium = list(itertools.combinations(lengths_medium, 2))

def generate(outdir, fun, sizes=sizes_large, name=None):
    name = name or fun.__name__
    outdir = outdir / f'{name}'
    plain_dir = outdir / 'no_mounting_holes'
    plain_dir.mkdir(parents=True, exist_ok=True)

    for w, h in sizes:
        outfile = plain_dir / f'{name}_{w}x{h}.svg'
        outfile.write_text(fun((w, h)))

    for dia in (2, 2.5, 3, 4):
        hole_dir  = outdir / f'mounting_holes_M{dia:.1f}'
        hole_dir.mkdir(exist_ok=True)

        for w, h in sizes:
            if w < 25 or h < 25:
                continue
            outfile = hole_dir / f'{name}_{w}x{h}_holes_M{dia:.1f}.svg'
            try:
                outfile.write_text(fun((w, h), (dia, dia+2)))
            except ValueError: # mounting hole keepout too large for small board, ignore.
                pass


@click.command()
@click.argument('outdir', type=click.Path(file_okay=False, dir_okay=True, path_type=pathlib.Path))
def generate_all(outdir):
    generate(outdir / 'simple', tht_normal_pitch100mil)
    generate(outdir / 'simple', tht_pitch_50mil)
    generate(outdir / 'mixed', tht_mixed_pitch)

    for pattern, name in smd_basic.items():
        def gen(size, mounting_holes=None):
            return ProtoBoard(common_defs, f'{pattern} + ground', mounting_holes, border=1).generate(*size)
        generate(outdir / 'simple', gen, sizes_small, name=f'{name}_ground_plane')

        def gen(size, mounting_holes=None):
            return ProtoBoard(common_defs, f'{pattern} + empty', mounting_holes, border=1).generate(*size)
        generate(outdir / 'simple', gen, sizes_small, name=f'{name}_single_side')

        def gen(size, mounting_holes=None):
            return ProtoBoard(common_defs, f'{pattern} + {pattern}', mounting_holes, border=1).generate(*size)
        generate(outdir / 'simple', gen, sizes_small, name=f'{name}_double_side')

        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'({pattern} + {pattern})@{f}mm / tht', mounting_holes, border=1).generate(*size)
        generate(outdir / 'mixed', gen, sizes_small, name=f'tht_and_{name}')

        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'({pattern} + {pattern}) / tht@{f}mm', mounting_holes, border=1).generate(*size)
        generate(outdir / 'mixed', gen, sizes_small, name=f'{name}_and_tht')

        *_, suffix = name.split('_')
        if suffix not in ('100mil', '950um'):
            def gen(size, mounting_holes=None):
                w, h = size
                f = max(1.27*5, min(50, h*0.3))
                f2 = max(1.27*5, min(30, w*0.2))
                return ProtoBoard(common_defs, f'((smd100 + smd100) | (smd950 + smd950) | ({pattern}r + {pattern}r)@{f2}mm)@{f}mm / tht', mounting_holes, border=1).generate(*size)
            generate(outdir / 'mixed', gen, sizes_medium, name=f'tht_and_three_smd_100mil_950um_{suffix}')

    for (pattern1, name1), (pattern2, name2) in itertools.combinations(smd_basic.items(), 2):
        *_, name1 = name1.split('_')
        *_, name2 = name2.split('_')

        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'(({pattern1} + {pattern1}) | ({pattern2} + {pattern2}))@{f}mm / tht', mounting_holes, border=1).generate(*size)
        generate(outdir / 'mixed', gen, sizes_small, name=f'tht_and_two_smd_{name1}_{name2}')

        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'({pattern1} + {pattern2})@{f}mm / tht', mounting_holes, border=1).generate(*size)
        generate(outdir / 'mixed', gen, sizes_small, name=f'tht_and_two_sided_smd_{name1}_{name2}')

        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'{pattern1} + {pattern2}', mounting_holes, border=1).generate(*size)
        generate(outdir / 'mixed', gen, sizes_small, name=f'two_sided_smd_{name1}_{name2}')

    def gen(size, mounting_holes=None):
        w, h = size
        f = max(1.27*5, min(50, h*0.3))
        f2 = max(1.27*5, min(30, w*0.2))
        return ProtoBoard(common_defs, f'((smd100 + smd100) | (smd950 + smd950) | tht50@{f2}mm)@{f}mm / tht', mounting_holes, border=1).generate(*size)
    generate(outdir / 'mixed', gen, sizes_medium, name=f'tht_and_50mil_and_two_smd_100mil_950um_{suffix}')

    def gen(size, mounting_holes=None):
        w, h = size
        f = max(1.27*5, min(30, h*0.3))
        f2 = max(1.27*5, min(25, w*0.1))
        return ProtoBoard(common_defs, f'tht50@10mm | tht | ((smd100r + smd100r) / (smd950r + smd950r) / (smd800 + smd800)@{f2}mm / (smd650 + smd650)@{f2}mm / (smd500 + smd500)@{f2}mm)@{f}mm', mounting_holes, border=1).generate(*size)
    generate(outdir / 'mixed', gen, [ (w, h) for w, h in sizes_medium if w > 60 and h > 60 ], name=f'all_tht_and_smd')


if __name__ == '__main__':
    generate_all()

