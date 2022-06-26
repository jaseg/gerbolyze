#!/usr/bin/env python3

import itertools
import pathlib
import textwrap

import click

from gerbolyze.protoboard import ProtoBoard, EmptyProtoArea, THTProtoAreaCircles, SMDProtoAreaRectangles

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
    return ProtoBoard(common_defs, 'tht', mounting_holes, border=2)

def tht_pitch_50mil(size, mounting_holes=None):
    return ProtoBoard(common_defs, 'tht50', mounting_holes, border=2)

def tht_mixed_pitch(size, mounting_holes=None):
    w, h = size
    f = max(1.27*5, min(30, h*0.3))
    return ProtoBoard(common_defs, f'tht50@{f}mm / tht', mounting_holes, border=2, tight_layout=True)

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

def generate(outdir, fun, sizes=sizes_large, name=None, generate_svg=True):
    name = name or fun.__name__
    outdir = outdir / f'{name}'
    plain_dir = outdir / 'no_mounting_holes'
    plain_dir.mkdir(parents=True, exist_ok=True)

    for w, h in sizes:
        outfile = plain_dir / f'{name}_{w}x{h}.svg'
        board = fun((w, h))
        yield outfile, (float(w), float(h), None, board.symmetric_sides, board.used_patterns)
        if generate_svg:
            outfile.write_text(board.generate(w, h))

    for dia in (2, 2.5, 3, 4):
        hole_dir  = outdir / f'mounting_holes_M{dia:.1f}'
        hole_dir.mkdir(exist_ok=True)

        for w, h in sizes:
            if w < 25 or h < 25:
                continue
            outfile = hole_dir / f'{name}_{w}x{h}_holes_M{dia:.1f}.svg'
            try:
                board = fun((w, h), (dia, dia+2))
                yield outfile, (float(w), float(h), float(dia), board.symmetric_sides, board.used_patterns)
                if generate_svg:
                    outfile.write_text(board.generate(w, h))
            except ValueError: # mounting hole keepout too large for small board, ignore.
                pass


