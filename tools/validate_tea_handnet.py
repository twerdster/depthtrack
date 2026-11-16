#!/usr/bin/env python3
"""Compare native/Python Tea inference and tracking on identical MAT depth frames.

Requires the matching Tea scripts directory (NumPy/SciPy), a selected deployment
manifest, and built depthtrack_tea/depthtrack_csv executables. This tool does not
train, select a model, open test data, or measure recognition accuracy. Synthetic
models and synthetic timestamps must be declared explicitly.
"""
import argparse
import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
import time

import numpy as np
import scipy.io as sio

ROOT = Path(__file__).resolve().parents[1]
MODES = ("none", "near-component")


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def save_json(path, value):
    temporary = path.with_suffix(path.suffix+".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False)+"\n")
    temporary.replace(path)


def load_reference(directory):
    directory = Path(directory).resolve()
    if not (directory/"tea_model.py").is_file():
        raise ValueError("--tea-reference must contain tea_model.py and handnet_to_tea.py")
    sys.path.insert(0, str(directory))
    spec = importlib.util.spec_from_file_location("depthtrack_tea_parity_reference", directory/"tea_model.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def actual_frames(directory, maximum):
    directory = Path(directory).resolve()
    if any(part.lower() in ("test", "testdata") for part in directory.parts):
        raise ValueError("This integration harness refuses test data; use train/validation frames")
    frames = sorted(directory.glob("Data_*.mat"))
    if not frames:
        raise ValueError("No Data_*.mat frames in --frames")
    return frames[:maximum]


def read_timestamps(args, count):
    if args.timestamps is not None:
        values = [float(line) for line in args.timestamps.read_text().splitlines()]
        provenance = {"kind": "caller-supplied", "sha256": sha256(args.timestamps),
                      "note": "Caller must establish clock units, chronology and frame correspondence."}
    else:
        values = [i*args.synthetic_step_s for i in range(count)]
        provenance = {"kind": "synthetic-api-cadence", "step_s": args.synthetic_step_s,
                      "note": "Frame order/cadence is an API parity experiment, not measured temporal performance."}
    if len(values) != count or any(not math.isfinite(v) for v in values) or any(b <= a for a, b in zip(values, values[1:])):
        raise ValueError("Need exactly one finite strictly increasing timestamp per frame")
    return values, provenance


def compare_tracking(rows, expected, tolerance):
    if len(rows) != len(expected):
        raise AssertionError("Tracker state count mismatch")
    maximum = 0.
    histories = 0
    for row, state in zip(rows, expected):
        for key in ("status", "accepted", "generation"):
            actual = row[key] if key == "status" else int(row[key])
            if actual != state[key]:
                raise AssertionError(f"Tracker {key} differs")
        if (row["reason"] or None) != state.get("reason") or bool(int(row["visible"])) != (state["position"] is not None):
            raise AssertionError("Tracker visibility/reason differs")
        if any(not math.isfinite(float(row[key])) for key in ("out_x", "out_y", "out_z")):
            raise AssertionError("Tracker position output is nonfinite")
        def close(actual, wanted):
            nonlocal maximum
            delta = abs(float(actual)-float(wanted))
            if not math.isfinite(delta) or delta > tolerance:
                raise AssertionError(f"Tracker numeric difference {delta} exceeds {tolerance}")
            maximum = max(maximum, delta)
        if state["position"] is not None:
            for key, value in zip(("out_x", "out_y", "out_z"), state["position"]):
                close(row[key], value)
        for key in ("observation_age_s", "cutoff_hz"):
            value = state.get(key)
            if value is None:
                if row[key] != "":
                    raise AssertionError(f"Tracker optional {key} differs")
            else:
                close(row[key], value)
        history = json.loads(row["history_json"])
        if len(history) != len(state["history"]):
            raise AssertionError("Tracker history length differs")
        for actual, wanted in zip(history, state["history"]):
            histories += 1
            close(actual["time_s"], wanted["time_s"])
            for x, y in zip(actual["position"], wanted["position"]):
                close(x, y)
    return {"states": len(rows), "history_samples": histories,
            "max_numeric_absolute_difference": maximum, "tolerance": tolerance,
            "status_acceptance_generation_missing_and_history_match": True}


def execute(args):
    if args.output.exists():
        raise ValueError("Output already exists; preserve earlier evidence and use a new directory")
    if args.max_frames < 1 or args.max_frames > 100:
        raise ValueError("This bounded integration check requires 1..100 frames")
    if args.synthetic_step_s is not None and (not math.isfinite(args.synthetic_step_s) or args.synthetic_step_s <= 0):
        raise ValueError("Synthetic cadence must be finite and positive")
    paths = actual_frames(args.frames, args.max_frames)
    reference = load_reference(args.tea_reference)
    forest = reference.TeaForest(args.manifest)
    shape = (forest.document["image"]["height"], forest.document["image"]["width"])
    post = forest.document.get("postprocessing", {})
    threshold = post.get("probability_threshold", .5)
    minimum = post.get("min_pixels", 5)
    class_ids = post.get("fingertip_classes", list(range(min(5, forest.classes))))
    if not class_ids or any(not isinstance(c, int) or not 0 <= c < min(5, forest.classes) for c in class_ids) or len(set(class_ids)) != len(class_ids):
        raise ValueError("HandNet parity requires explicit unique fingertip IDs in 0..4")
    method = post.get("method", "largest4connected-probability-weighted-centroid")
    if method != "largest4connected-probability-weighted-centroid":
        raise ValueError("Unsupported postprocessing method")
    timestamps, timeline = read_timestamps(args, len(paths))
    args.output.mkdir(parents=True)
    result = {"schema": "depthtrack-handnet-parity-v1", "phase": "running", "started_unix": time.time(),
              "model_role_declared_by_caller": args.model_role, "data_role_declared_by_caller": args.data_role,
              "split": args.split, "timeline": timeline, "recognition_accuracy_evaluated": False,
              "temporal_accuracy_or_latency_evaluated": False,
              "probability_absolute_tolerance": args.probability_atol,
              "geometry_and_tracking_absolute_tolerance": args.tolerance,
              "scope": "Implementation agreement on supplied raw depths; no training or model selection.",
              "manifest_sha256": sha256(args.manifest),
              "reference_sha256": sha256(args.tea_reference/"tea_model.py"),
              "native_binary_sha256": sha256(args.native), "tracker_binary_sha256": sha256(args.tracker),
              "harness_sha256": sha256(__file__),
              "trees": [{"index": i, "model_sha256": sha256(tree.path), "coords_sha256": sha256(tree.coords_path),
                         "split_decisions": tree.splits, "classes": tree.classes}
                        for i, tree in enumerate(forest.trees)], "frames": [], "modes": []}
    report = args.output/"report.json"
    save_json(report, result)
    try:
        depths = []
        for path in paths:
            # Labels/poses are deliberately not loaded: no oracle enters ROI,
            # prediction, localization, temporal filtering or parity checks.
            mat = sio.loadmat(path, variable_names=["depth", "session", "idx", "gidx", "sID"])
            depth = np.asarray(mat["depth"])
            if depth.shape != shape or not np.isfinite(depth).all() or np.any(depth < 0) or np.any(depth > 65535) or np.any(depth != np.floor(depth)):
                raise ValueError(f"Input must be exact integral uint16-millimetre depth with shape {shape}: {path.name}")
            depth = depth.astype("<u2", copy=False)
            depths.append(depth)
            identity = reference.frame_identity(mat, path.name, args.split)
            result["frames"].append({"name": path.name, "sha256": sha256(path), "frame_id": identity["frame_id"],
                                     "raw_depth_sha256": hashlib.sha256(depth.tobytes()).hexdigest(),
                                     "positive_pixels": int((depth > 0).sum())})
        input_path = args.output/"raw-depth.u16"
        with input_path.open("xb") as stream:
            for depth in depths:
                stream.write(depth.tobytes())
        time_path = args.output/"times.txt"
        time_path.write_text("".join(f"{t:.17g}\n" for t in timestamps))
        sys.path.insert(0, str(ROOT/"python"))
        from depthtrack import AdaptivePointTracker
        for mode in MODES:
            work = args.output/mode;work.mkdir()
            command = [str(args.native), str(args.manifest), str(input_path), str(work/"native"),
                       "--trace", "--foreground", mode, "--save-mask", str(work/"centres.u8"),
                       "--timestamps", str(time_path), "--class-ids", ",".join(map(str, class_ids)),
                       "--min-probability", str(threshold), "--min-support", str(minimum),
                       "--observations", str(work/"observations.csv"), "--tracks", str(work/"tracks.csv")]
            with (work/"native.log").open("x") as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=args.timeout_seconds)
            count, height, width, classes, trees = len(depths), *shape, forest.classes, len(forest.trees)
            native_labels = np.fromfile(work/"native.labels.u8", dtype=np.uint8).reshape(count, height, width)
            native_probabilities = np.fromfile(work/"native.probabilities.f32", dtype="<f4").reshape(count, height, width, classes)
            native_heaps = np.fromfile(work/"native.heap.u32", dtype="<u4").reshape(count, height, width, trees)
            native_masks = np.fromfile(work/"centres.u8", dtype=np.uint8).reshape(count, height, width)
            observations = list(csv.DictReader((work/"observations.csv").open()))
            if len(observations) != count*len(class_ids):
                raise AssertionError("Observation count mismatch")
            metrics = {"mode": mode, "phase": "running", "label_mismatches": 0, "probability_byte_mismatches": 0,
                       "probability_max_absolute_difference": 0., "probability_max_ulps": 0,
                       "probability_absolute_tolerance": args.probability_atol,
                       "heap_mismatches": 0, "mask_mismatches": 0,
                       "queried_pixels": 0, "observations_present": 0, "observations_missing": 0,
                       "observation_max_absolute_difference_mm": 0., "postprocessing": {
                           "method": method, "probability_threshold": threshold, "min_pixels": minimum,
                           "fingertip_classes": class_ids}, "raw_neighbor_depth_preserved": True}
            result["modes"].append(metrics);save_json(report, result)
            trackers = {cls: AdaptivePointTracker() for cls in class_ids};expected_states = [];expected_row_ids = []
            for frame, depth in enumerate(depths):
                mask = reference.near_component_mask(depth) if mode == "near-component" else depth > 0
                labels, probabilities = forest.predict(depth, mask=mask)
                if not np.isfinite(native_probabilities[frame]).all():
                    raise AssertionError("Native probability output is nonfinite")
                metrics["label_mismatches"] += int((labels != native_labels[frame]).sum())
                metrics["probability_byte_mismatches"] += int((probabilities.view(np.uint8) != native_probabilities[frame].view(np.uint8)).sum())
                metrics["probability_max_absolute_difference"] = max(metrics["probability_max_absolute_difference"], float(np.abs(probabilities-native_probabilities[frame]).max()))
                ulps = np.abs(probabilities.view("<u4").astype(np.int64)-native_probabilities[frame].view("<u4").astype(np.int64))
                metrics["probability_max_ulps"] = max(metrics["probability_max_ulps"], int(ulps.max()))
                metrics["mask_mismatches"] += int((mask != native_masks[frame]).sum())
                rows, cols = np.nonzero((depth > 0) & mask);metrics["queried_pixels"] += len(rows)
                expected_heaps = np.full((height, width, trees), 0xffffffff, dtype="<u4")
                for tree_index, tree in enumerate(forest.trees):
                    for start in range(0, len(rows), 8192):
                        rr, cc = rows[start:start+8192], cols[start:start+8192]
                        _, heaps = tree.predict_depth(depth, rr, cc, return_heaps=True)
                        expected_heaps[rr, cc, tree_index] = heaps
                metrics["heap_mismatches"] += int((expected_heaps != native_heaps[frame]).sum())
                # Retain independent outputs for diagnosis, including any
                # future label disagreement close to a float32 argmax tie.
                (work/f"python-{frame:03d}.labels.u8").write_bytes(labels.tobytes())
                (work/f"python-{frame:03d}.probabilities.f32").write_bytes(probabilities.astype("<f4", copy=False).tobytes())
                (work/f"python-{frame:03d}.heap.u32").write_bytes(expected_heaps.tobytes())
                if metrics["label_mismatches"] and "first_label_mismatch" not in metrics:
                    rr, cc = np.argwhere(labels != native_labels[frame])[0]
                    metrics["first_label_mismatch"] = {"frame": frame, "row": int(rr), "column": int(cc),
                        "python_label": int(labels[rr,cc]), "native_label": int(native_labels[frame,rr,cc]),
                        "python_probabilities": probabilities[rr,cc].tolist(),
                        "native_probabilities": native_probabilities[frame,rr,cc].tolist(),
                        "python_heaps": expected_heaps[rr,cc].tolist(), "native_heaps": native_heaps[frame,rr,cc].tolist()}
                centres = reference.fingertip_centres(labels, probabilities, depth,
                            forest.document["image"]["intrinsics"], threshold, minimum)
                for offset, cls in enumerate(class_ids):
                    row = observations[frame*len(class_ids)+offset];centre = centres[cls]
                    if int(row["frame"]) != frame or int(row["class_id"]) != cls or int(row["has_observation"]) != int(centre is not None):
                        raise AssertionError("Observation class/presence differs")
                    observation_time = float(row["time_s"])
                    if not math.isfinite(observation_time) or abs(observation_time-timestamps[frame]) > 1e-12:
                        raise AssertionError("Observation timestamp differs from the supplied frame time")
                    if centre is None:
                        metrics["observations_missing"] += 1;point = None
                    else:
                        metrics["observations_present"] += 1;point = centre["xyz"]
                        if int(row["support"]) != centre["pixels"]:
                            raise AssertionError("Observation component support differs")
                        error = max(abs(float(row[key])-value) for key, value in zip(("x", "y", "z"), point))
                        metrics["observation_max_absolute_difference_mm"] = max(metrics["observation_max_absolute_difference_mm"], error)
                        if not math.isfinite(error) or error > args.tolerance:
                            raise AssertionError(f"Observation difference {error} exceeds tolerance")
                    expected_states.append(trackers[cls].update(timestamps[frame], point))
                    expected_row_ids.append((frame, cls, timestamps[frame]))
            for key in ("label_mismatches", "heap_mismatches", "mask_mismatches"):
                if metrics[key]:
                    raise AssertionError(f"{mode}: {key}={metrics[key]}")
            if metrics["probability_max_absolute_difference"] > args.probability_atol:
                raise AssertionError(f"{mode}: probability difference exceeds declared float32 tolerance")
            metrics["probability_exact_bytes"] = metrics["probability_byte_mismatches"] == 0
            # Full C API state/history checks use the same recorded native
            # observations; expected Python states above independently use the
            # Python detector's calibrated observations.
            with (work/"tracker.log").open("x") as log:
                subprocess.run([str(args.tracker), str(work/"observations.csv"), str(work/"tracker-full.csv")],
                               stdout=log, stderr=subprocess.STDOUT, check=True, timeout=args.timeout_seconds)
            full_rows = list(csv.DictReader((work/"tracker-full.csv").open()))
            if len(full_rows) != len(expected_row_ids):
                raise AssertionError("Full C API tracker row count differs")
            for row, (frame, cls, timestamp) in zip(full_rows, expected_row_ids):
                actual_time = float(row["time_s"])
                if int(row["frame"]) != frame or int(row["class_id"]) != cls or not math.isfinite(actual_time) or abs(actual_time-timestamp) > 1e-12:
                    raise AssertionError("Full C API tracker frame/class/time correspondence differs")
            metrics["tracking"] = compare_tracking(full_rows, expected_states, args.tolerance)
            integrated_rows = list(csv.DictReader((work/"tracks.csv").open()))
            if len(integrated_rows) != len(full_rows):
                raise AssertionError("Integrated tracker count differs")
            for actual, full in zip(integrated_rows, full_rows):
                if any(actual[k] != full[k] for k in ("frame", "class_id", "visible", "accepted", "status", "generation", "reason")):
                    raise AssertionError("Integrated Tea tracker lifecycle differs from C API replay")
                actual_time = float(actual["time_s"])
                if not math.isfinite(actual_time) or abs(actual_time-float(full["time_s"])) > 1e-12:
                    raise AssertionError("Integrated Tea tracker timestamp differs")
                if any(not math.isfinite(float(actual[k])) or not math.isfinite(float(full[k])) or abs(float(actual[k])-float(full[k])) > args.tolerance for k in ("out_x", "out_y", "out_z")):
                    raise AssertionError("Integrated Tea tracker position differs from C API replay")
            metrics["integrated_tea_tracking_matches_full_c_api_replay"] = True
            metrics["phase"] = "passed";save_json(report, result)
        result.update(phase="passed", completed_unix=time.time(), actual_trained_model_check=args.model_role == "trained-model")
        if args.model_role == "synthetic-harness":
            result["limitation"] = "Untrained synthetic fixture model on supplied frames: harness validation only; no actual trained-model result."
        save_json(report, result)
        return result
    except BaseException as error:
        result.update(phase="failed", error=repr(error), completed_unix=time.time());save_json(report, result)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--frames", required=True, type=Path, help="Extracted train/validation directory containing Data_*.mat")
    parser.add_argument("--split", required=True, choices=("train", "validation"))
    parser.add_argument("--tea-reference", required=True, type=Path, help="Matching frozen Tea scripts directory")
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--tracker", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--max-frames", type=int, default=3)
    parser.add_argument("--model-role", required=True, choices=("trained-model", "synthetic-harness", "unspecified"))
    parser.add_argument("--data-role", required=True, choices=("actual-handnet", "synthetic"))
    time_group = parser.add_mutually_exclusive_group(required=True)
    time_group.add_argument("--timestamps", type=Path, help="One caller-validated timestamp in seconds per frame")
    time_group.add_argument("--synthetic-step-s", type=float, help="Explicit synthetic cadence; no physical timing claims")
    parser.add_argument("--tolerance", type=float, default=1e-8)
    parser.add_argument("--probability-atol", type=float, default=2*float(np.finfo(np.float32).eps),
                        help="Declared absolute posterior tolerance; labels/heaps/ROI still must match exactly")
    parser.add_argument("--timeout-seconds", type=float, default=600)
    args = parser.parse_args()
    for name in ("manifest", "frames", "tea_reference", "native", "tracker", "output", "timestamps"):
        if getattr(args, name) is not None:
            setattr(args, name, getattr(args, name).resolve())
    if not math.isfinite(args.probability_atol) or args.probability_atol < 0 or not math.isfinite(args.tolerance) or args.tolerance <= 0 or not math.isfinite(args.timeout_seconds) or args.timeout_seconds <= 0:
        parser.error("tolerance and timeout must be finite and positive")
    result = execute(args)
    print(json.dumps({"phase": result["phase"], "frames": len(result["frames"]), "modes": list(MODES),
                      "actual_trained_model_check": result["actual_trained_model_check"],
                      "report": str(args.output/"report.json")}))


if __name__ == "__main__":
    main()
