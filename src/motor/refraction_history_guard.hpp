#pragma once
#include <array>
#include <cstdint>

namespace circle::motor {
// Tracks native accumulation producers, not GPU completion. Native slots still
// ping-pong by backend parity; never blend a known peer-eye or stale producer.
struct RefractionProducer {
    std::uintptr_t image{};
    std::uint32_t backend{},sequence{};
    unsigned eye{};
    int width{},height{};
};
enum class RefractionHistoryStatus { Unknown, SameEye, OtherEye, Gap, Resized };
class RefractionHistoryGuard {
    std::array<RefractionProducer,4> slots_{};
public:
    void clear(){slots_={};}
    RefractionHistoryStatus observe(unsigned writeSlot,unsigned readSlot,
        RefractionProducer producer,std::uintptr_t readImage){
        if(writeSlot>=4||readSlot>=4||writeSlot==readSlot||producer.eye>1||
           !producer.image||!readImage||producer.image==readImage||
           producer.width<=0||producer.height<=0){clear();return RefractionHistoryStatus::Unknown;}
        const auto previous=slots_[readSlot];
        auto status=RefractionHistoryStatus::Unknown;
        if(previous.image==readImage){
            const auto gap=producer.backend-previous.backend;
            if(previous.eye!=producer.eye)status=RefractionHistoryStatus::OtherEye;
            else if(previous.width!=producer.width||previous.height!=producer.height)
                status=RefractionHistoryStatus::Resized;
            else if((gap!=1&&gap!=2)||producer.sequence-previous.sequence!=1)
                status=RefractionHistoryStatus::Gap;
            else status=RefractionHistoryStatus::SameEye;
        }
        slots_[writeSlot]=producer;
        return status;
    }
};
}
