#include "dx.h"
#include <glm\glm.hpp>
#define PAR_SHAPES_IMPLEMENTATION
#include <par_shapes.h>
#include <vector>
static const D3D12_HEAP_PROPERTIES uploadHeapProps = {
	D3D12_HEAP_TYPE_UPLOAD,
	D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
	D3D12_MEMORY_POOL_UNKNOWN,
	0,
	0,
};

static const D3D12_HEAP_PROPERTIES defaultHeapProps = {
	D3D12_HEAP_TYPE_DEFAULT,
	D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
	D3D12_MEMORY_POOL_UNKNOWN,
	0,
	0,
};

void CreateBuffer(ID3D12Device* device, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, D3D12_HEAP_PROPERTIES heap, ComPtr<ID3D12Resource>& resourceOut) {
	D3D12_RESOURCE_DESC bufDesc = {};
	bufDesc.Alignment = 0;
	bufDesc.DepthOrArraySize = 1;
	bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufDesc.Flags = flags;
	bufDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufDesc.Height = 1;
	bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufDesc.MipLevels = 1;
	bufDesc.SampleDesc.Count = 1;
	bufDesc.SampleDesc.Quality = 0;
	bufDesc.Width = size;
	HR(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufDesc, state, nullptr, IID_PPV_ARGS(&resourceOut)), "Create Commited resource");
}

UINT DXEngine::AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor)
{
	auto descriptorHeapCpuBase = m_CBVSRVUAVHeap->GetCPUDescriptorHandleForHeapStart();
	UINT index = m_AllocatedDescriptors++;
	*cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCpuBase, index, m_DescSize);
	return index;
}

WRAPPED_GPU_POINTER DXEngine::CreateWrappedPointer(ID3D12RaytracingFallbackDevice* rtdevice,  ID3D12Resource* resource, UINT bufferNumElements)
{

	D3D12_UNORDERED_ACCESS_VIEW_DESC rawBufferUavDesc = {};
	rawBufferUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	rawBufferUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
	rawBufferUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	rawBufferUavDesc.Buffer.NumElements = bufferNumElements;

	D3D12_CPU_DESCRIPTOR_HANDLE bottomLevelDescriptor;

	// Only compute fallback requires a valid descriptor index when creating a wrapped pointer.
	UINT descriptorHeapIndex = 0;
	if (!rtdevice->UsingRaytracingDriver())
	{
		descriptorHeapIndex = AllocateDescriptor(&bottomLevelDescriptor);
		m_Device->CreateUnorderedAccessView(resource, nullptr, &rawBufferUavDesc, bottomLevelDescriptor);
	}
	WRAPPED_GPU_POINTER gpu_pointer = rtdevice->GetWrappedPointerSimple(descriptorHeapIndex, resource->GetGPUVirtualAddress());
	return gpu_pointer;
}

void DXEngine::ExecuteCommandList() {
	HR(m_CmdList->Close(), "Closing CmdList");
	ID3D12CommandList *commandLists[] = { m_CmdList.Get() };
	m_CmdQueue->ExecuteCommandLists(ARRAYSIZE(commandLists), commandLists);
}

void DXEngine::WaitForGPU() {
	if (m_CmdQueue && m_SwapFence)
	{
		// Schedule a Signal command in the GPU queue.
		UINT64 fenceValue = m_SwapFenceValue[m_CurrentFrame];
		if (SUCCEEDED(m_CmdQueue->Signal(m_SwapFence.Get(), fenceValue)))
		{
			// Wait until the Signal has been processed.
			if (SUCCEEDED(m_SwapFence->SetEventOnCompletion(fenceValue, m_SwapEvent)))
			{
				WaitForSingleObjectEx(m_SwapEvent, INFINITE, FALSE);

				// Increment the fence value for the current frame.
				m_SwapFenceValue[m_CurrentFrame]++;
			}
		}
	}
}

