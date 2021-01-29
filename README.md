build/svg-render 2.0

Usage: build/svg-render [options]... [input_file] [output_file]

Specify "-" for stdin/stdout.

    -h, --help
        Print help and exit
    -v, --version
        Print version and exit
    -o, --format
        Output format. Supported: gerber, svg, s-exp (KiCAD S-Expression)
    -p, --precision
        Number of decimal places use for exported coordinates (gerber: 1-9,
        SVG: 0-*)
    --clear-color
        SVG color to use for "clear" areas (default: white)
    --dark-color
        SVG color to use for "dark" areas (default: black)
    -d, --trace-space
        Minimum feature size of elements in vectorized graphics
        (trace/space) in mm. Default: 0.1mm.
    --no-header
        Do not export output format header/footer, only export the
        primitives themselves
    --flatten
        Flatten output so it only consists of non-overlapping white
        polygons. This perform composition at the vector level. Potentially slow.
    --no-flatten
        Disable automatic flattening for KiCAD S-Exp export
    -g, --only-groups
        Comma-separated list of group IDs to export.
    -b, --vectorizer
        Vectorizer to use for bitmap images. One of poisson-disc (default),
        hex-grid, square-grid, binary-contours, dev-null.
    --vectorizer-map
        Map from image element id to vectorizer. Overrides --vectorizer.
        Format: id1=vectorizer,id2=vectorizer,...
    --force-svg
        Force SVG input irrespective of file name
    --force-png
        Force bitmap graphics input irrespective of file name
    -s, --size
        Bitmap mode only: Physical size of output image in mm. Format: 12.34x56.78
    --sexp-mod-name
        Module name for KiCAD S-Exp output
    --sexp-layer
        Layer for KiCAD S-Exp output
    -a, --preserve-aspect-ratio
        Bitmap mode only: Preserve aspect ratio of image. Allowed values
        are meet, slice. Can also parse full SVG preserveAspectRatio syntax.
    --no-usvg
        Do not preprocess input using usvg (do not use unless you know
        *exactly* what you're doing)
    -e, --exclude-groups
        Comma-separated list of group IDs to exclude from export. Takes
        precedence over --only-groups.
