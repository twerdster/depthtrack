#pragma once
// Native Tea inference; independent of the historical ten-level packed backend.
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace depthtrack::tea {
struct Camera { double fx=0,fy=0,cx=0,cy=0; };
struct TreeSource { std::filesystem::path model,coords; double weight=1; };
struct PostprocessingDefaults {
    bool declared=false;
    std::string foreground="none";
    std::string method="largest4connected-probability-weighted-centroid";
    double probability_threshold=0.5;
    std::size_t minimum_pixels=5;
    std::vector<std::size_t> fingertip_classes;
};
struct Configuration {
    std::size_t width=0,height=0;
    float max_diff=250,feature_max=127;
    std::uint8_t background=7,unknown=255;
    int modeled_background_class=-1;
    Camera camera;
    PostprocessingDefaults postprocessing;
    std::vector<std::string> classes;
    std::vector<TreeSource> trees;
};
Configuration read_manifest(const std::filesystem::path& path);
struct ForegroundResult {
    std::vector<std::uint8_t> mask;
    std::uint16_t near_depth_mm=0;
    std::size_t valid_pixels=0,candidate_pixels=0,component_pixels=0;
};
// Independent, fixed v1 heuristic: 100..1500 mm, second-percentile order
// statistic, near+150 mm, largest 4-connected component with support >=30.
// Ties select the component with the first row-major seed. No labels/camera,
// model output, future frames or temporal tracker enter this calculation.
ForegroundResult near_component_foreground(const std::uint16_t* depth,std::size_t width,std::size_t height);
struct ObservationOptions {
    // No anatomical class map is inferred. Callers provide verified class IDs.
    std::vector<std::size_t> class_ids;
    double probability_threshold=0.5;
    std::size_t minimum_support=5;
};
struct Observation {
    std::size_t class_id=0,support=0;
    bool present=false;
    std::array<double,3> xyz{};
    double confidence=0;
};
class Forest {
    struct Node { std::uint32_t feature;float threshold; };
    struct Tree {
        std::vector<Node> nodes;
        std::vector<std::array<float,4>> coords;
        std::vector<std::uint8_t> histograms;
        std::vector<float> prior;
        unsigned levels=0;
        double weight=1;
    };
    Configuration configuration_;
    std::vector<Tree> trees_;
    double total_weight_=0;
public:
    explicit Forest(Configuration configuration);
    explicit Forest(const std::filesystem::path& manifest):Forest(read_manifest(manifest)){}
    const Configuration& configuration() const noexcept { return configuration_; }
    std::size_t classes() const noexcept { return configuration_.classes.size(); }
    std::size_t pixels() const noexcept { return configuration_.width*configuration_.height; }
    std::size_t trees() const noexcept { return trees_.size(); }
    // Probabilities are pixel-major HWC, labels are row-major. Zero-depth pixels
    // have all-zero probabilities/background label. Buffers may not overlap.
    // Optional trace is pixel-major [pixel,tree] uint32 heap indices; UINT32_MAX
    // marks zero-depth/masked pixels. A centre_mask restricts queried centres
    // only: feature neighbours ALWAYS retain the original raw depth values.
    // No silent pruning/depth limit is applied.
    void predict(const std::uint16_t* depth,float* probabilities,std::uint8_t* labels,
                 std::uint32_t* trace=nullptr,const std::uint8_t* centre_mask=nullptr) const;
    std::vector<Observation> observations(const std::uint16_t* depth,const float* probabilities,
                                          const ObservationOptions& options) const;
};
} // namespace depthtrack::tea
