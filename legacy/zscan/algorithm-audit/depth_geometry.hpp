#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <opencv2/core.hpp>

// Geometry adapters only. No forest inference, sensor observations or tracking.
namespace depth2014 {
struct SpatialMode {
    cv::Point3f mode;
    float weight;  // Historical MeanShifter support count, not calibrated confidence.
};

// Native depth-grid rays K^-1*[col,row,1], packed row-major.
std::vector<cv::Point3f> raysFromK(int width, int height, const cv::Matx33f& K);

// Calls the unchanged recovered uniform-kernel 3D MeanShifter.
std::vector<SpatialMode> meanShiftModes(const std::vector<cv::Point3f>& points,
                                       float bandwidthMm);

struct HandMaskResult {
    std::vector<uint8_t> mask;       // 0/255, row-major, full native image.
    std::vector<float> geodesicMm;   // Original Flooder T map.
    int seedRow = -1;
    int seedCol = -1;
    bool validSeed = false;
};

// Main.cpp331-348, MULTI_Hand=1. Input is the median-filtered uint16 mm map.
// Projected centre/surface Z must come from image-derived foreground detections.
// The wrapper guards invalid/outside seeds; it does not invent a hand position.
HandMaskResult handMask(const uint16_t* depth, int rows, int cols,
                        const std::vector<cv::Point3f>& rays,
                        float projectedRow, float projectedCol,
                        float handSurfaceZMm, float deltaMm = 30.f,
                        float maxGeodesicMm = 250.f);

// Hard learned labels0..4, nonzero depth only, original20mm bandwidth.
// The caller decides support acceptance/tracker updates and retains missing modes.
std::array<std::vector<SpatialMode>, 5> fingerModes(
    const uint16_t* maskedDepth, const uint8_t* labels, int rows, int cols,
    const std::vector<cv::Point3f>& rays, float bandwidthMm = 20.f);
}
