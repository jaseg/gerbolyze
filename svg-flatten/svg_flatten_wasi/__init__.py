import os
import sys
import subprocess
import tempfile
import wasmtime
import platform
import click
from pathlib import Path
import hashlib
import lzma
import appdirs
from importlib import resources as importlib_resources
try:
    importlib_resources.files # py3.9+ stdlib
except AttributeError:
    import importlib_resources # py3.8- shim


# ==============================
# Note on wasmtime path handling
# ==============================
#
# Hack: Right now, wasmtime's preopen_dir / --map functionality is completely borked. AFAICT only the first mapping is
# even considered, and preopening both / and . simply does not work: Either all paths open'ed by the executable must be
# absolute, or all paths must be relative. I spent some hours trying to track down where exactly this borkage originates
# from, but I found the code confusing and did not succeed.
# 
# FOR NOW we work around this issue the dumb way: We simply have click parse enough of the command line to transform any
# paths given on the command line to absolute paths. The actual path resolution is done by click because of
# resolve_path=True.
# 


def _run_wasm_app(wasm_filename, argv, cachedir="svg-flatten-wasi"):

    module_binary = importlib_resources.read_binary(__package__, wasm_filename)

    module_path_digest = hashlib.sha256(__file__.encode()).hexdigest()
    module_digest = hashlib.sha256(module_binary).hexdigest()
    cache_path = Path(os.getenv("SVG_FLATTEN_WASI_CACHE_DIR", appdirs.user_cache_dir(cachedir)))
    cache_path.mkdir(parents=True, exist_ok=True)
    cache_filename = (cache_path / f'{wasm_filename}-{module_path_digest[:8]}-{module_digest[:16]}')
    
    wasi_cfg = wasmtime.WasiConfig()
    wasi_cfg.argv = argv
    wasi_cfg.preopen_dir('/', '/')
    wasi_cfg.inherit_stdin()
    wasi_cfg.inherit_stdout()
    wasi_cfg.inherit_stderr()
    engine = wasmtime.Engine()

    import time
    try:
        with cache_filename.open("rb") as cache_file:
            module = wasmtime.Module.deserialize(engine, lzma.decompress(cache_file.read()))
    except:
        print("Preparing to run {}. This might take a while...".format(argv[0]), file=sys.stderr)
        module = wasmtime.Module(engine, module_binary)
        with cache_filename.open("wb") as cache_file:
            cache_file.write(lzma.compress(module.serialize(), preset=0))

    linker = wasmtime.Linker(engine)
    linker.define_wasi()
    store = wasmtime.Store(engine)
    store.set_wasi(wasi_cfg)
    app = linker.instantiate(store, module)
    linker.define_instance(store, "app", app)

    try:
        app.exports(store)["_start"](store)
        return 0
    except wasmtime.ExitTrap as trap:
        return trap.code


def run_usvg(input_file, output_file, **usvg_args):

    args = ['--keep-named-groups']
    for key, value in usvg_args.items():
        if value is not None:
            if value is False:
                continue

            args.append(f'--{key.replace("_", "-")[5:]}')

            if value is not True:
                args.append(value)

    args += [input_file, output_file]
    print(args)

    # By default, try a number of options:
    candidates = [
        # somewhere in $PATH
        'usvg',
        'wasi-usvg',
        # in user-local cargo installation
        Path.home() / '.cargo' / 'bin' / 'usvg',
        # wasi-usvg in user-local pip installation
        Path.home() / '.local' / 'bin' / 'wasi-usvg',
        # next to our current python interpreter (e.g. in virtualenv)
        str(Path(sys.executable).parent / 'wasi-usvg')
        ]

    # if USVG envvar is set, try that first.
    if 'USVG' in os.environ:
        candidates = [os.environ['USVG'], *candidates]

    for candidate in candidates:
        try:
            res = subprocess.run([candidate, *args], check=True)
            print('used usvg:', candidate)
            break
        except FileNotFoundError:
            continue
    else:
        raise SystemError('usvg executable not found')


@click.command(context_settings={'ignore_unknown_options': True})
@click.option('--no-usvg', is_flag=True)
# Options forwarded to USVG
@click.option('--usvg-dpi')
@click.option('--usvg-font-family')
@click.option('--usvg-font-size')
@click.option('--usvg-serif-family')
@click.option('--usvg-sans-serif-family')
@click.option('--usvg-cursive-family')
@click.option('--usvg-fantasy-family')
@click.option('--usvg-monospace-family')
@click.option('--usvg-use-font-file')
@click.option('--usvg-use-fonts-dir')
@click.option('--usvg-skip-system-fonts', is_flag=True)
# Catch-all argument to forward options to svg-flatten
@click.argument('other_args', nargs=-1, type=click.UNPROCESSED)
# Input/output file
@click.argument('input_file',  type=click.Path(resolve_path=True, dir_okay=False))
@click.argument('output_file', type=click.Path(resolve_path=True, dir_okay=False, writable=True))
def run_svg_flatten(input_file, output_file, other_args, no_usvg, **usvg_args):

    with tempfile.NamedTemporaryFile() as f:
        if not no_usvg:
            run_usvg(input_file, f.name, **usvg_args)
            input_file = f.name

        cmdline = ['svg-flatten', '--force-svg', '--no-usvg', *other_args, input_file, output_file]
        exit_code = _run_wasm_app("svg-flatten.wasm", cmdline)
        if exit_code:
            exc = click.ClickException(f'Process returned exit code {exit_code}')
            exc.exit_code = exit_code
            raise exc

