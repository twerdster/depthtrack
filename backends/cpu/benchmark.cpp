// Resident-buffer one-thread CPU benchmarks for ten split decisions.
// Literal variant independently retains original float32 feature arithmetic
// and uint32 weighted votes. Integer variant uses exported raw cuts/leaf labels.
// All input/model loading, comparison, hashes, and file writes are outside timing.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
struct Node { int32_t feature,row1,col1,row2,col2; float threshold; };
struct Compact {uint32_t maximum; uint8_t label,pad[3];};
struct Packed {int16_t row1,col1,row2,col2,cut;uint16_t flags;};
static_assert(sizeof(Node)==24 && sizeof(Compact)==8 && sizeof(Packed)==12,"Historical ABI sizes");
constexpr int WIDTH=128,HEIGHT=128,PIXELS=WIDTH*HEIGHT,LEVELS=10,FIRST=1023;
struct Result {std::vector<uint8_t> labels;std::vector<uint16_t> leaves;Result():labels(PIXELS),leaves(PIXELS){}};
struct Frame {std::string name;std::vector<uint16_t> depth,expected_leaves;std::vector<uint8_t> expected;Result output;};
struct Model {std::vector<Node> nodes;std::vector<Compact> compact;std::vector<uint8_t> hist,labels;std::vector<Packed> packed;};
template<class T> std::vector<T> read(const fs::path& p,size_t count) {
 std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot open "+p.string());
 std::vector<T> v(count);f.read(reinterpret_cast<char*>(v.data()),count*sizeof(T));
 if(size_t(f.gcount())!=count*sizeof(T)||f.peek()!=EOF)throw std::runtime_error("Wrong file size "+p.string());return v;
}
template<class T> void write(const fs::path& p,const std::vector<T>& v){
 std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(v.data()),v.size()*sizeof(T));
 if(!f)throw std::runtime_error("Output failed "+p.string());
}
inline uint16_t sample(const uint16_t* img,int y,int x){return unsigned(y)<HEIGHT&&unsigned(x)<WIDTH?img[y*WIDTH+x]:0;}
template<bool Trace> void literal(const Model& m,const uint16_t* img,Result& o){
 for(int y=0;y<HEIGHT;++y)for(int x=0;x<WIDTH;++x){
  int i=y*WIDTH+x;float z=float(img[i]);if(z==0){o.labels[i]=7;if constexpr(Trace)o.leaves[i]=65535;continue;}
  unsigned heap=0;
  for(int d=0;d<LEVELS;++d){
   const Node& n=m.nodes[heap];
   int y1=int(float(y)+float(n.row1)/z),x1=int(float(x)+float(n.col1)/z);
   int y2=int(float(y)+float(n.row2)/z),x2=int(float(x)+float(n.col2)/z);
   float a=float(sample(img,y1,x1)),b=float(sample(img,y2,x2)),feat;
   if(a==0&&b==0)feat=0;else if(a==0)feat=126;else if(b==0)feat=-126;
   else{float scale=(a-b)/200.f;float value=scale*126.f;feat=std::ceil(std::max(std::min(double(value),126.),-126.));}
   heap=heap*2+1+unsigned(feat>n.threshold);
  }
  unsigned leaf=heap-FIRST;uint32_t best=0;uint8_t label=255;
  for(int c=0;c<7;++c){
   float p=float(m.hist[leaf*7+c])/255.f;uint32_t weight=uint32_t(p*float(m.compact[leaf].maximum));
   if(weight>best){best=weight;label=uint8_t(c);}
  }
  o.labels[i]=label;if constexpr(Trace)o.leaves[i]=uint16_t(heap);
 }
}
template<bool Trace> void integer(const Model& m,const uint16_t* img,Result& o){
 for(int y=0;y<HEIGHT;++y)for(int x=0;x<WIDTH;++x){
  int i=y*WIDTH+x,z=img[i];if(z==0){o.labels[i]=7;if constexpr(Trace)o.leaves[i]=65535;continue;}
  int yz=y*z,xz=x*z;unsigned heap=0;
  for(int d=0;d<LEVELS;++d){
   const Packed& n=m.packed[heap];
   int y1=(yz+int(n.row1))/z,x1=(xz+int(n.col1))/z;
   int y2=(yz+int(n.row2))/z,x2=(xz+int(n.col2))/z;
   int a=sample(img,y1,x1),b=sample(img,y2,x2);bool right;
   if(a==0&&b==0)right=n.flags&1;else if(a==0)right=n.flags&2;else if(b==0)right=n.flags&4;
   else if(n.flags&8)right=false;else if(n.flags&16)right=true;else right=a-b>n.cut;
   heap=heap*2+1+unsigned(right);
  }
  o.labels[i]=m.labels[heap-FIRST];if constexpr(Trace)o.leaves[i]=uint16_t(heap);
 }
}
using Inference=void(*)(const Model&,const uint16_t*,Result&);
void verify_labels(const Frame& f){if(f.output.labels!=f.expected)throw std::runtime_error("Label mismatch: "+f.name);}
void verify(const Frame& f){if(f.output.labels!=f.expected||f.output.leaves!=f.expected_leaves)throw std::runtime_error("Labels/heap mismatch: "+f.name);}
struct Stats {double min,median,mean,p95,max;};
Stats stats(std::vector<double> v){std::sort(v.begin(),v.end());size_t n=v.size();return{v.front(),n%2?v[n/2]:(v[n/2-1]+v[n/2])/2,std::accumulate(v.begin(),v.end(),0.)/n,v[size_t(std::ceil(.95*n))-1],v.back()};}
void print_stats(std::ostream& o,const std::vector<double>& v){auto s=stats(v);o<<"{\"minimum_ms\":"<<s.min<<",\"median_ms\":"<<s.median<<",\"mean_ms\":"<<s.mean<<",\"p95_ms\":"<<s.p95<<",\"maximum_ms\":"<<s.max<<"}";}
int main(int argc,char** argv){try{
 if(argc!=3&&argc!=4)throw std::runtime_error("Usage: cpu_benchmark project output-directory [--validate-only]");
 fs::path root=argv[1],out=argv[2];bool validate_only=argc==4&&std::string(argv[3])=="--validate-only";
 fs::create_directories(out);
 Model m;
 m.nodes=read<Node>(root/"model/source_sections/nodes_prefix.bin",1023);
 m.compact=read<Compact>(root/"model/source_sections/terminal_compact.bin",1024);
 m.hist=read<uint8_t>(root/"model/source_sections/terminal_histograms.bin",1024*7);
 m.labels=read<uint8_t>(root/"model/depth10/leaves.bin",1024);
 m.packed=read<Packed>(root/"model/depth10/nodes.bin",1023);
 std::vector<Frame> frames;
 for(const std::string name:{"historical_000","historical_001","historical_002","historical_003","historical_004","stress_dense"}){
  fs::path p=root/"fixtures"/name;Frame f;f.name=name;
  f.depth=read<uint16_t>(p/"depth.bin",PIXELS);f.expected=read<uint8_t>(p/"depth10_labels.bin",PIXELS);
  f.expected_leaves=read<uint16_t>(p/"depth10_leafids.bin",PIXELS);frames.push_back(std::move(f));
 }
 const char* names[]={"literal_float32","integer_raw_cut"};Inference functions[]={literal<false>,integer<false>};Inference trace_functions[]={literal<true>,integer<true>};
 std::ofstream report(out/(validate_only?"cpu-validation.json":"cpu-benchmark-raw.json"));report.precision(12);
 report<<"{\"passed\":true,\"background_label\":7,\"unknown_label\":255,\"split_decisions\":10,\"threads\":1,\"validation_only\":"<<(validate_only?"true":"false")<<",\"variants\":[";
 for(int v=0;v<2;++v){
  if(v)report<<',';report<<"{\"name\":\""<<names[v]<<"\",\"threads\":1,\"fixtures\":[";
  for(auto& f:frames){trace_functions[v](m,f.depth.data(),f.output);verify(f);fs::path p=out/names[v]/f.name;fs::create_directories(p);write(p/"labels.bin",f.output.labels);write(p/"leafids.bin",f.output.leaves);}
  const int warmups=validate_only?0:30,repeats=validate_only?0:100;
  for(int k=0;k<warmups;++k){auto& f=frames[k%frames.size()];functions[v](m,f.depth.data(),f.output);verify_labels(f);}
  std::vector<std::vector<double>> times(frames.size());
  for(int r=0;r<repeats;++r)for(size_t j=0;j<frames.size();++j){auto& f=frames[j];auto a=Clock::now();functions[v](m,f.depth.data(),f.output);auto b=Clock::now();times[j].push_back(std::chrono::duration<double,std::milli>(b-a).count());verify_labels(f);}
  std::vector<double> aggregate;
  for(size_t j=0;j<frames.size();++j){auto& f=frames[j];if(j)report<<',';report<<"{\"id\":\""<<f.name<<"\",\"pixels\":"<<PIXELS<<",\"nonzero_centres\":"<<std::count_if(f.depth.begin(),f.depth.end(),[](auto z){return z!=0;})<<",\"label_mismatches\":0,\"leaf_mismatches\":0,\"measured_frames\":"<<times[j].size();if(!times[j].empty()){report<<",\"latency\":";print_stats(report,times[j]);report<<",\"samples_ms\":[";for(size_t k=0;k<times[j].size();++k){if(k)report<<',';report<<times[j][k];}report<<']';}report<<'}';aggregate.insert(aggregate.end(),times[j].begin(),times[j].end());}
  report<<"],\"warmup_single_frames_excluded\":"<<warmups<<",\"measured_frames\":"<<aggregate.size();if(!aggregate.empty()){report<<",\"all_frames_latency\":";print_stats(report,aggregate);}report<<'}';
 }
 report<<"],\"timing_scope\":\"One inference call, one128x128 frame, preloaded model/input and preallocated buffers; class-label writes only, no reached-leaf writes in timed calls; serial CPU loop. Untimed full label+heap validation is separate. Excludes loading, comparison, hashing, allocations, writes. Literal includes final7class weighted voting; integer uses precomputed terminal labels.\",\"clock\":\"std::chrono::steady_clock host elapsed\",\"no_batching\":true}\n";
 if(!report)throw std::runtime_error("Report write failed");std::cout<<"PASS both CPU variants labels+heap traces on6frames; validation_only="<<validate_only<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
