#include "forest_adapter.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>
#include "compat/Forest_compat.hpp"
static_assert(sizeof(RFNode)==24, "Original node layout must be24 bytes");
static_assert(sizeof(CompactLeaf)==8, "Original compact layout must be8 bytes");
static_assert(sizeof(TreeHeader)==16, "Original header must be16 bytes");
static_assert(sizeof(unsigned int)==4, "Original histogram accumulator is uint32");
struct ExactForest::Impl {
    ZSHand::HND handle=nullptr;
    int rows=0,cols=0,num_classes=0,depth=0;
    ~Impl() { if(handle) ZSHand::Ensemble::RemoveEnsemble(handle); }
};
ExactForest::ExactForest(const std::string& modelPathOrDirectory,int rows,int cols):impl_(new Impl) {
    if(rows<=0||cols<=0||rows>10000||cols>10000) throw std::runtime_error("Invalid image dimensions");
    std::filesystem::path path(modelPathOrDirectory);
    if(path.extension()==".tree") {
        if(path.filename()!="Tree_0000.tree") throw std::runtime_error("Model must begin with Tree_0000.tree");
        path=path.parent_path();
    }
    std::string directory=path.string();
    if(directory.empty()) directory=".";
    if(directory.back()!='/') directory.push_back('/');
    // Validate exact little-endian native record layout before invoking original loader.
    TreeHeader th{};
    std::ifstream stream(directory+"Tree_0000.tree",std::ios::binary);
    if(!stream.read(reinterpret_cast<char*>(&th),sizeof th)) throw std::runtime_error("Cannot read model header");
    if(th.depth<0||th.depth>25||th.numClasses<1||th.numClasses>32) throw std::runtime_error("Invalid model header");
    const std::uint64_t n=(std::uint64_t(1)<<(th.depth+1))-1;
    if(th.totalNodes!=n||th.totalHists!=2*n+1) throw std::runtime_error("Model dimensions do not match original complete-tree loader");
    const std::uint64_t bytes=16+n*24+(2*n+1)*(8+th.numClasses);
    if(std::filesystem::file_size(directory+"Tree_0000.tree")!=bytes) throw std::runtime_error("Model size mismatch");
    const std::uint16_t endian=1;
    if(*reinterpret_cast<const std::uint8_t*>(&endian)!=1) throw std::runtime_error("Original model needs little-endian host");
    std::vector<char> name(directory.begin(),directory.end());name.push_back(0);
    auto error=ZSHand::Ensemble::LoadEnsemble(impl_->handle,name.data());
    if(error!=ZSHand::NO_ERROR) throw std::runtime_error("Original model loader failed: "+std::to_string(error));
    impl_->rows=rows;impl_->cols=cols;impl_->depth=th.depth;impl_->num_classes=th.numClasses;
    error=ZSHand::Ensemble::EnableOutputs(impl_->handle,ZSHand::LABEL,cols,rows);
    if(error!=ZSHand::NO_ERROR) throw std::runtime_error("Cannot enable original labels");
}
ExactForest::~ExactForest()=default;
std::vector<std::uint8_t> ExactForest::infer(const std::uint16_t* depth,int inclusiveMaxDepth) {
    if(!depth) throw std::runtime_error("Null depth input");
    if(inclusiveMaxDepth<0) throw std::runtime_error("Negative inclusive depth");
    // The original implementation only reads this input despite its mutable API.
    auto error=ZSHand::Ensemble::ApplyEnsemble(impl_->handle,const_cast<std::uint16_t*>(depth),inclusiveMaxDepth);
    if(error!=ZSHand::NO_ERROR) throw std::runtime_error("Original evaluator failed");
    ZSHand::LabelType labels=nullptr;
    error=ZSHand::Ensemble::GetLabels(impl_->handle,labels);
    if(error!=ZSHand::NO_ERROR||!labels) throw std::runtime_error("Original labels unavailable");
    return std::vector<std::uint8_t>(labels,labels+std::size_t(impl_->rows)*impl_->cols);
}
int ExactForest::classes() const { return impl_->num_classes; }
int ExactForest::source_depth() const { return impl_->depth; }
