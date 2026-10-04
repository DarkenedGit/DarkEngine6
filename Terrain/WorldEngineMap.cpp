#include "Terrain/WorldEngineMap.h"

#include "Assets/AssetManager.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Math/Color.h"
#include "Render/Renderer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "third_party/nlohmann/json.hpp"

#include <cctype>
#include <fstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace Dark::Terrain
{
namespace
{

using json = nlohmann::json;

std::string lowerCopy(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text)
        out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

bool nameHas(const std::string& lowerName, const char* key)
{
    return lowerName.find(key) != std::string::npos;
}

bool relativeEscapes(const std::filesystem::path& rel)
{
    if (rel.empty())
        return true;
    for (const std::filesystem::path& part : rel)
    {
        if (part == "..")
            return true;
    }
    return false;
}

int canonicalSurface(std::string_view id)
{
    const std::string lower = lowerCopy(id);
    if (lower == "dirt" || lower == "sand" || lower == "mud")
        return 0;
    if (lower == "grass" || lower == "forest" || lower == "moss")
        return 1;
    if (lower == "rock" || lower == "stone" || lower == "cliff")
        return 2;
    if (lower == "snow" || lower == "ice")
        return 3;
    return -1;
}

bool readChannelMap(const std::filesystem::path& directory, int outSrcToCanon[4])
{
    outSrcToCanon[0] = 0;
    outSrcToCanon[1] = 1;
    outSrcToCanon[2] = 2;
    outSrcToCanon[3] = 3;

    const std::filesystem::path path = directory / "surfaces.json";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec)
        return true;

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        DE_LOG_ERROR(LogCategory::Render, "WorldEngine: unreadable '{}'", path.string());
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        DE_LOG_ERROR(LogCategory::Render, "WorldEngine: surfaces.json is not an object ({})", path.string());
        return false;
    }
    const int version = root.value("version", 1);
    if (version != 1)
    {
        DE_LOG_ERROR(LogCategory::Render, "WorldEngine: surfaces.json version {} unsupported", version);
        return false;
    }
    if (!root.contains("channels") || !root["channels"].is_array())
    {
        DE_LOG_WARN(LogCategory::Render, "WorldEngine: surfaces.json has no channels — using dirt, grass, rock, snow");
        return true;
    }
    const json& channels = root["channels"];
    const int n = channels.size() > 4 ? 4 : static_cast<int>(channels.size());
    for (int i = 0; i < n; ++i)
    {
        if (!channels[i].is_string())
            continue;
        const int canon = canonicalSurface(channels[i].get<std::string>());
        if (canon < 0)
        {
            DE_LOG_WARN(LogCategory::Render, "WorldEngine: unknown surface '{}' — channel {} stays in place", channels[i].get<std::string>(), i);
            continue;
        }
        outSrcToCanon[i] = canon;
    }
    return true;
}

bool loadGrayU16(const std::filesystem::path& path, std::vector<uint16_t>& out, uint32_t& outW, uint32_t& outH)
{
    out.clear();
    outW = 0;
    outH = 0;

    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninit = SUCCEEDED(co);
    bool ok = false;

    // Release every WIC object before CoUninitialize. Dropping them afterwards is an access violation.
    {
        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr) || !factory)
            DE_LOG_ERROR(LogCategory::Render, "WorldEngine: WIC factory failed (0x{:08X})", static_cast<unsigned>(hr));
        else
        {
            ComPtr<IWICBitmapDecoder> decoder;
            hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
            if (FAILED(hr) || !decoder)
                DE_LOG_ERROR(LogCategory::Render, "WorldEngine: failed to open height '{}'", path.string());
            else
            {
                ComPtr<IWICBitmapFrameDecode> frame;
                hr = decoder->GetFrame(0, &frame);
                ComPtr<IWICFormatConverter> converter;
                if (SUCCEEDED(hr) && frame)
                    hr = factory->CreateFormatConverter(&converter);
                if (SUCCEEDED(hr) && converter)
                    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat16bppGray, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
                if (FAILED(hr) || !converter)
                    DE_LOG_ERROR(LogCategory::Render, "WorldEngine: height convert failed '{}'", path.string());
                else
                {
                    UINT w = 0;
                    UINT h = 0;
                    hr = converter->GetSize(&w, &h);
                    if (FAILED(hr) || w < 2 || h < 2)
                        DE_LOG_ERROR(LogCategory::Render, "WorldEngine: height '{}' is too small", path.string());
                    else
                    {
                        const UINT stride = w * 2u;
                        out.resize(static_cast<size_t>(w) * h);
                        hr = converter->CopyPixels(nullptr, stride, stride * h, reinterpret_cast<BYTE*>(out.data()));
                        if (FAILED(hr))
                        {
                            DE_LOG_ERROR(LogCategory::Render, "WorldEngine: height read failed '{}'", path.string());
                            out.clear();
                        }
                        else
                        {
                            outW = w;
                            outH = h;
                            ok   = true;
                        }
                    }
                }
            }
        }
    }

    if (uninit)
        CoUninitialize();
    return ok;
}

