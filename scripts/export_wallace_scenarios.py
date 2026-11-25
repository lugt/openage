#!/usr/bin/env python3
# Copyright 2026 the openage authors. See copying.md for legal info.
"""Extract selected original campaign scenes from a local installation."""

from argparse import ArgumentParser
import hashlib
import json
from collections import Counter
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from openage.scenario.de2_campaign import campaign_entries, extract_scenario, unpack_campaign
from openage.scenario.de2_scenario import read_scenario
from scripts.tech_tree.catalog import strings_at, write_json


def main():
    parser = ArgumentParser(description=__doc__)
    installation = Path.home() / 'Library/Application Support/Steam/steamapps/common/AoE2DE'
    parser.add_argument('--original-dir', type=Path, default=installation / 'AgeOfEmpires2Data')
    parser.add_argument('--executable', type=Path, default=installation / 'Age Of Empires II.app/Contents/MacOS/Age Of Empires II')
    parser.add_argument('--campaign', type=Path)
    parser.add_argument('--scenarios', nargs='+', help='Scenario filenames inside the selected campaign')
    parser.add_argument('--ai-map-sandbox', action='store_true', help='Explicitly omit all story triggers for map-based AI tests')
    parser.add_argument('--output', type=Path, default=ROOT / 'logs/wallace-original')
    args = parser.parse_args()
    source = args.campaign or args.original_dir / 'resources/_common/campaign/cam1.aoe2campaign'
    if not source.is_file() and source.with_suffix(source.suffix + '.gpv').is_file():
        source = source.with_suffix(source.suffix + '.gpv')
    args.output.mkdir(parents=True, exist_ok=True)
    campaign = unpack_campaign(source, args.output / source.name.removesuffix('.gpv'), args.executable)
    strings = strings_at(args.original_dir, 'en') | strings_at(args.original_dir, 'zh')
    entries = {entry['name']: entry for entry in campaign_entries(campaign)}
    manifest = {'campaign': str(source), 'sha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'missions': []}
    selected = args.scenarios or [f'1_Wallace_{index}.aoe2scenario' for index in (1, 2, 3)]
    for index, name in enumerate(selected, 1):
        path = args.output / name
        extract_scenario(campaign, entries[name], path)
        data = read_scenario(path, strings)
        data['source_file'] = name
        data['source_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
        if args.ai_map_sandbox:
            data['ai_test_variant'] = 'map-only'
            data['original_trigger_types'] = {
                'conditions': dict(Counter(c['condition_type'] for t in data['triggers'] for c in t['conditions'])),
                'effects': dict(Counter(e['effect_type'] for t in data['triggers'] for e in t['effects'])),
                'trigger_count': len(data['triggers']),
            }
            data['triggers'] = []
        write_json(args.output / f'mission-{index}.json', data)
        manifest['missions'].append({'file': name, 'sha256': data['source_sha256'], 'width': data['width'],
                                     'height': data['height'], 'entities': len(data['units']), 'triggers': len(data['triggers'])})
        del data
    write_json(args.output / 'manifest.json', manifest)
    print(json.dumps(manifest, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
