# Native Tea forest inference

The C++17 Tea CPU backend loads native Tea trees plus their exact feature
coordinates and combines normalized class probabilities across trees. It
supports three-tree forests and complete 20-decision trees without pruning.
This is a separate model format and execution path from the historical
seven-class ten-decision packed CPU/Metal/FPGA example.

## Trained HandNet models

The [Tea HandNet release](https://github.com/twerdster/tea/releases/tag/handnet-forest-2026-11-16)
contains the validation-selected three-tree ensemble and single tree (seed 202).
Both evaluate all **20 decisions**, with **1200 features** per tree and eight
classes: thumb, index, middle, ring, pinky, palm, wrist and learned background.
Their manifests freeze `foreground: none`, probability cutoff **0.25**, minimum
component support **3**, and fingertip class IDs `0..4`. Background class 7 is
separate from invalid-depth label 8 and unknown label 255.

| Download | Archive SHA256 |
| --- | --- |
| [Three-tree ensemble](https://github.com/twerdster/tea/releases/download/handnet-forest-2026-11-16/tea-handnet-selected-ensemble.zip) | `e9cff62eead049b5c5d7dc568acab3e2483a228188ec8290c62170d5fe580cf1` |
| [Single tree](https://github.com/twerdster/tea/releases/download/handnet-forest-2026-11-16/tea-handnet-selected-single.zip) | `6fbab3bee21e95b13b20f95c1fe4b31c979b07e17361d9d7a859993c464e2641` |

Each ZIP contains a relative-path `forest.json`, its exact model/coordinate
assets and release verification. Download the desired archive, verify its hash
against the table, then unpack it into a new directory. Build the CLI as shown
below. For example, after unpacking the ensemble into `models/handnet-ensemble`:

```sh
mkdir -p out
build/native/depthtrack_tea models/handnet-ensemble/forest.json \
  /path/to/depth.u16 out/handnet --trace \
  --timestamps /path/to/times-seconds.txt \
  --observations out/handnet-observations.csv --tracks out/handnet-tracks.csv
```

This command uses the selected manifest's postprocessing defaults. Input is
320×240 row-major uint16 depth in millimetres, with one real, strictly increasing
timestamp in seconds per frame. Preserve raw neighbor depths and the declared
camera intrinsics; another camera/crop needs its own verified calibration and
matching preprocessing. These are Mac/Linux CPU models; the release does not
establish deep Metal, mobile-device or FPGA execution.

Training used three independently seeded 90-percent frame bags, 181,979 training
frames per tree and about 84.76 million balanced samples per tree. Depth/cutoff
selection used one frozen 300-frame validation subset. The selected settings
remained fixed for the full validation and untouched test evaluations:

| Model | All 2773 validation frames: mean fingertip F1@15mm | All 10,000 test frames: mean fingertip F1@15mm |
| --- | ---: | ---: |
| Three-tree ensemble | 0.383344 | 0.374948 |
| Single tree (seed 202) | 0.337060 | 0.327988 |

These are F1 scores, not classification-accuracy percentages. Full-validation
scores are descriptive after selection. F1 includes missed and spurious
fingertips against valid adjusted sensor positions. The test reports passed an
independent audit of raw frame identities, separation, ground-truth counts,
reported localization hits/errors and score arithmetic. That metric audit did
not rerun per-pixel classifier predictions. Official random-frame splits do not
establish participant/session independence or reproduce the paper's metric.
The [Tea final-results report](https://github.com/twerdster/tea/blob/handnet-forest-training/docs/handnet-forest-results.md)
and [safe aggregate evidence](https://github.com/twerdster/tea/blob/handnet-forest-training/docs/evidence/handnet-forest-2026-11-16.json)
give the experiment, evaluation denominators and limitations.

Actual selected-model parity passed on **Linux x86-64 and Mac arm64**, using the
same three training depth frames in both `none` and `near-component` modes.
Native/Python labels, posteriors, reached heaps and masks matched byte for byte.
Maximum calibrated observation/tracker difference was 3.41e-13 mm; Linux/Mac
outputs agreed exactly. The [safe parity record](tea-handnet-selected-model-validation.json)
contains only aggregate counts, settings and asset identities. Synthetic 0.1 s
timestamps exercise the API and complete tracker histories; they do not measure
temporal accuracy or latency. Ground-truth labels and sensor poses were not used
by this parity check.

The frozen training revision is
[`27ae728e9cb012d2255337d9b3583bbe7f66b3cc`](https://github.com/twerdster/tea/commit/27ae728e9cb012d2255337d9b3583bbe7f66b3cc).
Later packaging/documentation commits do not change that provenance. Attribution,
dataset-release terms and modification details are in each manifest; the dataset's
CC BY 4.0 release does not relicense historical MATLAB or unrelated legacy code.

## Build and synthetic smoke check

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel 4
ctest --test-dir build/native --output-on-failure
build/native/depthtrack_tea tests/fixtures/tea_forest/forest.json \
  tests/fixtures/tea_forest/depth.u16 out/synthetic --trace
```

Create `out/` first. The CLI refuses existing output files. The example produces
`out/synthetic.labels.u8`, `.probabilities.f32` and `.heap.u32`. Labels are
row-major; probability channels are contiguous `[frame,row,column,class]`;
traces are `[frame,pixel,tree]`. All binary files are little-endian. Input is
contiguous uint16 depth in millimetres; an exact multiple of the manifest's frame
size is required. `--frames N` checks that count. Use `--no-probabilities` to omit
the probability file when only labels/observations are needed.

## Manifest and arithmetic

The complete small example is
[`tests/fixtures/tea_forest/forest.json`](../tests/fixtures/tea_forest/forest.json).
Schema `depthtrack-tea-forest-v1` records image dimensions, depth units, optional
camera intrinsics, class names, per-tree model/coordinate paths and positive
weights. Paths are relative to the manifest. Each tree is checked for the native
Tea 16-byte header, 8-byte feature-index/threshold node, 8-byte compact leaf and
byte histogram layout. A ZScan 24-byte node model is rejected.

Supported feature type is `F_CHAR`, with configurable positive `max_diff` and
integer `maximum` from 1 to 127 (the HandNet profile uses 250 and 127). A feature
samples two neighbors at `base + offset / centre_depth`; division and complete
sum use float32, followed by truncation toward zero. Offsets remain float32 from
`Coords.co`. Coordinates are row first, column second. Missing/OOB neighbors
produce 0 when both are absent,`+maximum`when the first is absent and `-maximum`
when the second is absent. Present neighbors produce
`ceil(clip((d1-d2)/max_diff*maximum,-maximum,maximum))` in float32. Branch right
exactly when this value is greater than the node's float32 threshold.

Each reached byte histogram is normalized by its sum, then positive tree weights
form the mean posterior. Compact maxVal cancels; trees are not weighted by the
number of samples in a leaf. Empty leaves use that tree's normalized root prior;
an entirely empty root is rejected. Ties select the lowest class ID. Centre
depth zero returns the declared background label and all-zero probabilities.
Unknown/no-vote stays a separate label. This backend does not reproduce the old
raw-count forest vote or silently apply its 200/126 feature profile.

Seven-class models have no learned background class. An optional eight-class
model may learn class 7 from valid-depth nonhand pixels; its invalid-depth label
must remain outside the modeled classes (for example 8). The manifest's optional
`modeled_background_class` documents that distinction; the native observation
stage rejects that class ID. The native loader supports both class counts.

Tea's depth parameter is inclusive: header 19 means 20 decisions. Heap traces use
uint32, supporting complete deep trees. The current loader permits header depths
0 through 24 and validates exact file dimensions before allocation. Large complete
trees can use substantial host RAM; models are not silently truncated.

## Independent foreground and explicit centre masks

`--foreground near-component` computes a fixed depth-only ROI without labels,
calibration, model predictions or temporal state. Valid depth is 100..1500 mm
inclusive. The near depth is the exact order statistic at index `(N-1)//50` in
sorted valid pixels (second percentile). Candidates are valid pixels no farther
than `near + 150 mm`. The largest four-connected component is retained only with at
least 30 pixels; equal sizes choose the earliest row-major seed. Empty/insufficient
inputs produce an empty ROI. This is an explicit heuristic, not a claim of
validated hand localization; foreground objects and forearms can remain.

Alternatively, `--mask centres.u8` accepts one caller-provided byte per input
pixel, with nonzero meaning query. These two options are mutually exclusive.
Use `--save-mask actual.u8` to record the resulting 0/1 centre ROI. Any supplied mask's
origin must be reported when evaluating results; ground-truth masks are an
oracle condition, not a deployed foreground detector.

Crucially, masks restrict **queried centre pixels only**. Feature neighbor
lookups always read the original raw depth, matching training feature-bank
construction. Multiplying depth by the centre mask would change missing-neighbor
features and is a different model contract. Outside the centre ROI, outputs are
background, zero probabilities and `UINT32_MAX` traces.

## Metric observations and temporal tracking

The optional native observation stage matches the Python evaluation detector:
select the largest four-connected component of pixels whose winning class is
the requested class and whose posterior meets the confidence gate. Equal
component sizes choose the first row-major seed. Compute probability-weighted
pixel X/Y and mean depth Z, then backproject that centroid using calibrated
`fx,fy,cx,cy`. Averaging individually backprojected XYZ points is a different
algorithm. XYZ is millimetres with X right, Y down, Z forward; depth is axial Z.
Without manifest defaults, the probability gate is .5 and minimum support is 5.
No anatomical class mapping or optimal operating point is inferred.

A selected deployment can freeze these defaults in its manifest:

```json
"postprocessing": {
  "method": "largest4connected-probability-weighted-centroid",
  "foreground": "none",
  "probability_threshold": 0.25,
  "min_pixels": 3,
  "fingertip_classes": [0, 1, 2, 3, 4]
}
```

The method is fixed when present. The CLI applies these optional defaults;
explicit `--foreground`, `--min-probability`, `--min-support`, and `--class-ids`
override them. A caller-provided `--mask` overrides the foreground default.
Core `Forest::predict` probability inference is independent of postprocessing.

```sh
build/native/depthtrack_tea /path/to/forest.json /path/to/depth.u16 out/run \
  --timestamps /path/to/times-seconds.txt --class-ids 0,1,2,3,4 \
  --observations out/observations.csv --tracks out/tracks.csv
```

Use those class IDs only after verifying the training label map. Supply one
strictly increasing timestamp per frame; frame rates are never invented. Missing
components are passed as missing measurements to the existing timestamp-aware
tracker, preserving held/lost/reacquisition policies. The observation CSV's first
seven columns match `depthtrack_csv` input, so tracker parameters can also be
evaluated separately. The C++ APIs are in
[`tea_forest.hpp`](../include/depthtrack/tea_forest.hpp).

For a different camera or crop, supply its own calibration and training-matched
pixel/depth preprocessing. The historical 320×240 demo camera matrix is not a
universal calibration. Quantitative fingertip accuracy requires independently
held-out labels and validated joint-to-surface geometry; inference parity and
tracker smoothing do not establish it.

## Validation and platform scope

The native tests compare exact labels, float32 posteriors and reached heaps with
an independent portable oracle. They cover unequal tree weights/support scales,
missing neighbors, an empty leaf, rejected malformed input, calibrated XYZ,
explicit missing measurements, and all 20 decisions of a three-tree forest.
A separate observation fixture checks finite/missing slots for all five requested
classes and varied-depth centroid geometry against the Python method. Manifest
defaults and explicit overrides are exercised through the CLI.
Existing tracker golden tests remain included. No training occurred in these
synthetic tests and no relationship to historical models is inferred.

Mac arm64 and Linux x86-64 CPU execution are verified; all six native CTests
pass on both, including exact portable-fixture outputs. Deep Tea forests have not been ported to Metal,
mobile inference or FPGA. Cross-compilation disables the optional Tea library
and CLI by default; existing mobile tracker builds retain their prior scope.
`DEPTHTRACK_BUILD_TEA=OFF` also disables this backend on desktop. The old packed
model, fixtures and hardware image are unchanged.

## Supplied HandNet frames: native/Python integration checks

`tools/validate_tea_handnet.py` accepts an immutable selected deployment manifest,
the matching Tea Python reference, and extracted train/validation MAT frames. It
does not change training sources or select models. Use the cropped deployment
manifest so both implementations execute the same selected number of decisions.
The tool refuses test-data directories and existing output directories.

```sh
python tools/validate_tea_handnet.py \
  --manifest /path/to/selected/forest.json \
  --frames /path/to/TrainData --split train \
  --tea-reference /path/to/tea/scripts \
  --native build/native/depthtrack_tea --tracker build/native/depthtrack_csv \
  --output out/selected-model-parity --max-frames 3 \
  --model-role trained-model --data-role actual-handnet --synthetic-step-s .1
```

NumPy and SciPy are required, along with the matching Tea reference modules.
Declare the model/data role honestly: use `--model-role synthetic-harness` for
untrained fixtures. Random HandNet frame splits do not establish a chronological
sequence. `--synthetic-step-s` explicitly marks timestamps as an API experiment;
it does not measure temporal tracking accuracy or latency. For a genuine recorded
sequence, use `--timestamps times-seconds.txt` after verifying frame/clock alignment.

Both `none` and independent `near-component` foreground conditions run on the
same unmodified raw depth. Queried-centre ROIs, labels and reached heaps must
agree exactly. Float32 posterior differences are reported in bytes, absolute
error and ULPs; the default absolute acceptance tolerance is twice float32
epsilon (2.3841858e-7), configurable with `--probability-atol`. Label disagreement
is always a failure, including near argmax ties, with the first differing pixel,
probabilities and heaps retained for diagnosis.

The selected confidence/minimum-support/class defaults drive connected-component
XYZ observations. Checks include finite/missing slots, support, frame/class/time
correspondence, native/Python tracker lifecycle, positions, optional fields and
complete histories. The integrated Tea tracker must also match separate native
C API replay. Geometry/tracking tolerance defaults to 1e-8; raw outputs, model/input
hashes, logs and a durable report remain in the new output directory. No ground
truth labels or sensor poses enter this parity tool, and no recognition score is
inferred from implementation agreement.

The recorded [harness check](tea-handnet-harness-validation.json) used three
actual **training** frames with an **untrained synthetic fixture forest** and a
synthetic 0.1 s cadence. Both foreground conditions passed: labels/heaps/ROI matched
exactly, observed posterior difference was zero, and the maximum observation
difference was 2.28e-12 mm. All 30 tracker states and their histories matched. This
validates the harness, not an actual trained HandNet model. Actual selected-model
parity is separately recorded in
[the trained-model parity evidence](tea-handnet-selected-model-validation.json).
