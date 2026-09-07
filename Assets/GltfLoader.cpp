#include "Assets/GltfLoader.h"
#include "Core/Log.h"

#include "cgltf/cgltf.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Dark
{
    namespace
    {
        Math::Matrix4f fromGltfMatrix(const float m[16])
        {
            // cgltf is column-major / column-vector. Engine is row-major / row-vector;
            // the 16 floats match as a memcpy (transpose of convention).
            Math::Matrix4f out;
            std::memcpy(out.m_afEntry, m, sizeof(float) * 16);
            return out;
        }

        const cgltf_accessor* findAttr(const cgltf_primitive& prim, cgltf_attribute_type type, int index = 0)
        {
            for (cgltf_size i = 0; i < prim.attributes_count; ++i)
            {
                if (prim.attributes[i].type == type && prim.attributes[i].index == index)
                    return prim.attributes[i].data;
            }
            return nullptr;
        }

        bool unpackVec3(const cgltf_accessor* acc, std::vector<Math::Vector3f>& out)
        {
            if (!acc || acc->count == 0)
                return false;
            std::vector<float> tmp(static_cast<size_t>(acc->count) * 3u);
            if (cgltf_accessor_unpack_floats(acc, tmp.data(), tmp.size()) != tmp.size())
                return false;
            out.resize(acc->count);
            for (cgltf_size i = 0; i < acc->count; ++i)
                out[i] = Math::Vector3f(tmp[i * 3], tmp[i * 3 + 1], tmp[i * 3 + 2]);
            return true;
        }

        bool unpackVec2(const cgltf_accessor* acc, std::vector<Math::Vector2f>& out)
        {
            if (!acc || acc->count == 0)
                return false;
            std::vector<float> tmp(static_cast<size_t>(acc->count) * 2u);
            if (cgltf_accessor_unpack_floats(acc, tmp.data(), tmp.size()) != tmp.size())
                return false;
            out.resize(acc->count);
            for (cgltf_size i = 0; i < acc->count; ++i)
                out[i] = Math::Vector2f(tmp[i * 2], tmp[i * 2 + 1]);
            return true;
        }

        bool unpackIndices(const cgltf_accessor* acc, uint32_t vertexCount, std::vector<uint32_t>& out)
        {
            if (!acc)
            {
                out.resize(vertexCount);
                for (uint32_t i = 0; i < vertexCount; ++i)
                    out[i] = i;
                return vertexCount >= 3;
            }
            out.resize(acc->count);
            if (cgltf_accessor_unpack_indices(acc, out.data(), sizeof(uint32_t), out.size()) != out.size())
            {
                for (cgltf_size i = 0; i < acc->count; ++i)
                    out[i] = static_cast<uint32_t>(cgltf_accessor_read_index(acc, i));
            }
            for (uint32_t i : out)
            {
                if (i >= vertexCount)
                    return false;
            }
            return out.size() >= 3;
        }

        bool loadImageBytes(const cgltf_image* image, const std::filesystem::path& gltfPath, GltfCpuPrimitive& prim)
        {
            if (!image)
                return true;
            if (image->uri)
            {
                std::string uri = image->uri;
                if (uri.rfind("data:", 0) == 0)
                {
                    const auto commaPos = uri.find(',');
                    if (commaPos == std::string::npos || commaPos < 7 || uri.compare(commaPos - 7, 7, ";base64") != 0)
                        return false;
                    const char* b64 = uri.c_str() + commaPos + 1;
                    size_t      n   = std::strlen(b64);
                    while (n > 0 && b64[n - 1] == '=')
                        --n;
                    const cgltf_size outLen = (n * 3) / 4;
                    cgltf_options    opt{};
                    void*            decoded = nullptr;
                    if (outLen == 0 || cgltf_load_buffer_base64(&opt, outLen, b64, &decoded) != cgltf_result_success || !decoded)
                        return false;
                    prim.albedoBytes.assign(static_cast<uint8_t*>(decoded), static_cast<uint8_t*>(decoded) + outLen);
                    std::free(decoded);
                    return true;
                }
                std::vector<char> decodedUri(uri.begin(), uri.end());
                decodedUri.push_back(0);
                cgltf_decode_uri(decodedUri.data());
                prim.albedoFile = gltfPath.parent_path() / decodedUri.data();
                return true;
            }
            if (image->buffer_view)
            {
                const uint8_t* data = cgltf_buffer_view_data(image->buffer_view);
                if (!data)
                    return false;
                prim.albedoBytes.assign(data, data + image->buffer_view->size);
                return true;
            }
            return true;
        }

        bool extractPrimitive(const cgltf_primitive& gp, const Math::Matrix4f& localToRoot, const std::filesystem::path& gltfPath, const cgltf_data* data, GltfCpuPrimitive& out)
        {
            if (gp.type != cgltf_primitive_type_triangles)
            {
                DE_LOG_WARN("GltfLoader: skipping non-triangle primitive");
                return false;
            }
            const cgltf_accessor* posAcc = findAttr(gp, cgltf_attribute_type_position);
            if (!unpackVec3(posAcc, out.mesh.positions))
            {
                DE_LOG_ERROR("GltfLoader: primitive missing POSITION");
                return false;
            }
            if (!unpackIndices(gp.indices, static_cast<uint32_t>(out.mesh.positions.size()), out.mesh.indices))
            {
                DE_LOG_ERROR("GltfLoader: bad indices");
                return false;
            }
            const cgltf_accessor* nrmAcc = findAttr(gp, cgltf_attribute_type_normal);
            if (!unpackVec3(nrmAcc, out.mesh.normals) || out.mesh.normals.size() != out.mesh.positions.size())
                detail::computeSmoothedNormals(out.mesh);
            const cgltf_accessor* uvAcc = findAttr(gp, cgltf_attribute_type_texcoord, 0);
            if (!unpackVec2(uvAcc, out.mesh.uvs) || out.mesh.uvs.size() != out.mesh.positions.size())
                out.mesh.uvs.assign(out.mesh.positions.size(), Math::Vector2f(0.0f, 0.0f));

            out.localToRoot = localToRoot;
            out.baseColor[0] = 1.0f;
            out.baseColor[1] = 1.0f;
            out.baseColor[2] = 1.0f;
            out.baseColor[3] = 1.0f;
            out.metallic     = 0.0f;
            out.roughness    = 1.0f;
            if (gp.material)
            {
                const cgltf_material* mat = gp.material;
                out.translucent = mat->alpha_mode == cgltf_alpha_mode_blend;
                out.doubleSided = mat->double_sided != 0;
                if (mat->has_pbr_metallic_roughness)
                {
                    const auto& pbr = mat->pbr_metallic_roughness;
                    out.baseColor[0] = pbr.base_color_factor[0];
                    out.baseColor[1] = pbr.base_color_factor[1];
                    out.baseColor[2] = pbr.base_color_factor[2];
                    out.baseColor[3] = pbr.base_color_factor[3];
                    out.metallic     = pbr.metallic_factor;
                    out.roughness    = pbr.roughness_factor;
                    if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
                    {
                        out.imageIndex = static_cast<int>(cgltf_image_index(data, pbr.base_color_texture.texture->image));
                        if (!loadImageBytes(pbr.base_color_texture.texture->image, gltfPath, out))
                            DE_LOG_WARN("GltfLoader: failed to read baseColor texture");
                    }
                }
            }
            return true;
        }

        void walkNode(const cgltf_node* node, const std::filesystem::path& gltfPath, const cgltf_data* data, GltfCpuModel& out)
        {
            if (!node)
                return;
            float world[16];
            cgltf_node_transform_world(node, world);
            const Math::Matrix4f localToRoot = fromGltfMatrix(world);
            if (node->mesh)
            {
                for (cgltf_size i = 0; i < node->mesh->primitives_count; ++i)
                {
                    GltfCpuPrimitive prim;
                    if (extractPrimitive(node->mesh->primitives[i], localToRoot, gltfPath, data, prim))
                        out.primitives.push_back(std::move(prim));
                }
            }
            for (cgltf_size i = 0; i < node->children_count; ++i)
                walkNode(node->children[i], gltfPath, data, out);
        }
    } // namespace

    bool parseGltfFile(const std::filesystem::path& path, GltfCpuModel& out)
    {
        out = GltfCpuModel{};
        if (path.empty())
        {
            DE_LOG_ERROR("GltfLoader: empty path");
            return false;
        }

        const std::string pathUtf8 = path.string();
        cgltf_options options{};
        cgltf_data*   data = nullptr;
        cgltf_result  r    = cgltf_parse_file(&options, pathUtf8.c_str(), &data);
        if (r != cgltf_result_success || !data)
        {
            DE_LOG_ERROR("GltfLoader: parse failed '{}' ({})", pathUtf8, static_cast<int>(r));
            return false;
        }
        r = cgltf_load_buffers(&options, data, pathUtf8.c_str());
        if (r != cgltf_result_success)
        {
            DE_LOG_ERROR("GltfLoader: load buffers failed '{}' ({})", pathUtf8, static_cast<int>(r));
            cgltf_free(data);
            return false;
        }

        if (data->asset.generator)
            out.generator = data->asset.generator;

        if (data->scene)
        {
            for (cgltf_size i = 0; i < data->scene->nodes_count; ++i)
                walkNode(data->scene->nodes[i], path, data, out);
        }
        else
        {
            for (cgltf_size i = 0; i < data->nodes_count; ++i)
            {
                if (!data->nodes[i].parent)
                    walkNode(&data->nodes[i], path, data, out);
            }
        }

        if (out.primitives.empty())
        {
            const Math::Matrix4f identity;
            for (cgltf_size m = 0; m < data->meshes_count; ++m)
            {
                for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p)
                {
                    GltfCpuPrimitive prim;
                    if (extractPrimitive(data->meshes[m].primitives[p], identity, path, data, prim))
                        out.primitives.push_back(std::move(prim));
                }
            }
        }

        cgltf_free(data);
        if (out.primitives.empty())
        {
            DE_LOG_ERROR("GltfLoader: no triangle primitives in '{}'", pathUtf8);
            return false;
        }
        DE_LOG_INFO("GltfLoader: '{}' ({} primitives)", pathUtf8, out.primitives.size());
        return true;
    }

} // namespace Dark
