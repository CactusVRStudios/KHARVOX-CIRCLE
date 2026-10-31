#pragma once
#include "stereo_seq/prev_matrices.hpp"

namespace circle::motor {
// Only CC8820's destinations. Native current fields and camera-cut counter
// +199BC are excluded. The final conditional block requires r_lockView == 0.
inline std::vector<circlevr::stereo_seq::ByteRange> previousViewRanges(){
    return {{0x18810,0x40},{0x18890,0x40},{0x18950,0x40},
            {0x189d0,0x100},{0x18b10,0x40},{0x18b90,0x40},
            {0x18c10,0x40},{0x18d10,0x40},{0x18d90,0x40},
            {0x18e50,0x6c},{0x18ee0,0x24},{0x18f0c,8},{0x18f2c,8}};
}
}
