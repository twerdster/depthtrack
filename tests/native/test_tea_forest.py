#!/usr/bin/env python3
"""Portable native Tea contracts: independent float32 oracle and deep-forest tests.

Uses only Python's standard library. Generated model data stays in a temporary
directory; the depth-20 test generates one ~38 MiB model, loaded as three trees.
"""
import argparse
import csv
import json
import math
import struct
import subprocess
import tempfile
from pathlib import Path


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def manifest(width, height, trees, classes=7):
    return {
        'schema': 'depthtrack-tea-forest-v1',
        'image': {'width': width, 'height': height, 'depth_unit': 'millimetres',
                  'intrinsics': {'fx': 500, 'fy': 500, 'cx': (width-1)/2, 'cy': (height-1)/2}},
        'feature': {'type': 'F_CHAR', 'max_diff': 250, 'maximum': 127,
                    'coordinate_rule': 'truncate-after-base-plus-offset-over-depth',
                    'coordinate_precision': 'float32-complete-sum',
                    'missing': {'both': 0, 'first': 127, 'second': -127}},
        'classes': [f'class_{i}' for i in range(classes)],
        'background_label': 7, 'unknown_label': 255,
        'aggregation': 'mean-normalized-leaf-probabilities', 'empty_leaf': 'root-prior',
        'trees': trees,
    }


def write_tree(path, depth, nodes, histograms, classes=7, maxval=1):
    n = (1 << (depth+1))-1
    assert len(nodes) == n and len(histograms) == 2*n+1
    with path.open('wb') as out:
        out.write(struct.pack('<4i', depth, classes, n, len(histograms)))
        for feature, threshold in nodes:
            out.write(struct.pack('<if', feature, threshold))
        for h in histograms:
            label = max(range(classes), key=h.__getitem__)
            out.write(struct.pack('<IH2x', maxval, label))
        for h in histograms:
            assert len(h) == classes
            out.write(bytes(h))


def distribution(cls, other=0):
    h = [other]*7
    h[cls] = 255
    return h


def reference(depth, width, height, specs):
    labels, probabilities, traces = [], [], []
    for pixel, z in enumerate(depth):
        if not z:
            labels.append(7)
            probabilities.extend([0.0]*7)
            traces.extend([0xffffffff]*len(specs))
            continue
        row, col = divmod(pixel, width)
        votes = [0.0]*7
        for spec in specs:
            heap = 0
            for _ in range(spec['levels']):
                feature, threshold = spec['nodes'][heap]
                coords = spec['coords'][feature]
                samples = []
                for offset in (0, 2):
                    # Float32 quotient AND complete sum, including negative
                    # fractions that truncate toward zero into row/column zero.
                    r = math.trunc(f32(f32(row)+f32(coords[offset]/f32(z))))
                    c = math.trunc(f32(f32(col)+f32(coords[offset+1]/f32(z))))
                    samples.append(depth[r*width+c] if 0 <= r < height and 0 <= c < width else 0)
                a, b = samples
                if not a and not b:
                    value = 0
                elif not a:
                    value = 127
                elif not b:
                    value = -127
                else:
                    value = math.ceil(max(-127, min(127, f32(f32(f32(a-b)/250)*127))))
                heap = heap*2+1+int(value > f32(threshold))
            traces.append(heap)
            histogram = spec['histograms'][heap]
            if not sum(histogram):
                histogram = spec['histograms'][0]
            for k, v in enumerate(histogram):
                votes[k] += spec['weight']*f32(v/sum(histogram))
        p = [f32(v/sum(s['weight'] for s in specs)) for v in votes]
        probabilities.extend(p)
        labels.append(max(range(7), key=p.__getitem__))
    return bytes(labels), struct.pack('<'+'f'*len(probabilities), *probabilities), struct.pack('<'+'I'*len(traces), *traces)


