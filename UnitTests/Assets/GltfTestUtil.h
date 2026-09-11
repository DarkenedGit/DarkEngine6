#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Dark::GltfTest
{
	inline std::filesystem::path writeTempGltf(const std::string& name, const std::string& json)
	{
		const auto dir = std::filesystem::temp_directory_path() / "de_gltf_tests";
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		const auto path = dir / name;
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << json;
		out.close();
		return path;
	}

	inline void appendU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }

	inline void appendU16(std::vector<uint8_t>& b, uint16_t v)
	{
		b.push_back(static_cast<uint8_t>(v & 0xff));
		b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
	}

	inline void appendF32(std::vector<uint8_t>& b, float v)
	{
		uint32_t u = 0;
		std::memcpy(&u, &v, 4);
		b.push_back(static_cast<uint8_t>(u & 0xff));
		b.push_back(static_cast<uint8_t>((u >> 8) & 0xff));
		b.push_back(static_cast<uint8_t>((u >> 16) & 0xff));
		b.push_back(static_cast<uint8_t>((u >> 24) & 0xff));
	}

	inline void appendMat4Translation(std::vector<uint8_t>& b, float x, float y, float z)
	{
		const float m[16] = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			x, y, z, 1
		};
		for (int i = 0; i < 16; ++i)
			appendF32(b, m[i]);
	}

	inline std::string base64Encode(const std::vector<uint8_t>& data)
	{
		static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		out.reserve(((data.size() + 2) / 3) * 4);
		size_t i = 0;
		while (i + 3 <= data.size())
		{
			const uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | uint32_t(data[i + 2]);
			out.push_back(tbl[(n >> 18) & 63]);
			out.push_back(tbl[(n >> 12) & 63]);
			out.push_back(tbl[(n >> 6) & 63]);
			out.push_back(tbl[n & 63]);
			i += 3;
		}
		if (i < data.size())
		{
			uint32_t n = uint32_t(data[i]) << 16;
			if (i + 1 < data.size())
				n |= uint32_t(data[i + 1]) << 8;
			out.push_back(tbl[(n >> 18) & 63]);
			out.push_back(tbl[(n >> 12) & 63]);
			out.push_back((i + 1 < data.size()) ? tbl[(n >> 6) & 63] : '=');
			out.push_back('=');
		}
		return out;
	}

	inline std::vector<uint8_t> makeSkinnedBuffer()
	{
		std::vector<uint8_t> b;
		b.reserve(264);
		appendF32(b, 0); appendF32(b, 0); appendF32(b, 0);
		appendF32(b, 1); appendF32(b, 0); appendF32(b, 0);
		appendF32(b, 0); appendF32(b, 1); appendF32(b, 0);
		appendU8(b, 0); appendU8(b, 0); appendU8(b, 0); appendU8(b, 0);
		appendU8(b, 0); appendU8(b, 0); appendU8(b, 0); appendU8(b, 0);
		appendU8(b, 0); appendU8(b, 0); appendU8(b, 0); appendU8(b, 0);
		for (int v = 0; v < 3; ++v)
		{
			appendF32(b, 1); appendF32(b, 0); appendF32(b, 0); appendF32(b, 0);
		}
		appendU16(b, 0); appendU16(b, 1); appendU16(b, 2);
		while (b.size() < 104)
			b.push_back(0);
		appendMat4Translation(b, 0, -4, 0);
		appendMat4Translation(b, 0, -5, 0);
		appendF32(b, 0); appendF32(b, 1);
		appendF32(b, 0); appendF32(b, 0); appendF32(b, 0);
		appendF32(b, 1); appendF32(b, 0); appendF32(b, 0);
		return b;
	}

	inline std::string makeSkinnedGltfJson(const std::vector<uint8_t>& buf, bool childFirstJoints)
	{
		const std::string uri = "data:application/octet-stream;base64," + base64Encode(buf);
		const char* joints = childFirstJoints ? "[2, 1]" : "[1, 2]";
		std::string json;
		json += "{\n  \"asset\": { \"version\": \"2.0\" },\n  \"scene\": 0,\n";
		json += "  \"scenes\": [{ \"nodes\": [0, 3] }],\n  \"nodes\": [\n";
		json += "    { \"name\": \"Armature\", \"translation\": [0, 4, 0], \"children\": [1] },\n";
		json += "    { \"name\": \"Hips\", \"children\": [2] },\n";
		json += "    { \"name\": \"Spine\", \"translation\": [0, 1, 0] },\n";
		json += "    { \"name\": \"Mesh\", \"translation\": [3, 0, 0], \"mesh\": 0, \"skin\": 0 }\n  ],\n";
		json += "  \"skins\": [{ \"joints\": ";
		json += joints;
		json += ", \"skeleton\": 0, \"inverseBindMatrices\": 4 }],\n";
		json += "  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0, \"JOINTS_0\": 1, \"WEIGHTS_0\": 2 }, \"indices\": 3 }] }],\n";
		json += "  \"animations\": [{ \"name\": \"Walk\", \"channels\": [{ \"sampler\": 0, \"target\": { \"node\": 1, \"path\": \"translation\" } }],\n";
		json += "    \"samplers\": [{ \"input\": 5, \"output\": 6, \"interpolation\": \"LINEAR\" }] }],\n";
		json += "  \"accessors\": [\n";
		json += "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n";
		json += "    { \"bufferView\": 1, \"componentType\": 5121, \"count\": 3, \"type\": \"VEC4\" },\n";
		json += "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC4\" },\n";
		json += "    { \"bufferView\": 3, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" },\n";
		json += "    { \"bufferView\": 4, \"componentType\": 5126, \"count\": 2, \"type\": \"MAT4\" },\n";
		json += "    { \"bufferView\": 5, \"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\" },\n";
		json += "    { \"bufferView\": 6, \"componentType\": 5126, \"count\": 2, \"type\": \"VEC3\" }\n  ],\n";
		json += "  \"bufferViews\": [\n";
		json += "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36 },\n";
		json += "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 12 },\n";
		json += "    { \"buffer\": 0, \"byteOffset\": 48, \"byteLength\": 48 },\n";
		json += "    { \"buffer\": 0, \"byteOffset\": 96, \"byteLength\": 6 },\n";
		json += "    { \"buffer\": 0, \"byteOffset\": 104, \"byteLength\": 128 },\n";
		json += "    { \"buffer\": 0, \"byteOffset\": 232, \"byteLength\": 8 },\n";
		json += "    { \"buffer\": 0, \"byteOffset\": 240, \"byteLength\": 24 }\n  ],\n";
		json += "  \"buffers\": [{ \"byteLength\": 264, \"uri\": \"";
		json += uri;
		json += "\" }]\n}\n";
		return json;
	}

	inline std::filesystem::path writeSkinnedGltf(const std::string& name, bool childFirstJoints = false)
	{
		return writeTempGltf(name, makeSkinnedGltfJson(makeSkinnedBuffer(), childFirstJoints));
	}
}
