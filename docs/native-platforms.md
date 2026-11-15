# Native tracking core and platform integration

The library in `src/tracker.cpp` is a dependency-free C++17 implementation of the timestamp-aware vector OneEuro tracker. Its C ABI is declared in `include/depthtrack/depthtrack.h`. It consumes already detected 3D points; it does not capture camera data, run a tree/network, or program an FPGA. The archived ZScan tracker remains a separate baseline.

## Verified status

Packaging checks on November 15, 2026:

| Target | Evidence | Limit |
| --- | --- | --- |
| macOS arm64 | Release library/CSV executable built; native unit tests, C-only header client, 650 CSV states and 650 full JSON states/history match Python within 1e-8 | Desktop execution |
| iOS arm64 | Static library cross-compiled with Xcode 16.1, iPhoneOS SDK 18.1, minimum iOS 15.0; Swift wrapper type-checked | No device execution or signed application |
| iOS Simulator arm64 | Static library cross-compiled with Simulator SDK 18.1; Swift wrapper type-checked | No simulator application executed |
| Android | CMake, Kotlin and JNI integration fragments supplied | No local NDK build, Kotlin application build or device execution |

`tests/native/platform-validation.json` records local artifact/source hashes and commands. Build trees/binaries are generated outputs, not required source dependencies. Linux execution evidence is maintained by the repository integration workflow.

## Build and test on a desktop

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel
ctest --test-dir build/native --output-on-failure
build/native/depthtrack_csv samples/handmesh/tracking_fixture.csv build/native/replayed.csv
```

The core requires a C++17 compiler and CMake 3.20 or newer. The full JSON golden test uses a Python interpreter and its standard library, when available; the C++ unit/CSV tests have no Python dependency. CMake exports `DepthTrack::depthtrack` for `add_subdirectory` integration. `DEPTHTRACK_BUILD_SHARED=ON` selects a shared library; static is the default.

CSV input begins `frame,time_s,class_id,has_observation,x,y,z`. Extra fixture columns are ignored. Output gives explicit status/reason/accepted/visibility, optional age/cutoff and copied chronological history as quoted JSON. `has_observation=0` supplies a missing measurement; zeros in that row's XYZ fields are placeholders. The command-line example accepts finite numeric fields; the C ABI additionally handles nonfinite observations as invalid/missing.

## C ABI contract

- Call `dt_parameters_default`, optionally change parameters, then `dt_tracker_create`. Passing NULL parameters selects defaults. Check the returned handle/error and `dt_abi_version()` against `DT_ABI_VERSION`.
- One handle tracks one semantic point. Coordinates are millimetres; finite timestamps are seconds and must strictly increase per handle. A first valid observation is visible immediately.
- NULL observations are missing. Nonfinite XYZ or z<=0 consumes the timestamp as `DT_REASON_INVALID`; it is not a fresh detection. Held/lost states, accepted flags and optional output fields remain explicit.
- Timestamp/API errors and allocation/numerical failures leave state and output unchanged. Native guards reject elapsed-time overflow and return `DT_NUMERICAL_RANGE` if arithmetic becomes nonfinite. These are deliberate safeguards beyond Python's unrealistic extreme-value behavior; ordinary-data golden parity is verified. Use physically meaningful coordinate/time scales and cutoff parameters.
- Filtering uses elapsed time since the last accepted observation. One shared XYZ cutoff avoids direction-dependent smoothing. Hold includes its endpoint; reset occurs only after the configured gap is exceeded. Reacquisition resets velocity/history and increments generation. `reset` clears the instance to its initial state.
- `dt_tracker_history_copy` copies chronological samples into caller-owned storage. On insufficient capacity, it reports the required count without partially writing the destination. No pointer into internal storage escapes.
- Serialize calls on a handle. Separate instances are independent. For a multi-class frame API, validate every class first if all-or-nothing frame updates are required; this primitive updates one point per call.

## Apple platforms

CMake recommends its Xcode generator for Apple device builds; select the device/simulator SDK through the documented system/sysroot variables. [CMake toolchain documentation](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html#cross-compiling-for-ios-tvos-visionos-or-watchos)

```sh
cmake -S . -B build/ios-device -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
  -DDEPTHTRACK_BUILD_TESTS=OFF -DDEPTHTRACK_BUILD_EXAMPLES=OFF
cmake --build build/ios-device --config Release -- CODE_SIGNING_ALLOWED=NO

cmake -S . -B build/ios-simulator -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
  -DDEPTHTRACK_BUILD_TESTS=OFF -DDEPTHTRACK_BUILD_EXAMPLES=OFF
cmake --build build/ios-simulator --config Release -- CODE_SIGNING_ALLOWED=NO
```

The respective archives are `build/ios-device/Release-iphoneos/libdepthtrack.a` and `build/ios-simulator/Release-iphonesimulator/libdepthtrack.a`. They target different platforms despite sharing the arm64 architecture; do not substitute one for the other. The Swift fragment uses a C bridging header, so Swift C++ interoperability mode is unnecessary. See `examples/swift/` for code and integration settings.

## Android NDK and JNI

The Android NDK documents its CMake toolchain file, ABI and minimum API arguments; use a separate build directory for each ABI. [Android NDK CMake guide](https://developer.android.com/ndk/guides/cmake)

With an already installed NDK, replace the explicit placeholder path:

```sh
cmake -S examples/android/app/src/main/cpp -B build/android-arm64 \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/android-ndk/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
cmake --build build/android-arm64 --parallel
```

For Android Studio, point `externalNativeBuild.cmake.path` to that example CMake file and select ABIs through `ndk.abiFilters`; Gradle supplies the toolchain. The example links the portable static core into `depthtrack_jni`, and `DepthTracker.kt` releases its native handle through `close()`. It is an integration fragment, not a complete app. No NDK, camera permissions, SDK capture code or Android packages were installed by this packaging task. Successful CI cross-compilation would add build evidence, not device/runtime verification.
