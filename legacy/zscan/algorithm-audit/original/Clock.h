// Adapter only: timing instrumentation is unused by the numerical result.
// The malformed historical Linux Clock.h is replaced by steady_clock.
#pragma once
#include <chrono>
class Clock {
 using Time=std::chrono::steady_clock; Time::time_point start_=Time::now(); double last_=0;
public:
 void tic(){start_=Time::now();}
 double toc(){last_=std::chrono::duration<double>(Time::now()-start_).count();return last_;}
 double last(){return last_;}
};
