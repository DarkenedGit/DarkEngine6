#include "Terrain/FoliageFile.h"

#include "Core/Log.h"

#include <fstream>

namespace Dark::Terrain
{
    namespace
    {
        constexpr uint64_t kHeaderBytes = 32ull;

        uint64_t foliageBytes(uint32_t count)
        {
            return kHeaderBytes + static_cast<uint64_t>(count) * static_cast<uint64_t>(sizeof(FoliageRecord));
        }

        bool countAccepted(uint32_t count)
        {
            return count <= kMaxFoliagePerFile && count <= kMaxFoliageInstances && foliageBytes(count) <= kMaxFoliageSidecarBytes;
        }
    }

    bool saveFoliageTile(const std::filesystem::path& path, int tileX, int tileZ, const FoliageRecord* records, uint32_t count)
    {
        if (count > 0 && records == nullptr)
        {
            DE_LOG_ERROR("FoliageFile: save '{}' null records", path.string());
            return false;
        }
        if (!countAccepted(count))
        {
            DE_LOG_ERROR("FoliageFile: save '{}' count {} rejected", path.string(), count);
            return false;
        }
        for (uint32_t i = 0; i < count; ++i)
        {
            if (records[i].kind >= static_cast<uint8_t>(FoliageKind::Count))
            {
                DE_LOG_ERROR("FoliageFile: save '{}' kind {} rejected", path.string(), static_cast<uint32_t>(records[i].kind));
                return false;
            }
        }

        std::error_code ec;
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
            {
                DE_LOG_ERROR("FoliageFile: failed to create directory for '{}' ({})", path.string(), ec.message());
                return false;
            }
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            DE_LOG_ERROR("FoliageFile: failed to open '{}' for write", path.string());
            return false;
        }

        const uint32_t magic      = kFoliageMagic;
        const uint32_t version    = kFoliageVersion;
        const int32_t  fileTileX  = static_cast<int32_t>(tileX);
        const int32_t  fileTileZ  = static_cast<int32_t>(tileZ);
        const uint32_t flags      = 0;
        const uint32_t reserved0  = 0;
        const uint32_t reserved1  = 0;
        out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));
        out.write(reinterpret_cast<const char*>(&fileTileX), sizeof(fileTileX));
        out.write(reinterpret_cast<const char*>(&fileTileZ), sizeof(fileTileZ));
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));
        out.write(reinterpret_cast<const char*>(&flags), sizeof(flags));
        out.write(reinterpret_cast<const char*>(&reserved0), sizeof(reserved0));
        out.write(reinterpret_cast<const char*>(&reserved1), sizeof(reserved1));
        for (uint32_t i = 0; i < count; ++i)
        {
            FoliageRecord rec = records[i];
            rec.pad[0]        = 0;
            rec.pad[1]        = 0;
            rec.pad[2]        = 0;
            out.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
        }
        out.flush();
        if (!out)
        {
            DE_LOG_ERROR("FoliageFile: failed while writing '{}'", path.string());
            return false;
        }
        return true;
    }

    bool loadFoliageTile(const std::filesystem::path& path, int expectTileX, int expectTileZ, std::vector<FoliageRecord>& out)
    {
        out.clear();

        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec)
        {
            DE_LOG_ERROR("FoliageFile: missing '{}'", path.string());
            return false;
        }
        const std::uintmax_t fileBytes = std::filesystem::file_size(path, ec);
        if (ec)
        {
            DE_LOG_ERROR("FoliageFile: '{}' unreadable", path.string());
            return false;
        }
        if (fileBytes > kMaxFoliageSidecarBytes)
        {
            DE_LOG_ERROR("FoliageFile: '{}' size {} exceeds cap", path.string(), static_cast<uint64_t>(fileBytes));
            return false;
        }
        if (fileBytes < kHeaderBytes)
        {
            DE_LOG_ERROR("FoliageFile: '{}' shorter than header", path.string());
            return false;
        }

        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            DE_LOG_ERROR("FoliageFile: failed to open '{}' for read", path.string());
            return false;
        }

        uint32_t magic   = 0;
        uint32_t version = 0;
        int32_t  tileX   = 0;
        int32_t  tileZ   = 0;
        uint32_t count   = 0;
        uint32_t flags   = 0;
        uint32_t reserved0 = 0;
        uint32_t reserved1 = 0;
        in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        in.read(reinterpret_cast<char*>(&version), sizeof(version));
        in.read(reinterpret_cast<char*>(&tileX), sizeof(tileX));
        in.read(reinterpret_cast<char*>(&tileZ), sizeof(tileZ));
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        in.read(reinterpret_cast<char*>(&flags), sizeof(flags));
        in.read(reinterpret_cast<char*>(&reserved0), sizeof(reserved0));
        in.read(reinterpret_cast<char*>(&reserved1), sizeof(reserved1));
        if (!in)
        {
            DE_LOG_ERROR("FoliageFile: '{}' header read failed", path.string());
            return false;
        }
        // Non-zero flags and reserved words are not a reject.
        (void)flags;
        (void)reserved0;
        (void)reserved1;

        if (magic != kFoliageMagic)
        {
            DE_LOG_ERROR("FoliageFile: '{}' magic {:08X} mismatch", path.string(), magic);
            return false;
        }
        if (version != kFoliageVersion)
        {
            DE_LOG_ERROR("FoliageFile: '{}' version {} mismatch", path.string(), version);
            return false;
        }
        if (tileX != expectTileX || tileZ != expectTileZ)
        {
            DE_LOG_ERROR("FoliageFile: '{}' tile {},{} expected {},{}", path.string(), tileX, tileZ, expectTileX, expectTileZ);
            return false;
        }
        if (!countAccepted(count))
        {
            DE_LOG_ERROR("FoliageFile: '{}' count {} rejected", path.string(), count);
            return false;
        }
        const uint64_t need = foliageBytes(count);
        if (static_cast<uint64_t>(fileBytes) < need)
        {
            DE_LOG_ERROR("FoliageFile: '{}' truncated count {}", path.string(), count);
            return false;
        }
        if (count == 0)
            return true;

        out.resize(count);
        in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(need - kHeaderBytes));
        if (!in)
        {
            out.clear();
            DE_LOG_ERROR("FoliageFile: '{}' record read failed", path.string());
            return false;
        }
        for (uint32_t i = 0; i < count; ++i)
        {
            if (out[i].kind >= static_cast<uint8_t>(FoliageKind::Count))
            {
                const uint32_t kind = out[i].kind;
                out.clear();
                DE_LOG_ERROR("FoliageFile: '{}' kind {} rejected", path.string(), kind);
                return false;
            }
        }
        return true;
    }

} // namespace Dark::Terrain
