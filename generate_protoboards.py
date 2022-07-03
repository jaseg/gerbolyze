#!/usr/bin/env python3

import itertools
import pathlib
import textwrap

import click

from gerbolyze.protoboard import ProtoBoard, EmptyProtoArea, THTProtoArea, SMDProtoAreaRectangles, ManhattanProtoArea

common_defs = '''
empty = Empty(copper=False);
ground = Empty(copper=True);

tht = THTPads();
thtsq = THTPads(pad_shape="square");
thtl = THTPads(drill=1.2);
thtxl = THTPads(drill=1.6, pad_size=2.1, pad_shape="square");
tht50 = THTPads(pad_size=1.0, drill=0.6, pitch=1.27);
tht50sq = THTPads(pad_size=1.0, drill=0.6, pitch=1.27, pad_shape="square");
manhattan = Manhattan();

conn125 = THTPads(drill=0.6, pad_size=1.0, pitch=1.25);
conn250 = THTPads(drill=1.0, pad_size=1.6, pitch=2.00);
conn200 = THTPads(drill=1.2, pad_size=2.0, pitch=2.50);
conn350 = THTPads(drill=1.6, pad_size=2.8, pitch=3.50);
conn396 = THTPads(drill=1.6, pad_size=2.8, pitch=3.96);

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


smd_basic = {
        'smd100': 'smd_soic_100mil',
        'smd950': 'smd_sot_950um',
        'smd800': 'smd_sop_800um',
        'smd650': 'smd_sot_650um',
        'smd500': 'smd_sop_500um',
        'manhattan': 'manhattan_400mil'}

connector_pitches = {
        'tht50': '50mil',
        'conn125': '1.25mm',
        'conn200': '2.00mm',
        'conn250': '2.50mm',
        'conn350': '3.50mm',
        'conn396': '3.96mm',
        }

#lengths_large = [15, 20, 25, 30, 35, 40, 45, 50, 60, 70, 80, 90, 100, 120, 150, 160, 180, 200, 250, 300]
lengths_large = [30, 40, 50, 60, 80, 100, 120, 150, 160]
sizes_large = list(itertools.combinations(lengths_large, 2))

lengths_small = [15, 20, 25, 30, 40, 50, 60, 80, 100]
sizes_small = list(itertools.combinations(lengths_small, 2))

lengths_medium = lengths_large
sizes_medium = list(itertools.combinations(lengths_medium, 2))

def min_dim(sizes, dim):
    return [(w, h) for w, h in sizes if w > dim and h > dim]

def write_index(index, outdir):
    tht_pitches = lambda patterns: [ p.pitch for p in patterns if isinstance(p, THTProtoArea) ]
    smd_pitches = lambda patterns: [ min(p.pitch_x, p.pitch_y) for p in patterns if isinstance(p, SMDProtoAreaRectangles) ]
    has_ground_plane = lambda patterns: any(isinstance(p, EmptyProtoArea) and p.copper for p in patterns)
    has_manhattan_area = lambda patterns: any(isinstance(p, ManhattanProtoArea) for p in patterns)
    has_square_pads = lambda patterns: any(isinstance(p, THTProtoArea) and p.pad_shape == 'square' for p in patterns)
    has_large_holes = lambda patterns: any(isinstance(p, THTProtoArea) and abs(p.pitch_x - 2.54) < 0.01 and p.drill > 1.1 for p in patterns)
    format_pitches = lambda pitches: ', '.join(f'{p:.2f}' for p in sorted(pitches))
    format_length = lambda length_or_none, default='': default if length_or_none is None else f'{length_or_none:.2f} mm'
    area_count = lambda patterns: len(set(p for p in patterns if not isinstance(p, EmptyProtoArea)))

    table_rows = [
            ('<tr>'
            f'<td><a href="gerber/{path.relative_to(outdir / "svg").with_suffix(".zip")}" download>Gerber</a></td>'
            f'<td><a href="png/{path.relative_to(outdir / "svg").with_suffix(".png")}">Preview</a></td>'
            f'<td><a href="{path.relative_to(outdir)}" download>SVG</a></td>'
            f'<td>{w:.2f}</td>'
            f'<td>{h:.2f}</td>'
            f'<td>{"Yes" if hole_dia is not None else "No"}</td>'
            f'<td>{f"{hole_dia:.2f}" if hole_dia is not None else ""}</td>'
            f'<td>{area_count(patterns)}</td>'
            f'<td>{"Yes" if symmetric else "No"}</td>'
            f'<td>{"Yes" if has_ground_plane(patterns) else "No"}</td>'
            f'<td>{"Yes" if has_manhattan_area(patterns) else "No"}</td>'
            f'<td>{"Yes" if has_square_pads(patterns) else "No"}</td>'
            f'<td>{"Yes" if has_large_holes(patterns) else "No"}</td>'
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
            'Number of Areas': sorted(set(area_count(patterns) for *_rest, patterns in index.values())),
            'Symmetric Top and Bottom?': ['Yes', 'No'],
            'Ground Plane?': ['Yes', 'No'],
            'Manhattan Area?': ['Yes', 'No'],
            'Square Pads?': ['Yes', 'No'],
            'Large Holes?': ['Yes', 'No'],
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
                    selected.push(checkbox.nextElementSibling.textContent.replace(/ mm$/, ''));
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
      border-collapse: collapse;
      box-shadow: 0 0 3px gray;
      width: 100%;
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
    
    #listing tr td {
        text-align: center;
    }

    #listing tr td:nth-child(4), #listing tr td:nth-child(5) {
        text-align: right;
    }

    #filter {
        margin-top: 2em;
    }

    button {
      margin: 2em 0.2em;
      padding: .5em 1em;
    }

    body {
        max-width: 80em;
        margin: 3em auto;
    }

    body > div {
        width: 100%;
    }
    '''.strip())
    html = textwrap.dedent(f'''
    <!DOCTYPE html>
    <html>
    <head><title>Gerbolyze Protoboard Index</title></head>
    <script src="tablesort.min.js"></script>
    <script src="tablesort.number.min.js"></script>
    <style>
    {style}
    </style>
    <body>
    <h1>Gerbolyze Protoboard Index</h1>
    <p>
    This page contains gerbers for many different types of prototype circuit boards. Everything from different pitches
    of THT hole patterns to SMD pad patterns is included in many different sizes and with several mounting hole options.
    </p>

    <p>
    All downloads on this page are licensed under the <a href="https://unlicense.org">Unlicense</a>. This means you can
    download what you like and do with it whatever you want. Just note that everything here is provided without any
    warranty, so if you send files you find here to a pcb board house and what you get back from them is all wrong,
    that's your problem.
    </p>

    <p>
    All files on this page have been generated automatically from a number of templates using
    <a href="https://gitlab.com/gerbolyze/gerbolyze/">gerbolyze</a>
    (<a href="https://github.com/jaseg/gerbolyze">github mirror</a>). If you have any suggestions for additional layouts
    or layout options, please feel free to file an issue on
    <a href="https://github.com/jaseg/gerbolyze/issues">Gerbolyze's issue tracker</a> on github.
    </p>
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
        <th data-sort-method="none" width="6em">Download</th>
        <th data-sort-method="none" width="6em">Preview</th>
        <th data-sort-method="none" width="3em">Source SVG</th>
        <th data-filter-key="width" width="3.5em">Width [mm]</th>
        <th data-filter-key="height" width="3.5em">Height [mm]</th>
        <th width="3em">Has Mounting Holes?</th>
        <th data-filter-key="mounting_hole_diameter" width="3em">Mounting Hole Diameter [mm]</th>
        <th data-filter-key="number_of_areas" width="3em">Number of Areas</th>
        <th data-filter-key="symmetric_top_and_bottom" width="3em">Symmetric Top and Bottom?</th>
        <th data-filter-key="ground_plane" width="3em">Ground Plane?</th>
        <th data-filter-key="manhattan_area" width="3em">Manhattan Area?</th>
        <th data-filter-key="square_pads" width="3em">Square Pads?</th>
        <th data-filter-key="large_holes" width="3em">Large Holes?</th>
        <th data-filter-key="tht_pitches">THT Pitches [mm]</th>
        <th data-filter-key="smd_pitches">SMD Pitches [mm]</th>
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
                # Add 0.2 mm tolerance to mounting holes for easier insertion of screw
                board = fun((w, h), (dia+0.2, dia+2))
                yield outfile, (float(w), float(h), float(dia), board.symmetric_sides, board.used_patterns)
                if generate_svg:
                    outfile.write_text(board.generate(w, h))
            except ValueError: # mounting hole keepout too large for small board, ignore.
                pass

@click.command()
@click.argument('outdir', type=click.Path(file_okay=False, dir_okay=True, path_type=pathlib.Path))
@click.option('--generate-svg/--no-generate-svg')
def generate_all(outdir, generate_svg):
    index_d = {}
    def index(sizes=sizes_large, name=None):
        def deco(fun):
            nonlocal index_d
            index_d.update(generate(outdir / 'svg', fun, sizes=sizes, name=name, generate_svg=generate_svg))
            return fun
        return deco

    @index()
    def tht_normal_pitch100mil(size, mounting_holes=None):
        return ProtoBoard(common_defs, 'tht', mounting_holes, border=2)

    @index()
    def tht_normal_pitch100mil_large_holes(size, mounting_holes=None):
        return ProtoBoard(common_defs, 'thtl', mounting_holes, border=2)

    @index()
    def tht_normal_pitch100mil_xl_holes(size, mounting_holes=None):
        return ProtoBoard(common_defs, 'thtl', mounting_holes, border=2)

    @index()
    def tht_normal_pitch100mil_square_pads(size, mounting_holes=None):
        return ProtoBoard(common_defs, 'thtl', mounting_holes, border=2)

    @index()
    def tht_pitch_50mil(size, mounting_holes=None):
        return ProtoBoard(common_defs, 'tht50', mounting_holes, border=2)

    @index()
    def tht_pitch_50mil_square_pads(size, mounting_holes=None):
        return ProtoBoard(common_defs, 'tht50', mounting_holes, border=2)

    @index()
    def tht_mixed_pitch(size, mounting_holes=None):
        w, h = size
        f = max(1.27*5, min(30, h*0.3))
        return ProtoBoard(common_defs, f'tht50@{f}mm / tht', mounting_holes, border=2, tight_layout=True)

    @index()
    def tht_mixed_pitch_square_pads(size, mounting_holes=None):
        w, h = size
        f = max(1.27*5, min(30, h*0.3))
        return ProtoBoard(common_defs, f'tht50@{f}mm / tht', mounting_holes, border=2, tight_layout=True)

    for pattern, name in connector_pitches.items():
        @index(name=f'tht_and_connector_area_{name}')
        def tht_and_connector_area(size, mounting_holes=None):
            w, h = size
            f = max(3.96*2.1, min(15, h*0.1))
            return ProtoBoard(common_defs, f'{pattern}@{f}mm / tht', border=2, tight_layout=True)

    @index()
    def tht_and_connector_areas(size, mounting_holes=None):
        w, h = size
        fh = max(3.96*2.1, min(15, h*0.1))
        fw = max(3.96*2.1, min(15, w*0.1))
        return ProtoBoard(common_defs, f'conn396@{fw}mm | ((tht50 | conn200)@{fh}mm / tht / (conn125|conn250)@{fh}mm) | conn350@{fw}mm', border=2, tight_layout=True)

    for pattern, name in smd_basic.items():
        pattern_sizes = sizes_small if pattern not in ['manhattan'] else sizes_medium
        # Default to ground plane on back for manhattan proto boards
        pattern_back = pattern if pattern not in ['manhattan'] else 'ground'

        @index(sizes=pattern_sizes, name=f'{name}_ground_plane')
        def gen(size, mounting_holes=None):
            return ProtoBoard(common_defs, f'{pattern} + ground', mounting_holes, border=1)

        @index(sizes=pattern_sizes, name=f'{name}_single_side')
        def gen(size, mounting_holes=None):
            return ProtoBoard(common_defs, f'{pattern} + empty', mounting_holes, border=1)

        @index(sizes=pattern_sizes, name=f'{name}_double_side')
        def gen(size, mounting_holes=None):
            return ProtoBoard(common_defs, f'{pattern} + {pattern}', mounting_holes, border=1)

        @index(sizes=pattern_sizes, name=f'tht_and_{name}_large_holes')
        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'({pattern} + {pattern_back})@{f}mm / thtl', mounting_holes, border=1, tight_layout=True)

        @index(sizes=pattern_sizes, name=f'{name}_and_tht_large_holes')
        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'({pattern} + {pattern_back}) / thtl@{f}mm', mounting_holes, border=1, tight_layout=True)

        @index(sizes=pattern_sizes, name=f'tht_and_{name}')
        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'({pattern} + {pattern_back})@{f}mm / tht', mounting_holes, border=1, tight_layout=True)

        @index(sizes=pattern_sizes, name=f'{name}_and_tht')
        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'({pattern} + {pattern_back}) / tht@{f}mm', mounting_holes, border=1, tight_layout=True)

        @index(sizes=min_dim(pattern_sizes, 20), name=f'{name}_and_connector_areas')
        def gen(size, mounting_holes=None):
            w, h = size
            fh = max(3.96*2.1, min(15, h*0.1))
            fw = max(3.96*2.1, min(15, w*0.1))
            return ProtoBoard(common_defs, f'conn396@{fw}mm | ((tht50 | conn200)@{fh}mm / ({pattern} + {pattern_back}) / (conn125|conn250)@{fh}mm) | conn350@{fw}mm', border=2, tight_layout=True)

        *_, suffix = name.split('_')
        if suffix not in ('100mil', '950um'):
            @index(sizes=sizes_medium, name=f'tht_and_three_smd_100mil_950um_{suffix}')
            def gen(size, mounting_holes=None):
                w, h = size
                f = max(1.27*5, min(50, h*0.3))
                f2 = max(1.27*5, min(30, w*0.2))
                pattern_rot = f'{pattern}r' if pattern not in ['manhattan'] else pattern
                pattern_back_rot = f'{pattern_back}r' if pattern not in ['manhattan'] else 'ground'
                return ProtoBoard(common_defs, f'((smd100 + smd100) | (smd950 + smd950) | ({pattern_rot} + {pattern_back_rot})@{f2}mm)@{f}mm / tht', mounting_holes, border=1, tight_layout=True)

    for (pattern1, name1), (pattern2, name2) in itertools.combinations(smd_basic.items(), 2):
        *_, name1 = name1.split('_')
        *_, name2 = name2.split('_')

        @index(sizes=sizes_small, name=f'tht_and_two_smd_{name1}_{name2}')
        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'(({pattern1} + {pattern1}) | ({pattern2} + {pattern2}))@{f}mm / tht', mounting_holes, border=1, tight_layout=True)

        @index(sizes=sizes_small, name=f'tht_and_two_sided_smd_{name1}_{name2}')
        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'({pattern1} + {pattern2})@{f}mm / tht', mounting_holes, border=1, tight_layout=True)

        @index(sizes=sizes_small, name=f'two_sided_smd_{name1}_{name2}')
        def gen(size, mounting_holes=None):
            w, h = size
            f = max(1.27*5, min(30, h*0.3))
            return ProtoBoard(common_defs, f'{pattern1} + {pattern2}', mounting_holes, border=1)

    @index(sizes_medium, name=f'tht_and_50mil_and_two_smd_100mil_950um')
    def gen(size, mounting_holes=None):
        w, h = size
        f = max(1.27*5, min(50, h*0.3))
        f2 = max(1.27*5, min(30, w*0.2))
        return ProtoBoard(common_defs, f'((smd100 + smd100) | (smd950 + smd950) | tht50@{f2}mm)@{f}mm / tht', mounting_holes, border=1, tight_layout=True)

    @index(sizes=min_dim(sizes_medium, 60), name=f'all_tht_and_smd')
    def gen(size, mounting_holes=None):
        w, h = size
        f = max(1.27*5, min(30, h*0.3))
        f2 = max(1.27*5, min(25, w*0.1))
        return ProtoBoard(common_defs, f'tht50@10mm | tht | ((smd100r + smd100r) / (smd950r + smd950r) / (smd800 + smd800)@{f2}mm / (smd650 + smd650)@{f2}mm / (smd500 + smd500)@{f2}mm)@{f}mm', mounting_holes, border=1, tight_layout=True)

    write_index(index_d, outdir)


if __name__ == '__main__':
    generate_all()

