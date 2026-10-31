#pragma once
#include "bridge.hpp"
#include <d3d12.h>
#include <memory>
#include <string>
namespace circle::fg {
bool fidelityRequested();
bool fidelityAvailable(std::string& reason);
class FidelityFeature {
 struct Impl;
 std::unique_ptr<Impl> impl;
public:
 FidelityFeature();
 ~FidelityFeature();
 bool evaluate(ID3D12Device*,ID3D12GraphicsCommandList*,ID3D12Resource* color,
               ID3D12Resource* depth,ID3D12Resource* motion,ID3D12Resource* output,
               const Capture&,bool reset,std::string& reason);
};
}
