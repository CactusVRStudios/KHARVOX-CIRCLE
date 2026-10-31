#pragma once
#include <cstdint>
// Versioned C ABI. One instance per renderer; caller serializes calls.
// No STL objects or allocator ownership cross the DLL boundary.
namespace circle::fg {
constexpr uint32_t AdaptiveAbi=5;
struct AdaptiveSample {
 double nowMs,periodMs,renderMs,generatedMs;
 double realIntervalMs; // publication interval excluding measured producer holds
 uint32_t maximum,pendingPairs,fresh; // latched producer pressure OR current queue depth
};
struct AdaptiveDecision {
 uint32_t tenths,trialFrom,samples,misses,events;
 double reserveMs,cooldownMs;
 const char* reason; // static DLL storage, valid while DLL is loaded
};
struct AdaptiveApi {
 uint32_t size,version;
 void* (*create)();
 void (*destroy)(void*);
 AdaptiveDecision (*sample)(void*,const AdaptiveSample*);
 void (*output)(void*,double,double,uint32_t,double); // monotonic time, refresh, fresh, XR display time (ms)
 uint32_t (*plan)(void*,double,double,uint32_t); // source interval, refresh, continuous history
 void (*suspend)(void*,double);
};
using GetAdaptiveApi=const AdaptiveApi* (*)(uint32_t);
}
#if defined(CIRCLE_ADAPTIVE_EXPORTS)
#define CIRCLE_ADAPTIVE_API __declspec(dllexport)
#else
#define CIRCLE_ADAPTIVE_API __declspec(dllimport)
#endif
extern "C" CIRCLE_ADAPTIVE_API const circle::fg::AdaptiveApi* CircleAdaptiveFgGetApi(uint32_t version);
