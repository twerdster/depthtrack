#include "depthtrack/depthtrack.h"
int main(void){dt_error e=DT_OK;dt_tracker*t=dt_tracker_create(0,&e);dt_point p={1,2,300};dt_state s={0};int ok=t&&e==DT_OK&&dt_abi_version()==DT_ABI_VERSION&&dt_tracker_update(t,0,&p,&s)==DT_OK&&s.status==DT_INITIALIZED&&s.position.z==300;dt_tracker_destroy(t);return ok?0:1;}
