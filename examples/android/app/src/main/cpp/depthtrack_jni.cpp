#include <jni.h>
#include <cstdint>
#include "depthtrack/depthtrack.h"
namespace {
dt_tracker* ptr(jlong h){return reinterpret_cast<dt_tracker*>(static_cast<std::uintptr_t>(h));}
void fail(JNIEnv* e,const char* message){jclass c=e->FindClass("java/lang/IllegalArgumentException");if(c)e->ThrowNew(c,message);}
}
extern "C" JNIEXPORT jlong JNICALL Java_com_example_depthtrack_DepthTracker_nativeCreate(JNIEnv* e,jobject){dt_error error;auto*t=dt_tracker_create(nullptr,&error);if(!t){fail(e,dt_error_string(error));return 0;}return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(t));}
extern "C" JNIEXPORT void JNICALL Java_com_example_depthtrack_DepthTracker_nativeDestroy(JNIEnv*,jobject,jlong h){dt_tracker_destroy(ptr(h));}
extern "C" JNIEXPORT jdoubleArray JNICALL Java_com_example_depthtrack_DepthTracker_nativeUpdate(JNIEnv* e,jobject,jlong h,jdouble time,jdoubleArray observation){
 if(!h){fail(e,"Tracker closed");return nullptr;}
 dt_point p{};const dt_point* input=nullptr;
 if(observation){if(e->GetArrayLength(observation)!=3){fail(e,"Expected XYZ");return nullptr;}jdouble v[3];e->GetDoubleArrayRegion(observation,0,3,v);if(e->ExceptionCheck())return nullptr;p={v[0],v[1],v[2]};input=&p;}
 // Allocate before advancing native state, so Java allocation failure is atomic.
 auto out=e->NewDoubleArray(11);if(!out)return nullptr;
 dt_state s{};auto error=dt_tracker_update(ptr(h),time,input,&s);if(error!=DT_OK){fail(e,dt_error_string(error));return nullptr;}
 jdouble v[]={double(s.status),double(s.reason),double(s.accepted),double(s.has_position),s.position.x,s.position.y,s.position.z,double(s.has_observation_age),s.observation_age_s,double(s.has_cutoff),s.cutoff_hz};e->SetDoubleArrayRegion(out,0,11,v);return out;
}
