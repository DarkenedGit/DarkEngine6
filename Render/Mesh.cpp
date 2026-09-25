#include "Render/Mesh.h"
#include "Render/Renderer.h"
#include "Core/Log.h"

#include <cmath>
#include <cstring>
#include <iterator>
#include <vector>

namespace Dark
{
	using namespace Math;
	namespace
	{
		bool FailedHr(HRESULT hr, const char* what)
		{
			if (SUCCEEDED(hr))
				return false;
			DE_LOG_ERROR("{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
			return true;
		}

		ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, uint64_t size, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initialState)
		{
			D3D12_HEAP_PROPERTIES heap{};
			heap.Type = heapType;
			D3D12_RESOURCE_DESC desc{};
			desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width            = size;
			desc.Height           = 1;
			desc.DepthOrArraySize = 1;
			desc.MipLevels        = 1;
			desc.Format           = DXGI_FORMAT_UNKNOWN;
			desc.SampleDesc       = { 1, 0 };
			desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			desc.Flags            = D3D12_RESOURCE_FLAG_NONE;
			ComPtr<ID3D12Resource> res;
			if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&res)), "CreateCommittedResource buffer"))
				return nullptr;
			return res;
		}

	} // namespace

	bool Mesh::uploadBuffers(Renderer& renderer, const void* verts, uint64_t vbBytes, uint32_t stride, const uint32_t* indices, uint32_t indexCount, Mesh& out)
	{
			ID3D12Device* device = renderer.device();
			if (!device)
			{
				DE_LOG_ERROR("Mesh::tryCreate: null device");
				return false;
			}
			const uint64_t ibBytes = static_cast<uint64_t>(indexCount) * sizeof(uint32_t);
			out.m_vertexCount = static_cast<uint32_t>(vbBytes / stride);
			out.m_indexCount  = indexCount;
			out.m_vb = CreateBuffer(device, vbBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
			out.m_ib = CreateBuffer(device, ibBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
			if (!out.m_vb || !out.m_ib)
			{
				out = Mesh{};
				return false;
			}
			ComPtr<ID3D12Resource> uploadVb = CreateBuffer(device, vbBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
			ComPtr<ID3D12Resource> uploadIb = CreateBuffer(device, ibBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
			if (!uploadVb || !uploadIb)
			{
				out = Mesh{};
				return false;
			}
			{
				void* mapped = nullptr;
				if (FailedHr(uploadVb->Map(0, nullptr, &mapped), "Map VB upload"))
				{
					out = Mesh{};
					return false;
				}
				memcpy(mapped, verts, static_cast<size_t>(vbBytes));
				uploadVb->Unmap(0, nullptr);
			}
			{
				void* mapped = nullptr;
				if (FailedHr(uploadIb->Map(0, nullptr, &mapped), "Map IB upload"))
				{
					out = Mesh{};
					return false;
				}
				memcpy(mapped, indices, static_cast<size_t>(ibBytes));
				uploadIb->Unmap(0, nullptr);
			}
			if (!renderer.submitBufferCopies(out.m_vb.Get(), uploadVb.Get(), vbBytes, out.m_ib.Get(), uploadIb.Get(), ibBytes))
			{
				out = Mesh{};
				return false;
			}
			renderer.deferRelease(uploadVb.Get());
			renderer.deferRelease(uploadIb.Get());
			out.m_vbv.BufferLocation = out.m_vb->GetGPUVirtualAddress();
			out.m_vbv.StrideInBytes  = stride;
			out.m_vbv.SizeInBytes    = static_cast<UINT>(vbBytes);
			out.m_ibv.BufferLocation = out.m_ib->GetGPUVirtualAddress();
			out.m_ibv.Format         = DXGI_FORMAT_R32_UINT;
			out.m_ibv.SizeInBytes    = static_cast<UINT>(ibBytes);
			DE_LOG_INFO("Mesh: uploaded {} verts, {} indices (stride {})", out.m_vertexCount, out.m_indexCount, stride);
			return true;
	}

	uint32_t packBlendWeightsUnorm8(float w0, float w1, float w2, float w3)
	{
		float w[4] = { w0, w1, w2, w3 };
		const float sum = w[0] + w[1] + w[2] + w[3];
		if (sum <= 1.0e-8f)
			return 255u;
		w[0] /= sum;
		w[1] /= sum;
		w[2] /= sum;
		w[3] /= sum;
		int b[4];
		int total = 0;
		int maxI = 0;
		for (int i = 0; i < 4; ++i)
		{
			int v = static_cast<int>(floorf(w[i] * 255.0f + 0.5f));
			if (v < 0)
				v = 0;
			if (v > 255)
				v = 255;
			b[i] = v;
			total += v;
			if (w[i] > w[maxI])
				maxI = i;
		}
		b[maxI] += 255 - total;
		if (b[maxI] < 0)
			b[maxI] = 0;
		if (b[maxI] > 255)
			b[maxI] = 255;
		return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16)
			 | (static_cast<uint32_t>(b[3]) << 24);
	}

	bool Mesh::tryCreate(Renderer& renderer, const MeshData& data, Mesh& out)
	{
		out = Mesh{};
		if (data.positions.empty() || data.indices.empty())
		{
			DE_LOG_ERROR("Mesh::tryCreate: empty mesh data");
			return false;
		}
		if (data.normals.size() != data.positions.size())
		{
			DE_LOG_ERROR("Mesh::tryCreate: normals size mismatch");
			return false;
		}
		const size_t nVerts = data.positions.size();
		std::vector<Vector4f> tangents;
		if (data.tangents.size() == nVerts)
			tangents = data.tangents;
		else if (!detail::computeTangents(data, tangents))
			tangents.assign(nVerts, Vector4f(1.0f, 0.0f, 0.0f, 1.0f));
		std::vector<MeshVertex> verts(nVerts);
		for (size_t i = 0; i < nVerts; ++i)
		{
			verts[i].point = data.positions[i];
			verts[i].normal = data.normals[i];
			verts[i].uv = (i < data.uvs.size()) ? data.uvs[i] : Vect2f::ZERO;
			verts[i].tangent = tangents[i];
		}
		return uploadBuffers(renderer, verts.data(), verts.size() * sizeof(MeshVertex), sizeof(MeshVertex), data.indices.data(), static_cast<uint32_t>(data.indices.size()), out);
	}

	bool Mesh::tryCreateSkinned(Renderer& renderer, const MeshData& data, Mesh& out)
	{
		out = Mesh{};
		if (data.positions.empty() || data.indices.empty())
		{
			DE_LOG_ERROR("Mesh::tryCreateSkinned: empty mesh data");
			return false;
		}
		if (data.normals.size() != data.positions.size() || data.jointPacked.size() != data.positions.size()
			|| data.weights.size() != data.positions.size())
		{
			DE_LOG_ERROR("Mesh::tryCreateSkinned: attribute size mismatch");
			return false;
		}
		const size_t nVerts = data.positions.size();
		std::vector<Vector4f> tangents;
		if (data.tangents.size() == nVerts)
			tangents = data.tangents;
		else if (!detail::computeTangents(data, tangents))
			tangents.assign(nVerts, Vector4f(1.0f, 0.0f, 0.0f, 1.0f));
		std::vector<SkinnedMeshVertex> verts(nVerts);
		for (size_t i = 0; i < nVerts; ++i)
		{
			verts[i].point = data.positions[i];
			verts[i].normal = data.normals[i];
			verts[i].uv = (i < data.uvs.size()) ? data.uvs[i] : Vect2f::ZERO;
			verts[i].tangent = tangents[i];
			verts[i].joints = data.jointPacked[i];
			const auto& w = data.weights[i];
			verts[i].packedWeights = packBlendWeightsUnorm8(w.x, w.y, w.z, w.w);
			verts[i].pad[0] = 0;
			verts[i].pad[1] = 0;
		}
		return uploadBuffers(renderer, verts.data(), verts.size() * sizeof(SkinnedMeshVertex), sizeof(SkinnedMeshVertex), data.indices.data(), static_cast<uint32_t>(data.indices.size()), out);
	}

	Mesh Mesh::Create(Renderer& renderer, const MeshData& data)
	{
		Mesh mesh;
		if (!tryCreate(renderer, data, mesh))
			return Mesh{};
		return mesh;
	}

	void Mesh::deferRelease(Renderer& renderer)
	{
		renderer.deferRelease(m_vb.Get());
		renderer.deferRelease(m_ib.Get());
		*this = Mesh{};
	}

	void GpuMeshRetire::push(Mesh&& mesh)
	{
		if (!mesh.valid())
			return;
		Item item;
		item.mesh   = std::move(mesh);
		item.frames = kFrames;
		m_items.push_back(std::move(item));
	}

	void GpuMeshRetire::takeFrom(GpuMeshRetire& other)
	{
		if (other.m_items.empty())
			return;
		m_items.insert(m_items.end(), std::make_move_iterator(other.m_items.begin()), std::make_move_iterator(other.m_items.end()));
		other.m_items.clear();
	}

	void GpuMeshRetire::tick()
	{
		for (size_t i = 0; i < m_items.size();)
		{
			if (--m_items[i].frames <= 0)
			{
				m_items[i] = std::move(m_items.back());
				m_items.pop_back();
			}
			else
				++i;
		}
	}

	void Mesh::draw(ID3D12GraphicsCommandList* cmd, bool pointList) const
	{
		if (!cmd || !valid())
			return;
		cmd->IASetVertexBuffers(0, 1, &m_vbv);
		if (pointList)
		{
			cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
			cmd->DrawInstanced(m_vertexCount, 1, 0, 0);
			return;
		}
		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmd->IASetIndexBuffer(&m_ibv);
		cmd->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);
	}

	void Mesh::drawInstanced(ID3D12GraphicsCommandList* cmd, uint32_t instanceCount) const
	{
		if (!cmd || !valid() || instanceCount == 0)
			return;
		cmd->IASetVertexBuffers(0, 1, &m_vbv);
		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmd->IASetIndexBuffer(&m_ibv);
		cmd->DrawIndexedInstanced(m_indexCount, instanceCount, 0, 0, 0);
	}
} // namespace Dark
