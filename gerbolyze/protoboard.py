#!/usr/bin/env python3

import re
import textwrap
import ast
import uuid

svg_str = lambda content: content if isinstance(content, str) else '\n'.join(str(c) for c in content)

class Pattern:
    def __init__(self, w, h, content):
        self.w = w
        self.h = h
        self.content = content

    def svg_def(self, svg_id, off_x, off_y):
        return textwrap.dedent(f'''
            <pattern id="{svg_id}" x="{off_x}" y="{off_y}" viewBox="0,0,{self.w},{self.h}" width="{self.w}" height="{self.h}" patternUnits="userSpaceOnUse">
                {svg_str(self.content)}
            </pattern>''')

def make_rect(svg_id, x, y, w, h, clip=''):
    #import random
    #c = random.randint(0, 2**24)
    #return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#{c:06x}"/>'
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" {clip} fill="url(#{svg_id})"/>'

class CirclePattern(Pattern):
    def __init__(self, d, w, h=None):
        self.d = d
        self.w = w
        self.h = h or w

    @property
    def content(self):
        return f'<circle cx="{self.w/2}" cy="{self.h/2}" r="{self.d/2}"/>'

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
    def __init__(self, pitch_x, pitch_y=None):
        self.pitch_x = pitch_x
        self.pitch_y = pitch_y or pitch_x

    @property
    def pitch(self):
        if self.pitch_x != self.pitch_y:
            raise ValueError('Pattern has different X and Y pitches')
        return self.pitch_x

    def fit_rect(self, x, y, w, h, center=True):
        w_mod, h_mod = round((w + 5e-7) % self.pitch_x, 6), round((h + 5e-7) % self.pitch_y, 6)
        w_fit, h_fit = round(w - w_mod, 6), round(h - h_mod, 6)

        if center:
            x = x + (w-w_fit)/2
            y = y + (h-h_fit)/2
            return x, y, w_fit, h_fit

        else:
            return x, y, w_fit, h_fit

class THTProtoAreaCircles(PatternProtoArea):
    def __init__(self, pad_dia=2.0, drill=1.0, pitch=2.54, sides='both', plated=True):
        super().__init__(pitch)
        self.pad_dia = pad_dia
        self.drill = drill
        self.drill_pattern = CirclePattern(self.drill, self.pitch)
        self.pad_pattern = CirclePattern(self.pad_dia, self.pitch)
        self.patterns = [self.drill_pattern, self.pad_pattern]
        self.plated = plated
        self.sides = sides
    
    def generate(self, x, y, w, h, center=True, clip=''):
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
        if self.sides in ('bottom', 'both'):
            d['bottom copper'] = make_rect(pad_id, x, y, w, h, clip)

        return d

    def __repr__(self):
        return f'THTCircles(d={self.pad_dia}, h={self.drill}, p={self.pitch}, sides={self.sides}, plated={self.plated})'

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
    def __init__(self, defs, expr, mounting_holes=None):
        self.defs = eval_defs(defs)
        self.layout = parse_layout(expr)
        self.mounting_holes = mounting_holes

    def generate(self, w, h):
        out = {l: [] for l in LAYERS}
        svg_defs = []
        clip = ''

        if self.mounting_holes:
            d, o, k = self.mounting_holes # diameter, offset from edge, keepout to proto area
            q = o + k
            clip_d = f'M 0 {q} L {q} {q} L {q} 0 L {w-q} 0 L {w-q} {q} L {w} {q} L {w} {h-q} L {w-q} {h-q} L {w-q} {h} L {q} {h} L {q} {h-q} L 0 {h-q} Z'
            svg_defs.append(f'<clipPath id="hole-clip"><path d="{clip_d}"/></clipPath>')
            clip = 'clip-path="url(#hole-clip)"'

            out['nonplated drill'].append([
                f'<circle cx="{o}" cy="{o}" r="{d/2}"/>',
                f'<circle cx="{w-o}" cy="{o}" r="{d/2}"/>',
                f'<circle cx="{w-o}" cy="{h-o}" r="{d/2}"/>',
                f'<circle cx="{o}" cy="{h-o}" r="{d/2}"/>' ])

        for layer_dict in self.layout.generate(0, 0, w, h, self.defs, clip):
            for l in LAYERS:
                if l in layer_dict:
                    out[l].append(layer_dict[l])
            svg_defs += layer_dict.get('defs', [])

        out['outline'] = f'<rect x="0" y="0" width="{w}" height="{h}" fill="none" stroke="black" stroke-width="0.1mm"/>'

        layers = [ make_layer(l, out[l]) for l in LAYERS ]
        return svg_template.format(w=w, h=h, defs='\n'.join(svg_defs), layers='\n'.join(layers)) 


def convert_to_mm(value, unit):
    match unit.lower():
        case 'mm': return value
        case 'cm': return value*10
        case 'in': return value*25.4
        case 'mil': return value/1000*25.4
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

    def generate(self, x, y, w, h, defs, clip=''):
        for (c_x, c_y, c_w, c_h), child in self.layout_2d(x, y, w, h):
            if isinstance(child, str):
                yield defs[child].generate(c_x, c_y, c_w, c_h, defs, clip)

            else:
                yield from child.generate(c_x, c_y, c_w, c_h, defs, clip)

    def layout_2d(self, x, y, w, h):
        for l, child in zip(self.layout(w if self.direction == 'h' else h), self.content):
            this_w, this_h = w, h
            this_x, this_y = x, y

            if self.direction == 'h':
                this_w = l
                x += l
            else:
                this_h = l
                y += l

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

def _map_expression(node):
    match node:
        case ast.Name():
            return node.id

        case ast.Constant():
            return node.value

        case ast.BinOp(op=ast.BitOr()) | ast.BinOp(op=ast.BitAnd()):
            left_prop = right_prop = None

            left, right = node.left, node.right

            if isinstance(left, ast.BinOp) and isinstance(left.op, ast.MatMult):
                left_prop = _map_expression(left.right)
                left = left.left

            if isinstance(right, ast.BinOp) and isinstance(right.op, ast.MatMult):
                right_prop = _map_expression(right.right)
                right = right.left

            direction = 'h' if isinstance(node.op, ast.BitOr) else 'v'
            left, right = _map_expression(left), _map_expression(right)

            if isinstance(left, PropLayout) and left.direction == direction and left_prop is None:
                left.content.append(right)
                left.proportions.append(right_prop)
                return left

            elif isinstance(right, PropLayout) and right.direction == direction and right_prop is None:
                right.content.insert(0, left)
                right.proportions.insert(0, left_prop)
                return right

            else:
                return PropLayout([left, right], direction, [left_prop, right_prop])
            
        case ast.BinOp(op=ast.MatMult()):
            raise SyntaxError(f'Unexpected width specification "{ast.unparse(node.right)}"')

        case _:
            raise SyntaxError(f'Invalid layout expression "{ast.unparse(node)}"')

def parse_layout(expr):
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
                return PropLayout([expr.id], 'h', [None])

            case ast.BinOp(op=ast.MatMult()):
                assert isinstance(expr.right, ast.Constant)
                return PropLayout([_map_expression(expr.left)], 'h', [expr.right.value])

            case _:
                return _map_expression(expr)
    except SyntaxError as e:
        raise SyntaxError('Invalid layout expression') from e

PROTO_AREA_TYPES = {
    'THTCircles': THTProtoAreaCircles
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
    b = ProtoBoard('tht = THTCircles(); tht_small = THTCircles(pad_dia=1.0, drill=0.6, pitch=1.27)', 'tht@1in|(tht_small@2/tht@1)', mounting_holes=(3.2, 5.0, 5.0))
    print(b.generate(80, 60))