def write_index(index, outdir):
    tht_pitches = lambda patterns: [ p.pitch for p in patterns if isinstance(p, THTProtoAreaCircles) ]
    smd_pitches = lambda patterns: [ min(p.pitch_x, p.pitch_y) for p in patterns if isinstance(p, SMDProtoAreaRectangles) ]
    has_ground_plane = lambda patterns: any(isinstance(p, EmptyProtoArea) and p.copper for p in patterns)
    format_pitches = lambda pitches: ', '.join(f'{p:.2f} mm' for p in sorted(pitches))
    format_length = lambda length_or_none, default='': default if length_or_none is None else f'{length_or_none:.2f} mm'

    table_rows = [
            ('<tr>'
            f'<td><a href="gerber/{path.relative_to(outdir / "svg").with_suffix(".zip")}" download>Gerbers</a></td>'
            f'<td><a href="png/{path.relative_to(outdir / "svg").with_suffix(".png")}">Preview</a></td>'
            f'<td><a href="{path.relative_to(outdir)}" download>SVG</a></td>'
            f'<td>{w:.2f} mm</td>'
            f'<td>{h:.2f} mm</td>'
            f'<td>{"Yes" if hole_dia is not None else "No"}</td>'
            f'<td>{format_length(hole_dia)}</td>'
            f'<td>{len(patterns)}</td>'
            f'<td>{"Yes" if symmetric else "No"}</td>'
            f'<td>{"Yes" if has_ground_plane(patterns) else "No"}</td>'
            f'<td>{format_pitches(tht_pitches(patterns))}</td>'
            f'<td>{format_pitches(smd_pitches(patterns))}</td>'
            '</tr>')
            for path, (w, h, hole_dia, symmetric, patterns) in index.items()
            ]
    table_content = '\n'.join(table_rows)
    length_sort = lambda length: float(length.partition(' ')[0])
    filter_cols = {
            'Width': sorted(set(w for w, h, *rest in index.values())),
            'Height': sorted(set(h for w, h, *rest in index.values())),
            'Mounting Hole Diameter': sorted(set(dia for w, h, dia, *rest in index.values() if dia)) + ['None'],
            'Number of Areas': sorted(set(len(patterns) for *_rest, patterns in index.values())),
            'Symmetric Top and Bottom?': ['Yes', 'No'],
            'Ground Plane?': ['Yes', 'No'],
            'THT Pitches': sorted(set(p for *_rest, patterns in index.values() for p in tht_pitches(patterns))) + ['None'],
            'SMD Pitches': sorted(set(p for *_rest, patterns in index.values() for p in smd_pitches(patterns))) + ['None'],
            }
    filter_headers = '\n'.join(f'<th>{key}</th>' for key in filter_cols)
    key_id = lambda key: key.lower().replace("?", "").replace(" ", "_")
    val_id = lambda value: str(value).replace(".", "_")

    def format_value(value):
        if isinstance(value, str):
            return value
        elif isinstance(value, int):
            return str(value)
        elif isinstance(value, bool):
            return value and 'Yes' or 'No'
        else:
            return format_length(value)

    filter_cols = {
            key: '\n'.join(f'<div class="filter-check"><input type="checkbox" id="check-{key_id(key)}-{val_id(value)}"><label for="check-{key_id(key)}-{val_id(value)}">{format_value(value)}</label></div>' for value in values)
            for key, values in filter_cols.items() }
    filter_cols = [f'<td id="filter-{key_id(key)}">{values}</td>' for key, values in filter_cols.items()]
    filter_content = '\n'.join(filter_cols)

    filter_js = textwrap.dedent('''
    function get_filters(){
        let filters = {};
        table = document.querySelector('#filter');
        for (let filter of table.querySelectorAll('td')) {
            selected = [];
            for (let checkbox of filter.querySelectorAll('input')) {
                if (checkbox.checked) {
                    selected.push(checkbox.nextElementSibling.textContent);
                }
            }
            filters[filter.id.replace(/^filter-/, '')] = selected;
        }
        return filters;
    }

    filter_indices = {
    };
    for (const [i, header] of document.querySelectorAll("#listing th").entries()) {
        if (header.hasAttribute('data-filter-key')) {
            filter_indices[header.attributes['data-filter-key'].value] = i;
        }
    }

    function filter_row(filters, row) {
        cols = row.querySelectorAll('td');

        for (const [filter_id, values] of Object.entries(filters)) {
            if (values.length == 0) {
                continue;
            }

            const row_value = cols[filter_indices[filter_id]].textContent;

            if (values.includes("None") && !row_value) {
                continue;
            }

            if (values.includes(row_value)) {
                continue;
            }

            return false;
        }

        return true;
    }

    let timeout = undefined;
    function apply_filters() {
        if (timeout) {
            clearTimeout(timeout);
            timeout = undefined;
        }
        const filters = get_filters();
        for (let row of document.querySelectorAll("#listing tbody tr")) {
            if (filter_row(filters, row)) {
                row.style.display = '';
            } else {
                row.style.display = 'none';
            }
        }
    }

    function refresh_filters() {
        if (timeout) {
            clearTimeout(timeout);
        }
        timeout = setTimeout(apply_filters, 2000);
    }

    function reset_filters() {
        for (let checkbox of document.querySelectorAll("#filter input")) {
            checkbox.checked = false;
        }
        refresh_filters();
    }

    document.querySelector("#apply").onclick = apply_filters;
    document.querySelector("#reset-filters").onclick = reset_filters;
    for (let checkbox of document.querySelectorAll("#filter input")) {
        checkbox.onchange = refresh_filters;
    }

    apply_filters();
    '''.strip())

    style = textwrap.dedent('''
    :root {
      --gray1: #d0d0d0;
      --gray2: #eeeeee;
      font-family: sans-serif;
    }

    table {
      table-layout: fixed;
      border-collapse: collapse;
      box-shadow: 0 0 3px gray;
    }

    td {
      border: 1px solid var(--gray1);
      padding: .1em .5em;
    }

    th {
      border: 1px solid var(--gray1);
      padding: .5em;
      background: linear-gradient(0deg, #e0e0e0, #eeeeee);
    }

    #listing tr:hover {
      background-color: #ffff80;
    }

    button {
      margin: 2em 0.2em;
      padding: .5em 1em;
    }
    '''.strip())
    html = textwrap.dedent(f'''
    <!DOCTYPE html>
    <html>
    <head><title>Protoboard Index</title></head>
    <script src="tablesort.min.js"></script>
    <script src="tablesort.number.min.js"></script>
    <style>
    {style}
    </style>
    <body>
    <div id="filters-container">
    <table id="filter">
    <tr>
        {filter_headers}
    </tr>
    <tr>
        {filter_content}
    </tr>
    </table>
    <button type="button" id="apply">Apply</button>
    <button type="button" id="reset-filters">Reset filters</button>
    </div>
    <div id="listing-container">
    <table id="listing">
    <thead>
    <tr>
        <th data-sort-method="none">Download</th>
        <th data-sort-method="none">Preview</th>
        <th data-sort-method="none">Source SVG</th>
        <th data-filter-key="width">Width</th>
        <th data-filter-key="height">Height</th>
        <th>Has Mounting Holes?</th>
        <th data-filter-key="mounting_hole_diameter">Mounting Hole Diameter</th>
        <th data-filter-key="number_of_areas">Number of Areas</th>
        <th data-filter-key="symmetric_top_and_bottom">Symmetric Top and Bottom?</th>
        <th data-filter-key="ground_plane">Ground Plane?</th>
        <th data-filter-key="tht_pitches">THT Pitches</th>
        <th data-filter-key="smd_pitches">SMD Pitches</th>
    </tr>
    </thead>
    <tbody>
        {table_content}
    </tbody>
    </table>
    </div>
    <script>
    new Tablesort(document.getElementById('listing'));

    {filter_js}
    </script>
    </body>
    </html>
    '''.strip())
    (outdir / 'index.html').write_text(html) 