uint8_t sampleR(const Image& image, uint32_t x, uint32_t y)
{
    const uint8_t* row = image.pixels() + static_cast<size_t>(y) * image.rowPitchBytes();
    return row[static_cast<size_t>(x) * 4u];
}

uint8_t sampleG(const Image& image, uint32_t x, uint32_t y)
{
    const uint8_t* row = image.pixels() + static_cast<size_t>(y) * image.rowPitchBytes();
    return row[static_cast<size_t>(x) * 4u + 1u];
}

bool maskVaries(const Image& mask)
{
    if (!mask.valid() || !mask.pixels())
        return false;
    const uint8_t r0 = sampleR(mask, 0, 0);
    const uint8_t g0 = sampleG(mask, 0, 0);
    for (uint32_t y = 0; y < mask.height(); ++y)
    {
        for (uint32_t x = 0; x < mask.width(); ++x)
        {
            if (sampleR(mask, x, y) != r0 || sampleG(mask, x, y) != g0)
                return true;
        }
    }
    return false;
}

} // namespace

std::filesystem::path resolveWorldEngineDirectory(const std::string& source)
{
    if (source.empty())
        return {};
    std::error_code ec;
    const std::filesystem::path raw(source);
    if (raw.is_absolute() && std::filesystem::is_directory(raw, ec) && !ec)
        return raw;
    for (const std::filesystem::path& root : contentRootCandidates())
    {
        const std::filesystem::path candidate = root / raw;
        if (std::filesystem::is_directory(candidate, ec) && !ec)
            return candidate;
    }
    if (std::filesystem::is_directory(raw, ec) && !ec)
        return std::filesystem::absolute(raw, ec);
    return {};
}

std::string worldEngineSourceKey(const std::filesystem::path& directory)
{
    std::error_code ec;
    const std::filesystem::path authoring = authoringContentRoot();
    if (!authoring.empty())
    {
        const std::filesystem::path rel = std::filesystem::relative(directory, authoring, ec);
        if (!ec && !relativeEscapes(rel))
            return rel.generic_string();
    }
    for (const std::filesystem::path& root : contentRootCandidates())
    {
        const std::filesystem::path rel = std::filesystem::relative(directory, root, ec);
        if (!ec && !relativeEscapes(rel))
            return rel.generic_string();
    }
    return directory.generic_string();
}

