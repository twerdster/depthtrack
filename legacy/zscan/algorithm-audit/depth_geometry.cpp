#include "depth_geometry.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include <opencv2/features2d.hpp>

// Standard/OpenCV headers are loaded globally before unchanged historical
// headers enter this private namespace. Their definitions occur in one TU only.
namespace recovered_geometry2014 {
using cv::Point3f;
#include "original/MeanShifter.h"
#undef MAX
#undef MIN
#include "original/Flooder.h"
}
#undef SQR
#undef IND
#undef MAX
#undef MIN
#undef DBG
#undef MAXMIN
#undef DIST2
#undef MINUS

namespace depth2014 {
namespace {
size_t checkedPixels(int rows, int cols) {
    if (rows <= 0 || cols <= 0 || rows > 4096 || cols > 4096)
        throw std::invalid_argument("Invalid image dimensions");
    return size_t(rows) * size_t(cols);
}
bool finitePoint(const cv::Point3f& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
}

std::vector<cv::Point3f> raysFromK(int width, int height, const cv::Matx33f& K) {
    const size_t count = checkedPixels(height, width);
    for (float value : K.val)
        if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite camera matrix");
    if (K(0,0) <= 0 || K(1,1) <= 0 || std::abs(cv::determinant(K)) < 1.e-12)
        throw std::invalid_argument("Invalid camera matrix");
    const cv::Matx33f inverse = K.inv();
    std::vector<cv::Point3f> rays(count);
    for (int row = 0; row < height; ++row)
        for (int col = 0; col < width; ++col) {
            const cv::Vec3f ray = inverse * cv::Vec3f(float(col), float(row), 1.f);
            rays[size_t(row) * width + col] = cv::Point3f(ray[0], ray[1], ray[2]);
        }
    return rays;
}

std::vector<SpatialMode> meanShiftModes(const std::vector<cv::Point3f>& points,
                                       float bandwidthMm) {
    if (!std::isfinite(bandwidthMm) || bandwidthMm <= 0)
        throw std::invalid_argument("Mean shift requires positive finite bandwidth");
    for (const auto& point : points)
        if (!finitePoint(point)) throw std::invalid_argument("Nonfinite mean-shift point");
    if (points.empty()) return {};
    recovered_geometry2014::PointCloud cloud;
    cloud.pts = points;
    recovered_geometry2014::NanoKDTree tree(
        3, cloud, recovered_geometry2014::nanoflann::KDTreeSingleIndexAdaptorParams(10));
    tree.buildIndex();
    std::vector<recovered_geometry2014::Mode> original;
    recovered_geometry2014::meanShift(cloud, tree, bandwidthMm, original);
    std::vector<SpatialMode> result;
    result.reserve(original.size());
    for (const auto& mode : original) {
        if (!finitePoint(mode.mode) || !std::isfinite(mode.weight))
            throw std::runtime_error("Original MeanShifter returned nonfinite output");
        result.push_back({mode.mode, mode.weight});
    }
    return result;
}

HandMaskResult handMask(const uint16_t* depth, int rows, int cols,
                        const std::vector<cv::Point3f>& rays,
                        float projectedRow, float projectedCol,
                        float handSurfaceZMm, float deltaMm, float maxGeodesicMm) {
    const size_t count = checkedPixels(rows, cols);
    if (!depth || rays.size() != count)
        throw std::invalid_argument("Depth/ray shape mismatch");
    if (!std::isfinite(deltaMm) || deltaMm <= 0 ||
        !std::isfinite(maxGeodesicMm) || maxGeodesicMm <= 0)
        throw std::invalid_argument("Invalid flood parameters");
    for (const auto& ray : rays)
        if (!finitePoint(ray)) throw std::invalid_argument("Nonfinite camera ray");
    HandMaskResult result;
    result.mask.assign(count, uint8_t(0));
    result.geodesicMm.assign(count, 1.e30f);
    // Main.cpp indexes projected positions directly. These caller guards avoid
    // its negative leftward scan or invalid seed without fabricating a seed.
    if (!std::isfinite(projectedRow) || !std::isfinite(projectedCol) ||
        !std::isfinite(handSurfaceZMm) || handSurfaceZMm <= 0 ||
        projectedRow < 0 || projectedRow >= rows || projectedCol < 0 || projectedCol >= cols)
        return result;
    const int row = int(projectedRow);
    int walk = std::max(0, int(projectedCol - 10.f));
    for (; walk < cols; ++walk)
        if (depth[size_t(row) * cols + walk] != 0 &&
            std::abs(float(depth[size_t(row) * cols + walk]) - handSurfaceZMm) < 50.f)
            break;
    if (walk >= cols) return result;
    result.seedRow = row;
    result.seedCol = walk;
    result.validSeed = true;
    std::vector<float> metricDepth(count);
    for (size_t i = 0; i < count; ++i) metricDepth[i] = float(depth[i]);
    static_assert(sizeof(cv::Point3f) == 3 * sizeof(float), "Point3f must be packed XYZ");
    // Original Flooder only reads its ray/map inputs. It owns its scratch T/S.
    recovered_geometry2014::Flooder flood(metricDepth.data(), cols, rows,
        reinterpret_cast<float*>(const_cast<cv::Point3f*>(rays.data())));
    flood.flood3d(row, walk, deltaMm);
    result.geodesicMm.assign(flood.T, flood.T + count);
    for (size_t i = 0; i < count; ++i)
        result.mask[i] = flood.T[i] < maxGeodesicMm ? 255 : 0;
    // Main.cpp343-348 uses a square native-image window and strict depth bounds.
    const int halfWidth = int(25000.f / handSurfaceZMm);
    const int left = std::max(0, int(projectedCol - halfWidth));
    const int right = std::min(cols, int(projectedCol + halfWidth));
    const int top = std::max(0, int(projectedRow - halfWidth));
    const int bottom = std::min(rows, int(projectedRow + halfWidth));
    for (int r = top; r < bottom; ++r)
        for (int c = left; c < right; ++c) {
            const auto i = size_t(r) * cols + c;
            if (depth[i] > handSurfaceZMm - 120.f && depth[i] < handSurfaceZMm + 120.f)
                result.mask[i] = 255;
        }
    return result;
}

std::array<std::vector<SpatialMode>, 5> fingerModes(
    const uint16_t* maskedDepth, const uint8_t* labels, int rows, int cols,
    const std::vector<cv::Point3f>& rays, float bandwidthMm) {
    const size_t count = checkedPixels(rows, cols);
    if (!maskedDepth || !labels || rays.size() != count)
        throw std::invalid_argument("Finger depth/labels/rays mismatch");
    std::array<std::vector<cv::Point3f>, 5> clouds;
    for (size_t i = 0; i < count; ++i)
        if (maskedDepth[i] != 0 && labels[i] < 5)
            clouds[labels[i]].push_back(rays[i] * float(maskedDepth[i]));
    std::array<std::vector<SpatialMode>, 5> result;
    for (size_t finger = 0; finger < result.size(); ++finger)
        result[finger] = meanShiftModes(clouds[finger], bandwidthMm);
    return result;
}
}
