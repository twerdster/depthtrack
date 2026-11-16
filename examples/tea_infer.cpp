#include "depthtrack/tea_forest.hpp"
#include "depthtrack/depthtrack.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {
double number(const std::string& s){std::size_t n=0;double x=std::stod(s,&n);if(n!=s.size()||!std::isfinite(x))throw std::runtime_error("Invalid numeric option");return x;}
std::size_t count(const std::string& s){double x=number(s);if(x<1||x>1000000000||x!=std::floor(x))throw std::runtime_error("Invalid positive count");return std::size_t(x);}
void write32(std::ostream& out,const std::uint32_t* values,std::size_t size){std::vector<unsigned char> bytes(size*4);for(std::size_t i=0;i<size;++i)for(unsigned k=0;k<4;++k)bytes[i*4+k]=static_cast<unsigned char>(values[i]>>(8*k));out.write(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));if(!out)throw std::runtime_error("Output write failed");}
std::ofstream output(const std::filesystem::path& path){if(std::filesystem::exists(path))throw std::runtime_error("Refusing existing output: "+path.string());std::ofstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot create output: "+path.string());return f;}
}
int main(int argc,char** argv){try{
    if(argc==2&&std::string(argv[1])=="--help"){
        std::cout<<"Usage: depthtrack_tea manifest.json depth.u16 output_prefix [options]\n"
          "Writes PREFIX.labels.u8 and PREFIX.probabilities.f32 (pixel-major HWC).\n"
          "--frames N              Default: infer from exact input byte count\n"
          "--trace                 Also write PREFIX.heap.u32 [frame,pixel,tree]\n"
          "--no-probabilities      Omit probability file (still computed internally)\n"
          "--foreground none|near-component  Independent fixed depth-only centre ROI\n"
          "--mask FILE             Caller-supplied uint8 centre mask; nonzero means query\n"
          "--save-mask FILE        Save normalized centre ROI, one uint8 per pixel\n"
          "--timestamps FILE       One strictly increasing time in seconds per frame\n"
          "--class-ids 0,1,...      Explicit verified classes to convert into XYZ\n"
          "--observations FILE     Write observations CSV; requires timestamps/classes/K\n"
          "--tracks FILE           Also apply portable tracker; requires same inputs\n"
          "--min-probability P     Default .5; manifest may set a frozen threshold\n"
          "--min-support N         Default5; manifest min_pixels overrides the default\n"
          "Zero depth returns manifest background with zero probabilities.\n"
          "Optional manifest postprocessing defaults are honored unless overridden.\n"
          "Existing outputs are never overwritten. Raw depth must be little-endian.\n";return 0;
    }
    if(argc<4)throw std::runtime_error("Expected manifest, depth input and output prefix; use --help");
    std::filesystem::path manifest=argv[1],input_path=argv[2],prefix=argv[3],time_path,observation_path,track_path,mask_path,save_mask_path;
    std::string foreground;
    std::size_t requested_frames=0;bool trace_enabled=false,save_probabilities=true,foreground_override=false,probability_override=false,support_override=false,classes_override=false;depthtrack::tea::ObservationOptions options;
    for(int i=4;i<argc;++i){std::string flag=argv[i];if(flag=="--trace"){trace_enabled=true;continue;}if(flag=="--no-probabilities"){save_probabilities=false;continue;}if(++i==argc)throw std::runtime_error("Missing option value");std::string v=argv[i];
        if(flag=="--frames")requested_frames=count(v);else if(flag=="--timestamps")time_path=v;else if(flag=="--observations")observation_path=v;else if(flag=="--tracks")track_path=v;
        else if(flag=="--foreground"){foreground=v;foreground_override=true;}else if(flag=="--mask")mask_path=v;else if(flag=="--save-mask")save_mask_path=v;
        else if(flag=="--min-probability"){options.probability_threshold=number(v);probability_override=true;}else if(flag=="--min-support"){options.minimum_support=count(v);support_override=true;}
        else if(flag=="--class-ids"){classes_override=true;std::stringstream s(v);std::string item;while(std::getline(s,item,',')){double id=number(item);if(id<0||id>253||id!=std::floor(id))throw std::runtime_error("Invalid observation class");options.class_ids.push_back(std::size_t(id));}}
        else throw std::runtime_error("Unknown option: "+flag);
    }
    depthtrack::tea::Forest forest(manifest);std::size_t pixels=forest.pixels(),nc=forest.classes();auto bytes=std::filesystem::file_size(input_path);
    const auto& post=forest.configuration().postprocessing;
    if(post.declared){
        if(!foreground_override&&mask_path.empty())foreground=post.foreground;
        if(!probability_override)options.probability_threshold=post.probability_threshold;
        if(!support_override)options.minimum_support=post.minimum_pixels;
        if(!classes_override)options.class_ids=post.fingertip_classes;
    }
    if(!bytes||bytes%(pixels*2))throw std::runtime_error("Depth input must contain complete nonempty uint16 frames");std::size_t frames=std::size_t(bytes/(pixels*2));
    if(requested_frames&&requested_frames!=frames)throw std::runtime_error("--frames disagrees with exact input size");
    if(foreground=="none")foreground.clear();
    if(!foreground.empty()&&foreground!="near-component")throw std::runtime_error("Unknown foreground policy");
    if(foreground_override&&!mask_path.empty())throw std::runtime_error("--foreground and --mask are mutually exclusive");
    if(!mask_path.empty()&&std::filesystem::file_size(mask_path)!=frames*pixels)throw std::runtime_error("Mask input size does not match depth frames");
    bool want_observations=!observation_path.empty()||!track_path.empty();std::vector<double> times;
    if(want_observations&&(time_path.empty()||options.class_ids.empty()))throw std::runtime_error("Observations/tracking require --timestamps and explicit --class-ids");
    if(!time_path.empty()){std::ifstream f(time_path);if(!f)throw std::runtime_error("Cannot read timestamps");std::string line;while(std::getline(f,line)){if(line.empty())throw std::runtime_error("Empty timestamp");double t=number(line);if(!times.empty()&&t<=times.back())throw std::runtime_error("Timestamps must increase strictly");times.push_back(t);}if(times.size()!=frames)throw std::runtime_error("Timestamp count does not equal frame count");}
    if(want_observations&&observation_path.empty())observation_path=prefix.string()+".observations.csv";
    // Validate camera/options before creating any output, even for empty classes.
    if(want_observations){std::vector<std::uint16_t> d(pixels,0);std::vector<float> p(pixels*nc,0);forest.observations(d.data(),p.data(),options);}
    std::vector<std::filesystem::path> paths={prefix.string()+".labels.u8"};if(save_probabilities)paths.push_back(prefix.string()+".probabilities.f32");if(trace_enabled)paths.push_back(prefix.string()+".heap.u32");if(want_observations)paths.push_back(observation_path);if(!track_path.empty())paths.push_back(track_path);if(!save_mask_path.empty())paths.push_back(save_mask_path);
    std::map<std::filesystem::path,bool> unique;for(const auto& path:paths){auto normalized=std::filesystem::absolute(path).lexically_normal();if(!unique.emplace(normalized,true).second||std::filesystem::exists(path))throw std::runtime_error("Duplicate/existing output: "+path.string());}
    std::ifstream input(input_path,std::ios::binary),mask_input;if(!input)throw std::runtime_error("Cannot read depth input");if(!mask_path.empty()){mask_input.open(mask_path,std::ios::binary);if(!mask_input)throw std::runtime_error("Cannot read mask input");}auto labels_out=output(prefix.string()+".labels.u8");std::ofstream probability_out,trace_out,observations_out,tracks_out,mask_out;
    if(!save_mask_path.empty())mask_out=output(save_mask_path);
    if(save_probabilities)probability_out=output(prefix.string()+".probabilities.f32");if(trace_enabled)trace_out=output(prefix.string()+".heap.u32");
    if(want_observations){observations_out=output(observation_path);observations_out<<std::setprecision(17)<<"frame,time_s,class_id,has_observation,x,y,z,support,confidence\n";}
    if(!track_path.empty()){tracks_out=output(track_path);tracks_out<<std::setprecision(17)<<"frame,time_s,class_id,visible,out_x,out_y,out_z,accepted,status,generation,reason\n";}
    using Handle=std::unique_ptr<dt_tracker,decltype(&dt_tracker_destroy)>;std::map<std::size_t,Handle> trackers;
    if(tracks_out.is_open())for(auto cls:options.class_ids){dt_error error;Handle h(dt_tracker_create(nullptr,&error),dt_tracker_destroy);if(!h)throw std::runtime_error(dt_error_string(error));trackers.emplace(cls,std::move(h));}
    std::vector<unsigned char> raw(pixels*2);std::vector<std::uint16_t> depth(pixels);std::vector<std::uint8_t> labels(pixels),mask(pixels);std::vector<float> probabilities(pixels*nc);std::vector<std::uint32_t> bits(pixels*nc),trace(trace_enabled?pixels*forest.trees():0);
    for(std::size_t frame=0;frame<frames;++frame){if(!input.read(reinterpret_cast<char*>(raw.data()),std::streamsize(raw.size())))throw std::runtime_error("Short input read");for(std::size_t i=0;i<pixels;++i)depth[i]=std::uint16_t(raw[2*i])|(std::uint16_t(raw[2*i+1])<<8);
        if(!foreground.empty())mask=depthtrack::tea::near_component_foreground(depth.data(),forest.configuration().width,forest.configuration().height).mask;
        else if(mask_input.is_open()){if(!mask_input.read(reinterpret_cast<char*>(mask.data()),std::streamsize(mask.size())))throw std::runtime_error("Short mask input read");for(std::size_t i=0;i<pixels;++i)mask[i]=std::uint8_t(mask[i]&&depth[i]);}
        else for(std::size_t i=0;i<pixels;++i)mask[i]=std::uint8_t(depth[i]!=0);
        forest.predict(depth.data(),probabilities.data(),labels.data(),trace_enabled?trace.data():nullptr,mask.data());labels_out.write(reinterpret_cast<char*>(labels.data()),std::streamsize(labels.size()));
        if(mask_out.is_open())mask_out.write(reinterpret_cast<char*>(mask.data()),std::streamsize(mask.size()));
        if(save_probabilities){for(std::size_t i=0;i<bits.size();++i)std::memcpy(&bits[i],&probabilities[i],4);write32(probability_out,bits.data(),bits.size());}if(trace_enabled)write32(trace_out,trace.data(),trace.size());
        if(want_observations)for(const auto& observation:forest.observations(depth.data(),probabilities.data(),options)){
            observations_out<<frame<<','<<times[frame]<<','<<observation.class_id<<','<<observation.present<<','<<observation.xyz[0]<<','<<observation.xyz[1]<<','<<observation.xyz[2]<<','<<observation.support<<','<<observation.confidence<<'\n';
            if(tracks_out.is_open()){dt_point p{observation.xyz[0],observation.xyz[1],observation.xyz[2]};dt_state state{};auto error=dt_tracker_update(trackers.at(observation.class_id).get(),times[frame],observation.present?&p:nullptr,&state);if(error!=DT_OK)throw std::runtime_error(dt_error_string(error));tracks_out<<frame<<','<<times[frame]<<','<<observation.class_id<<','<<state.has_position<<','<<state.position.x<<','<<state.position.y<<','<<state.position.z<<','<<state.accepted<<','<<dt_status_string(state.status)<<','<<state.generation<<','<<dt_reason_string(state.reason)<<'\n';}
        }
    }
    if(!labels_out||(probability_out.is_open()&&!probability_out)||(trace_out.is_open()&&!trace_out)||(observations_out.is_open()&&!observations_out)||(tracks_out.is_open()&&!tracks_out)||(mask_out.is_open()&&!mask_out))throw std::runtime_error("Output write failed");
    std::cout<<"{\"schema\":\"depthtrack-tea-inference-v1\",\"frames\":"<<frames<<",\"width\":"<<forest.configuration().width<<",\"height\":"<<forest.configuration().height<<",\"classes\":"<<nc<<",\"trees\":"<<forest.trees()<<",\"backend\":\"cpu\"}\n";return 0;
}catch(const std::exception& e){std::cerr<<"depthtrack_tea: "<<e.what()<<'\n';return 1;}}
