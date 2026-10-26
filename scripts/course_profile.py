"""Shared course identity and lab contract, also bundled with native hooks."""

import json
import re
from pathlib import Path

PROFILE_NAME = 'course-profile.json'


def load_profile(start=None):
    """Read the nearest profile, preferring an installed hook's own snapshot."""
    start = Path(start or __file__).resolve()
    if start.is_file():
        start = start.parent
    path = next((parent / PROFILE_NAME for parent in (start, *start.parents)
                 if (parent / PROFILE_NAME).is_file()), None)
    if path is None:
        raise ValueError('Missing course-profile.json; install course tools from the bootstrap branch.')
    return validate_profile(json.loads(path.read_text(encoding='utf-8')))


def validate_profile(profile):
    if not isinstance(profile, dict) or type(profile.get('schema_version')) is not int or profile['schema_version'] != 1:
        raise ValueError('course-profile.json: schema_version must be 1')
    for key in ('project', 'plugin_name', 'marketplace_name'):
        value = profile.get(key)
        if not isinstance(value, str) or not re.fullmatch(r'[a-z][a-z0-9-]*', value):
            raise ValueError('course-profile.json: invalid ' + key)
    if not isinstance(profile.get('display_name'), str) or not profile['display_name'].strip():
        raise ValueError('course-profile.json: display_name must be nonempty')

    def branch(value):
        return isinstance(value, str) and re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*', value)

    if not branch(profile.get('bootstrap_branch')):
        raise ValueError('course-profile.json: invalid bootstrap_branch')
    labs = profile.get('lab_branches')
    if not isinstance(labs, dict) or not labs or profile['bootstrap_branch'] in labs:
        raise ValueError('course-profile.json: lab_branches must be a nonempty object excluding bootstrap_branch')
    for name, paths in labs.items():
        if not branch(name) or not isinstance(paths, list) or not paths:
            raise ValueError('course-profile.json: invalid lab branch contract')
        for path in paths:
            if (not isinstance(path, str) or not re.fullmatch(r'[A-Za-z0-9_./-]+', path)
                    or any(part in ('', '.', '..') for part in path.split('/'))):
                raise ValueError('course-profile.json: expected paths must be relative project paths')
        if len(paths) != len(set(paths)):
            raise ValueError('course-profile.json: duplicate expected paths')
    return profile


def validate_distribution(bundle, profile):
    """External clients need static manifests; reject identity drift before install."""
    bundle = Path(bundle)
    plugin = profile['plugin_name']
    expected_source = './plugins/' + plugin
    for name in ('.agents/plugins/marketplace.json', '.claude-plugin/marketplace.json'):
        manifest = json.loads((bundle / name).read_text(encoding='utf-8'))
        entries = manifest.get('plugins', [])
        entry = next((item for item in entries if item.get('name') == plugin), {})
        source = entry.get('source')
        if isinstance(source, dict):
            source = source.get('path') if source.get('source') == 'local' else None
        if manifest.get('name') != profile['marketplace_name'] or source != expected_source:
            raise ValueError('Course profile does not match marketplace: ' + name)
    for name in ('.codex-plugin/plugin.json', '.claude-plugin/plugin.json'):
        manifest = json.loads((bundle / 'plugins' / plugin / name).read_text(encoding='utf-8'))
        if manifest.get('name') != plugin:
            raise ValueError('Course profile does not match plugin: ' + name)


if __name__ == '__main__':
    # The shell entry point delegates path resolution to the shared loader.
    import runpy
    import sys
    bundle = Path(__file__).resolve().parents[1]
    try:
        profile = load_profile(bundle)
        entry = bundle / 'plugins' / profile['plugin_name'] / 'scripts/setup_agents.py'
        sys.path.insert(0, str(entry.parent))
        sys.argv[0] = str(entry)
        runpy.run_path(str(entry), run_name='__main__')
    except (OSError, ValueError) as error:
        sys.exit(str(error))
