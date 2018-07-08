#!/usr/bin/env python3

import gerbolyze

if __name__ == '__main__':
    # Parse command line arguments
    import argparse
    parser = argparse.ArgumentParser()

    subcommand = parser.add_subparsers(help='Sub-commands')
    subcommand.required, subcommand.dest = True, 'command'
    vectorize_parser = subcommand.add_parser('vectorize', help='Vectorize bitmap image onto gerber layer')
    render_parser = subcommand.add_parser('render', help='Render bitmap preview of board suitable as a template for positioning and scaling the input image')

    parser.add_argument('-d', '--debugdir', type=str, default=None, help='Directory to place intermediate images into for debuggin')

    vectorize_parser.add_argument('side', choices=['top', 'bottom'], help='Target board side')
    vectorize_parser.add_argument('--layer', '-l', choices=['silk', 'mask', 'copper'], default='silk', help='Target layer on given side')

    vectorize_parser.add_argument('source', help='Source gerber directory')
    vectorize_parser.add_argument('target', help='Target gerber directory')
    vectorize_parser.add_argument('image', help='Image to render')

    render_parser.add_argument('--fab-resolution', '-r', type=float, nargs='?', default=6.0, help='Smallest feature size supported by PCB manufacturer, in mil. On silkscreen layers, this is the minimum font stroke width.')
    render_parser.add_argument('--oversampling', '-o', type=float, nargs='?', default=10, help='Oversampling factor for the image. If set to say, 10 pixels, one minimum feature size (see --fab-resolution) will be 10 pixels long. The input image for vectorization should not contain any detail of smaller pixel size than this number in order to be manufacturable.')
    render_parser.add_argument('side', choices=['top', 'bottom'], help='Target board side')
    render_parser.add_argument('source', help='Source gerber directory')
    render_parser.add_argument('image', help='Output image filename')
    args = parser.parse_args()

    if args.command == 'vectorize':
        gerbolyze.process_gerbers(args.source, args.target, args.image, args.side, args.layer, args.debugdir)
    else: # command == render
        gerbolyze.render_preview(args.source, args.image, args.side, args.fab_resolution, args.oversampling)

