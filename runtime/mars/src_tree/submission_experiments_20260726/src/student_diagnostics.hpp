#pragma once
#include "../../code/h2h.h"
#ifndef STUDENT_DIAGNOSTICS
#define STUDENT_DIAGNOSTICS 0
#endif
// Phases 0-5: initialization, road compilation, pivot, rest, result IDs, cleanup.
// Phase 6 records post-query checks. Phase 7 records untimed reference replay.
struct StudentDiagnostics {
 uint64_t h2h[8]={0,0,0,0,0,0,0,0};
 uint64_t post_direct=0,post_interval=0,post_fringe=0,binary_comparisons=0;
 uint64_t bound_visits=0,bound_h2h_requested=0,euclidean_pruned=0,invalid_source=0,invalid_representative=0;
 double phase_ms[8]={0,0,0,0,0,0,0,0};
 double pivot_parts_ms[3]={0,0,0}; int pivot_part=-1; std::chrono::steady_clock::time_point pivot_part_start;
 int phase=0;
 std::chrono::steady_clock::time_point phase_start;
};
inline StudentDiagnostics student_diagnostics;
inline void studentBeginCore(std::chrono::steady_clock::time_point t){
#if STUDENT_DIAGNOSTICS
 student_diagnostics=StudentDiagnostics{};student_diagnostics.phase_start=t;
#endif
}
inline void studentSwitchPhase(int phase){
#if STUDENT_DIAGNOSTICS
 auto t=std::chrono::steady_clock::now();
 student_diagnostics.phase_ms[student_diagnostics.phase]+=std::chrono::duration<double,std::milli>(t-student_diagnostics.phase_start).count();
 student_diagnostics.phase=phase;student_diagnostics.phase_start=t;
#endif
}
inline void studentEndCore(std::chrono::steady_clock::time_point t){
#if STUDENT_DIAGNOSTICS
 student_diagnostics.phase_ms[student_diagnostics.phase]+=std::chrono::duration<double,std::milli>(t-student_diagnostics.phase_start).count();
 student_diagnostics.phase=6;student_diagnostics.phase_start=t;
#endif
}
inline double studentH2HQuery(int a,int b){
#if STUDENT_DIAGNOSTICS
 ++student_diagnostics.h2h[student_diagnostics.phase];
#endif
 return h2hQuery(a,b);
}
inline void studentCountPost(int kind){
#if STUDENT_DIAGNOSTICS
 if(kind==0)++student_diagnostics.post_direct;
 else if(kind==1)++student_diagnostics.post_interval;
 else ++student_diagnostics.post_fringe;
#endif
}
inline void studentCountBinary(){
#if STUDENT_DIAGNOSTICS
 ++student_diagnostics.binary_comparisons;
#endif
}

inline void studentPivotPart(int next){
#if STUDENT_DIAGNOSTICS
 auto t=std::chrono::steady_clock::now();if(student_diagnostics.pivot_part>=0&&student_diagnostics.pivot_part<3)student_diagnostics.pivot_parts_ms[student_diagnostics.pivot_part]+=std::chrono::duration<double,std::milli>(t-student_diagnostics.pivot_part_start).count();student_diagnostics.pivot_part=next;student_diagnostics.pivot_part_start=t;
#endif
}