void DXEngine::InitDXR() {
	//create vbo
	par_shapes_mesh* sphereMesh = par_shapes_create_subdivided_sphere(3);
	std::vector<glm::vec3> vertices;
	for (int t = 0; t < sphereMesh->ntriangles * 3; t += 3) {
		for (int i = 0; i < 3; ++i) {
			uint16_t index = sphereMesh->triangles[t + i];
			glm::vec3 v(sphereMesh->points[index * 3 + 0], sphereMesh->points[index * 3 + 1], sphereMesh->points[index * 3 + 2]);
			vertices.push_back(v);
		}
	}

	CreateBuffer(m_Device.Get(), sizeof(glm::vec3) * vertices.size(), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, uploadHeapProps, m_VBO);
	uint8_t* pData;
	m_VBO->Map(0, nullptr, (void**)&pData);
	memcpy(pData, vertices.data(), sizeof(glm::vec3) * vertices.size());
	m_VBO->Unmap(0, nullptr);

	D3D12CreateRaytracingFallbackDevice(m_Device.Get(), CreateRaytracingFallbackDeviceFlags::None, 0, IID_PPV_ARGS(&m_RTDevice));
	m_RTDevice->QueryRaytracingCommandList(m_CmdList.Get(), IID_PPV_ARGS(&m_RTCmdList));
	ID3D12DescriptorHeap *pDescriptorHeaps[] = { m_CBVSRVUAVHeap.Get() };
	m_RTCmdList->SetDescriptorHeaps(1, pDescriptorHeaps);
	uint32_t numBuffer = 0;
	//create blas
	{
		D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
		geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geomDesc.Triangles.VertexBuffer.StartAddress = m_VBO->GetGPUVirtualAddress();
		geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(glm::vec3);
		geomDesc.Triangles.VertexCount = vertices.size();
		geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

		D3D12_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_DESC prebuildDesc = {};
		prebuildDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		prebuildDesc.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		prebuildDesc.NumDescs = 1;
		prebuildDesc.pGeometryDescs = &geomDesc;
		prebuildDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
		m_RTDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildDesc, &info);
		numBuffer = static_cast<uint32_t>(info.ResultDataMaxSizeInBytes) / sizeof(uint32_t);

		CreateBuffer(m_Device.Get(), info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, defaultHeapProps, m_BLAS.scratch);
		CreateBuffer(m_Device.Get(), info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, m_RTDevice->GetAccelerationStructureResourceState(), defaultHeapProps, m_BLAS.result);

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasDesc = {};
		blasDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		blasDesc.pGeometryDescs = &geomDesc;
		blasDesc.DestAccelerationStructureData.StartAddress = m_BLAS.result->GetGPUVirtualAddress();
		blasDesc.DestAccelerationStructureData.SizeInBytes = info.ResultDataMaxSizeInBytes;
		blasDesc.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		blasDesc.NumDescs = 1;
		blasDesc.ScratchAccelerationStructureData.StartAddress = m_BLAS.scratch->GetGPUVirtualAddress();
		blasDesc.ScratchAccelerationStructureData.SizeInBytes = info.ScratchDataSizeInBytes;
		blasDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		
		m_RTCmdList->BuildRaytracingAccelerationStructure(&blasDesc);
		m_CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_BLAS.result.Get()));
	}
	//create tlas
	{
		D3D12_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_DESC prebuildDesc = {};
		prebuildDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		prebuildDesc.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		prebuildDesc.NumDescs = 1;
		prebuildDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
		m_RTDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildDesc, &info);

		CreateBuffer(m_Device.Get(), info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, defaultHeapProps, m_TLAS.scratch);
		CreateBuffer(m_Device.Get(), info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, m_RTDevice->GetAccelerationStructureResourceState(), defaultHeapProps, m_TLAS.result);
		CreateBuffer(m_Device.Get(), sizeof(D3D12_RAYTRACING_FALLBACK_INSTANCE_DESC), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, uploadHeapProps, m_TLAS.instanceDesc);
		
		D3D12_RAYTRACING_FALLBACK_INSTANCE_DESC* instanceDesc;
		m_TLAS.instanceDesc->Map(0, nullptr, (void**)&instanceDesc);
		instanceDesc->InstanceID = 0;
		instanceDesc->InstanceContributionToHitGroupIndex = 0;
		instanceDesc->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		glm::mat4 m(1);
		memcpy(instanceDesc->Transform, &m, sizeof(instanceDesc->Transform));
		WRAPPED_GPU_POINTER gpu_pointer = CreateWrappedPointer(m_RTDevice.Get(), m_BLAS.result.Get(), numBuffer);
		instanceDesc->AccelerationStructure = gpu_pointer;
		instanceDesc->InstanceMask = 0xFF;
		m_TLAS.instanceDesc->Unmap(0, nullptr);

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasDesc = {};
		tlasDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		tlasDesc.InstanceDescs = m_TLAS.instanceDesc->GetGPUVirtualAddress();
		tlasDesc.DestAccelerationStructureData.StartAddress = m_TLAS.result->GetGPUVirtualAddress();
		tlasDesc.DestAccelerationStructureData.SizeInBytes = info.ResultDataMaxSizeInBytes;
		tlasDesc.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		tlasDesc.NumDescs = 1;
		tlasDesc.ScratchAccelerationStructureData.StartAddress = m_TLAS.scratch->GetGPUVirtualAddress();
		tlasDesc.ScratchAccelerationStructureData.SizeInBytes = info.ScratchDataSizeInBytes;
		tlasDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

		m_RTCmdList->BuildRaytracingAccelerationStructure(&tlasDesc);
	}

	ExecuteCommandList();
	WaitForGPU();

	m_CmdAllocator[m_CurrentFrame]->Reset();
	HR(m_CmdList->Reset(m_CmdAllocator[m_CurrentFrame].Get(), nullptr), "Reset Command list");

	//create pipeline
	m_PipelineState = CompileRTPipeline( m_RTDevice.Get(), L"shader/raytracing.hlsl");
	//create shader table
	m_ShaderTable = CompileShaderTable(m_Device.Get(), m_RTDevice.Get(), m_PipelineState);
}

