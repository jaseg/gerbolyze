import os
import sys
import tempfile
import wasmtime
import platform
import click
import pathlib
import hashlib
import appdirs
import lzma
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
    cache_path = pathlib.Path(os.getenv("SVG_FLATTEN_WASI_CACHE_DIR", appdirs.user_cache_dir(cachedir)))
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


@click.command(context_settings={'ignore_unknown_options': True})
@click.argument('other_args', nargs=-1, type=click.UNPROCESSED)
@click.argument('input_file',  type=click.Path(resolve_path=True, dir_okay=False))
@click.argument('output_file', type=click.Path(resolve_path=True, dir_okay=False, writable=True))
def run_usvg(input_file, output_file, other_args):

    cmdline = ['svg-flatten', *other_args, input_file, output_file]
    sys.exit(_run_wasm_app("svg-flatten.wasm", cmdline))
