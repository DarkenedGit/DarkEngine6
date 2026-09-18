#include "Assets/GltfMaterialSave.h"
#include "Assets/Image.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Core/Log.h"

#include "third_party/nlohmann/json.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace Dark
{
namespace
{

using json = nlohmann::json;

constexpr uint32_t kGlbMagic     = 0x46546C67u; // glTF
constexpr uint32_t kGlbJsonChunk = 0x4E4F534Au; // JSON
constexpr uint32_t kGlbBinChunk  = 0x004E4942u; // BIN

uint32_t readU32(const uint8_t* p)
{
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

void writeU32(uint8_t* p, uint32_t v)
{
    std::memcpy(p, &v, 4);
}

const char* alphaModeName(MaterialAlphaMode mode)
{
    switch (mode)
    {
    case MaterialAlphaMode::Mask:
        return "MASK";
    case MaterialAlphaMode::Blend:
        return "BLEND";
    case MaterialAlphaMode::Opaque:
    default:
        return "OPAQUE";
    }
}

bool collectMaterials(const Model& model, std::unordered_map<int, const Material*>& out, std::string* errorOut)
{
    out.clear();
    auto consider = [&](const Model::Part& part) {
        if (part.materialIndex < 0 || !part.material)
            return;
        const auto it = out.find(part.materialIndex);
        if (it == out.end())
            out[part.materialIndex] = part.material.get();
    };
    for (const Model::Part& p : model.opaque())
        consider(p);
    for (const Model::Part& p : model.translucent())
        consider(p);
    if (out.empty())
    {
        if (errorOut)
            *errorOut = "model has no glTF materials to save";
        return false;
    }
    return true;
}

bool hasTextureIndex(const json& obj, const char* key)
{
    if (!obj.contains(key) || !obj[key].is_object())
        return false;
    return obj[key].contains("index");
}

bool albedoLooksAuthored(const Material& mat)
{
    const AssetRef<Image>& albedo = mat.albedo();
    if (!albedo || !albedo->valid() || !albedo->pixels())
        return false;
    if (albedo->width() != 1u || albedo->height() != 1u)
        return true;
    const uint8_t* px = albedo->pixels();
    return px[0] != 255 || px[1] != 255 || px[2] != 255 || px[3] != 255;
}

void warnLiveMapNotInJson()
{
    static bool s_warned = false;
    if (s_warned)
        return;
    s_warned = true;
    DE_LOG_WARN("GltfMaterialSave: live Image assigned but JSON has no texture index; writing factors only");
}

bool patchMaterialsObject(json& root, const Model& model, std::string* errorOut)
{
    if (!root.is_object())
    {
        if (errorOut)
            *errorOut = "glTF JSON is not an object";
        return false;
    }

    std::unordered_map<int, const Material*> mats;
    if (!collectMaterials(model, mats, errorOut))
        return false;

    if (!root.contains("materials") || !root["materials"].is_array())
        root["materials"] = json::array();
    json& arr = root["materials"];

    int maxIndex = -1;
    for (const auto& kv : mats)
    {
        if (kv.first > maxIndex)
            maxIndex = kv.first;
    }
    while (static_cast<int>(arr.size()) <= maxIndex)
        arr.push_back(json::object());

    for (const auto& kv : mats)
    {
        const Material* mat = kv.second;
        if (!mat)
            continue;
        json& jm = arr[static_cast<size_t>(kv.first)];
        if (!jm.is_object())
            jm = json::object();
        if (!jm.contains("pbrMetallicRoughness") || !jm["pbrMetallicRoughness"].is_object())
            jm["pbrMetallicRoughness"] = json::object();
        json& pbr = jm["pbrMetallicRoughness"];
        const float* c = mat->baseColor();
        pbr["baseColorFactor"] = json::array({ c[0], c[1], c[2], c[3] });
        pbr["metallicFactor"]  = mat->metallic();
        pbr["roughnessFactor"] = mat->roughness();
        jm["alphaMode"]        = alphaModeName(mat->alphaMode());
        jm["alphaCutoff"]      = mat->alphaCutoff();
        const float  es = mat->emissive();
        const float* e  = mat->emissiveColor();
        jm["emissiveFactor"] = json::array({ e[0] * es, e[1] * es, e[2] * es });

        if (jm.contains("normalTexture") && jm["normalTexture"].is_object())
            jm["normalTexture"]["scale"] = mat->normalScale();
        else if (mat->normalScale() != 1.0f)
        {
            jm["normalTexture"]          = json::object();
            jm["normalTexture"]["scale"] = mat->normalScale();
        }

        if (jm.contains("occlusionTexture") && jm["occlusionTexture"].is_object())
            jm["occlusionTexture"]["strength"] = mat->ao();
        else if (mat->ao() != 1.0f)
        {
            jm["occlusionTexture"]             = json::object();
            jm["occlusionTexture"]["strength"] = mat->ao();
        }

        if (!hasTextureIndex(pbr, "baseColorTexture") && albedoLooksAuthored(*mat))
            warnLiveMapNotInJson();
        if (!hasTextureIndex(jm, "normalTexture") && mat->normalImage() && mat->normalImage()->valid())
            warnLiveMapNotInJson();
        if (!hasTextureIndex(jm, "emissiveTexture") && mat->emissiveImage() && mat->emissiveImage()->valid())
            warnLiveMapNotInJson();
        const bool jsonHasOrm = hasTextureIndex(pbr, "metallicRoughnessTexture") || hasTextureIndex(jm, "occlusionTexture");
        if (!jsonHasOrm && mat->ormImage() && mat->ormImage()->valid())
            warnLiveMapNotInJson();
    }
    return true;
}

bool readAllBytes(const std::filesystem::path& path, std::vector<uint8_t>& out, std::string* errorOut)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        if (errorOut)
            *errorOut = "failed to open " + path.string();
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff len = in.tellg();
    in.seekg(0, std::ios::beg);
    if (len < 0)
        return false;
    out.resize(static_cast<size_t>(len));
    if (len > 0)
        in.read(reinterpret_cast<char*>(out.data()), len);
    return static_cast<bool>(in) || len == 0;
}

bool writeAllBytes(const std::filesystem::path& path, const uint8_t* data, size_t len, std::string* errorOut)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        if (errorOut)
            *errorOut = "failed to write " + path.string();
        return false;
    }
    if (len > 0)
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
    return static_cast<bool>(out);
}

