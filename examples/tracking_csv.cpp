#include "depthtrack/depthtrack.h"
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using Handle=std::unique_ptr<dt_tracker,decltype(&dt_tracker_destroy)>;
std::vector<std::string> split(const std::string& line){std::vector<std::string> v;std::stringstream s(line);std::string x;while(std::getline(s,x,','))v.push_back(x);return v;}
double number(const std::string& s){size_t n=0;double x=std::stod(s,&n);if(n!=s.size()||!std::isfinite(x))throw std::runtime_error("Invalid finite number: "+s);return x;}
std::string quoted(const std::string& s){std::string out="\"";for(char c:s){if(c=='\"')out+='\"';out+=c;}return out+'\"';}
int main(int argc,char**argv){try{
 dt_parameters p;dt_parameters_default(&p);
 std::map<std::string,double*> flags={{"--min-cutoff",&p.min_cutoff_hz},{"--speed-beta",&p.speed_beta},{"--derivative-cutoff",&p.derivative_cutoff_hz},{"--max-cutoff",&p.max_cutoff_hz},{"--jump-slack",&p.jump_slack_mm},{"--max-speed",&p.max_speed_mm_s},{"--hold",&p.hold_seconds},{"--reset-gap",&p.reset_gap_seconds},{"--trail",&p.trail_seconds}};
 std::vector<std::string> paths;
 for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--help"){std::cout<<"Usage: depthtrack_csv [parameter flags] [input.csv|- [output.csv|-]]\nInput first columns: frame,time_s,class_id,has_observation,x,y,z\nAdditional golden columns are ignored. Output includes copied history JSON.\n";return 0;}if(a=="--version"){std::cout<<"DepthTrack C ABI "<<dt_abi_version()<<"\n";return 0;}auto flag=flags.find(a);if(flag!=flags.end()){if(++i==argc)throw std::runtime_error("Missing flag value");*flag->second=number(argv[i]);}else if(a.rfind("--",0)==0)throw std::runtime_error("Unknown flag: "+a);else paths.push_back(a);}
 if(paths.size()>2)throw std::runtime_error("Too many paths");
 std::ifstream file;std::ofstream output;std::istream* in=&std::cin;std::ostream* out=&std::cout;
 if(!paths.empty()&&paths[0]!="-"){file.open(paths[0]);if(!file)throw std::runtime_error("Cannot open input");in=&file;}
 if(paths.size()>1&&paths[1]!="-"){output.open(paths[1]);if(!output)throw std::runtime_error("Cannot open output");out=&output;}
 // Validate parameters even for an empty CSV.
 dt_error err;Handle probe(dt_tracker_create(&p,&err),dt_tracker_destroy);if(!probe)throw std::runtime_error(dt_error_string(err));
 std::map<int,Handle> trackers;
 *out<<"frame,time_s,class_id,visible,out_x,out_y,out_z,accepted,status,generation,reason,observation_age_s,cutoff_hz,history_json\n"<<std::setprecision(17);
 std::string line;size_t row=0;
 while(std::getline(*in,line)){++row;if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;auto v=split(line);if(row==1&&v[0]=="frame")continue;if(v.size()<7)throw std::runtime_error("Expected at least seven input columns at row "+std::to_string(row));
  double frame=number(v[0]),t=number(v[1]),cls=number(v[2]),has=number(v[3]);
  if(frame<0||frame!=std::floor(frame)||cls<0||cls>1000000||cls!=std::floor(cls)||(has!=0&&has!=1))throw std::runtime_error("Invalid frame/class/observation flag");
  dt_point point{};if(has)point={number(v[4]),number(v[5]),number(v[6])};
  int id=static_cast<int>(cls);auto it=trackers.find(id);if(it==trackers.end()){Handle h(dt_tracker_create(&p,&err),dt_tracker_destroy);if(!h)throw std::runtime_error(dt_error_string(err));it=trackers.emplace(id,std::move(h)).first;}
  dt_state s{};err=dt_tracker_update(it->second.get(),t,has?&point:nullptr,&s);if(err!=DT_OK)throw std::runtime_error("Row "+std::to_string(row)+": "+dt_error_string(err));
  std::vector<dt_history_sample> history(s.history_count);size_t written=0;err=dt_tracker_history_copy(it->second.get(),history.data(),history.size(),&written);if(err!=DT_OK)throw std::runtime_error("History copy failed");
  std::ostringstream j;j<<std::setprecision(17)<<'[';for(size_t k=0;k<written;++k){if(k)j<<',';auto h=history[k];j<<"{\"time_s\":"<<h.time_s<<",\"position\":["<<h.position.x<<','<<h.position.y<<','<<h.position.z<<"]}";}j<<']';
  *out<<v[0]<<','<<t<<','<<id<<','<<s.has_position<<','<<s.position.x<<','<<s.position.y<<','<<s.position.z<<','<<s.accepted<<','<<dt_status_string(s.status)<<','<<s.generation<<','<<dt_reason_string(s.reason)<<',';
  if(s.has_observation_age)*out<<s.observation_age_s;*out<<',';if(s.has_cutoff)*out<<s.cutoff_hz;*out<<','<<quoted(j.str())<<'\n';
 }
 if(!*out)throw std::runtime_error("Output write failed");return 0;
}catch(const std::exception&e){std::cerr<<"depthtrack_csv: "<<e.what()<<'\n';return 1;}}
