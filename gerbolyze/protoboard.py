#!/usr/bin/env python3

import re
import textwrap
import ast
import uuid

svg_str = lambda content: content if isinstance(content, str) else '\n'.join(str(c) for c in content)

class Pattern:
    def __init__(self, w, h=None):
        self.vb_w = self.w = w
        self.vb_h = self.h = h or w

    def svg_def(self, svg_id, off_x, off_y):
        return textwrap.dedent(f'''
            <pattern id="{svg_id}" x="{off_x}" y="{off_y}" viewBox="0,0,{self.vb_w},{self.vb_h}" width="{self.w}" height="{self.h}" patternUnits="userSpaceOnUse">
                {svg_str(self.content)}
            </pattern>''')
    
def make_rect(svg_id, x, y, w, h, clip=''):
    #import random
    #c = random.randint(0, 2**24)
    #return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#{c:06x}"/>'
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" {clip} fill="url(#{svg_id})"/>'

class CirclePattern(Pattern):
    def __init__(self, d, w, h=None):
        super().__init__(w, h)
        self.d = d

    @property
    def content(self):
        return f'<circle cx="{self.w/2}" cy="{self.h/2}" r="{self.d/2}"/>'

class RectPattern(Pattern):
    def __init__(self, rw, rh, w, h):
        super().__init__(w, h)
        self.rw, self.rh = rw, rh

    @property
    def content(self):
        x = (self.w - self.rw) / 2
        y = (self.h - self.rh) / 2
        return f'<rect x="{x}" y="{y}" width="{self.rw}" height="{self.rh}"/>'

class ManhattanPattern(Pattern):
    def __init__(self, pitch=2.54*4, gap=0.2):
        super().__init__(pitch)
        self.vb_w, self.vb_h = 1, 1
        self.gap = gap

    @property
    def content(self):
        return textwrap.dedent('''
                <rect x="0"   y="0"   width="0.5" height="0.5" style="fill: black; stroke: white; stroke-width: 0.01mm"/>
                <rect x="0"   y="0.5" width="0.5" height="0.5" style="fill: black; stroke: white; stroke-width: 0.01mm"/>
                <rect x="0.5" y="0"   width="0.5" height="0.5" style="fill: black; stroke: white; stroke-width: 0.01mm"/>
                <rect x="0.5" y="0.5" width="0.5" height="0.5" style="fill: black; stroke: white; stroke-width: 0.01mm"/>
                <rect x="0.3" y="0.3" width="0.4" height="0.4" style="fill: black; stroke: white; stroke-width: 0.01mm" transform="rotate(45 0.5 0.5)"/>
                '''.strip())

make_layer = lambda layer_name, content: \
  f'<g id="g-{layer_name.replace(" ", "-")}" inkscape:label="{layer_name}" inkscape:groupmode="layer">{svg_str(content)}</g>'

svg_template = textwrap.dedent('''
    <?xml version="1.0" encoding="UTF-8" standalone="no"?>
    <svg version="1.1" width="{w}mm" height="{h}mm" viewBox="0 0 {w} {h}" id="svg18" sodipodi:docname="proto.svg"
       inkscape:version="1.2 (dc2aedaf03, 2022-05-15)"
       xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
       xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
       xmlns="http://www.w3.org/2000/svg"
       xmlns:svg="http://www.w3.org/2000/svg">
      <defs id="defs2">
        {defs}
      </defs>
      <sodipodi:namedview inkscape:current-layer="g-top-copper" id="namedview4" pagecolor="#ffffff" bordercolor="#666666"
         borderopacity="1.0" inkscape:showpageshadow="2" inkscape:pageopacity="0.0" inkscape:pagecheckerboard="0"
         inkscape:deskcolor="#d1d1d1" inkscape:document-units="mm" showgrid="false" inkscape:zoom="2.8291492"
         inkscape:cx="157.29111" inkscape:cy="80.943063" inkscape:window-width="1920" inkscape:window-height="1011"
         inkscape:window-x="0" inkscape:window-y="0" inkscape:window-maximized="1" />
      {layers}
    </svg>
''').strip()

class PatternProtoArea:
    def __init__(self, pitch_x, pitch_y=None, border=None):
        self.pitch_x = pitch_x
        self.pitch_y = pitch_y or pitch_x

        if border is None:
            self.border = (0, 0, 0, 0)
        elif hasattr(border, '__iter__'):
            if len(border == 4):
                self.border = border
            else:
                raise TypeError('border must be None, int, or a 4-tuple of floats (top, right, bottom, left)')
        else:
            self.border = (border, border, border, border)

    @property
    def pitch(self):
        if self.pitch_x != self.pitch_y:
            raise ValueError('Pattern has different X and Y pitches')
        return self.pitch_x

    def fit_size(self, w, h):
        x, y, w, h = self.fit_rect(0, 0, w, h, False)
        t, r, b, l = self.border
        return (w+l+r), (h+t+b)

    def fit_rect(self, x, y, w, h, center=True):
        t, r, b, l = self.border
        x, y, w, h = (x+l), (y+t), (w-l-r), (h-t-b)

        w_mod, h_mod = round((w + 5e-7) % self.pitch_x, 6), round((h + 5e-7) % self.pitch_y, 6)
        w_fit, h_fit = round(w - w_mod, 6), round(h - h_mod, 6)

        if center:
            x = x + (w-w_fit)/2
            y = y + (h-h_fit)/2
            return x, y, w_fit, h_fit

        else:
            return x, y, w_fit, h_fit

    def generate(self, x, y, w, h, center=True, clip='', tight_layout=False):
        yield {}

    def symmetric_sides(self):
        return False

    def used_patterns(self):
        yield self


class EmptyProtoArea:
    def __init__(self, copper=False, border=None):
        self.copper = copper

        if border is None:
            self.border = (0, 0, 0, 0)
        elif hasattr(border, '__iter__'):
            if len(border == 4):
                self.border = border
            else:
                raise TypeError('border must be None, int, or a 4-tuple of floats (top, right, bottom, left)')
        else:
            self.border = (border, border, border, border)

    def fit_size(self, w, h):
        return w, h

    def generate(self, x, y, w, h, center=True, clip='', tight_layout=False):
        if self.copper:
            t, r, b, l = self.border
            x, y, w, h = x+l, y+t, w-l-r, h-t-b
            yield { 'top copper': f'<rect x="{x}" y="{y}" width="{w}" height="{h}" {clip} fill="black"/>' }
        else:
            yield {}

    def used_patterns(self):
        yield self


class THTProtoArea(PatternProtoArea):
    def __init__(self, pad_size=2.0, drill=1.0, pitch=2.54, sides='both', plated=True, border=None, pad_shape='circle'):
        super().__init__(pitch, border=border)
        self.pad_size = pad_size
        self.pad_shape = pad_shape.lower().rstrip('s')
        self.drill = drill
        self.drill_pattern = CirclePattern(self.drill, self.pitch)
        if self.pad_shape == 'circle':
            self.pad_pattern = CirclePattern(self.pad_size, self.pitch)
        elif self.pad_shape == 'square':
            self.pad_pattern = RectPattern(self.pad_size, self.pad_size, self.pitch, self.pitch)
        self.patterns = [self.drill_pattern, self.pad_pattern]
        self.plated = plated
        self.sides = sides
    
    def generate(self, x, y, w, h, center=True, clip='', tight_layout=False):
        x, y, w, h = self.fit_rect(x, y, w, h, center)
        drill = 'plated drill' if self.plated else 'nonplated drill'

        pad_id = str(uuid.uuid4())
        drill_id = str(uuid.uuid4())

        d = { drill: make_rect(drill_id, x, y, w, h, clip),
             'defs': [
                 self.pad_pattern.svg_def(pad_id, x, y),
                 self.drill_pattern.svg_def(drill_id, x, y)]}

        if self.sides in ('top', 'both'):
            d['top copper'] = make_rect(pad_id, x, y, w, h, clip)
            d['top mask'] = make_rect(pad_id, x, y, w, h, clip)
        if self.sides in ('bottom', 'both'):
            d['bottom copper'] = make_rect(pad_id, x, y, w, h, clip)
            d['bottom mask'] = make_rect(pad_id, x, y, w, h, clip)

        yield d

    def __repr__(self):
        return f'THTPads(size={self.pad_size}, h={self.drill}, p={self.pitch}, sides={self.sides}, plated={self.plated}, pad_shape="{self.pad_shape}")'

    def symmetric_sides(self):
        return True


class SMDProtoAreaRectangles(PatternProtoArea):
    def __init__(self, pitch_x, pitch_y, w=None, h=None, border=None):
        super().__init__(pitch_x, pitch_y, border=border)
        w = w or pitch_x - 0.15
        h = h or pitch_y - 0.15
        self.w, self.h = w, h
        self.pad_pattern = RectPattern(w, h, pitch_x, pitch_y)
        self.patterns = [self.pad_pattern]

    def generate(self, x, y, w, h, center=True, clip='', tight_layout=False):
        x, y, w, h = self.fit_rect(x, y, w, h, center)
        pad_id = str(uuid.uuid4())
        yield {'defs': [self.pad_pattern.svg_def(pad_id, x, y)],
            'top copper': make_rect(pad_id, x, y, w, h, clip),
            'top mask': make_rect(pad_id, x, y, w, h, clip)}

    def symmetric_sides(self):
        return False

class ManhattanProtoArea(PatternProtoArea):
    def __init__(self, pitch=2.54*4, gap=0.25, border=None):
        super().__init__(pitch, pitch, border=border)
        self.gap = gap
        self.pad_pattern = ManhattanPattern(pitch, gap)
        self.patterns = [self.pad_pattern]

    def generate(self, x, y, w, h, center=True, clip='', tight_layout=False):
        x, y, w, h = self.fit_rect(x, y, w, h, center)
        pad_id = str(uuid.uuid4())
        yield {'defs': [self.pad_pattern.svg_def(pad_id, x, y)],
            'top copper': make_rect(pad_id, x, y, w, h, clip),
            'top mask': make_rect(pad_id, x, y, w, h, clip)}

    def symmetric_sides(self):
        return False

LAYERS = [
        'top paste',
        'top silk',
        'top mask',
        'top copper',
        'bottom copper',
        'bottom mask',
        'bottom silk',
        'bottom paste',
        'outline',
        'nonplated drill',
        'plated drill'
        ]

class ProtoBoard:
    def __init__(self, defs, expr, mounting_holes=None, border=None, center=True, tight_layout=False):
        self.defs = eval_defs(defs)
        self.layout = parse_layout(expr, self.defs)
        self.mounting_holes = mounting_holes
        self.center = center
        self.tight_layout = tight_layout

        if border is None:
            self.border = (0, 0, 0, 0)
        elif hasattr(border, '__iter__'):
            if len(border == 4):
                self.border = border
            else:
                raise TypeError('border must be None, int, or a 4-tuple of floats (top, right, bottom, left)')
        else:
            self.border = (border, border, border, border)

    @property
    def symmetric_sides(self):
        return self.layout.symmetric_sides()

    @property
    def used_patterns(self):
        return set(self.layout.used_patterns())

    def generate(self, w, h):
        out = {l: [] for l in LAYERS}
        svg_defs = []
        clip = ''

        if self.mounting_holes:
            d, o, *k = self.mounting_holes # diameter, offset from edge, keepout to proto area
            k = k[0] if k else o
            q = o + k
            if 2*q < w:
                if 2*q < h:
                    clip_d = f'M 0 {q} L {q} {q} L {q} 0 L {w-q} 0 L {w-q} {q} L {w} {q} L {w} {h-q} L {w-q} {h-q} L {w-q} {h} L {q} {h} L {q} {h-q} L 0 {h-q} Z'
                else:
                    clip_d = f'M {q} 0 L {w-q} 0 L {w-q} {h} L 0 {h} Z'
            else:
                if 2*q < h:
                    clip_d = f'M 0 {q} L 0 {h-q} L {w} {h-q} L {w} {q} Z'
                else:
                    raise ValueError(f'Hole keepout areas are so large that no board area is left. Available size is {w}x{h} mm, keepout areas are {q}x{q} mm in all four corners.')

            svg_defs.append(f'<clipPath id="hole-clip"><path d="{clip_d}"/></clipPath>')
            clip = 'clip-path="url(#hole-clip)"'

            out['nonplated drill'].append([
                f'<circle cx="{o}" cy="{o}" r="{d/2}"/>',
                f'<circle cx="{w-o}" cy="{o}" r="{d/2}"/>',
                f'<circle cx="{w-o}" cy="{h-o}" r="{d/2}"/>',
                f'<circle cx="{o}" cy="{h-o}" r="{d/2}"/>' ])

        t, r, b, l = self.border
        for layer_dict in self.layout.generate(l, t, w-l-r, h-t-b, self.center, clip, self.tight_layout):
            for l in LAYERS:
                if l in layer_dict:
                    out[l].append(layer_dict[l])
            svg_defs += layer_dict.get('defs', [])

        out['outline'] = f'<rect x="0" y="0" width="{w}" height="{h}" fill="none" stroke="black" stroke-width="0.1mm"/>'

        layers = [ make_layer(l, out[l]) for l in LAYERS ]
        return svg_template.format(w=w, h=h, defs='\n'.join(svg_defs), layers='\n'.join(layers)) 


def convert_to_mm(value, unit):
    unitl  = unit.lower()
    if unitl == 'mm':
        return value
    elif unitl == 'cm':
        return value*10
    elif unitl == 'in':
        return value*25.4
    elif unitl == 'mil':
        return value/1000*25.4
    else:
        raise ValueError(f'Invalid unit {unit}, allowed units are mm, cm, in, and mil.')

value_re = re.compile('([0-9]*\.?[0-9]+)(cm|mm|in|mil|%)')
def eval_value(value, total_length=None):
    if not isinstance(value, str):
        return None

    m = value_re.match(value.lower())
    number, unit = m.groups()
    if unit == '%':
        if total_length is None:
            raise ValueError('Percentages are not allowed for this value')
        return total_length * float(number) / 100
    return convert_to_mm(float(number), unit)

class PropLayout:
    def __init__(self, content, direction, proportions):
        self.content = content
        self.direction = direction
        self.proportions = proportions
        if len(content) != len(proportions):
            raise ValueError('proportions and content must have same length')

    def generate(self, x, y, w, h, center=True, clip='', tight_layout=False):
        for (c_x, c_y, c_w, c_h), child in self.layout_2d(x, y, w, h, tight_layout):
            yield from child.generate(c_x, c_y, c_w, c_h, center, clip, tight_layout)

    def fit_size(self, w, h):
        widths = []
        heights = []
        for (_x, _y, w, h), child in self.layout_2d(0, 0, w, h, True):
            if not isinstance(child, EmptyProtoArea):
                widths.append(w)
                heights.append(h)
        if self.direction == 'h':
            return sum(widths), max(heights)
        else:
            return max(widths), sum(heights)

    def layout_2d(self, x, y, w, h, tight_layout=False):
        actual_l = 0
        target_l = 0
        for l, child in zip(self.layout(w if self.direction == 'h' else h), self.content):
            this_x, this_y = x, y
            this_w, this_h = w, h
            target_l += l

            if self.direction == 'h':
                this_w = target_l - actual_l
            else:
                this_h = target_l - actual_l

            if tight_layout:
                this_w, this_h = child.fit_size(this_w, this_h)

            if self.direction == 'h':
                x += this_w
                actual_l += this_w
                this_h = h
            else:
                y += this_h
                actual_l += this_h
                this_w = w

            yield (this_x, this_y, this_w, this_h), child

    def layout(self, length):
        out = [ eval_value(value, length) for value in self.proportions ]
        total_length = sum(value for value in out if value is not None)
        if length - total_length < -1e-6:
            raise ValueError(f'Proportions sum to {total_length} mm, which is greater than the available space of {length} mm.')

        leftover = length - total_length
        sum_props = sum( (value or 1.0) for value in self.proportions if not isinstance(value, str) )
        return [ (leftover * (value or 1.0) / sum_props if not isinstance(value, str) else calculated)
                for value, calculated in zip(self.proportions, out) ]

    def __str__(self):
        children = ', '.join( f'{elem}:{width}' for elem, width in zip(self.content, self.proportions))
        return f'PropLayout[{self.direction.upper()}]({children})'

    def symmetric_sides(self):
        return all(child.symmetric_sides() for child in self.content)

    def used_patterns(self):
        for child in self.content:
            yield from child.used_patterns()


class TwoSideLayout:
    def __init__(self, top, bottom):
        self.top, self.bottom = top, bottom

    def flip(self, defs):
        out = dict(defs)
        for layer in ('copper', 'mask', 'silk', 'paste'):
            top, bottom = f'top {layer}', f'bottom {layer}'
            tval, bval = defs.get(top), defs.get(bottom)

            if tval:
                defs[bottom] = tval
            elif bottom in defs:
                del defs[bottom]

            if bval:
                defs[top] = bval
            elif top in defs:
                del defs[top]

        return defs

    def fit_size(self, w, h):
        top, bottom = self.top, self.bottom
        w1, h1 = top.fit_size(w, h)
        w2, h2 = bottom.fit_size(w, h)
        if isinstance(top, EmptyProtoArea):
            if isinstance(bottom, EmptyProtoArea):
                return w1, h1
            return w2, h2
        if isinstance(bottom, EmptyProtoArea):
            return w1, h1
        return max(w1, w2), max(h1, h2)

    def generate(self, x, y, w, h, center=True, clip='', tight_layout=False):
        yield from self.top.generate(x, y, w, h, center, clip, tight_layout)
        yield from map(self.flip, self.bottom.generate(x, y, w, h, center, clip, tight_layout))

    def symmetric_sides(self):
        return self.top == self.bottom

    def used_patterns(self):
        yield from self.top.used_patterns()
        yield from self.bottom.used_patterns()


def _map_expression(node, defs):
    if isinstance(node, ast.Name):
        return defs[node.id]

    elif isinstance(node, ast.Constant):
        return node.value


    elif isinstance(node, ast.BinOp) and isinstance(node.op, (ast.BitOr, ast.BitAnd, ast.Add)):
        left_prop = right_prop = None

        left, right = node.left, node.right

        if isinstance(left, ast.BinOp) and isinstance(left.op, ast.MatMult):
            left_prop = _map_expression(left.right, defs)
            left = left.left

        if isinstance(right, ast.BinOp) and isinstance(right.op, ast.MatMult):
            right_prop = _map_expression(right.right, defs)
            right = right.left

        left, right = _map_expression(left, defs), _map_expression(right, defs)

        direction = 'h' if isinstance(node.op, ast.BitOr) else 'v'
        if isinstance(left, PropLayout) and left.direction == direction and left_prop is None:
            left.content.append(right)
            left.proportions.append(right_prop)
            return left

        elif isinstance(right, PropLayout) and right.direction == direction and right_prop is None:
            right.content.insert(0, left)
            right.proportions.insert(0, left_prop)
            return right

        elif isinstance(node.op, ast.Add):
            if left_prop or right_prop:
                raise SyntaxError(f'Proportions ("@") not supported for two-side layout ("+")')

            return TwoSideLayout(left, right)

        else:
            return PropLayout([left, right], direction, [left_prop, right_prop])
        
    elif isinstance(node, ast.BinOp) and isinstance(node.op, ast.MatMult):
        raise SyntaxError(f'Unexpected width specification "{ast.unparse(node.right)}"')

    else:
        raise SyntaxError(f'Invalid layout expression "{ast.unparse(node)}"')

def parse_layout(expr, defs):
    ''' Example layout:

        ( tht @ 2in | smd ) @ 50% / tht
    '''

    expr = re.sub(r'\s', '', expr)
    expr = re.sub(r'([0-9]*\.?[0-9]+)([Mm][Mm]|[Cc][Mm]|[Ii][Nn]|[Mm][Ii][Ll]|%)', r'"\1\2"', expr)
    expr = expr.replace('/', '&')
    try:
        expr = ast.parse(expr, mode='eval').body
        match expr:
            case ast.Name():
                return PropLayout([defs[expr.id]], 'h', [None])

            case ast.BinOp(op=ast.MatMult()):
                assert isinstance(expr.right, ast.Constant)
                return PropLayout([_map_expression(expr.left, defs)], 'h', [expr.right.value])

            case _:
                return _map_expression(expr, defs)
    except SyntaxError as e:
        raise SyntaxError('Invalid layout expression') from e

PROTO_AREA_TYPES = {
    'THTPads': THTProtoArea,
    'SMDPads': SMDProtoAreaRectangles,
    'Manhattan': ManhattanProtoArea,
    'Empty': EmptyProtoArea,
}

def eval_defs(defs):
    defs = defs.replace('\n', ';')
    defs = re.sub(r'\s', '', defs)

    out = {}
    for elem in defs.split(';'):
        if not elem:
            continue

        if not (m := re.match('([a-zA-Z_][a-zA-Z0-9_]*)=([a-zA-Z_][a-zA-Z0-9_]*)\((.*)\)', elem)):
            raise SyntaxError(f'Invalid pattern definition "{elem}"')

        key, pattern, params = m.groups()
        args, kws = [], {}
        for elem in params.split(','):
            if not elem:
                continue
            if (m := re.match('([a-zA-Z_][a-zA-Z0-9_]*)=(.*)', elem)):
                param_name, param_value = m.groups()
                kws[param_name] = ast.literal_eval(param_value)

            else:
                args.append(ast.literal_eval(elem))

        out[key] = PROTO_AREA_TYPES[pattern](*args, **kws)
    return out

COMMON_DEFS = '''
empty = Empty(copper=False);
ground = Empty(copper=True);

tht = THTPads();
manhattan = Manhattan();
tht50 = THTPads(pad_size=1.0, drill=0.6, pitch=1.27);

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


if __name__ == '__main__':
#    import sys
#    print('===== Layout expressions =====')
#    for line in [
#            'tht',
#            'tht@1mm',
#            'tht|tht',
#            'tht@1mm|tht',
#            'tht|tht|tht',
#            'tht@1mm|tht@2mm|tht@3mm',
#            '(tht@1mm|tht@2mm)|tht@3mm',
#            'tht@1mm|(tht@2mm|tht@3mm)',
#            'tht@2|tht|tht',
#            '(tht@1mm|tht|tht@3mm) / tht',
#            ]:
#        layout = parse_layout(line)
#        print(line, '->', layout)
#        print('    ', layout.layout(100))
#        print()
#    print('===== Pattern definitions =====')
#    for line in [
#            'tht = THTCircles()',
#            'tht = THTCircles(10)',
#            'tht = THTCircles(10, 20)',
#            'tht = THTCircles(plated=False)',
#            'tht = THTCircles(10, plated=False)',
#            ]:
#        print(line, '->', eval_defs(line))
#    print()
#    print('===== Proto board =====')
    #b = ProtoBoard('tht = THTCircles(); tht_small = THTCircles(pad_size=1.0, drill=0.6, pitch=1.27)',
    #        'tht@1in|(tht_small@2/tht@1)', mounting_holes=(3.2, 5.0, 5.0), border=2, center=False)
    #b = ProtoBoard('tht = THTCircles(); smd1 = SMDPads(2.0, 2.0); smd2 = SMDPads(0.95, 1.895); plane=Empty(copper=True)', 'tht@25mm | (smd1 + plane)', mounting_holes=(3.2, 5.0, 5.0), border=2, tight_layout=True)
    #b = ProtoBoard(COMMON_DEFS, f'((smd100 + smd100) | (smd950 + smd950) | tht50@20mm)@20mm / tht', mounting_holes=(3.2,5,5), border=1, tight_layout=True, center=True)
    b = ProtoBoard(COMMON_DEFS, f'manhattan', mounting_holes=(3.2,5,5), border=1, tight_layout=True, center=True)
    print(b.generate(80, 60))
