#include "cilk/cilk.h"
#include "cilk/cilk_api.h"
#define CTIMER_MEASURE_ON_STOP
#include "ctimer.h"



#define spawn cilk_spawn
#define sync cilk_sync
#define cilk 
#define Cilk_exit exit
#define Cilk_rand rand

// Timing
#define Cilk_time uint64_t
#define Cilk_user_critical_path 0
#define Cilk_user_work 0
#define Cilk_get_wall_time() 0
#define Cilk_active_size __cilkrts_get_nworkers()
#define Cilk_wall_time_to_sec
#define Cilk_time_to_sec

