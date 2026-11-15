#include "depthtrack/depthtrack.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <vector>
#include <utility>
namespace {
constexpr double pi=3.141592653589793238462643383279502884;
using V=std::array<double,3>;
V as_v(dt_point p){return {p.x,p.y,p.z};}
dt_point as_p(V p){return {p[0],p[1],p[2]};}
V sub(V a,V b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
double norm(V p){return std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);}
double alpha(double cutoff,double dt){double x=2.0*pi*cutoff*dt;return x/(1.0+x);}
bool valid_parameters(const dt_parameters& p){
 const double values[]={p.min_cutoff_hz,p.speed_beta,p.derivative_cutoff_hz,p.max_cutoff_hz,p.jump_slack_mm,p.max_speed_mm_s,p.hold_seconds,p.reset_gap_seconds,p.trail_seconds};
 for(double x:values)if(!std::isfinite(x)||x<0)return false;
 return p.min_cutoff_hz>0&&p.derivative_cutoff_hz>0&&p.max_cutoff_hz>=p.min_cutoff_hz&&p.reset_gap_seconds>=p.hold_seconds;
}
bool valid_point(dt_point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&p.z>0;}
}
struct dt_tracker {
 dt_parameters p{};
 bool initialized=false,has_input_time=false,has_pending=false;
 double input_time=0,accepted_time=0,pending_time=0;
 V position{},raw{},velocity{},pending{};
 uint64_t generation=0;
 std::vector<dt_history_sample> history;
 dt_state snapshot(double t,uint32_t status,bool accepted,uint32_t reason){
  dt_state s{};s.reason=reason;s.accepted=accepted;s.generation=generation;
  s.has_observation_age=initialized;s.observation_age_s=initialized?t-accepted_time:0;
  s.has_position=initialized&&s.observation_age_s<=p.hold_seconds;
  s.status=s.has_position?status:DT_LOST;
  if(s.has_position)s.position=as_p(position);
  history.erase(std::remove_if(history.begin(),history.end(),[&](const auto& h){return t-h.time_s>p.trail_seconds;}),history.end());
  s.history_count=history.size();return s;
 }
 dt_state initialize(double t,V z){
  bool was_initialized=initialized;position=raw=z;velocity={0,0,0};accepted_time=t;
  has_pending=false;initialized=true;++generation;history.clear();history.push_back({t,as_p(z)});
  return snapshot(t,was_initialized?DT_REACQUIRED:DT_INITIALIZED,true,DT_REASON_NONE);
 }
 dt_state update(double t,const dt_point* point){
  input_time=t;has_input_time=true;
  if(!point||!valid_point(*point)){
   has_pending=false;
   return snapshot(t,DT_HELD,false,point?DT_REASON_INVALID:DT_REASON_MISSING);
  }
  V z=as_v(*point);
  if(!initialized||t-accepted_time>p.reset_gap_seconds)return initialize(t,z);
  double dt=t-accepted_time,gate=p.jump_slack_mm+p.max_speed_mm_s*dt;
  if(norm(sub(z,raw))>gate){
   bool confirmed=has_pending&&t-pending_time<=p.reset_gap_seconds&&norm(sub(z,pending))<=p.jump_slack_mm+p.max_speed_mm_s*(t-pending_time);
   if(confirmed)return initialize(t,z);
   pending=z;pending_time=t;has_pending=true;
   return snapshot(t,DT_HELD,false,DT_REASON_JUMP);
  }
  has_pending=false;
  double ad=alpha(p.derivative_cutoff_hz,dt);
  for(size_t i=0;i<3;++i)velocity[i]=ad*((z[i]-raw[i])/dt)+(1.0-ad)*velocity[i];
  double cutoff=std::min(p.max_cutoff_hz,p.min_cutoff_hz+p.speed_beta*norm(velocity));
  double a=alpha(cutoff,dt);
  for(size_t i=0;i<3;++i)position[i]=a*z[i]+(1.0-a)*position[i];
  raw=z;accepted_time=t;history.push_back({t,as_p(position)});
  dt_state s=snapshot(t,DT_UPDATED,true,DT_REASON_NONE);s.has_cutoff=1;s.cutoff_hz=cutoff;return s;
 }
};
extern "C" {
uint32_t dt_abi_version(void){return DT_ABI_VERSION;}
void dt_parameters_default(dt_parameters* out){if(out)*out={1.2,0.02,1.0,10.0,25.0,1200.0,0.25,0.45,0.65};}
dt_tracker* dt_tracker_create(const dt_parameters* parameters,dt_error* error_out){
 dt_parameters p;dt_parameters_default(&p);if(parameters)p=*parameters;
 if(!valid_parameters(p)){if(error_out)*error_out=DT_INVALID_PARAMETERS;return nullptr;}
 try{auto* t=new dt_tracker;t->p=p;if(error_out)*error_out=DT_OK;return t;}
 catch(const std::bad_alloc&){if(error_out)*error_out=DT_ALLOCATION_FAILED;return nullptr;}
 catch(...){if(error_out)*error_out=DT_INTERNAL_ERROR;return nullptr;}
}
void dt_tracker_destroy(dt_tracker* tracker){delete tracker;}
dt_error dt_tracker_reset(dt_tracker* tracker){if(!tracker)return DT_INVALID_ARGUMENT;auto p=tracker->p;*tracker=dt_tracker{};tracker->p=p;return DT_OK;}
dt_error dt_tracker_update(dt_tracker* tracker,double t,const dt_point* observation,dt_state* out){
 if(!tracker||!out)return DT_INVALID_ARGUMENT;
 if(!std::isfinite(t)||(tracker->has_input_time&&t<=tracker->input_time))return DT_INVALID_TIMESTAMP;
 // Prevent subtraction overflow; ordinary finite camera timestamps are unaffected.
 if(tracker->has_input_time&&!std::isfinite(t-tracker->input_time))return DT_INVALID_TIMESTAMP;
 if(tracker->initialized&&!std::isfinite(t-tracker->accepted_time))return DT_INVALID_TIMESTAMP;
 try{
  // A transaction also keeps the original state/output intact on allocation failure.
  dt_tracker candidate=*tracker;dt_state result=candidate.update(t,observation);
  if(candidate.initialized){
   for(double x:candidate.position)if(!std::isfinite(x))return DT_NUMERICAL_RANGE;
   for(double x:candidate.velocity)if(!std::isfinite(x))return DT_NUMERICAL_RANGE;
  }
  if((result.has_observation_age&&!std::isfinite(result.observation_age_s))||
     (result.has_cutoff&&!std::isfinite(result.cutoff_hz)))return DT_NUMERICAL_RANGE;
  *tracker=std::move(candidate);*out=result;return DT_OK;
 }catch(const std::bad_alloc&){return DT_ALLOCATION_FAILED;}catch(...){return DT_INTERNAL_ERROR;}
}
size_t dt_tracker_history_size(const dt_tracker* tracker){return tracker?tracker->history.size():0;}
dt_error dt_tracker_history_copy(const dt_tracker* tracker,dt_history_sample* destination,size_t capacity,size_t* written){
 if(!tracker||!written)return DT_INVALID_ARGUMENT;
 size_t n=tracker->history.size();
 if(capacity<n){*written=n;return DT_BUFFER_TOO_SMALL;}
 if(n&&!destination)return DT_INVALID_ARGUMENT;
 if(n)std::copy(tracker->history.begin(),tracker->history.end(),destination);
 *written=n;return DT_OK;
}
const char* dt_error_string(dt_error e){switch(e){case DT_OK:return "ok";case DT_INVALID_ARGUMENT:return "invalid argument";case DT_INVALID_PARAMETERS:return "invalid parameters";case DT_INVALID_TIMESTAMP:return "invalid timestamp";case DT_BUFFER_TOO_SMALL:return "buffer too small";case DT_ALLOCATION_FAILED:return "allocation failed";case DT_NUMERICAL_RANGE:return "numerical range exceeded";default:return "internal error";}}
const char* dt_status_string(uint32_t s){switch(s){case DT_LOST:return "lost";case DT_INITIALIZED:return "initialized";case DT_UPDATED:return "updated";case DT_HELD:return "held";case DT_REACQUIRED:return "reacquired";default:return "unknown";}}
const char* dt_reason_string(uint32_t r){switch(r){case DT_REASON_NONE:return "";case DT_REASON_MISSING:return "missing";case DT_REASON_INVALID:return "invalid";case DT_REASON_JUMP:return "jump";default:return "unknown";}}
}
