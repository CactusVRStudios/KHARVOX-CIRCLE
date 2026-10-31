# NVIDIA DLSS SDK 310.9.1. Runtime DLLs are supplied by the selected game installation.
set(CIRCLE_DLSS_SDK_ROOT "${CMAKE_SOURCE_DIR}/references/dlss-sdk" CACHE PATH "Pinned NVIDIA DLSS SDK checkout")
if(NOT EXISTS "${CIRCLE_DLSS_SDK_ROOT}/include/nvsdk_ngx_helpers_dlssg_d3d.h")
  include(FetchContent)
  FetchContent_Declare(circle_dlss_sdk
    GIT_REPOSITORY https://github.com/NVIDIA/DLSS.git
    GIT_TAG 374959484e79a640feaba44c93ac8cfb0a03f5b5
    GIT_PROGRESS TRUE)
  FetchContent_MakeAvailable(circle_dlss_sdk)
  set(CIRCLE_DLSS_SDK_ROOT "${circle_dlss_sdk_SOURCE_DIR}")
endif()
add_library(circle_ngx_sdk INTERFACE)
target_include_directories(circle_ngx_sdk INTERFACE "${CIRCLE_DLSS_SDK_ROOT}/include")
# Circle uses /MT, not the reference port's /MD.
target_link_libraries(circle_ngx_sdk INTERFACE
  optimized "${CIRCLE_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib"
  debug "${CIRCLE_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s_dbg.lib"
  d3d12 dxgi)