@click.command()
@click.argument('outdir', type=click.Path(file_okay=False, dir_okay=True, path_type=pathlib.Path))
@click.option('--generate-svg/--no-generate-svg')
def generate_all(outdir, generate_svg):
    index = {}

    index.update(generate(outdir / 'svg' / 'simple', tht_normal_pitch100mil, generate_svg=generate_svg))
    index.update(generate(outdir / 'svg' / 'simple', tht_pitch_50mil, generate_svg=generate_svg))
    index.update(generate(outdir / 'svg' / 'mixed', tht_mixed_pitch, generate_svg=generate_svg))

    for pattern, name in smd_basic.items():
        def gen(size, mounting_holes=None):
            return ProtoBoard(common_defs, f'{pattern} + ground', mounting_holes, border=1)
        index.update(generate(outdir / 'svg' / 'simple', gen, sizes_small, name=f'{name}_ground_plane', generate_svg=generate_svg))

        def gen(size, mounting_holes=None):
            return ProtoBoard(common_defs, f'{pattern} + empty', mounting_holes, border=1)
        index.update(generate(outdir / 'svg' / 'simple', gen, sizes_small, name=f'{name}_single_side', generate_svg=generate_svg))

        def gen(size, mounting_holes=None):
            return ProtoBoard(common_defs, f'{pattern} + {pattern}', mounting_holes, border=1)
        index.update(generate(outdir / 'svg' / 'simple', gen, sizes_small, name=f'{name}_double_side', generate_svg=generate_svg))

        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'({pattern} + {pattern})@{f}mm / tht', mounting_holes, border=1, tight_layout=True)
        index.update(generate(outdir / 'svg' / 'mixed', gen, sizes_small, name=f'tht_and_{name}', generate_svg=generate_svg))

        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'({pattern} + {pattern}) / tht@{f}mm', mounting_holes, border=1, tight_layout=True)
        index.update(generate(outdir / 'svg' / 'mixed', gen, sizes_small, name=f'{name}_and_tht', generate_svg=generate_svg))

        *_, suffix = name.split('_')
        if suffix not in ('100mil', '950um'):
            def gen(size, mounting_holes=None):
                w, h = size
                f = max(1.27*5, min(50, h*0.3))
                f2 = max(1.27*5, min(30, w*0.2))
                return ProtoBoard(common_defs, f'((smd100 + smd100) | (smd950 + smd950) | ({pattern}r + {pattern}r)@{f2}mm)@{f}mm / tht', mounting_holes, border=1, tight_layout=True)
            index.update(generate(outdir / 'svg' / 'mixed', gen, sizes_medium, name=f'tht_and_three_smd_100mil_950um_{suffix}', generate_svg=generate_svg))

    for (pattern1, name1), (pattern2, name2) in itertools.combinations(smd_basic.items(), 2):
        *_, name1 = name1.split('_')
        *_, name2 = name2.split('_')

        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'(({pattern1} + {pattern1}) | ({pattern2} + {pattern2}))@{f}mm / tht', mounting_holes, border=1, tight_layout=True)
        index.update(generate(outdir / 'svg' / 'mixed', gen, sizes_small, name=f'tht_and_two_smd_{name1}_{name2}', generate_svg=generate_svg))

        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'({pattern1} + {pattern2})@{f}mm / tht', mounting_holes, border=1, tight_layout=True)
        index.update(generate(outdir / 'svg' / 'mixed', gen, sizes_small, name=f'tht_and_two_sided_smd_{name1}_{name2}', generate_svg=generate_svg))

        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'{pattern1} + {pattern2}', mounting_holes, border=1)
        index.update(generate(outdir / 'svg' / 'mixed', gen, sizes_small, name=f'two_sided_smd_{name1}_{name2}', generate_svg=generate_svg))

    def gen(size, mounting_holes=None):
        w, h = size
        f = max(1.27*5, min(50, h*0.3))
        f2 = max(1.27*5, min(30, w*0.2))
        return ProtoBoard(common_defs, f'((smd100 + smd100) | (smd950 + smd950) | tht50@{f2}mm)@{f}mm / tht', mounting_holes, border=1, tight_layout=True)
    index.update(generate(outdir / 'svg' / 'mixed', gen, sizes_medium, name=f'tht_and_50mil_and_two_smd_100mil_950um', generate_svg=generate_svg))

    def gen(size, mounting_holes=None):
        w, h = size
        f = max(1.27*5, min(30, h*0.3))
        f2 = max(1.27*5, min(25, w*0.1))
        return ProtoBoard(common_defs, f'tht50@10mm | tht | ((smd100r + smd100r) / (smd950r + smd950r) / (smd800 + smd800)@{f2}mm / (smd650 + smd650)@{f2}mm / (smd500 + smd500)@{f2}mm)@{f}mm', mounting_holes, border=1, tight_layout=True)
    index.update(generate(outdir / 'svg' / 'mixed', gen, [ (w, h) for w, h in sizes_medium if w > 61 and h > 60 ], name=f'all_tht_and_smd', generate_svg=generate_svg))

    write_index(index, outdir)


if __name__ == '__main__':
    generate_all()

