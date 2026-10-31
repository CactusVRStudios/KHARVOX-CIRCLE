#include "world_gui_policy.hpp"
#include <cstdlib>
int main(){
    using circle::motor::reuseWorldGuiStamp;
    using circle::motor::replayGuiBatch;
    if(replayGuiBatch(true,false,0)||replayGuiBatch(true,false,-1)||
       !replayGuiBatch(true,false,1)||!replayGuiBatch(true,true,0)||
       !replayGuiBatch(false,false,0))return EXIT_FAILURE;
    if(!reuseWorldGuiStamp(true,41,42))return EXIT_FAILURE;
    if(reuseWorldGuiStamp(false,41,42)||reuseWorldGuiStamp(true,42,42)||
       reuseWorldGuiStamp(true,40,42)||reuseWorldGuiStamp(true,43,42)||
       reuseWorldGuiStamp(true,-1,0)||reuseWorldGuiStamp(true,-1,42)||
       reuseWorldGuiStamp(true,0,0))return EXIT_FAILURE;
    using circle::motor::reuseGlyphPayload;
    if(!reuseGlyphPayload(100,100,20,21,48*17)||!reuseGlyphPayload(100,100,20,21,0x4000*48)||
       reuseGlyphPayload(100,101,20,21,48)||reuseGlyphPayload(100,100,20,22,48)||
       reuseGlyphPayload(100,100,20,20,48)||reuseGlyphPayload(100,100,20,21,0)||
       reuseGlyphPayload(100,100,20,21,49)||reuseGlyphPayload(100,100,20,21,0x4001*48))return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