void DXEngine::Init(HWND hWnd, int w, int h) {
	//Create Device
	//currently only fallback will work since we do not have a DXR compatible device
	UUID experimentalFeaturesSMandDXR[] = { D3D12ExperimentalShaderModels /*,D3D12RaytracingPrototype*/ };
	HRESULT hr = D3D12EnableExperimentalFeatures(1, experimentalFeaturesSMandDXR, nullptr, nullptr);

	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&m_Debug)))) {
		//m_Debug->EnableDebugLayer();
	}

	HR(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_Device)), "CreateDevice");
	//Create command queue
	D3D12_COMMAND_QUEUE_DESC cqDesc = {};
	cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	HR(m_Device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&m_CmdQueue)), "CreateCommandQueue");
	//create command allocator
	for (int i = 0; i < BUFFER_COUNT; ++i) {
		HR(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_CmdAllocator[i])), "CreateCommandAllocator");
	}
	HR(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CmdAllocator[0].Get(), nullptr, IID_PPV_ARGS(&m_CmdList)), "CreateGraphicsCommandList");
	//m_CmdList->Close();//cmdlists start in open mode
	//Create swapchain
	IDXGIFactory2* dxgiFact;
	HR(CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), (void**)&dxgiFact),"CreateDXGIFactory");
	HR(dxgiFact->QueryInterface(IID_PPV_ARGS(&m_DXGIFactory)), "QueryFactory");
	IDXGISwapChain1* tempSC;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = BUFFER_COUNT;
	swapChainDesc.Width = w;
	swapChainDesc.Height = h;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;
	HR(m_DXGIFactory->CreateSwapChainForHwnd(m_CmdQueue.Get(), hWnd, &swapChainDesc, nullptr, nullptr, &tempSC), "CreateSwapchain");
	HR(tempSC->QueryInterface(IID_PPV_ARGS(&m_Swapchain)),"QuerySwapchain");
	//Create RTV Descriptor heap
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
	descHeapDesc.NodeMask = 0;
	descHeapDesc.NumDescriptors = 2;
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HR(m_Device->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&m_RTVHeap)), "CreateDescriptorHeap");
	m_RTVDescSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	
	//Create RTVs
	for (int i = 0; i < BUFFER_COUNT; ++i) {
		m_Swapchain->GetBuffer(i, IID_PPV_ARGS(&m_SwapBuffers[i]));
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;
		rtvDesc.Texture2D.MipSlice = 0;
		rtvDesc.Texture2D.PlaneSlice = 0;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		m_Device->CreateRenderTargetView(m_SwapBuffers[i].Get(), &rtvDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE(m_RTVHeap->GetCPUDescriptorHandleForHeapStart(), i * m_RTVDescSize));
	}

	//create CBV/SRV/UAV heap
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descHeapDesc.NumDescriptors = 100000;
	HR(m_Device->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&m_CBVSRVUAVHeap)), "CreateDescriptorHeap");
	m_DescSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	//Create fence
	HR(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_SwapFence)),"CreateFence");
	HR(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_InitFence)), "CreateFence");
	m_SwapEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	m_CurrentFrame = m_Swapchain->GetCurrentBackBufferIndex();

	InitDXR();
}

void DXEngine::Render() {
	//set swapbuffer state to render target
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = m_SwapBuffers[m_CurrentFrame].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.Subresource = 0;
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	m_CmdList->ResourceBarrier(1, &barrier);
	//clear
	const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	m_CmdList->ClearRenderTargetView(CD3DX12_CPU_DESCRIPTOR_HANDLE(m_RTVHeap->GetCPUDescriptorHandleForHeapStart(), m_CurrentFrame * m_RTVDescSize), clearColor, 0, nullptr);
	//Raytrace!
	D3D12_FALLBACK_DISPATCH_RAYS_DESC dispatchDesc = {};
	m_RTCmdList->SetDescriptorHeaps(1, m_CBVSRVUAVHeap.GetAddressOf());
	m_CmdList->SetComputeRootDescriptorTable(0, m_CBVSRVUAVHeap->GetGPUDescriptorHandleForHeapStart());

	//set state to present
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	m_CmdList->ResourceBarrier(1, &barrier);
	//submit cmdlist
	ExecuteCommandList();
	//present
	HRESULT hr = m_Swapchain->Present(1, 0);

	// Schedule a Signal command in the queue.
	const UINT64 currentFenceValue = m_SwapFenceValue[m_CurrentFrame];
	HR(m_CmdQueue->Signal(m_SwapFence.Get(), currentFenceValue), "Signal fence");
	// Update the back buffer index.
	m_CurrentFrame = m_Swapchain->GetCurrentBackBufferIndex();
	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (m_SwapFence->GetCompletedValue() < m_SwapFenceValue[m_CurrentFrame]){
		HR(m_SwapFence->SetEventOnCompletion(m_SwapFenceValue[m_CurrentFrame], m_SwapEvent), "Set event");
		WaitForSingleObjectEx(m_SwapEvent, INFINITE, FALSE);
	}
	// Set the fence value for the next frame.
	m_SwapFenceValue[m_CurrentFrame] = currentFenceValue + 1;

	m_CmdAllocator[m_CurrentFrame]->Reset();
	HR(m_CmdList->Reset(m_CmdAllocator[m_CurrentFrame].Get(), nullptr), "Reset Command list");
}