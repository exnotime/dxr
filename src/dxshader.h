#pragma once
#include <dx/d3d12_1.h>
#include <wrl/client.h>
#include <dx/D3D12RaytracingFallback.h>
using Microsoft::WRL::ComPtr;
#define HR(x, s) if(x != S_OK) {MessageBoxA(nullptr, s, "Failure", MB_OK);}

struct RaytracingPipeline {
	ComPtr<ID3D12RaytracingFallbackStateObject> pipelineState;
	ComPtr<ID3D12RootSignature> localRootSig;
	ComPtr<ID3D12RootSignature> globalRootSig;
};

struct ShaderTable {
	ComPtr<ID3D12Resource> rayGenTable;
	ComPtr<ID3D12Resource> missTable;
	ComPtr<ID3D12Resource> hitGroupTable;
};

static wchar_t* rayGenStr = L"MyRaygenShader";
static wchar_t* missStr = L"MyMissShader";
static wchar_t* chsStr = L"MyClosestHitShader";
static wchar_t* hitGroupStr = L"MyHitGroup";

RaytracingPipeline CompileRTPipeline(ID3D12RaytracingFallbackDevice* rtDevice, const wchar_t* filename);
ShaderTable CompileShaderTable(ID3D12Device* device, ID3D12RaytracingFallbackDevice* rtDevice,RaytracingPipeline& rtPipe);
