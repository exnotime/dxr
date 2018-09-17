#pragma once
#include <dxgi1_6.h>
#include <dx/d3d12.h>
#include <dx/d3d12_1.h>
#include <dx/d3dx12.h>
#include <dx/D3D12RaytracingFallback.h>
#include <wrl/client.h>
#include <stdint.h>
#include "dxshader.h"
using Microsoft::WRL::ComPtr;
#define HR(x, s) if(x != S_OK) {MessageBoxA(nullptr, s, "Failure", MB_OK);}
#define BUFFER_COUNT 2
class DXEngine {
public:
	DXEngine(){}
	~DXEngine(){}

	void Init(HWND hWnd, int w, int h);
	void Render();
private:
	void InitDXR();
	uint32_t AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor);
	WRAPPED_GPU_POINTER CreateWrappedPointer(ID3D12RaytracingFallbackDevice* rtdevice, ID3D12Resource* resource, UINT bufferNumElements);
	void ExecuteCommandList();
	void WaitForGPU();
private:
	ComPtr<ID3D12Debug> m_Debug;
	ComPtr<ID3D12Device3> m_Device;
	ComPtr<IDXGISwapChain4> m_Swapchain;
	ComPtr<IDXGIFactory4> m_DXGIFactory;
	ComPtr<ID3D12CommandQueue> m_CmdQueue;
	ComPtr<ID3D12CommandAllocator> m_CmdAllocator[BUFFER_COUNT];
	ComPtr<ID3D12GraphicsCommandList> m_CmdList;
	ComPtr<ID3D12Resource> m_SwapBuffers[BUFFER_COUNT];
	ComPtr<ID3D12Fence> m_SwapFence;
	uint64_t m_SwapFenceValue[BUFFER_COUNT];
	HANDLE m_SwapEvent;
	ComPtr<ID3D12DescriptorHeap> m_RTVHeap;
	uint32_t m_RTVDescSize;
	uint32_t m_CurrentFrame; 

	ComPtr<ID3D12DescriptorHeap> m_CBVSRVUAVHeap;
	uint32_t m_AllocatedDescriptors = 0;
	uint32_t m_DescSize;
	//DXR stuff
	struct ASBuffer {
		ComPtr<ID3D12Resource> scratch;
		ComPtr<ID3D12Resource> result;
		ComPtr<ID3D12Resource> instanceDesc;
	};
	ComPtr<ID3D12RaytracingFallbackDevice> m_RTDevice;
	ComPtr<ID3D12RaytracingFallbackCommandList> m_RTCmdList;
	ComPtr<ID3D12Resource> m_VBO;
	ASBuffer m_BLAS;
	ASBuffer m_TLAS;
	ComPtr<ID3D12Fence> m_InitFence;
	RaytracingPipeline m_PipelineState;
	ShaderTable m_ShaderTable;
};