#!/bin/bash
set -euo pipefail
task_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$task_dir/_build"
xcrun swiftc -O "$task_dir/gpu/tree_metal.swift" -o "$task_dir/_build/tree_metal" -framework Metal -framework Foundation -framework CryptoKit
