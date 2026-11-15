#include "depthtrack/tea_forest.hpp"
#include "tea_json.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace depthtrack::tea {
namespace {
using json::Value;
std::size_t integer(const Value& v,std::size_t limit,const char* name) {
    double x=v.numeric();if(x<0||x>double(limit)||x!=std::floor(x))throw std::runtime_error(std::string("Invalid ")+name);return std::size_t(x);
}
void expect(const Value& v,const std::string& expected) {if(v.text()!=expected)throw std::runtime_error("Unsupported manifest contract: expected "+expected);}
std::uint32_t u32(std::istream& f) {
    unsigned char b[4];if(!f.read(reinterpret_cast<char*>(b),4))throw std::runtime_error("Truncated Tea model/coordinates");
    return std::uint32_t(b[0])|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);
}
float f32(std::istream& f){std::uint32_t u=u32(f);float x;static_assert(sizeof x==4,"Requires IEEE float32");std::memcpy(&x,&u,4);if(!std::isfinite(x))throw std::runtime_error("Nonfinite Tea value");return x;}
}
Configuration read_manifest(const std::filesystem::path& path) {
    if(std::filesystem::file_size(path)>1024*1024)throw std::runtime_error("Manifest is too large");
    std::ifstream f(path);if(!f)throw std::runtime_error("Cannot read manifest");std::string text((std::istreambuf_iterator<char>(f)),{});
    Value root=json::Reader(text).read();expect(root.at("schema"),"depthtrack-tea-forest-v1");
    Configuration c;const auto& image=root.at("image");
    c.width=integer(image.at("width"),16384,"width");c.height=integer(image.at("height"),16384,"height");
    expect(image.at("depth_unit"),"millimetres");
    if(auto k=image.find("intrinsics")){c.camera={k->at("fx").numeric(),k->at("fy").numeric(),k->at("cx").numeric(),k->at("cy").numeric()};}
    const auto& feature=root.at("feature");expect(feature.at("type"),"F_CHAR");
    expect(feature.at("coordinate_rule"),"truncate-after-base-plus-offset-over-depth");
    expect(feature.at("coordinate_precision"),"float32-complete-sum");
    c.max_diff=float(feature.at("max_diff").numeric());c.feature_max=float(feature.at("maximum").numeric());
    const auto& missing=feature.at("missing");
    if(missing.at("both").numeric()!=0||missing.at("first").numeric()!=c.feature_max||missing.at("second").numeric()!=-c.feature_max)throw std::runtime_error("Unsupported missing-depth feature values");
    const auto& names=root.at("classes");if(names.kind!=Value::Array)throw std::runtime_error("classes must be an array");
    for(const auto& name:names.array)c.classes.push_back(name.text());
    if(auto background=root.find("modeled_background_class"))c.modeled_background_class=int(integer(*background,253,"modeled_background_class"));
    if(auto post=root.find("postprocessing")){
        c.postprocessing.declared=true;
        if(auto method=post->find("method"))c.postprocessing.method=method->text();
        if(c.postprocessing.method!="largest4connected-probability-weighted-centroid")throw std::runtime_error("Unsupported manifest observation method");
        c.postprocessing.foreground=post->at("foreground").text();
        if(c.postprocessing.foreground!="none"&&c.postprocessing.foreground!="near-component")throw std::runtime_error("Unsupported manifest foreground default");
        c.postprocessing.probability_threshold=post->at("probability_threshold").numeric();
        if(c.postprocessing.probability_threshold<=0||c.postprocessing.probability_threshold>1)throw std::runtime_error("Invalid manifest posterior threshold");
        c.postprocessing.minimum_pixels=integer(post->at("min_pixels"),1000000000,"min_pixels");
        if(!c.postprocessing.minimum_pixels)throw std::runtime_error("Invalid manifest minimum support");
        const auto& ids=post->at("fingertip_classes");
        if(ids.kind!=Value::Array||ids.array.empty())throw std::runtime_error("Manifest requires explicit fingertip classes");
        std::set<std::size_t> seen;
        for(const auto& id:ids.array){auto cls=integer(id,253,"fingertip class");if(cls>=c.classes.size()||int(cls)==c.modeled_background_class||!seen.insert(cls).second)throw std::runtime_error("Invalid manifest fingertip class");c.postprocessing.fingertip_classes.push_back(cls);}
    }
    c.background=std::uint8_t(integer(root.at("background_label"),255,"background_label"));c.unknown=std::uint8_t(integer(root.at("unknown_label"),255,"unknown_label"));
    expect(root.at("aggregation"),"mean-normalized-leaf-probabilities");expect(root.at("empty_leaf"),"root-prior");
    const auto& trees=root.at("trees");if(trees.kind!=Value::Array)throw std::runtime_error("trees must be an array");
    for(const auto& tree:trees.array){double w=tree.find("weight")?tree.at("weight").numeric():1;c.trees.push_back({path.parent_path()/tree.at("model").text(),path.parent_path()/tree.at("coords").text(),w});}
    return c;
}
ForegroundResult near_component_foreground(const std::uint16_t* depth,std::size_t width,std::size_t height) {
    if(!depth||!width||!height||width>16384||height>16384)throw std::invalid_argument("Invalid foreground image");
    std::size_t pixels=width*height;ForegroundResult out;out.mask.resize(pixels,0);std::vector<std::uint16_t> valid;
    for(std::size_t i=0;i<pixels;++i)if(depth[i]>=100&&depth[i]<=1500)valid.push_back(depth[i]);
    out.valid_pixels=valid.size();if(valid.empty())return out;
    std::size_t order=(valid.size()-1)/50;std::nth_element(valid.begin(),valid.begin()+std::ptrdiff_t(order),valid.end());out.near_depth_mm=valid[order];
    std::vector<std::uint8_t> candidates(pixels,0);
    for(std::size_t i=0;i<pixels;++i)if(depth[i]>=100&&depth[i]<=1500&&depth[i]<=unsigned(out.near_depth_mm)+150){candidates[i]=1;++out.candidate_pixels;}
    std::vector<std::size_t> queue,best;
    for(std::size_t seed=0;seed<pixels;++seed){if(!candidates[seed])continue;queue.clear();queue.push_back(seed);candidates[seed]=0;
        for(std::size_t head=0;head<queue.size();++head){std::size_t p=queue[head],r=p/width,c=p%width;
            auto visit=[&](std::size_t index){if(candidates[index]){candidates[index]=0;queue.push_back(index);}};
            if(r)visit(p-width);
            if(c)visit(p-1);
            if(c+1<width)visit(p+1);
            if(r+1<height)visit(p+width);
        }
        if(queue.size()>best.size())best=queue;
    }
    if(best.size()>=30){out.component_pixels=best.size();for(auto index:best)out.mask[index]=1;}
    return out;
}
Forest::Forest(Configuration c):configuration_(std::move(c)) {
    const auto& cfg=configuration_;const std::size_t nc=classes();
    if(!cfg.width||!cfg.height||cfg.width>16384||cfg.height>16384||!nc||nc>254||cfg.background<nc||cfg.unknown<nc||cfg.background==cfg.unknown||cfg.trees.empty())throw std::invalid_argument("Invalid Tea forest dimensions/classes/labels");
    if(cfg.modeled_background_class < -1 || cfg.modeled_background_class>=int(nc))throw std::invalid_argument("Modeled background class is outside class map");
    if(!std::isfinite(cfg.max_diff)||cfg.max_diff<=0||!std::isfinite(cfg.feature_max)||cfg.feature_max<1||cfg.feature_max>127||cfg.feature_max!=std::floor(cfg.feature_max))throw std::invalid_argument("Invalid F_CHAR feature profile");
    for(const auto& source:cfg.trees) {
        if(!std::isfinite(source.weight)||source.weight<=0)throw std::runtime_error("Tree weight must be positive and finite");
        Tree tree;tree.weight=source.weight;std::ifstream f(source.model,std::ios::binary);if(!f)throw std::runtime_error("Cannot read Tea model: "+source.model.string());
        auto depth=u32(f),model_classes=u32(f),nodes=u32(f),hists=u32(f);
        if(depth>24||model_classes!=nc||nodes!=((std::uint32_t(1)<<(depth+1))-1)||hists!=2*std::uint64_t(nodes)+1)throw std::runtime_error("Tea model header/dimensions mismatch");
        std::uint64_t bytes=16+std::uint64_t(nodes)*8+std::uint64_t(hists)*(8+nc);
        if(std::filesystem::file_size(source.model)!=bytes)throw std::runtime_error("Tea model size/layout mismatch (native Tea RFNode must be 8 bytes)");
        auto coords_size=std::filesystem::file_size(source.coords);
        if(!coords_size||coords_size%16||coords_size/16>10000000)throw std::runtime_error("Invalid Coords.co size");
        std::ifstream coords(source.coords,std::ios::binary);if(!coords)throw std::runtime_error("Cannot read Coords.co");
        tree.coords.resize(std::size_t(coords_size/16));for(auto& xy:tree.coords)for(auto& x:xy)x=f32(coords);
        tree.nodes.reserve(nodes);for(std::uint32_t n=0;n<nodes;++n){auto feature=u32(f);float threshold=f32(f);if(feature>=tree.coords.size())throw std::runtime_error("Tea node feature index outside Coords.co");tree.nodes.push_back({feature,threshold});}
        // CompactLeaf is uint32 maxVal,uint16 label,2 padding. Normalizing byte
        // histograms cancels maxVal and avoids its old class-weighting defects.
        f.seekg(std::streamoff(std::uint64_t(hists)*8),std::ios::cur);
        tree.histograms.resize(std::size_t(hists)*nc);
        if(!f.read(reinterpret_cast<char*>(tree.histograms.data()),std::streamsize(tree.histograms.size())))throw std::runtime_error("Truncated Tea histogram table");
        unsigned prior_sum=0;for(std::size_t k=0;k<nc;++k)prior_sum+=tree.histograms[k];
        if(!prior_sum)throw std::runtime_error("Tea root histogram is empty: cannot apply root-prior fallback");
        for(std::size_t k=0;k<nc;++k)tree.prior.push_back(float(tree.histograms[k])/float(prior_sum));
        tree.levels=depth+1;total_weight_+=tree.weight;trees_.push_back(std::move(tree));
    }
    if(!std::isfinite(total_weight_))throw std::runtime_error("Tree weights overflow");
}
void Forest::predict(const std::uint16_t* depth,float* probabilities,std::uint8_t* labels,std::uint32_t* trace,const std::uint8_t* centre_mask) const {
    if(!depth||!probabilities||!labels)throw std::invalid_argument("Null Tea inference buffer");
    const auto& cfg=configuration_;const auto nc=classes();std::vector<double> votes(nc);
    for(std::size_t pixel=0;pixel<pixels();++pixel) {
        float* output=probabilities+pixel*nc;std::fill(output,output+nc,0);int z=depth[pixel];
        if(!z||(centre_mask&&!centre_mask[pixel])){labels[pixel]=cfg.background;if(trace)std::fill(trace+pixel*trees_.size(),trace+(pixel+1)*trees_.size(),std::numeric_limits<std::uint32_t>::max());continue;}
        std::fill(votes.begin(),votes.end(),0);std::size_t row=pixel/cfg.width,col=pixel%cfg.width;
        auto sample=[&](float dr,float dc)->float {
            // Separate operations and strict FP flags deliberately preserve the
            // float32 MEX/reference rounding before truncation toward zero.
            float qr=dr/float(z),qc=dc/float(z);float r=float(row)+qr,c=float(col)+qc;
            if(!std::isfinite(r)||!std::isfinite(c)||r<=-1||c<=-1||r>=float(cfg.height)||c>=float(cfg.width))return 0;
            return float(depth[std::size_t(int(r))*cfg.width+std::size_t(int(c))]);
        };
        for(std::size_t ti=0;ti<trees_.size();++ti){const auto& tree=trees_[ti];std::uint32_t heap=0;
            for(unsigned level=0;level<tree.levels;++level){const auto& node=tree.nodes[heap];const auto& xy=tree.coords[node.feature];float a=sample(xy[0],xy[1]),b=sample(xy[2],xy[3]),feature=0;
                if(!a&&!b)feature=0;else if(!a)feature=cfg.feature_max;else if(!b)feature=-cfg.feature_max;
                else{float difference=a-b;float normalized=difference/cfg.max_diff;float scaled=normalized*cfg.feature_max;feature=std::ceil(std::clamp(scaled,-cfg.feature_max,cfg.feature_max));}
                heap=heap*2+1+std::uint32_t(feature>node.threshold);
            }
            if(trace)trace[pixel*trees_.size()+ti]=heap;
            const auto* histogram=tree.histograms.data()+std::size_t(heap)*nc;unsigned sum=0;for(std::size_t k=0;k<nc;++k)sum+=histogram[k];
            for(std::size_t k=0;k<nc;++k){float p=sum?float(histogram[k])/float(sum):tree.prior[k];votes[k]+=tree.weight*double(p);}
        }
        double best=0;labels[pixel]=cfg.unknown;
        for(std::size_t k=0;k<nc;++k){output[k]=float(votes[k]/total_weight_);if(output[k]>best){best=output[k];labels[pixel]=std::uint8_t(k);}}
    }
}
std::vector<Observation> Forest::observations(const std::uint16_t* depth,const float* probabilities,const ObservationOptions& options) const {
    if(!depth||!probabilities)throw std::invalid_argument("Null observation input");
    const auto& camera=configuration_.camera;
    if(!std::isfinite(camera.fx)||!std::isfinite(camera.fy)||!std::isfinite(camera.cx)||!std::isfinite(camera.cy)||camera.fx<=0||camera.fy<=0)throw std::invalid_argument("Observations require calibrated intrinsics");
    if(!std::isfinite(options.probability_threshold)||options.probability_threshold<=0||options.probability_threshold>1||!options.minimum_support)throw std::invalid_argument("Invalid observation options");
    for(std::size_t i=0;i<pixels()*classes();++i)if(!std::isfinite(probabilities[i])||probabilities[i]<0||probabilities[i]>1)throw std::invalid_argument("Invalid observation probability");
    std::set<std::size_t> seen;std::vector<Observation> result;
    const auto width=configuration_.width,height=configuration_.height;
    for(auto cls:options.class_ids){
        if(cls>=classes()||int(cls)==configuration_.modeled_background_class||!seen.insert(cls).second)throw std::invalid_argument("Invalid/duplicate/background observation class");
        Observation out;out.class_id=cls;std::vector<std::uint8_t> candidates(pixels(),0);
        for(std::size_t i=0;i<pixels();++i){if(!depth[i]||probabilities[i*classes()+cls]<options.probability_threshold)continue;
            const auto* p=probabilities+i*classes();auto winner=std::size_t(std::max_element(p,p+classes())-p);if(winner==cls)candidates[i]=1;
        }
        std::vector<std::size_t> queue,best;
        for(std::size_t seed=0;seed<pixels();++seed){if(!candidates[seed])continue;queue.clear();queue.push_back(seed);candidates[seed]=0;
            for(std::size_t head=0;head<queue.size();++head){auto pixel=queue[head],row=pixel/width,col=pixel%width;
                auto visit=[&](std::size_t index){if(candidates[index]){candidates[index]=0;queue.push_back(index);}};
                if(row)visit(pixel-width);
                if(col)visit(pixel-1);
                if(col+1<width)visit(pixel+1);
                if(row+1<height)visit(pixel+width);
            }
            if(queue.size()>best.size())best=queue;
        }
        if(best.size()<options.minimum_support){result.push_back(out);continue;}
        std::sort(best.begin(),best.end());double mass=0;
        for(auto i:best)mass+=probabilities[i*classes()+cls];
        double x=0,y=0,z=0;
        for(auto i:best){double weight=double(probabilities[i*classes()+cls])/mass;x+=weight*double(i%width);y+=weight*double(i/width);z+=weight*double(depth[i]);}
        // Match the selected Python detector: weighted pixel X/Y and mean Z,
        // then pinhole projection. Averaging individual 3D points is different.
        out.xyz={(x-camera.cx)*z/camera.fx,(y-camera.cy)*z/camera.fy,z};
        if(!std::isfinite(out.xyz[0])||!std::isfinite(out.xyz[1])||!std::isfinite(out.xyz[2]))throw std::invalid_argument("Camera backprojection overflows");
        out.present=true;out.support=best.size();out.confidence=mass/double(best.size());result.push_back(out);
    }
    return result;
}
} // namespace depthtrack::tea