bool iequalsExt(const std::filesystem::path& path, const char* ext)
{
    std::string e = path.extension().string();
    for (char& c : e)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return e == ext;
}

} // namespace

bool patchGltfMaterialsJson(const char* jsonText, const Model& model, std::string& outJson, std::string* errorOut)
{
    if (!jsonText || jsonText[0] == '\0')
    {
        if (errorOut)
            *errorOut = "empty glTF JSON";
        return false;
    }
    json root = json::parse(jsonText, nullptr, false);
    if (root.is_discarded())
    {
        if (errorOut)
            *errorOut = "invalid glTF JSON";
        return false;
    }
    if (!patchMaterialsObject(root, model, errorOut))
        return false;
    outJson = root.dump(2);
    return true;
}

bool saveGltfMaterials(const std::filesystem::path& path, const Model& model, std::string* errorOut)
{
    if (path.empty())
    {
        if (errorOut)
            *errorOut = "empty save path";
        return false;
    }

    if (iequalsExt(path, ".glb"))
    {
        std::vector<uint8_t> bytes;
        if (!readAllBytes(path, bytes, errorOut))
            return false;
        if (bytes.size() < 20 || readU32(bytes.data()) != kGlbMagic)
        {
            if (errorOut)
                *errorOut = "not a GLB file";
            return false;
        }
        const uint32_t jsonLen = readU32(bytes.data() + 12);
        const uint32_t jsonType = readU32(bytes.data() + 16);
        if (jsonType != kGlbJsonChunk || bytes.size() < 20u + jsonLen)
        {
            if (errorOut)
                *errorOut = "GLB JSON chunk missing";
            return false;
        }
        std::string jsonText(reinterpret_cast<const char*>(bytes.data() + 20), jsonLen);
        while (!jsonText.empty() && (jsonText.back() == ' ' || jsonText.back() == '\0'))
            jsonText.pop_back();

        json root = json::parse(jsonText, nullptr, false);
        if (root.is_discarded() || !patchMaterialsObject(root, model, errorOut))
            return false;
        std::string newJson = root.dump();
        while (newJson.size() % 4u != 0)
            newJson.push_back(' ');

        const size_t binOffset = 20u + jsonLen;
        std::vector<uint8_t> binTail;
        if (bytes.size() > binOffset)
            binTail.assign(bytes.begin() + static_cast<std::ptrdiff_t>(binOffset), bytes.end());

        std::vector<uint8_t> out(20 + newJson.size() + binTail.size());
        writeU32(out.data() + 0, kGlbMagic);
        writeU32(out.data() + 4, 2);
        writeU32(out.data() + 8, static_cast<uint32_t>(out.size()));
        writeU32(out.data() + 12, static_cast<uint32_t>(newJson.size()));
        writeU32(out.data() + 16, kGlbJsonChunk);
        std::memcpy(out.data() + 20, newJson.data(), newJson.size());
        if (!binTail.empty())
            std::memcpy(out.data() + 20 + newJson.size(), binTail.data(), binTail.size());

        if (!writeAllBytes(path, out.data(), out.size(), errorOut))
            return false;
        DE_LOG_INFO("GltfMaterialSave: wrote GLB materials → {}", path.string());
        return true;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        if (errorOut)
            *errorOut = "failed to open " + path.string();
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string patched;
    if (!patchGltfMaterialsJson(ss.str().c_str(), model, patched, errorOut))
        return false;
    if (!writeAllBytes(path, reinterpret_cast<const uint8_t*>(patched.data()), patched.size(), errorOut))
        return false;
    DE_LOG_INFO("GltfMaterialSave: wrote glTF materials → {}", path.string());
    return true;
}

} // namespace Dark
