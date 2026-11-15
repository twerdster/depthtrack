# Android integration fragments

This is a Kotlin/JNI example, not a complete Gradle application. The CMake/JNIarm64 shared library cross-compiled in the [clean-checkout CI run](https://github.com/twerdster/depthtrack/actions/runs/43712008750). No Kotlin application or Android device runtime was executed, and no NDK was installed locally. It consumes already inferred XYZ points and does not provide camera capture or model inference.

Copy/adapt the Kotlin class and point your app's `externalNativeBuild.cmake.path` to `app/src/main/cpp/CMakeLists.txt`. Keep the package/class and JNI symbol names synchronized. Android Gradle supplies the NDK toolchain and ABI; select ABIs with `ndk.abiFilters`. Serialize updates and call `close()`/`use` to release the handle. The wrapper uses millimetres, seconds and nullable observations.

The small returned array carries status/reason/accepted/visibility, XYZ, optional age and optional cutoff. These values come from the C API; held positions are not fresh detections. See the C header for enum values and `docs/native-platforms.md` for direct NDK commands and test limitations.
