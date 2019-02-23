from os import listdir
from os.path import join, isfile
from typing import List, Tuple


# Gerber file name extensions for Altium/Protel, KiCAD, Eagle
LAYER_SPEC = {
    'top': {
        'paste': ['.gtp', '-F.Paste.gbr', '.pmc'],
        'silk': ['.gto', '-F.SilkS.gbr', '.plc'],
        'mask': ['.gts', '-F.Mask.gbr', '.stc'],
        'copper': ['.gtl', '-F.Cu.gbr', '.cmp'],
        'outline': ['.gm1', '-Edge.Cuts.gbr', '.gmb'],
    },
    'bottom': {
        'paste': ['.gbp', '-B.Paste.gbr', '.pms'],
        'silk': ['.gbo', '-B.SilkS.gbr', '.pls'],
        'mask': ['.gbs', '-B.Mask.gbr', '.sts'],
        'copper': ['.gbl', '-B.Cu.gbr', '.sol'],
        'outline': ['.gm1', '-Edge.Cuts.gbr', '.gmb']
    },
}


class GerbolyzeError(Exception):
    pass


def find_gerber_in_dir(dir_path: str, extensions: List[str], exclude: List[str] = None) -> Tuple[str, str]:
    if exclude is None:
        exclude = []

    for entry in listdir(dir_path):
        if any(entry.lower().endswith(ext.lower()) for ext in extensions) \
                and not any(entry.lower().endswith(ex) for ex in exclude):
            lname = join(dir_path, entry)
            if not isfile(lname):
                continue
            with open(lname, 'r') as fd:
                return lname, fd.read()

    raise FileNotFoundError(f'Cannot find file with suffix { "|".join(extensions) } in dir { dir_path }')
