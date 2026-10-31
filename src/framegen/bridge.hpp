#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "stereo_seq/eye_tags.hpp"
namespace circle::fg {
// Exact Streamline Constants v2 payload. Matrices retain Motor's actual
// asymmetric (or configured symmetric) projection; no synthetic FOV rebuild.
struct Camera {
 std::array<float,16> viewToClip{},clipToView{},lens{},clipToPrev{},prevToClip{};
 std::array<float,2> jitter{},motionScale{},pinhole{};
 std::array<float,3> position{},up{},right{},forward{};
 float nearPlane{},farPlane{},fov{},aspect{},invalidMotion{};
 uint8_t inverted{},cameraMotion{},motion3d{},reset{},orthographic{},dilated{},jittered{},padding[5]{};
};
static_assert(sizeof(Camera)==424);
struct Resource {
 uint64_t image{}; uint32_t layout{},width{},height{},format{},usage{};
 uint32_t left{},top{},extentWidth{},extentHeight{};
};
struct Capture {
 circlevr::stereo_seq::RenderTag tag{};
 Camera camera{};
 Resource depth{},motion{};
 void* command{};
 float frameMs=16.6667f,meters=1.f;
 uint32_t fsrFlags{};
 double producerHoldMs{}; // Last hold at eye capture: diagnostic only, not the planning clock.
 bool cancelled{}; // An upscaler failed after its input copy was recorded.
};
using CaptureCallback=std::function<void(const Capture&)>;
bool requested();
void setCapture(CaptureCallback callback);
void capture(const Capture& input);
struct Status {
 const char* state="off";
 std::string reason;
 bool available{};
 double multiplier=1;
 const char* backend="none";
 uint64_t captures{},realSubmitted{},generatedSubmitted{},repeated{};
 uint64_t evaluations{},warmups{},historyGaps{};
 double gpuMs{},imageAgeMs{},realRate{},generatedRate{};
};
void state(const char* value,const char* reason="",bool available=false);
Status status();
void configure(double multiplier,const char* backend);
void countCapture();
void evaluated(bool interpolated,bool historyGap);
void submitted(bool generated,uint64_t serial,bool success,bool image,double gpuMs,double ageMs);
}
