#include "secondary_cache.hpp"
#include "secondary_cache_patch.hpp"
#include "motor/sequential.hpp"
#include "vkcore/dump_format.hpp"
#include <windows.h>
#include <atomic>
#include <array>
namespace circle::motor::secondary_cache {
namespace { std::atomic<bool> shader{},fault{}; std::array<std::atomic<std::uint64_t>,64> frames{}; }
bool requested(){
    static const bool on=[]{
        wchar_t value[8]{};
        return sequential::requested()&&!sequential::monoVrEnabled()&&
            GetEnvironmentVariableW(L"CIRCLE_SECONDARY_CACHE_FIX",value,8)==1&&value[0]==L'1';
    }();
    return on;
}
bool ready(){return requested()&&shader&&!fault;}
void shaderReady(){shader=true;}
void shaderFailed(){fault=true;}
void markHistory(std::uint32_t backend){frames[backend%frames.size()]=std::uint64_t(backend)+1;}
bool usesHistory(std::uint32_t backend){return ready()&&frames[backend%frames.size()]==std::uint64_t(backend)+1;}
std::vector<std::uint32_t> patch(std::span<const std::uint32_t> words){
    if(!requested())return {};
    const auto hash=circlevr::vkcore::dump::fnv1a64(words.data(),words.size_bytes());
    // Exact audited shader identities with the secondary-cache previous-stamp
    // expression. All other vertex and fragment shaders remain unchanged.
    switch(hash){
    case 0x7e64fc8a43115ff5ull: case 0x2d79f8583337bbf5ull:
    case 0x78d1b6ce68737e5bull: case 0x7e8f2d501768a555ull:
    case 0xcdbca621d061b3bfull: case 0xdea8ccca30c0066full:break;
    default:return {};
    }
    auto result=patchHistoryDistance(words);
    if(result.empty())shaderFailed();
    return result;
}
}
