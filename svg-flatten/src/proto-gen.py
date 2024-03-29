#!/usr/bin/env python3

import re
import textwrap
import ast

svg_str = lambda content: content if isinstance(content, str) else '\n'.join(str(c) for c in content)

class Pattern:
    def __init__(self, w, h, content):
        self.w = w
        self.h = h
        self.content = content

    @property
    def svg_id(self):
        return f'pat-{id(self):16x}'

    def __str__(self):
        return textwrap.dedent(f'''
            <pattern id="{self.svg_id}" viewBox="0,0,{self.w},{self.h}" width="{self.w}" height="{self.h}" patternUnits="userSpaceOnUse">
                {svg_str(self.content)}
            </pattern>''')

    def make_rect(x, y, w, h):
        return f'<rect x="{x}" y="{y}" w="{w}" h="{h}" fill="url(#{self.svg_id})"/>'

class CirclePattern(Pattern):
    def __init__(self, d, w, h=None):
        self.d = d
        self.w = w
        self.h = h or w

    @property
    def content(self):
        return f'<circle cx={self.w/2} cy={self.h/2} r={self.d/2}/>'

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
''')

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
        w_fit, h_fit = round(w - (w % self.pitch_x), 6), round(h - (h % self.pitch_y), 6)

        if center:
            x = x + (w-w_fit)/2
            y = y + (h-h_fit)/2
            return x, y, w_fit, h_fit

        else:
            return x, y, w_fit, h_fit

class THTProtoAreaCircles:
    def __init__(self, pad_dia=2.0, drill=1.0, pitch=2.54, sides='both', plated=True):
        super(pitch)
        self.pad_dia = pad_dia
        self.drill = drill
        self.drill_pattern = CirclePattern(self.drill, self.pitch)
        self.pad_pattern = CirclePattern(self.pad_dia, self.pitch)
        self.patterns = [self.drill_pattern, self.pad_pattern]
        self.plated = plated
        self.sides = sides
    
    def generate(self, x, y, w, h, center=True):
        x, y, w, h = self.fit_rect(x, y, w, h, center)
        drill = 'plated drill' if self.plated else 'nonplated drill'
        d = { drill: self.drill_pattern.make_rect(x, y, w, h) }

        if self.sides in ('top', 'both'):
            d['top copper'] = self.pad_pattern.make_rect(x, y, w, h)
        if self.sides in ('bottom', 'both'):
            d['bottom copper'] = self.pad_pattern.make_rect(x, y, w, h)

        return d

class ProtoBoard:
    def __init__(self, desc_str):
        pass

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

    m = value_re.match(value)
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

    expr = re.sub(r'\s', '', expr).lower()
    expr = re.sub(r'([0-9]*\.?[0-9]+)(mm|cm|in|mil|%)', r'"\1\2"', expr)
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

if __name__ == '__main__':
    import sys
    for line in [
            'tht',
            'tht@1mm',
            'tht|tht',
            'tht@1mm|tht',
            'tht|tht|tht',
            'tht@1mm|tht@2mm|tht@3mm',
            '(tht@1mm|tht@2mm)|tht@3mm',
            'tht@1mm|(tht@2mm|tht@3mm)',
            'tht@2|tht|tht',
            '(tht@1mm|tht|tht@3mm) / tht',
            ]:
        layout = parse_layout(line)
        print(line, '->', layout)
        print('    ', layout.layout(100))
        print()

