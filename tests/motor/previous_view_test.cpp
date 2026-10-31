#include "previous_view.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

int main(){
    using namespace circlevr::stereo_seq;
    int failures=0;
    const auto check=[&](bool ok,const char* message){if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",message);}};
    std::vector<std::byte> view(0x199c0,std::byte{0xa5});
    const auto ranges=circle::motor::previousViewRanges();
    PrevMatrixBook book(ranges);
    const auto nativeStore=[&](unsigned char stamp){for(const auto& r:ranges)std::fill_n(view.begin()+r.offset,r.size,std::byte{stamp});};
    const auto matches=[&](unsigned char stamp){for(const auto& r:ranges)for(std::size_t i=0;i<r.size;++i)if(view[r.offset+i]!=std::byte{stamp})return false;return true;};
    nativeStore(0);check(!book.afterStore(view.data(),Eye::Left,10),"first left retains native initialization");
    nativeStore(0x11);check(!book.afterStore(view.data(),Eye::Right,11),"first right seeds left history");
    nativeStore(0x22);check(book.afterStore(view.data(),Eye::Left,12),"next left restores own history");
    check(matches(0x11),"all 928 history bytes belong to previous left, not right");
    nativeStore(0x33);check(book.afterStore(view.data(),Eye::Right,13),"next right restores own history");
    check(matches(0x22),"all history bytes belong to previous right, not current left");
    for(std::size_t i=0;i<view.size();++i){
        const bool history=std::any_of(ranges.begin(),ranges.end(),[&](const auto& r){return i>=r.offset && i<r.offset+r.size;});
        if(!history && view[i]!=std::byte{0xa5}){check(false,"current camera, projection and cut counter remain untouched");break;}
    }
    nativeStore(0x44);check(!book.afterStore(view.data(),Eye::Left,20),"frame gap rejects stale eye history");
    check(matches(0x44),"gap retains native values");
    book.clear();nativeStore(0x55);check(!book.afterStore(view.data(),Eye::Right,21),"cinema/map reset rejects reused view address");
    check(matches(0x55),"reset never restores prior scene bytes");
    return failures?1:0;
}
