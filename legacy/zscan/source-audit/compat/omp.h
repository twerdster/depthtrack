#ifndef ZSCAN_OFFLINE_OMP_SHIM_H
#define ZSCAN_OFFLINE_OMP_SHIM_H
// Original Forest.h parallel pragma is commented; offline replay is single-threaded.
static inline int omp_get_thread_num(void) { return 0; }
#endif
