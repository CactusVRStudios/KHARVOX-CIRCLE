# NVIDIA DLSS SDK 310.9.1. Runtime DLLs are supplied by the selected game installation.
set(CIRCLE_DLSS_SDK_ROOT "${CMAKE_SOURCE_DIR}/references/dlss-sdk" CACHE PATH "Pinned NVIDIA DLSS SDK checkout")
if(NOT EXISTS "${CIRCLE_DLSS_SDK_ROOT}/include/nvsdk_ngx_helpers_dlssg_d3d.h")
  message(FATAL_ERROR "DLSS SDK missing. See docs/native-vr-framegen.md for the pinned SDK setup.")
endif()
add_library(circle_ngx_sdk INTERFACE)
target_include_directories(circle_ngx_sdk INTERFACE "${CIRCLE_DLSS_SDK_ROOT}/include")
# Circle uses /MT, not the reference port's /MD.
target_link_libraries(circle_ngx_sdk INTERFACE
  optimized "${CIRCLE_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib"
  debug "${CIRCLE_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s_dbg.lib"
  d3d12 dxgi)
