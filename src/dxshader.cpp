#include "dxshader.h"
#include <dx/dxcapi.use.h>
#include <dx/D3D12RaytracingPrototypeHelpers.hpp>
#include <dx/d3dx12.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <glm/glm.hpp> 
using namespace dxc;

ComPtr<IDxcBlob> CompileLibrary(const wchar_t* filename) {
	dxc::DxcDllSupport dllSupport;
	dllSupport.Initialize();
	IDxcCompiler* dxcCompiler;
	IDxcLibrary* dxcLibrary;
	dllSupport.CreateInstance(CLSID_DxcCompiler, &dxcCompiler);
	dllSupport.CreateInstance(CLSID_DxcLibrary, &dxcLibrary);

	std::ifstream file(filename);
	std::stringstream ss;
	ss << file.rdbuf();
	std::string str = ss.str();
	IDxcBlobEncoding* textBlob;
	dxcLibrary->CreateBlobWithEncodingFromPinned((LPBYTE)str.data(), str.size(), 0, &textBlob);
	IDxcOperationResult* result;
	dxcCompiler->Compile(textBlob, filename, L"", L"lib_6_1", nullptr, 0, nullptr, 0, nullptr, &result);
	HRESULT status;
	result->GetStatus(&status);
	if (FAILED(status)) {
		IDxcBlobEncoding* error;
		result->GetErrorBuffer(&error);
		printf("ShaderCompiler: Error! \n%s\n", (char*)error->GetBufferPointer());
	}

	ComPtr<IDxcBlob> ret;
	status = result->GetResult(&ret);
	return ret;
}

struct DXILLib {
	DXILLib(ID3DBlob* pblob, const WCHAR* entryPoint[], uint32_t entryPointCount) {
		stateSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		stateSubObject.pDesc = &dxilLibDesc;
		exportDescs.resize(entryPointCount);
		exportNames.resize(entryPointCount);
		if (pblob) {
			dxilLibDesc.DXILLibrary.pShaderBytecode = pblob->GetBufferPointer();
			dxilLibDesc.DXILLibrary.BytecodeLength = pblob->GetBufferSize();
			dxilLibDesc.NumExports = entryPointCount;
			dxilLibDesc.pExports = exportDescs.data();

			for (uint32_t i = 0; i < entryPointCount; ++i) {
				exportNames[i] = entryPoint[i];
				exportDescs[i].Name = exportNames[i].c_str();
				exportDescs[i].Flags = D3D12_EXPORT_FLAG_NONE;
				exportDescs[i].ExportToRename = nullptr;
			}
		}
	}
	DXILLib() : DXILLib(nullptr, nullptr, 0) {}

	D3D12_DXIL_LIBRARY_DESC dxilLibDesc = {};
	D3D12_STATE_SUBOBJECT stateSubObject{};
	ID3DBlob* blob;
	std::vector<D3D12_EXPORT_DESC> exportDescs;
	std::vector<std::wstring> exportNames;
};

void CreateRootSigns(ID3D12RaytracingFallbackDevice* device, ComPtr<ID3D12RootSignature>* localRootSign, ComPtr<ID3D12RootSignature>* globalRootSign) {
	// Global Root Signature
	{
		CD3DX12_DESCRIPTOR_RANGE UAVDescriptor;
		UAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
		CD3DX12_ROOT_PARAMETER rootParameters[2];
		rootParameters[0].InitAsDescriptorTable(1, &UAVDescriptor);
		rootParameters[1].InitAsShaderResourceView(0);
		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
		ComPtr<ID3DBlob> signBlob, errorBlob;
		HR(device->D3D12SerializeRootSignature(&globalRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signBlob, &errorBlob), "Failed to serialize global root signature");
		HR(device->CreateRootSignature(0, signBlob->GetBufferPointer(), signBlob->GetBufferSize(), IID_PPV_ARGS(&(*globalRootSign))), "Failed to create global root signature");
	}

	// Local Root Signature
	{
		CD3DX12_DESCRIPTOR_RANGE UAVDescriptor;
		CD3DX12_ROOT_PARAMETER rootParameters[1];
		rootParameters[0].InitAsConstants(sizeof(glm::vec4) * 2, 0, 0);
		CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
		localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		ComPtr<ID3DBlob> signBlob, errorBlob;
		HR(device->D3D12SerializeRootSignature(&localRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signBlob, &errorBlob), "Failed to serialize local root signature");
		HR(device->CreateRootSignature(0, signBlob->GetBufferPointer(), signBlob->GetBufferSize(), IID_PPV_ARGS(&(*localRootSign))), "Failed to create local root signature");
	}
}

