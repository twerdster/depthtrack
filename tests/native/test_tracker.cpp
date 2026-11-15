#include "depthtrack/depthtrack.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using H=std::unique_ptr<dt_tracker,decltype(&dt_tracker_destroy)>;
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
bool near(double a,double b){return std::abs(a-b)<=1e-8;}
H make(){dt_error e;H h(dt_tracker_create(nullptr,&e),dt_tracker_destroy);require(h&&e==DT_OK,"create");return h;}
dt_state update(H&h,double t,const dt_point*p){dt_state s{};require(dt_tracker_update(h.get(),t,p,&s)==DT_OK,"update");return s;}
void unit(){
 require(dt_abi_version()==1,"ABI version");dt_parameters p;dt_parameters_default(&p);require(p.min_cutoff_hz==1.2&&p.reset_gap_seconds==.45,"defaults");
 dt_error e;p.min_cutoff_hz=0;require(!dt_tracker_create(&p,&e)&&e==DT_INVALID_PARAMETERS,"parameter validation");
 auto h=make();auto s=update(h,0,nullptr);require(s.status==DT_LOST&&!s.has_position&&!s.has_observation_age&&s.generation==0&&s.reason==DT_REASON_MISSING,"initial missing");
 dt_point a{0,0,300};s=update(h,.1,&a);require(s.status==DT_INITIALIZED&&s.accepted&&s.has_position&&s.generation==1&&s.position.z==300,"immediate start");
 a.x=9999;size_t count=0;dt_history_sample saved{};require(dt_tracker_history_copy(h.get(),&saved,1,&count)==DT_OK&&count==1&&saved.position.x==0,"owned input/history");
 dt_state untouched;std::memset(&untouched,0x5a,sizeof untouched);dt_state before=untouched;
 require(dt_tracker_update(h.get(),.1,nullptr,&untouched)==DT_INVALID_TIMESTAMP&&std::memcmp(&before,&untouched,sizeof before)==0,"duplicate atomicity");
 require(dt_tracker_update(h.get(),std::numeric_limits<double>::infinity(),nullptr,&untouched)==DT_INVALID_TIMESTAMP,"nonfinite time");
 dt_point invalid{0,0,-1};s=update(h,.15,&invalid);require(s.reason==DT_REASON_INVALID&&!s.accepted&&s.position.z==300,"invalid point consumed as missing");
 require(dt_tracker_update(h.get(),.15,nullptr,&untouched)==DT_INVALID_TIMESTAMP,"invalid point consumes time");
 s=update(h,.35,nullptr);require(s.has_position,"inclusive hold boundary");s=update(h,.350001,nullptr);require(!s.has_position&&s.status==DT_LOST,"held visibility expires");
 require(dt_tracker_history_copy(h.get(),nullptr,0,&count)==DT_BUFFER_TOO_SMALL&&count==1,"history capacity query");
 dt_history_sample sentinel{77,{8,9,10}};count=0;require(dt_tracker_history_copy(h.get(),&sentinel,0,&count)==DT_BUFFER_TOO_SMALL&&sentinel.time_s==77&&count==1,"insufficient buffer no partial write");
 dt_point b{500,1,300};s=update(h,.7,&b);require(s.status==DT_REACQUIRED&&s.generation==2&&s.history_count==1,"expiry fresh start");
 dt_tracker_reset(h.get());a={0,0,300};update(h,0,&a);b={1000,0,300};s=update(h,.1,&b);require(!s.accepted&&s.reason==DT_REASON_JUMP&&s.status==DT_HELD,"spike rejection");b.x=1010;s=update(h,.2,&b);require(s.status==DT_REACQUIRED&&s.position.x==1010&&s.generation==2,"confirmed relocation");
 dt_tracker_reset(h.get());update(h,0,&a);b={1000,0,300};update(h,.1,&b);update(h,.15,nullptr);b.x=1010;s=update(h,.2,&b);require(!s.accepted&&s.reason==DT_REASON_JUMP,"missing clears pending");
 s=update(h,1,nullptr);require(s.history_count==0,"prune during missing/lost");
 require(dt_tracker_update(nullptr,0,nullptr,&s)==DT_INVALID_ARGUMENT&&dt_tracker_reset(nullptr)==DT_INVALID_ARGUMENT,"null arguments");
 dt_tracker_destroy(nullptr);
 // Native-only overflow guards are atomic; normal reference parity is unchanged.
 auto overflow=make();update(overflow,-1e308,&a);update(overflow,0,nullptr);
 require(dt_tracker_update(overflow.get(),1e308,nullptr,&untouched)==DT_INVALID_TIMESTAMP,"accepted elapsed overflow");
 dt_parameters extreme;dt_parameters_default(&extreme);extreme.derivative_cutoff_hz=1e308;
 H huge(dt_tracker_create(&extreme,&e),dt_tracker_destroy);require(bool(huge),"finite extreme parameters");a={0,0,300};update(huge,0,&a);a.x=1;
 require(dt_tracker_update(huge.get(),.01,&a,&untouched)==DT_NUMERICAL_RANGE,"arithmetic overflow guard");
 require(dt_tracker_history_size(huge.get())==1&&std::memcmp(&before,&untouched,sizeof before)==0,"numerical failure unchanged state/output");

 // Whole coordinate rotations leave the shared norm/alpha equivariant.
 auto r=make();auto q=make();for(int i=0;i<50;++i){dt_point x{40*std::cos(i*.2),40*std::sin(i*.2),300.0+i};dt_point y{-x.y,x.x,x.z};auto u=update(r,i*.13,&x),v=update(q,i*.13,&y);require(near(-u.position.y,v.position.x)&&near(u.position.x,v.position.y)&&near(u.position.z,v.position.z),"rotation equivariance");}
 std::cout<<"native unit checks passed\n";
}
std::vector<std::string> split(const std::string& line){std::vector<std::string>v;std::stringstream s(line);std::string x;while(std::getline(s,x,','))v.push_back(x);return v;}
void golden(const char*path){
 std::ifstream in(path);require(bool(in),"golden CSV missing");std::string line;require(bool(std::getline(in,line)),"empty CSV");std::vector<H> hs;for(int i=0;i<5;++i)hs.push_back(make());size_t rows=0;
 while(std::getline(in,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;auto v=split(line);require(v.size()>=14,"golden columns");int c=std::stoi(v[2]);require(c>=0&&c<5,"golden class");bool has=std::stoi(v[3])!=0;dt_point z{std::stod(v[4]),std::stod(v[5]),std::stod(v[6])};auto s=update(hs[c],std::stod(v[1]),has?&z:nullptr);
  std::string id="golden row "+std::to_string(rows+1);require(s.has_position==std::stoul(v[7]),id+" visible");if(s.has_position)require(near(s.position.x,std::stod(v[8]))&&near(s.position.y,std::stod(v[9]))&&near(s.position.z,std::stod(v[10])),id+" XYZ");require(s.accepted==std::stoul(v[11])&&dt_status_string(s.status)==v[12]&&s.generation==std::stoull(v[13]),id+" status/generation");++rows;
 }
 require(rows>0,"golden has no samples");std::cout<<"native golden CSV passed "<<rows<<" states, XYZ tolerance1e-8mm\n";
}
int main(int argc,char**argv){try{if(argc==3&&std::string(argv[1])=="--golden")golden(argv[2]);else if(argc==1)unit();else throw std::runtime_error("bad arguments");return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