bool loadWorldEngineDirectory(const std::filesystem::path& directory, const WorldEngineLoadDesc& desc, WorldEngineMaps& out)
{
    out = WorldEngineMaps{};
    std::error_code ec;
    if (directory.empty() || !std::filesystem::is_directory(directory, ec) || ec)
    {
        DE_LOG_ERROR(LogCategory::Render, "WorldEngine: '{}' is not a directory", directory.string());
        return false;
    }
    if (!(desc.worldSizeMeters > 1.0f) || !(desc.heightRangeMeters > 0.0f))
    {
        DE_LOG_ERROR(LogCategory::Render, "WorldEngine: world size and height range must be positive");
        return false;
    }

    std::filesystem::path heightPath;
    std::filesystem::path splatPath;
    std::filesystem::path diffusePath;
    std::filesystem::path roughPath;
    std::filesystem::path maskPath;
    std::filesystem::path dispPath;

    for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc) || fileEc)
            continue;
        const std::filesystem::path file = it->path();
        const std::string ext = lowerCopy(file.extension().string());
        if (ext != ".png" && ext != ".tif" && ext != ".tiff" && ext != ".tga" && ext != ".bmp" && ext != ".jpg" && ext != ".jpeg")
            continue;
        const std::string name = lowerCopy(file.filename().string());
        if (nameHas(name, "height") && !nameHas(name, "disp"))
            heightPath = file;
        else if (nameHas(name, "splat"))
            splatPath = file;
        else if (nameHas(name, "diffuse") || nameHas(name, "albedo") || nameHas(name, "basecolor"))
            diffusePath = file;
        else if (nameHas(name, "rough"))
            roughPath = file;
        else if (nameHas(name, "mask") && !nameHas(name, "splat"))
            maskPath = file;
        else if (nameHas(name, "disp"))
            dispPath = file;
    }
    if (ec)
    {
        DE_LOG_ERROR(LogCategory::Render, "WorldEngine: failed to list '{}'", directory.string());
        return false;
    }
    if (heightPath.empty() || splatPath.empty() || diffusePath.empty())
    {
        DE_LOG_ERROR(LogCategory::Render, "WorldEngine: '{}' needs a height map, a splat map, and a diffuse map", directory.string());
        return false;
    }

    std::vector<uint16_t> gray;
    uint32_t w = 0;
    uint32_t h = 0;
    if (!loadGrayU16(heightPath, gray, w, h))
        return false;
    if (w != h || w > kMaxHeightMapSize)
    {
        DE_LOG_ERROR(LogCategory::Render, "WorldEngine: height {}x{} is not a square within {}", w, h, kMaxHeightMapSize);
        return false;
    }

    uint16_t lo = 65535;
    uint16_t hi = 0;
    for (uint16_t s : gray)
    {
        if (s < lo)
            lo = s;
        if (s > hi)
            hi = s;
    }
    const float span = (hi > lo) ? static_cast<float>(hi - lo) : 1.0f;
    std::vector<float> raw(gray.size());
    for (size_t i = 0; i < gray.size(); ++i)
        raw[i] = static_cast<float>(gray[i] - lo) / span;

    const float cell = desc.worldSizeMeters / static_cast<float>(w - 1u);
    HeightMap height;
    if (!height.createFrom(w, h, raw.data(), cell, desc.heightRangeMeters))
        return false;
    height.setOrigin(Math::Vector3f{ -0.5f * desc.worldSizeMeters, desc.baseHeightMeters, -0.5f * desc.worldSizeMeters });

    Image splatImg;
    if (!splatImg.createFromFile(splatPath) || !splatImg.valid() || splatImg.format() != ImageFormat::RGBA8)
    {
        DE_LOG_ERROR(LogCategory::Render, "WorldEngine: splat '{}' failed to load", splatPath.string());
        return false;
    }
    if (splatImg.width() != w || splatImg.height() != h)
    {
        DE_LOG_ERROR(LogCategory::Render, "WorldEngine: splat {}x{} does not match height {}x{}", splatImg.width(), splatImg.height(), w, h);
        return false;
    }

    int srcToCanon[4] = { 0, 1, 2, 3 };
    if (!readChannelMap(directory, srcToCanon))
        return false;

    std::vector<uint8_t> packed(static_cast<size_t>(w) * h * 4u, 0);
    for (uint32_t y = 0; y < h; ++y)
    {
        const uint8_t* row = splatImg.pixels() + static_cast<size_t>(y) * splatImg.rowPitchBytes();
        uint8_t* dst = packed.data() + static_cast<size_t>(y) * w * 4u;
        for (uint32_t x = 0; x < w; ++x)
        {
            const uint8_t* src = row + static_cast<size_t>(x) * 4u;
            unsigned acc[4] = {};
            for (int c = 0; c < 4; ++c)
            {
                const unsigned sum = acc[srcToCanon[c]] + src[c];
                acc[srcToCanon[c]] = sum > 255u ? 255u : sum;
            }
            dst[0] = static_cast<uint8_t>(acc[0]);
            dst[1] = static_cast<uint8_t>(acc[1]);
            dst[2] = static_cast<uint8_t>(acc[2]);
            dst[3] = static_cast<uint8_t>(acc[3]);
            dst += 4;
        }
    }

    SplatMap splat;
    if (!splat.createFromRGBA(w, h, packed.data()))
        return false;

    out.height       = std::move(height);
    out.splat        = std::move(splat);
    out.diffuse      = diffusePath;
    out.roughness    = roughPath;
    out.mask         = maskPath;
    out.displacement = dispPath;
    out.sourceKey    = worldEngineSourceKey(directory);
    DE_LOG_INFO(
        LogCategory::Render,
        "WorldEngine: loaded '{}' {}x{}  world {:.0f} m  relief {:.0f} m",
        directory.string(),
        w,
        h,
        desc.worldSizeMeters,
        desc.heightRangeMeters);
    if (!dispPath.empty())
        DE_LOG_INFO(LogCategory::Render, "WorldEngine: displacement '{}' is not applied; the mesh is the height map", dispPath.filename().string());
    return true;
}

