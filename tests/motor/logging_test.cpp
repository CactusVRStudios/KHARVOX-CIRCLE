#include "vkcore/log.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstring>
int main(int argc,char** argv){
 using namespace circlevr::vkcore;
 const bool on=argc>1&&!std::strcmp(argv[1],"on");
 SetEnvironmentVariableW(L"CIRCLE_ENABLE_LOGGING",on?L"1":argc>1&&!std::strcmp(argv[1],"off")?L"0":nullptr);
 const auto dir=std::filesystem::current_path()/(L"logging-test-"+std::to_wstring(GetCurrentProcessId()));
 SetEnvironmentVariableW(L"KHARVOX_CIRCLE_LOG_DIR",dir.c_str());
 if(loggingEnabled()!=on||logDirectory().empty()==on)return 1;
 logOpen();logf("logging regression sentinel %d",42);
 const auto path=dir/L"capture.csv";
 FILE* f=openLogFileShared(path.c_str(),L"w",_SH_DENYWR);
 if(bool(f)!=on)return 2;if(f){fputs("test",f);fclose(f);}
 FILE* g=nullptr;const auto status=openLogFile(&g,(dir/L"capture.bin").c_str(),L"wb");
 if((status==0)!=on||bool(g)!=on)return 3;if(g)fclose(g);
 if(!on)return std::filesystem::exists(dir)?4:0;
 bool found=false;
 for(const auto& e:std::filesystem::directory_iterator(dir))if(e.path().extension()==L".log"){
  std::ifstream stream(e.path());const std::string text((std::istreambuf_iterator<char>(stream)),{});
  found=text.find("logging regression sentinel 42")!=std::string::npos;
 }
 return found&&std::filesystem::exists(dir/L"LAYER_LOADED")?0:5;
}
