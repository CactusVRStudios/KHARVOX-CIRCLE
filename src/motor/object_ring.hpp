#pragma once
#include "stereo_seq/object_prev.hpp"
#include <array>

namespace circle::motor {
// Motor's pair token is a timestamp, whereas Eternal's allocator expects
// consecutive simulation ticks. Normalize once per first-seen render, and
// retain the answer for all five consumers of that render.
class ObjectRing {
    struct Answer {bool valid{};unsigned frame{};circlevr::stereo_seq::ObjectRing::Picks picks{};circlevr::stereo_seq::Eye eye{};uint64_t sourceToken{};};
    std::array<Answer,64> answers{};
    circlevr::stereo_seq::ObjectRing ring;
    uint64_t token{},ordinal{};
public:
    struct Observation {bool first{},inOrder{true},tagMismatch{};};
    circlevr::stereo_seq::ObjectRing::Picks picks(circlevr::stereo_seq::Eye eye,uint64_t sourceToken,unsigned frame,Observation* observation=nullptr){
        if(observation)*observation={};
        auto& a=answers[frame%answers.size()];
        if(a.valid&&a.frame==frame){
            if(observation)observation->tagMismatch=a.eye!=eye||a.sourceToken!=sourceToken;
            return a.picks;
        }
        if(eye==circlevr::stereo_seq::Eye::Mono||!sourceToken){token=0;++ordinal;}
        else if(sourceToken!=token){token=sourceToken;++ordinal;}
        bool inOrder=true;
        auto p=ring.picksFor(eye,ordinal,frame,&inOrder);
        if(observation)*observation={true,inOrder,false};
        a={true,frame,p,eye,sourceToken};return p;
    }
};
}
