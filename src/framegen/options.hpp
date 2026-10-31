#pragma once
#include <windows.h>
#include <string_view>
#include "fidelityfx.hpp"
namespace circle::fg {
inline bool fractionalMultiplier(){char v[16]{};GetEnvironmentVariableA("CIRCLE_VR_FG_MULTIPLIER",v,16);return !fidelityRequested()&&(std::string_view(v)=="auto"||std::string_view(v)=="adaptive");}
inline bool stableAutomaticMultiplier(){char v[16]{};GetEnvironmentVariableA("CIRCLE_VR_FG_MULTIPLIER",v,16);return std::string_view(v)=="auto-fixed";}
inline bool automaticMultiplier(){char v[16]{};GetEnvironmentVariableA("CIRCLE_VR_FG_MULTIPLIER",v,16);return std::string_view(v)=="auto-fixed"||(fidelityRequested()&&std::string_view(v)=="auto");}
inline unsigned requestedMultiplier(){char v[8]{};return GetEnvironmentVariableA("CIRCLE_VR_FG_MULTIPLIER",v,8)==1&&(v[0]=='3'||v[0]=='4')?unsigned(v[0]-'0'):2u;}
inline bool communityRequested(){char v[8]{};return GetEnvironmentVariableA("CIRCLE_VR_FG_COMMUNITY",v,8)==1&&v[0]=='1';}
}
