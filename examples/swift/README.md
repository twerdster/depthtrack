# Swift integration fragment

`PointTracker.swift` wraps the C ABI; it does not capture depth or perform inference. One instance consumes one semantic point, in millimetres and monotonically increasing seconds. Send `nil` for a missing observation and serialize calls with an actor or serial queue.

In an Xcode target, set the Objective-C bridging header to `DepthTrack-Bridging-Header.h`, add this repository's `include/` to header search paths, link the matching platform's `libdepthtrack.a`, and link libc++. Device and simulator archives are different platform builds. Example:

```swift
let tracker = try PointTracker()
let first = try tracker.update(timeSeconds: 0.0,
                               observationMillimeters: SIMD3(10, 20, 350))
let missing = try tracker.update(timeSeconds: 0.13,
                                 observationMillimeters: nil)
```

See `docs/native-platforms.md` for executed build evidence and commands. A cross-compiled library is not an executed iPhone application.
