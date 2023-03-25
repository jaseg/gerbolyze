#!/usr/bin/env python3

from math import *

def calc(mat):
    [xx, yx], [xy, yy] = mat

    a = xx**2 + xy**2
    b = xx*yx + xy*yy
    c = yy**2 + yx**2

    print(f'{a=:.2f} {c=:.2f} {b=:.2f}')
    tan_2_alpha = 2*b/(a-c)
    print(f'atan2={atan2(2*b, a-c)/pi:.2f}*pi')
    #tan_alpha = tan_2_alpha / (1 + sqrt(1 + tan_2_alpha**2)) # FIXME: bounds?
    cos_2_alpha = 1/sqrt(1 + tan_2_alpha**2)
    sin_2_alpha = tan_2_alpha / sqrt(1 + tan_2_alpha**2)
    print(f'tan(2a)={tan_2_alpha:.2f} cos(2a)={cos_2_alpha:.2f} sin(2a)={sin_2_alpha:.2f}')
    cos_alpha = sqrt((1 + cos_2_alpha)/2)
    sin_alpha = sqrt((1 - cos_2_alpha)/2)
    print(f'cos(a)={cos_alpha:.2f} sin(a)={sin_alpha:.2f}')

    for sgn_cos, sgn_sin in [(-1, -1), (-1, 1), (1, -1), (1, 1)]:
        p = xx * sgn_cos * cos_alpha + yx * sgn_sin * sin_alpha
        q = xy * sgn_cos * cos_alpha + yy * sgn_sin * sin_alpha
        dist = hypot(p, q)
        yield dist

def gen(sx, sy, m, theta):
    xx = sx * cos(theta)
    xy = sx * sin(theta)
    yy = sy * (cos(theta) + m * sin(theta))
    yx = sy * (m * cos(theta) - sin(theta))

    mat = [xx, yx], [xy, yy]
    return mat


for sx, sy in [
        (1, 0.9),
        (1, 1.0),
        (1, 1.1),
        (0.9, 1),
        (1.0, 1),
        (1.1, 1)]:
    for m in [0, 0.1, 1, 10]:
        for theta in [0, pi/8, pi/4, pi/3, pi/2, pi, 3*pi/4]:
            print(f'{sx=:.1f} {sy=:.1f} {m=:.1f} theta={theta/pi:.2f}*pi |', end=' ')
            mat = gen(sx, sy, m, theta)

            try:
                dists = list(calc(mat))
                str_dists = ' '.join(f'{x:.2f}' for x in dists)
                print(f'[{str_dists}] | min={min(dists):.2f} max={max(dists):.2f}')
            except:
                print('E')
            break
        break
    break

