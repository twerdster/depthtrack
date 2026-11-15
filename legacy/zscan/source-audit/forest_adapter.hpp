#ifndef ZSCAN_EXACT_FOREST_ADAPTER_HPP
#define ZSCAN_EXACT_FOREST_ADAPTER_HPP
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
// Literal original ZScanV02 Forest.h evaluator, isolated from pipeline macros.
// modelPathOrDirectory: full Tree_0000.tree path OR its containing directory.
// Input/output are row-major; max depth is inclusive (20 means21 decisions).
class ExactForest {
public:
    ExactForest(const std::string& modelPathOrDirectory, int rows, int cols);
    ~ExactForest();
    ExactForest(const ExactForest&) = delete;
    ExactForest& operator=(const ExactForest&) = delete;
    std::vector<std::uint8_t> infer(const std::uint16_t* depth, int inclusiveMaxDepth);
    int classes() const;
    int source_depth() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#endif
