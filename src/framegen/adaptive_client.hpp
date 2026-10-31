#pragma once
#include "adaptive_api.hpp"
#include <windows.h>
#include <filesystem>
namespace circle::fg {
// Loaded from the renderer's own directory, never the current/game directory.
// Retained until process exit, avoiding DLL teardown under the loader lock.
struct AdaptiveClient {
 const AdaptiveApi* api{};void* instance{};bool attempted{};
 bool load(){
  if(attempted)return instance!=nullptr;attempted=true;
  HMODULE owner{};wchar_t path[32768]{};
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
     reinterpret_cast<LPCWSTR>(&anchor),&owner)||!GetModuleFileNameW(owner,path,32768))return false;
  auto file=std::filesystem::path(path).parent_path()/L"CircleAdaptiveFG.dll";
  const auto dll=LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if(!dll)return false;
  const auto get=reinterpret_cast<GetAdaptiveApi>(GetProcAddress(dll,"CircleAdaptiveFgGetApi"));
  api=get?get(AdaptiveAbi):nullptr;
  if(!api||api->size!=sizeof(AdaptiveApi)||api->version!=AdaptiveAbi||!api->create||!api->sample||!api->plan||!api->output||!api->suspend)return false;
  instance=api->create();return instance!=nullptr;
 }
 static void anchor(){}
};
}