def run(cli, work, args, success=True):
    process = subprocess.run([str(cli), *map(str, args)], cwd=work, capture_output=True, text=True)
    if (process.returncode == 0) != success:
        raise AssertionError(f'Unexpected exit {process.returncode}: {process.stderr}\n{args}')
    return process


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cli', type=Path, required=True)
    args = parser.parse_args()
    cli = args.cli.resolve()
    with tempfile.TemporaryDirectory(prefix='depthtrack-tea-test-') as temporary:
        work = Path(temporary)
        fixture = Path(__file__).resolve().parent.parent/'fixtures'/'tea_forest'
        run(cli, work, [fixture/'forest.json', fixture/'depth.u16', 'portable', '--trace'])
        for suffix in ('.labels.u8', '.probabilities.f32', '.heap.u32'):
            assert (work/('portable'+suffix)).read_bytes() == (fixture/('expected'+suffix)).read_bytes(), suffix
        coords = [(-200.0, 0.0, 0.0, 0.0), (0.0, 250.0, 0.0, -250.0),
                  (150.25, -99.99999, -1000.0, 2000.0)]
        coords = [tuple(map(f32, xy)) for xy in coords]
        (work/'Coords.co').write_bytes(b''.join(struct.pack('<4f', *xy) for xy in coords))
        specs, trees = [], []
        for t, weight in enumerate((1.0, 1.0, 2.0)):
            nodes = [(0, -0.5+t), (1, -126.5+t), (2, 126.5-t)]
            histograms = [distribution(t, 7), distribution(1), distribution(2),
                          distribution(3), [0]*7, distribution(5), distribution(6)]
            filename = f'Tree_{t:04d}.tree'
            write_tree(work/filename, 1, nodes, histograms, maxval=(1000000 if t == 0 else 1))
            specs.append(dict(nodes=nodes, histograms=histograms, coords=coords, levels=2, weight=weight))
            trees.append(dict(model=filename, coords='Coords.co', weight=weight))
        config = manifest(4, 3, trees)
        (work/'forest.json').write_text(json.dumps(config))
        depth = [0, 100, 0, 400, 200, 201, 250, 0, 0, 450, 450, 600]
        (work/'depth.u16').write_bytes(struct.pack('<12H', *depth))
        run(cli, work, ['forest.json', 'depth.u16', 'prediction', '--trace'])
        expected = reference(depth, 4, 3, specs)
        for suffix, data in zip(('.labels.u8', '.probabilities.f32', '.heap.u32'), expected):
            assert (work/('prediction'+suffix)).read_bytes() == data, suffix
        # Centre ROI must preserve neighbors outside the ROI. Masking the
        # depth image itself would change pixel5's missing-neighbor branches.
        centre_mask = bytearray(12);centre_mask[5] = 255
        (work/'centre.u8').write_bytes(centre_mask)
        run(cli, work, ['forest.json', 'depth.u16', 'centre', '--trace', '--mask', 'centre.u8', '--save-mask', 'centre-saved.u8'])
        roi_labels = bytearray([7]*12);roi_labels[5] = expected[0][5]
        roi_probs = bytearray(12*7*4);roi_probs[5*7*4:6*7*4] = expected[1][5*7*4:6*7*4]
        roi_heaps = bytearray(b'\xff'*(12*3*4));roi_heaps[5*3*4:6*3*4] = expected[2][5*3*4:6*3*4]
        assert (work/'centre.labels.u8').read_bytes() == roi_labels
        assert (work/'centre.probabilities.f32').read_bytes() == roi_probs
        assert (work/'centre.heap.u32').read_bytes() == roi_heaps
        assert (work/'centre-saved.u8').read_bytes() == bytes(1 if i == 5 else 0 for i in range(12))
        # The CLI must not silently overwrite an earlier result.
        run(cli, work, ['forest.json', 'depth.u16', 'prediction'], success=False)
        bad = json.loads(json.dumps(config))
        for name, mutate in (
            ('negative-weight', lambda c: c['trees'][0].update(weight=-1)),
            ('wrong-profile', lambda c: c['feature'].update(type='F_SHORT')),
            ('ambiguous-precision', lambda c: c['feature'].pop('coordinate_precision')),
            ('bad-background', lambda c: c.update(background_label=0)),
        ):
            bad = json.loads(json.dumps(config)); mutate(bad)
            (work/'bad.json').write_text(json.dumps(bad))
            run(cli, work, ['bad.json', 'depth.u16', name], success=False)
        # Corrupt feature indices, missing root prior, truncated native layout.
        original = (work/'Tree_0000.tree').read_bytes()
        broken = bytearray(original);struct.pack_into('<i', broken, 16, 999999)
        (work/'Tree_0000.tree').write_bytes(broken)
        run(cli, work, ['forest.json', 'depth.u16', 'bad-feature'], success=False)
        broken = bytearray(original);hist_start=16+3*8+7*8;broken[hist_start:hist_start+7]=bytes(7)
        (work/'Tree_0000.tree').write_bytes(broken)
        run(cli, work, ['forest.json', 'depth.u16', 'empty-root'], success=False)
        (work/'Tree_0000.tree').write_bytes(original[:-1])
        run(cli, work, ['forest.json', 'depth.u16', 'truncated'], success=False)
        (work/'Tree_0000.tree').write_bytes(original)
        # Calibrated image-to-observation-to-tracker: one real measurement,
        # explicit zero-depth missing frame, then a new real measurement.
        write_tree(work/'constant.tree', 0, [(0, 0)], [distribution(0)]*3)
        (work/'zero.co').write_bytes(struct.pack('<4f', 0, 0, 0, 0))
        constant = manifest(3, 3, [dict(model='constant.tree', coords='zero.co')])
        (work/'constant.json').write_text(json.dumps(constant))
        (work/'sequence.u16').write_bytes(struct.pack('<27H', *([500]*9+[0]*9+[500]*9)))
        (work/'times.txt').write_text('0\n0.1\n0.2\n')
        run(cli, work, ['constant.json', 'sequence.u16', 'sequence', '--timestamps', 'times.txt',
                       '--class-ids', '0', '--observations', 'observations.csv', '--tracks', 'tracks.csv'])
        observations = list(csv.DictReader((work/'observations.csv').open()))
        assert [r['has_observation'] for r in observations] == ['1', '0', '1']
        assert max(abs(float(observations[0][k])-v) for k, v in zip(('x', 'y', 'z'), (0, 0, 500))) < 1e-10
        assert int(observations[0]['support']) == 9
        tracks = list(csv.DictReader((work/'tracks.csv').open()))
        assert [r['accepted'] for r in tracks] == ['1', '0', '1']
        assert tracks[1]['status'] == 'held' and tracks[1]['reason'] == 'missing'
        (work/'bad-times.txt').write_text('0\n0\n0.2\n')
        run(cli, work, ['constant.json', 'sequence.u16', 'bad-times', '--timestamps', 'bad-times.txt'], success=False)
        # Frozen deployment defaults must select the same postprocessor and
        # operating point as validation. Pixel centroid then mean Z differs
        # deliberately from the mean of individually backprojected XYZ points.
        posterior_hist = [102, 26, 26, 26, 26, 26, 23]
        write_tree(work/'posterior.tree', 0, [(0, 0)], [posterior_hist]*3)
        deployment = manifest(3, 1, [dict(model='posterior.tree', coords='zero.co')])
        deployment['postprocessing'] = {'method': 'largest4connected-probability-weighted-centroid',
                                      'foreground': 'none', 'probability_threshold': .25,
                                      'min_pixels': 3, 'fingertip_classes': [0]}
        (work/'deployment.json').write_text(json.dumps(deployment))
        (work/'varying.u16').write_bytes(struct.pack('<3H', 200, 400, 600))
        (work/'deployment-time.txt').write_text('0\n')
        run(cli, work, ['deployment.json', 'varying.u16', 'deployment', '--timestamps', 'deployment-time.txt', '--observations', 'deployment.csv'])
        deployed = list(csv.DictReader((work/'deployment.csv').open()))
        assert deployed[0]['has_observation'] == '1' and deployed[0]['support'] == '3'
        assert max(abs(float(deployed[0][k])-v) for k, v in zip(('x', 'y', 'z'), (0, 0, 400))) < 1e-10
        run(cli, work, ['deployment.json', 'varying.u16', 'override-threshold', '--timestamps', 'deployment-time.txt', '--observations', 'override-threshold.csv', '--min-probability', '.5'])
        assert list(csv.DictReader((work/'override-threshold.csv').open()))[0]['has_observation'] == '0'
        run(cli, work, ['deployment.json', 'varying.u16', 'override-support', '--timestamps', 'deployment-time.txt', '--observations', 'override-support.csv', '--min-support', '4'])
        assert list(csv.DictReader((work/'override-support.csv').open()))[0]['has_observation'] == '0'
        run(cli, work, ['deployment.json', 'varying.u16', 'override-classes', '--timestamps', 'deployment-time.txt', '--observations', 'override-classes.csv', '--class-ids', '1'])
        assert list(csv.DictReader((work/'override-classes.csv').open()))[0]['class_id'] == '1'
        # Fixed, label-free foreground: equal-sized disconnected near-depth
        # components select the earliest row-major seed; reject support<30.
        foreground_config = manifest(10, 10, [dict(model='constant.tree', coords='zero.co')])
        (work/'foreground.json').write_text(json.dumps(foreground_config))
        foreground_depth = [200 if c < 3 else 220 if c > 6 else 800 for r in range(10) for c in range(10)]
        (work/'foreground.u16').write_bytes(struct.pack('<100H', *foreground_depth))
        run(cli, work, ['foreground.json', 'foreground.u16', 'foreground', '--foreground', 'near-component', '--save-mask', 'foreground-mask.u8'])
        assert (work/'foreground-mask.u8').read_bytes() == bytes(int(c < 3) for r in range(10) for c in range(10))
        foreground_config['postprocessing'] = dict(deployment['postprocessing'], foreground='near-component')
        (work/'foreground-default.json').write_text(json.dumps(foreground_config))
        run(cli, work, ['foreground-default.json', 'foreground.u16', 'foreground-default', '--save-mask', 'foreground-default-mask.u8'])
        assert (work/'foreground-default-mask.u8').read_bytes() == (work/'foreground-mask.u8').read_bytes()
        run(cli, work, ['foreground-default.json', 'foreground.u16', 'foreground-override', '--foreground', 'none', '--save-mask', 'foreground-override-mask.u8'])
        assert (work/'foreground-override-mask.u8').read_bytes() == bytes([1]*100)
        foreground_depth = [200 if i < 29 else 0 for i in range(100)]
        (work/'foreground-small.u16').write_bytes(struct.pack('<100H', *foreground_depth))
        run(cli, work, ['foreground.json', 'foreground-small.u16', 'foreground-small', '--foreground', 'near-component', '--save-mask', 'foreground-small-mask.u8'])
        assert (work/'foreground-small-mask.u8').read_bytes() == bytes(100)
        # Optional eighth modeled class is distinct from zero-depth background.
        h8 = [0]*7+[255]
        write_tree(work/'background.tree', 0, [(0, 0)], [h8]*3, classes=8)
        background = manifest(2, 1, [dict(model='background.tree', coords='zero.co')], classes=8)
        background['background_label'] = 8
        background['modeled_background_class'] = 7
        (work/'background.json').write_text(json.dumps(background))
        (work/'background.u16').write_bytes(struct.pack('<2H', 500, 0))
        run(cli, work, ['background.json', 'background.u16', 'background'])
        assert (work/'background.labels.u8').read_bytes() == bytes([7, 8])
        assert struct.unpack('<16f', (work/'background.probabilities.f32').read_bytes()) == (0, 0, 0, 0, 0, 0, 0, 1)+(0,)*8
        (work/'one-time.txt').write_text('0\n')
        run(cli, work, ['background.json', 'background.u16', 'bad-background-observation', '--timestamps', 'one-time.txt',
                       '--class-ids', '7', '--observations', 'bad-background-observation.csv'], success=False)
        # Full 20-decision, three-tree traversal. It must retain uint32 heaps,
        # evaluate all decisions and never silently reduce to the FPGA prefix.
        depth_header = 19;n=(1 << 20)-1;h=2*n+1;leaf=(1 << 20)-1
        with (work/'deep.tree').open('wb') as f:
            f.write(struct.pack('<4i', depth_header, 7, n, h))
            node = struct.pack('<if', 0, 0)
            block = node*8192
            q, r = divmod(n, 8192)
            for _ in range(q): f.write(block)
            f.write(node*r)
            f.write(bytes(h*8))
            histograms = bytearray(h*7)
            histograms[0] = 255
            histograms[leaf*7+6] = 255
            f.write(histograms)
        deep = manifest(1, 1, [dict(model='deep.tree', coords='zero.co')]*3)
        (work/'deep.json').write_text(json.dumps(deep));(work/'one.u16').write_bytes(struct.pack('<H', 500))
        run(cli, work, ['deep.json', 'one.u16', 'deep-result', '--trace'])
        assert (work/'deep-result.labels.u8').read_bytes() == bytes([6])
        assert struct.unpack('<3I', (work/'deep-result.heap.u32').read_bytes()) == (leaf,)*3
        assert struct.unpack('<7f', (work/'deep-result.probabilities.f32').read_bytes()) == (0, 0, 0, 0, 0, 0, 1)
    print('Tea CPU contracts passed: float32/trace parity, weighted posterior averaging, empty prior, malformed input, centre-only ROI/raw neighbors, independent foreground, eight-class background, calibrated observations/tracking, full 20-decision three-tree traversal')


if __name__ == '__main__':
    main()
