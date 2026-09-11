#include "Assets/GltfLoader.h"
#include "Animation/AnimSampler.h"
#include "Core/Log.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
#include "Math/Matrix3f.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"

#include "cgltf/cgltf.h"

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <unordered_map>
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

        void decomposeLocal(const Math::Matrix4f& m, Math::Vector3f& T, Math::Quaternion& R, Math::Vector3f& S)
        {
            T = m.GetTranslation();
            Math::Vector3f bx = m.GetBasisX();
            Math::Vector3f by = m.GetBasisY();
            Math::Vector3f bz = m.GetBasisZ();
            S = Math::Vector3f(bx.Magnitude(), by.Magnitude(), bz.Magnitude());
            if (S.x > Math::Epsilon)
                bx *= 1.0f / S.x;
            if (S.y > Math::Epsilon)
                by *= 1.0f / S.y;
            if (S.z > Math::Epsilon)
                bz *= 1.0f / S.z;
            Math::Matrix3f rot;
            rot.SetRow(0, bx);
            rot.SetRow(1, by);
            rot.SetRow(2, bz);
            R = Math::Quaternion::FromMatrix3(rot);
            R.Normalize();
        }

        void nodeLocalTRS(const cgltf_node* n, Math::Vector3f& T, Math::Quaternion& R, Math::Vector3f& S)
        {
            T = Math::Vector3f::ZERO;
            R = Math::Quaternion::IDENTITY;
            S = Math::Vector3f(1.0f, 1.0f, 1.0f);
            if (!n)
                return;
            if (n->has_translation || n->has_rotation || n->has_scale)
            {
                if (n->has_translation)
                    T = Math::Vector3f(n->translation[0], n->translation[1], n->translation[2]);
                if (n->has_rotation)
                    R = Math::Quaternion(n->rotation[3], n->rotation[0], n->rotation[1], n->rotation[2]);
                if (n->has_scale)
                    S = Math::Vector3f(n->scale[0], n->scale[1], n->scale[2]);
                return;
            }
            if (n->has_matrix)
                decomposeLocal(fromGltfMatrix(n->matrix), T, R, S);
        }

        bool unpackInfluences(const cgltf_primitive& gp, uint32_t jointCount, uint32_t vertexCount, MeshData& mesh, bool& warnedJoints1)
        {
            if (findAttr(gp, cgltf_attribute_type_joints, 1) || findAttr(gp, cgltf_attribute_type_weights, 1))
            {
                if (!warnedJoints1)
                {
                    DE_LOG_WARN("GltfLoader: JOINTS_1/WEIGHTS_1 ignored (v1 is 4 influences)");
                    warnedJoints1 = true;
                }
            }
            const cgltf_accessor* jAcc = findAttr(gp, cgltf_attribute_type_joints, 0);
            const cgltf_accessor* wAcc = findAttr(gp, cgltf_attribute_type_weights, 0);
            if (!jAcc || !wAcc || jAcc->count != vertexCount || wAcc->count != vertexCount)
            {
                DE_LOG_ERROR("GltfLoader: skinned primitive missing JOINTS_0/WEIGHTS_0");
                return false;
            }

            mesh.jointPacked.resize(vertexCount);
            mesh.weights.resize(vertexCount);
            for (uint32_t v = 0; v < vertexCount; ++v)
            {
                cgltf_uint joints[4]{};
                if (!cgltf_accessor_read_uint(jAcc, v, joints, 4))
                {
                    DE_LOG_ERROR("GltfLoader: failed to read JOINTS_0");
                    return false;
                }
                for (int k = 0; k < 4; ++k)
                {
                    if (joints[k] >= jointCount)
                    {
                        DE_LOG_ERROR("GltfLoader: JOINTS_0 index {} >= joint count {}", joints[k], jointCount);
                        return false;
                    }
                }
                mesh.jointPacked[v] = joints[0] | (joints[1] << 8) | (joints[2] << 16) | (joints[3] << 24);

                float w[4]{};
                if (!cgltf_accessor_read_float(wAcc, v, w, 4))
                {
                    DE_LOG_ERROR("GltfLoader: failed to read WEIGHTS_0");
                    return false;
                }
                const float sum = w[0] + w[1] + w[2] + w[3];
                if (sum > 1.0e-8f)
                {
                    const float inv = 1.0f / sum;
                    mesh.weights[v] = Math::Vector4f(w[0] * inv, w[1] * inv, w[2] * inv, w[3] * inv);
                }
                else
                {
                    mesh.jointPacked[v] = 0;
                    mesh.weights[v] = Math::Vector4f(1.0f, 0.0f, 0.0f, 0.0f);
                }
            }
            return true;
        }

        bool loadSkeleton(const cgltf_data* data, Skeleton& skel)
        {
            skel = Skeleton{};
            if (!data || data->skins_count == 0)
                return true;
            if (data->skins_count > 1)
                DE_LOG_WARN("GltfLoader: extra skins ignored (v1 uses the first skin)");

            const cgltf_skin* skin = &data->skins[0];
            const uint32_t jointCount = static_cast<uint32_t>(skin->joints_count);
            if (jointCount == 0 || jointCount > AnimPose::kMaxBones)
            {
                DE_LOG_ERROR("GltfLoader: skin has {} joints, max {}", jointCount, AnimPose::kMaxBones);
                return false;
            }

            std::unordered_map<const cgltf_node*, uint32_t> nodeToJoint;
            nodeToJoint.reserve(jointCount);
            skel.joints.resize(jointCount);
            for (uint32_t i = 0; i < jointCount; ++i)
            {
                const cgltf_node* jn = skin->joints[i];
                if (!jn)
                {
                    DE_LOG_ERROR("GltfLoader: skin joint {} is null", i);
                    return false;
                }
                nodeToJoint[jn] = i;
                Joint& j = skel.joints[i];
                j.name = (jn->name && jn->name[0]) ? jn->name : ("joint_" + std::to_string(i));
                nodeLocalTRS(jn, j.restT, j.restR, j.restS);
            }

            std::vector<Math::Matrix4f> nodeWorld(data->nodes_count);
            for (cgltf_size i = 0; i < data->nodes_count; ++i)
            {
                float world[16];
                cgltf_node_transform_world(&data->nodes[i], world);
                nodeWorld[i] = fromGltfMatrix(world);
            }

            auto nodeIndex = [&](const cgltf_node* n) -> int32_t {
                if (!n)
                    return -1;
                return static_cast<int32_t>(cgltf_node_index(data, n));
            };

            for (uint32_t i = 0; i < jointCount; ++i)
            {
                const cgltf_node* jn = skin->joints[i];
                Joint& j = skel.joints[i];
                j.parent = -1;
                for (const cgltf_node* p = jn->parent; p; p = p->parent)
                {
                    auto it = nodeToJoint.find(p);
                    if (it != nodeToJoint.end())
                    {
                        j.parent = static_cast<int32_t>(it->second);
                        break;
                    }
                }

                if (j.parent < 0)
                {
                    if (jn->parent)
                    {
                        const int32_t pi = nodeIndex(jn->parent);
                        if (pi >= 0)
                            j.ancestorBindWorld = nodeWorld[static_cast<size_t>(pi)];
                    }
                }
                else
                {
                    const cgltf_node* parentJointNode = skin->joints[static_cast<size_t>(j.parent)];
                    if (jn->parent == parentJointNode)
                    {
                        j.ancestorBindWorld = Math::Matrix4f();
                    }
                    else if (jn->parent)
                    {
                        const int32_t pi = nodeIndex(jn->parent);
                        const int32_t pji = nodeIndex(parentJointNode);
                        if (pi >= 0 && pji >= 0)
                            j.ancestorBindWorld = nodeWorld[static_cast<size_t>(pji)].Inverse() * nodeWorld[static_cast<size_t>(pi)];
                    }
                }
            }

            if (skin->skeleton)
            {
                auto it = nodeToJoint.find(skin->skeleton);
                skel.skeletonRoot = (it != nodeToJoint.end()) ? static_cast<int32_t>(it->second) : -1;
            }
            skel.rootMotionJoint = (skel.skeletonRoot >= 0) ? skel.skeletonRoot : 0;

            const cgltf_node* meshNode = nullptr;
            for (cgltf_size i = 0; i < data->nodes_count; ++i)
            {
                if (data->nodes[i].mesh && data->nodes[i].skin == skin)
                {
                    meshNode = &data->nodes[i];
                    break;
                }
            }
            if (meshNode)
            {
                const int32_t mi = nodeIndex(meshNode);
                if (mi >= 0)
                    skel.meshWorld = nodeWorld[static_cast<size_t>(mi)];
            }

            if (skin->inverse_bind_matrices)
            {
                const cgltf_accessor* ibmAcc = skin->inverse_bind_matrices;
                if (ibmAcc->count != jointCount)
                {
                    DE_LOG_ERROR("GltfLoader: IBM count {} != joint count {}", static_cast<uint32_t>(ibmAcc->count), jointCount);
                    return false;
                }
                std::vector<float> tmp(static_cast<size_t>(jointCount) * 16u);
                if (cgltf_accessor_unpack_floats(ibmAcc, tmp.data(), tmp.size()) != tmp.size())
                {
                    DE_LOG_ERROR("GltfLoader: failed to unpack inverse bind matrices");
                    return false;
                }
                for (uint32_t i = 0; i < jointCount; ++i)
                    skel.joints[i].inverseBind = fromGltfMatrix(&tmp[static_cast<size_t>(i) * 16u]) * skel.meshWorld;
            }
            else
            {
                DE_LOG_WARN("GltfLoader: missing inverseBindMatrices; using inverse rest world");
                for (uint32_t i = 0; i < jointCount; ++i)
                {
                    const int32_t ni = nodeIndex(skin->joints[i]);
                    const Math::Matrix4f restW = (ni >= 0) ? nodeWorld[static_cast<size_t>(ni)] : Math::Matrix4f();
                    skel.joints[i].inverseBind = restW.Inverse() * skel.meshWorld;
                }
            }

            skel.fkOrder.clear();
            skel.fkOrder.reserve(jointCount);
            std::vector<char> vis(jointCount, 0);
            auto visit = [&](auto&& self, uint32_t i) -> void {
                if (vis[i])
                    return;
                vis[i] = 1;
                const int32_t p = skel.joints[i].parent;
                if (p >= 0 && static_cast<uint32_t>(p) < jointCount)
                    self(self, static_cast<uint32_t>(p));
                skel.fkOrder.push_back(i);
            };
            for (uint32_t i = 0; i < jointCount; ++i)
                visit(visit, i);

            std::vector<Math::Vector3f> T(jointCount);
            std::vector<Math::Quaternion> R(jointCount);
            std::vector<Math::Vector3f> S(jointCount);
            for (uint32_t i = 0; i < jointCount; ++i)
            {
                T[i] = skel.joints[i].restT;
                R[i] = skel.joints[i].restR;
                S[i] = skel.joints[i].restS;
            }
            localToPalette(skel, T.data(), R.data(), S.data(), skel.restPose);
            for (uint32_t i = 0; i < jointCount && i < AnimPose::kMaxBones; ++i)
                skel.restPose.prevPalette[i] = skel.restPose.palette[i];
            return true;
        }

        bool loadClips(const cgltf_data* data, const Skeleton& skel, std::vector<AnimationClip>& clips)
        {
            clips.clear();
            if (!data)
                return true;

            std::unordered_map<const cgltf_node*, uint32_t> nodeToJoint;
            if (data->skins_count > 0 && !skel.joints.empty())
            {
                const cgltf_skin* skin = &data->skins[0];
                for (cgltf_size i = 0; i < skin->joints_count; ++i)
                    nodeToJoint[skin->joints[i]] = static_cast<uint32_t>(i);
            }

            bool warnedMorph = false;
            for (cgltf_size a = 0; a < data->animations_count; ++a)
            {
                const cgltf_animation* ga = &data->animations[a];
                AnimationClip clip;
                clip.name = (ga->name && ga->name[0]) ? ga->name : ("animation_" + std::to_string(a));
                for (cgltf_size c = 0; c < ga->channels_count; ++c)
                {
                    const cgltf_animation_channel* ch = &ga->channels[c];
                    if (ch->target_path == cgltf_animation_path_type_weights)
                    {
                        if (!warnedMorph)
                        {
                            DE_LOG_WARN("GltfLoader: morph weight channels ignored");
                            warnedMorph = true;
                        }
                        continue;
                    }
                    if (!ch->target_node || !ch->sampler || !ch->sampler->input || !ch->sampler->output)
                        continue;
                    auto it = nodeToJoint.find(ch->target_node);
                    if (it == nodeToJoint.end())
                        continue;

                    AnimChannel ac;
                    ac.joint = it->second;
                    if (ch->target_path == cgltf_animation_path_type_translation)
                        ac.path = AnimPath::Translation;
                    else if (ch->target_path == cgltf_animation_path_type_rotation)
                        ac.path = AnimPath::Rotation;
                    else if (ch->target_path == cgltf_animation_path_type_scale)
                        ac.path = AnimPath::Scale;
                    else
                        continue;

                    if (ch->sampler->interpolation == cgltf_interpolation_type_step)
                        ac.interp = AnimInterp::Step;
                    else if (ch->sampler->interpolation == cgltf_interpolation_type_cubic_spline)
                        ac.interp = AnimInterp::CubicSpline;
                    else
                        ac.interp = AnimInterp::Linear;

                    const cgltf_accessor* inAcc = ch->sampler->input;
                    const cgltf_accessor* outAcc = ch->sampler->output;
                    ac.times.resize(inAcc->count);
                    if (cgltf_accessor_unpack_floats(inAcc, ac.times.data(), ac.times.size()) != ac.times.size())
                    {
                        DE_LOG_WARN("GltfLoader: skipped channel with bad times");
                        continue;
                    }
                    if (ac.times.size() > 4096)
                    {
                        DE_LOG_ERROR("GltfLoader: channel has {} keys, max 4096 — skipping", static_cast<uint32_t>(ac.times.size()));
                        continue;
                    }

                    const int comps = (ac.path == AnimPath::Rotation) ? 4 : 3;
                    const cgltf_size expected = (ac.interp == AnimInterp::CubicSpline)
                        ? inAcc->count * 3u * static_cast<cgltf_size>(comps)
                        : inAcc->count * static_cast<cgltf_size>(comps);
                    std::vector<float> raw(expected);
                    if (cgltf_accessor_unpack_floats(outAcc, raw.data(), raw.size()) != raw.size())
                    {
                        if (ac.interp == AnimInterp::CubicSpline)
                            DE_LOG_ERROR("GltfLoader: CUBICSPLINE output count mismatch — skipping channel");
                        else
                            DE_LOG_WARN("GltfLoader: skipped channel with bad values");
                        continue;
                    }
                    if (ac.path == AnimPath::Rotation)
                    {
                        const int stride = (ac.interp == AnimInterp::CubicSpline) ? 12 : 4;
                        for (size_t k = 0; k + 3 < raw.size(); k += static_cast<size_t>(stride))
                        {
                            // glTF xyzw -> engine wxyz for each vec4 in the key (and tangents)
                            const int vecs = stride / 4;
                            for (int v = 0; v < vecs; ++v)
                            {
                                const size_t o = k + static_cast<size_t>(v) * 4u;
                                const float x = raw[o], y = raw[o + 1], z = raw[o + 2], w = raw[o + 3];
                                raw[o] = w;
                                raw[o + 1] = x;
                                raw[o + 2] = y;
                                raw[o + 3] = z;
                            }
                        }
                    }
                    ac.values = std::move(raw);
                    if (!ac.times.empty())
                        clip.duration = Math::Max(clip.duration, ac.times.back());
                    clip.channels.push_back(std::move(ac));
                }

                bool nameTaken = false;
                for (const AnimationClip& existing : clips)
                {
                    if (existing.name == clip.name)
                    {
                        nameTaken = true;
                        break;
                    }
                }
                if (nameTaken)
                {
                    DE_LOG_WARN("GltfLoader: duplicate clip name '{}'", clip.name);
                    clip.name += "_" + std::to_string(a);
                }
                clips.push_back(std::move(clip));
            }
            return true;
        }

        bool extractPrimitive(const cgltf_primitive& gp, const Math::Matrix4f& localToRoot, const std::filesystem::path& gltfPath, const cgltf_data* data, bool skinned, uint32_t jointCount, bool& warnedJoints1, GltfCpuPrimitive& out)
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

            if (skinned)
            {
                if (!unpackInfluences(gp, jointCount, static_cast<uint32_t>(out.mesh.positions.size()), out.mesh, warnedJoints1))
                    return false;
                out.skinned = true;
            }

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

        void walkNode(const cgltf_node* node, const std::filesystem::path& gltfPath, const cgltf_data* data, const cgltf_skin* primarySkin, uint32_t jointCount, bool& warnedJoints1, bool& warnedExtraSkin, GltfCpuModel& out)
        {
            if (!node)
                return;
            float world[16];
            cgltf_node_transform_world(node, world);
            Math::Matrix4f localToRoot = fromGltfMatrix(world);
            if (node->mesh)
            {
                bool skinned = false;
                if (node->skin)
                {
                    if (primarySkin && node->skin != primarySkin)
                    {
                        if (!warnedExtraSkin)
                        {
                            DE_LOG_WARN("GltfLoader: primitives using non-primary skins skipped");
                            warnedExtraSkin = true;
                        }
                    }
                    else if (primarySkin && jointCount > 0)
                    {
                        skinned = true;
                        localToRoot = out.skeleton.meshWorld;
                    }
                    else if (node->skin && !primarySkin)
                    {
                        DE_LOG_ERROR("GltfLoader: skinned node without a loaded skin");
                    }
                }
                if (!(node->skin && primarySkin && node->skin != primarySkin))
                {
                    for (cgltf_size i = 0; i < node->mesh->primitives_count; ++i)
                    {
                        GltfCpuPrimitive prim;
                        if (extractPrimitive(node->mesh->primitives[i], localToRoot, gltfPath, data, skinned, jointCount, warnedJoints1, prim))
                            out.primitives.push_back(std::move(prim));
                    }
                }
            }
            for (cgltf_size i = 0; i < node->children_count; ++i)
                walkNode(node->children[i], gltfPath, data, primarySkin, jointCount, warnedJoints1, warnedExtraSkin, out);
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

        if (!loadSkeleton(data, out.skeleton))
        {
            cgltf_free(data);
            out = GltfCpuModel{};
            return false;
        }

        const cgltf_skin* primarySkin = (data->skins_count > 0) ? &data->skins[0] : nullptr;
        const uint32_t jointCount = static_cast<uint32_t>(out.skeleton.joints.size());
        bool warnedJoints1 = false;
        bool warnedExtraSkin = false;

        if (data->scene)
        {
            for (cgltf_size i = 0; i < data->scene->nodes_count; ++i)
                walkNode(data->scene->nodes[i], path, data, primarySkin, jointCount, warnedJoints1, warnedExtraSkin, out);
        }
        else
        {
            for (cgltf_size i = 0; i < data->nodes_count; ++i)
            {
                if (!data->nodes[i].parent)
                    walkNode(&data->nodes[i], path, data, primarySkin, jointCount, warnedJoints1, warnedExtraSkin, out);
            }
        }

        if (out.primitives.empty())
        {
            const Math::Matrix4f identity;
            bool dummy = false;
            for (cgltf_size m = 0; m < data->meshes_count; ++m)
            {
                for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p)
                {
                    GltfCpuPrimitive prim;
                    if (extractPrimitive(data->meshes[m].primitives[p], identity, path, data, false, 0, dummy, prim))
                        out.primitives.push_back(std::move(prim));
                }
            }
        }

        if (!loadClips(data, out.skeleton, out.clips))
        {
            cgltf_free(data);
            out = GltfCpuModel{};
            return false;
        }

        cgltf_free(data);
        if (out.primitives.empty())
        {
            DE_LOG_ERROR("GltfLoader: no triangle primitives in '{}'", pathUtf8);
            return false;
        }
        DE_LOG_INFO("GltfLoader: '{}' ({} primitives, {} joints, {} clips)", pathUtf8, out.primitives.size(), jointCount, out.clips.size());
        return true;
    }

} // namespace Dark
