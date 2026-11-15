import Foundation

// Add DepthTrack-Bridging-Header.h to the target's Objective-C bridging header.
// The imported API is C; Swift C++ interoperability mode is not required.
public final class PointTracker {
    public struct Sample {
        public let status: String
        public let reason: String?
        public let accepted: Bool
        public let positionMillimeters: SIMD3<Double>?
        public let observationAgeSeconds: Double?
    }
    public enum Failure: Error { case native(Int32) }
    private let handle: OpaquePointer
    // Call from one actor/serial queue. Different instances are independent.
    public init() throws {
        var code = DT_OK
        guard let value = dt_tracker_create(nil, &code) else {
            throw Failure.native(Int32(code.rawValue))
        }
        handle = value
    }
    deinit { dt_tracker_destroy(handle) }
    public func reset() throws {
        let code = dt_tracker_reset(handle)
        if code != DT_OK { throw Failure.native(Int32(code.rawValue)) }
    }
    public func update(timeSeconds: Double,
                       observationMillimeters: SIMD3<Double>?) throws -> Sample {
        var state = dt_state()
        let code: dt_error
        if let xyz = observationMillimeters {
            var point = dt_point(x: xyz.x, y: xyz.y, z: xyz.z)
            code = dt_tracker_update(handle, timeSeconds, &point, &state)
        } else {
            code = dt_tracker_update(handle, timeSeconds, nil, &state)
        }
        if code != DT_OK { throw Failure.native(Int32(code.rawValue)) }
        let reason = String(cString: dt_reason_string(state.reason))
        return Sample(status: String(cString: dt_status_string(state.status)),
                      reason: reason.isEmpty ? nil : reason,
                      accepted: state.accepted != 0,
                      positionMillimeters: state.has_position != 0
                        ? SIMD3(state.position.x, state.position.y, state.position.z) : nil,
                      observationAgeSeconds: state.has_observation_age != 0
                        ? state.observation_age_s : nil)
    }
    public func history() throws -> [(timeSeconds: Double, millimeters: SIMD3<Double>)] {
        var samples = Array(repeating: dt_history_sample(),
                            count: dt_tracker_history_size(handle))
        var written = 0
        let code = samples.withUnsafeMutableBufferPointer { buffer in
            dt_tracker_history_copy(handle, buffer.baseAddress, buffer.count, &written)
        }
        if code != DT_OK { throw Failure.native(Int32(code.rawValue)) }
        return samples.prefix(written).map {
            ($0.time_s, SIMD3($0.position.x, $0.position.y, $0.position.z))
        }
    }
}
