#!/usr/bin/env python3
"""Replace exactly the eight declared exercise bodies in a chapter 9 checkout."""
import argparse
import json
from pathlib import Path
import re


def transform(root, write=False):
    config = json.loads((root / 'lab.json').read_text())
    functions = config['functions']
    if len(functions) != 8 or len(config['allowed_code_files']) != 3:
        raise ValueError('chapter 9 must contain eight exercises in three files')
    changed = {}
    for entry in functions:
        path, name = root / entry['file'], entry['name']
        if entry['file'] not in config['allowed_code_files']:
            raise ValueError('exercise outside allowed code scope')
        text = changed.get(path, path.read_text())
        pattern = (r'(/\* LAB9_BEGIN ' + re.escape(name) + r' \*/)' +
                   r'.*?' + r'(/\* LAB9_END ' + re.escape(name) + r' \*/)')
        replacement = r'\1\n\tpanic("TODO(LAB9): ' + name + r'");\n\t\2'
        text, count = re.subn(pattern, replacement, text, flags=re.S)
        if count != 1:
            raise ValueError(f'{name}: expected exactly one exercise region')
        changed[path] = text
    for path, text in changed.items():
        if write:
            path.write_text(text)
    return {str(path.relative_to(root)): text.count('TODO(LAB9)') for path, text in changed.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path, nargs='?', default=Path(__file__).resolve().parents[1])
    parser.add_argument('--write', action='store_true', help='replace the bodies in this checkout')
    args = parser.parse_args()
    print(json.dumps(transform(args.root.resolve(), args.write), indent=2))

if __name__ == '__main__':
    main()