bool buildWorldEngineOrmImage(const std::filesystem::path& roughnessPath, const std::filesystem::path& maskPath, Image& out)
{
    out = Image{};
    Image rough;
    uint32_t w = 4;
    uint32_t h = 4;
    const bool haveRough = !roughnessPath.empty() && rough.createFromFile(roughnessPath) && rough.valid() && rough.pixels();
    if (haveRough)
    {
        w = rough.width();
        h = rough.height();
    }
    else if (!roughnessPath.empty())
        DE_LOG_WARN(LogCategory::Render, "WorldEngine: roughness '{}' failed — using 0.8", roughnessPath.string());

    Image mask;
    const bool haveMask = !maskPath.empty() && mask.createFromFile(maskPath) && mask.valid() && mask.width() == w && mask.height() == h && maskVaries(mask);
    if (!maskPath.empty() && !haveMask)
        DE_LOG_INFO(LogCategory::Render, "WorldEngine: mask '{}' has no surface variation — metal 0, AO 1", maskPath.filename().string());

    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4u);
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            const uint8_t roughV = haveRough ? sampleR(rough, x, y) : static_cast<uint8_t>(204);
            const uint8_t ao     = haveMask ? sampleG(mask, x, y) : static_cast<uint8_t>(255);
            const uint8_t metal  = haveMask ? sampleR(mask, x, y) : static_cast<uint8_t>(0);
            uint8_t* px          = rgba.data() + (static_cast<size_t>(y) * w + x) * 4u;
            px[0]                = ao;
            px[1]                = roughV;
            px[2]                = metal;
            px[3]                = 128;
        }
    }
    if (!out.createFromRGBA(rgba.data(), w, h, w * 4u))
        return false;
    out.setColorSpace(Color::ColorSpace::Linear);
    return true;
}

bool uploadWorldEngineMaterial(
    Renderer& renderer,
    AssetManager& assets,
    const SplatMap& splat,
    const WorldEngineMaps& maps,
    TerrainMaterial& material,
    TerrainSurfaceDesc* outSurface)
{
    if (!splat.valid())
    {
        DE_LOG_ERROR(LogCategory::Render, "WorldEngine: material needs a splat");
        return false;
    }
    if (!material.createDefault(renderer, splat))
        return false;

    AssetRef<Image> diffuse = assets.loadImageFile(maps.diffuse);
    if (!diffuse || !diffuse->valid())
    {
        DE_LOG_ERROR(LogCategory::Render, "WorldEngine: diffuse '{}' failed to load", maps.diffuse.string());
        return false;
    }
    diffuse->setColorSpace(Color::ColorSpace::sRGB);

    Image ormImage;
    if (!buildWorldEngineOrmImage(maps.roughness, maps.mask, ormImage))
        return false;
    const std::string ormKey = std::string("worldengine-orm:") + maps.roughness.generic_string();
    AssetRef<Image> orm = assets.internImage(std::make_shared<Image>(std::move(ormImage)), ormKey);
    if (!orm || !orm->valid())
    {
        DE_LOG_ERROR(LogCategory::Render, "WorldEngine: ORM intern failed");
        return false;
    }

    TerrainSurfaceDesc surface{};
    surface.params.heightBlendK   = kWorldEngineMacroBlend;
    surface.params.heightBlendT   = 0.1f;
    surface.params.triplanarSlope = 1.0f;
    surface.layers[0].tiling      = -1.0f;
    surface.albedo[0]             = diffuse;
    surface.orm[0]                = orm;
    if (!material.applySurfaceDesc(renderer, surface))
    {
        DE_LOG_ERROR(LogCategory::Render, "WorldEngine: material pack failed");
        return false;
    }
    if (outSurface)
        *outSurface = std::move(surface);
    return true;
}

} // namespace Dark::Terrain
