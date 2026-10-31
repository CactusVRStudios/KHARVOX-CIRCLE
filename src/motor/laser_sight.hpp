#pragma once
#include "experimental.hpp"
namespace circle::motor::laser {
bool enabled();
void install();
bool visible(uint64_t owner,uint64_t weapon,uint64_t life);
void publish(std::array<experimental::LiveEye,2>& eyes);
}