RaytracingPipeline CompileRTPipeline(ID3D12RaytracingFallbackDevice* rtDevice, const wchar_t* filename) {
	ComPtr<ID3D12RootSignature> globalRootSign;
	ComPtr<ID3D12RootSignature> localRootSign;

	CreateRootSigns(rtDevice, &localRootSign, &globalRootSign);

	ComPtr<IDxcBlob> codeBlob = CompileLibrary(filename);

	CD3D12_STATE_OBJECT_DESC stateObjectDesc{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};
	auto lib = stateObjectDesc.CreateSubobject<CD3D12_DXIL_LIBRARY_SUBOBJECT>();
	D3D12_SHADER_BYTECODE libDXIL{};
	libDXIL.pShaderBytecode = codeBlob->GetBufferPointer();
	libDXIL.BytecodeLength = codeBlob->GetBufferSize();
	lib->SetDXILLibrary(&libDXIL);
	lib->DefineExport(rayGenStr);
	lib->DefineExport(missStr);
	lib->DefineExport(chsStr);

	auto hitGroup = stateObjectDesc.CreateSubobject<CD3D12_HIT_GROUP_SUBOBJECT>();
	hitGroup->SetClosestHitShaderImport(chsStr);
	hitGroup->SetHitGroupExport(hitGroupStr);

	auto shaderConfig = stateObjectDesc.CreateSubobject<CD3D12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
	shaderConfig->Config(4, 8);

	auto shaderConfigAssociation = stateObjectDesc.CreateSubobject<CD3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
	shaderConfigAssociation->SetSubobjectToAssociate(*shaderConfig);
	shaderConfigAssociation->AddExport(rayGenStr);
	shaderConfigAssociation->AddExport(missStr);
	shaderConfigAssociation->AddExport(hitGroupStr);

	auto localRootSignature = stateObjectDesc.CreateSubobject<CD3D12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
	localRootSignature->SetRootSignature(localRootSign.Get());

	auto rootSignatureAssociation = stateObjectDesc.CreateSubobject<CD3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
	rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
	rootSignatureAssociation->AddExport(rayGenStr);
	rootSignatureAssociation->AddExport(missStr);
	rootSignatureAssociation->AddExport(hitGroupStr);

	auto globalRootSignature = stateObjectDesc.CreateSubobject<CD3D12_ROOT_SIGNATURE_SUBOBJECT>();
	globalRootSignature->SetRootSignature(globalRootSign.Get());

	auto pipelineConfig = stateObjectDesc.CreateSubobject<CD3D12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
	pipelineConfig->Config(2);

	RaytracingPipeline rtPipe;
	HRESULT hr = rtDevice->CreateStateObject(stateObjectDesc, IID_PPV_ARGS(&rtPipe.pipelineState));
	rtPipe.localRootSig = localRootSign;
	rtPipe.globalRootSig = globalRootSign;
	return rtPipe;
}
inline UINT Align(UINT size, UINT alignment)
{
	return (size + (alignment - 1)) & ~(alignment - 1);
}

void AllocateTableToUploadBuffer(ID3D12Device* device, void* shaderID, uint32_t shaderIDSize, void* rootArgs, uint32_t rootArgsSize, ComPtr<ID3D12Resource>& resourceOut ) {
	const auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	UINT size = Align(shaderIDSize + rootArgsSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
	device->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resourceOut));
	uint8_t *pMappedData;
	resourceOut->Map(0, nullptr, reinterpret_cast<void**>(&pMappedData));
	memcpy(pMappedData, shaderID, shaderIDSize);
	memcpy(pMappedData + shaderIDSize, rootArgs, rootArgsSize);
	resourceOut->Unmap(0, nullptr);
}


ShaderTable CompileShaderTable(ID3D12Device* device, ID3D12RaytracingFallbackDevice* rtDevice, RaytracingPipeline& rtPipe) {
	void* rayGenID = rtPipe.pipelineState->GetShaderIdentifier(rayGenStr);
	void* missID = rtPipe.pipelineState->GetShaderIdentifier(missStr);
	void* hitGroupID = rtPipe.pipelineState->GetShaderIdentifier(hitGroupStr);
	uint32_t shaderIDSize = rtDevice->GetShaderIdentifierSize();

	struct RootArgs {
		glm::vec4 viewport;
		glm::vec4 stencil;
	} rootArgs;
	rootArgs.viewport = { -1, 1, -1, 1 };
	rootArgs.stencil = rootArgs.viewport;

	ShaderTable table;
	AllocateTableToUploadBuffer(device, rayGenID, shaderIDSize, &rootArgs, sizeof(rootArgs), table.rayGenTable);
	AllocateTableToUploadBuffer(device, missID, shaderIDSize, &rootArgs, sizeof(rootArgs), table.missTable);
	AllocateTableToUploadBuffer(device, hitGroupID, shaderIDSize, &rootArgs, sizeof(rootArgs), table.hitGroupTable);
	return table;
}