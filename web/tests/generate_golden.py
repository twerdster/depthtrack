#!/usr/bin/env python3
"""Regenerate small JS parity fixtures using a specified Python reference.

Usage: python generate_golden.py /path/to/adaptive_tracker.py
The recorded fixture already contains independent Python expected states.
"""
from pathlib import Path
import hashlib
import importlib.util
import json
import math
import sys

def main():
    source = Path(sys.argv[1]).resolve()
    spec = importlib.util.spec_from_file_location('depthtrack_reference_for_js', source)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    p = [30.0, -20.0, 350.0]
    cases = {
        'nonzero_stationary': [(0, p), (.1, p), (.23, p), (.41, p), (.55, p), (1.01, p)],
        'missing_hold_lost_reacquire': [(0, p), (.1, None), (.3, None), (.6, [50., 10., 360.])],
        'one_jump_then_return': [(0, p), (.1, [800., -20., 350.]), (.2, p)],
        'confirmed_relocation': [(0, p), (.1, [800., -20., 350.]), (.2, [805., -20., 350.])],
        'invalid_depth': [(0, p), (.1, [10., 0., 0.]), (.2, p)],
        'hold_and_reset_edges': [(0, p), (.25, None), (.250001, None), (.450001, [50., 10., 360.])],
        'irregular_moving': [(i * .131 + .015 * math.sin(i), [70 * math.sin(i * .4), 50 * math.cos(i * .3), 350 + 10 * math.sin(i * .2)]) for i in range(50)],
    }
    out = []
    for name, observations in cases.items():
        tracker = module.AdaptivePointTracker()
        out.append({'name': name, 'parameters': {}, 'updates': [
            {'time_s': t, 'point': point, 'expected': tracker.update(t, point)} for t, point in observations
        ]})
    result = {'schema_version': 1, 'source_filename': source.name,
              'python_reference_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
              'cases': out}
    destination = Path(__file__).parent / 'golden.json'
    destination.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
    print(f'Wrote {len(out)} Python-reference cases / {sum(len(c["updates"]) for c in out)} updates')

if __name__ == '__main__':
    main()
