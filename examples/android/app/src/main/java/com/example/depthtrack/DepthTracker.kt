package com.example.depthtrack

/** Integration fragment, not a complete Android app. Coordinates: mm; time: s. */
class DepthTracker : AutoCloseable {
    data class Sample(val status: Int, val reason: Int, val accepted: Boolean,
                      val positionMm: DoubleArray?, val ageSeconds: Double?,
                      val cutoffHz: Double?)
    private var handle: Long = nativeCreate()
    init { check(handle != 0L) { "Could not create native tracker" } }
    @Synchronized fun update(timeSeconds: Double, xyzMm: DoubleArray?): Sample {
        check(handle != 0L) { "Tracker closed" }
        require(xyzMm == null || xyzMm.size == 3)
        val s = nativeUpdate(handle, timeSeconds, xyzMm)
        return Sample(s[0].toInt(), s[1].toInt(), s[2] != 0.0,
            if (s[3] != 0.0) doubleArrayOf(s[4], s[5], s[6]) else null,
            if (s[7] != 0.0) s[8] else null,
            if (s[9] != 0.0) s[10] else null)
    }
    @Synchronized override fun close() {
        if (handle != 0L) { nativeDestroy(handle); handle = 0L }
    }
    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeUpdate(handle: Long, timeSeconds: Double,
                                      xyzMm: DoubleArray?): DoubleArray
    companion object { init { System.loadLibrary("depthtrack_jni") } }
}
