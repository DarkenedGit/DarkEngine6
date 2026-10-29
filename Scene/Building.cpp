#include "Scene/Building.h"

#include "Core/Json.h"
#include "Core/Log.h"
#include "Scene/EntityMaster.h"

#include <cmath>
#include <fstream>
#include <sstream>

namespace Dark
{
    namespace
    {
        using json = nlohmann::json;

        constexpr uintmax_t kMaxBuildingBytes = 512 * 1024;

        bool fail(std::string* error, std::string message)
        {
            if (error)
                *error = std::move(message);
            return false;
        }

        bool readFloats(const json& j, float* out, int count)
        {
            if (!j.is_array() || static_cast<int>(j.size()) < count)
                return false;
            for (int i = 0; i < count; ++i)
            {
                if (!j[static_cast<size_t>(i)].is_number())
                    return false;
                out[i] = j[static_cast<size_t>(i)].get<float>();
            }
            return true;
        }

        bool readXZ(const json& j, Math::Vector2f& out)
        {
            float v[2]{};
            if (!readFloats(j, v, 2))
                return false;
            out = Math::Vector2f{ v[0], v[1] };
            return true;
        }

        bool readText(const std::filesystem::path& path, std::string& out, std::string* error)
        {
            std::error_code ec;
            if (path.empty() || !std::filesystem::is_regular_file(path, ec) || ec)
                return fail(error, "file not found: " + path.string());
            const uintmax_t size = std::filesystem::file_size(path, ec);
            if (ec || size > kMaxBuildingBytes)
                return fail(error, "unreadable or too large: " + path.string());
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return fail(error, "cannot open: " + path.string());
            std::ostringstream ss;
            ss << in.rdbuf();
            out = ss.str();
            return true;
        }

        int quarterTurns(float yawDeg)
        {
            const int q = static_cast<int>(std::lround(yawDeg / 90.0f)) % 4;
            return q < 0 ? q + 4 : q;
        }

        Math::Vector2f turn(Math::Vector2f p, int q)
        {
            for (int i = 0; i < q; ++i)
                p = Math::Vector2f{ p.y, -p.x };
            return p;
        }

        struct Placer
        {
            const SceneBuildingDesc& place;
            int                      q = 0;

            Math::Vector3f point(float lx, float ly, float lz) const
            {
                const Math::Vector2f w = turn(Math::Vector2f{ lx, lz }, q);
                return Math::Vector3f{ place.position.x + w.x, place.position.y + ly, place.position.z + w.y };
            }

            SceneObjectData box(float x0, float y0, float z0, float x1, float y1, float z1, const float* color, bool block) const
            {
                const Math::Vector3f a = point(x0, y0, z0);
                const Math::Vector3f b = point(x1, y1, z1);
                SceneObjectData      o;
                o.type     = SceneObjectType::Cube;
                o.position = Math::Vector3f{ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f };
                o.scale    = Math::Vector3f{ std::fabs(b.x - a.x), std::fabs(b.y - a.y), std::fabs(b.z - a.z) };
                for (int i = 0; i < 4; ++i)
                    o.color[i] = color[i];
                o.block = block;
                return o;
            }
        };
    } // namespace

    std::string buildingVirtualPath(std::string_view id)
    {
        return "buildings/" + std::string(id) + ".building.json";
    }

    std::string buildingKitVirtualPath(std::string_view kit)
    {
        return "buildings/" + std::string(kit) + ".kit.json";
    }

    bool parseBuildingKit(std::string_view jsonText, BuildingKit& out, std::string* error)
    {
        const json root = json::parse(jsonText, nullptr, false);
        if (root.is_discarded() || !root.is_object())
            return fail(error, "kit is not a JSON object");
        BuildingKit kit;
        bool        ok = true;
        ok &= jsonFloatOr(root, "cell", kit.cell, 4.0f, "BuildingKit", error);
        ok &= jsonFloatOr(root, "height", kit.height, 3.2f, "BuildingKit", error);
        ok &= jsonFloatOr(root, "thickness", kit.thickness, 0.3f, "BuildingKit", error);
        if (!ok)
            return false;
        if (kit.cell < 1.0f || kit.height < 1.0f || kit.thickness <= 0.0f)
            return fail(error, "kit cell, height and thickness must be positive");
        if (root.contains("wallColor"))
            readFloats(root["wallColor"], kit.wallColor, 4);
        if (root.contains("floorColor"))
            readFloats(root["floorColor"], kit.floorColor, 4);
        if (root.contains("symbols") && root["symbols"].is_object())
        {
            for (auto it = root["symbols"].begin(); it != root["symbols"].end(); ++it)
            {
                if (it.key().size() == 1 && it.value().is_string())
                    kit.symbols[it.key()[0]] = it.value().get<std::string>();
            }
        }
        if (root.contains("pieces") && root["pieces"].is_object())
        {
            for (auto it = root["pieces"].begin(); it != root["pieces"].end(); ++it)
            {
                KitPiece piece;
                if (it.value().is_object() && it.value().contains("leaf"))
                {
                    float v[4]{};
                    if (!readFloats(it.value()["leaf"], v, 4))
                        return fail(error, "piece '" + it.key() + "' leaf must be [a, b, y0, y1]");
                    piece.hasLeaf = true;
                    piece.leaf    = KitSpan{ v[0], v[1], v[2], v[3] };
                    if (!jsonStringOr(it.value(), "lock", piece.lock, std::string(), "BuildingKit", error))
                        return false;
                }
                if (it.value().is_object() && it.value().contains("spans") && it.value()["spans"].is_array())
                {
                    for (const json& span : it.value()["spans"])
                    {
                        float v[4]{};
                        if (!readFloats(span, v, 4))
                            return fail(error, "piece '" + it.key() + "' has a span that is not [a, b, y0, y1]");
                        piece.spans.push_back(KitSpan{ v[0], v[1], v[2], v[3] });
                    }
                }
                kit.pieces[it.key()] = std::move(piece);
            }
        }
        if (root.contains("props") && root["props"].is_object())
        {
            for (auto it = root["props"].begin(); it != root["props"].end(); ++it)
            {
                if (!it.value().is_object())
                    continue;
                const json& jp = it.value();
                KitProp     prop;
                float       size[3]{ 1.0f, 1.0f, 1.0f };
                if (jp.contains("size") && !readFloats(jp["size"], size, 3))
                    return fail(error, "prop '" + it.key() + "' size must be [x, y, z]");
                prop.size = Math::Vector3f{ size[0], size[1], size[2] };
                if (jp.contains("color"))
                    readFloats(jp["color"], prop.color, 4);
                if (!jsonBoolOr(jp, "hide", prop.hide, false, "BuildingKit", error) || !jsonStringOr(jp, "label", prop.label, it.key(), "BuildingKit", error))
                    return false;
                kit.props[it.key()] = std::move(prop);
            }
        }
        out = std::move(kit);
        return true;
    }

    bool parseBuilding(std::string_view jsonText, BuildingDef& out, std::string* error)
    {
        const json root = json::parse(jsonText, nullptr, false);
        if (root.is_discarded() || !root.is_object())
            return fail(error, "building is not a JSON object");
        BuildingDef def;
        if (!jsonStringOr(root, "id", def.id, std::string(), "Building", error) || !jsonStringOr(root, "kit", def.kit, "facility", "Building", error))
            return false;
        if (def.id.empty())
            return fail(error, "building needs an \"id\"");
        if (!root.contains("layout") || !root["layout"].is_array())
            return fail(error, "building needs a \"layout\" list of text rows");
        for (const json& row : root["layout"])
        {
            if (!row.is_string())
                return fail(error, "layout rows must be text");
            def.layout.push_back(row.get<std::string>());
        }
        if (def.layout.size() < 3 || def.layout.size() % 2 == 0)
            return fail(error, "layout needs an odd number of rows (3, 5, 7...)");
        for (const std::string& row : def.layout)
        {
            if (row.size() != def.layout[0].size() || row.size() < 3 || row.size() % 2 == 0)
                return fail(error, "layout rows must all have the same odd length");
        }
        if (root.contains("props") && root["props"].is_array())
        {
            for (const json& jp : root["props"])
            {
                BuildingPropDef p;
                if (!jp.is_object() || !jsonStringOr(jp, "prop", p.prop, std::string(), "Building", error) || !jsonFloatOr(jp, "yaw", p.yaw, 0.0f, "Building", error))
                    return fail(error, "bad prop entry");
                if (!jp.contains("pos") || !readXZ(jp["pos"], p.pos))
                    return fail(error, "prop '" + p.prop + "' needs \"pos\": [x, z]");
                if (jp.contains("loot") && jp["loot"].is_array())
                {
                    for (const json& jl : jp["loot"])
                    {
                        BuildingLoot l;
                        if (jl.is_object() && jsonStringOr(jl, "item", l.item, std::string(), "Building", error) && jsonIntOr(jl, "count", l.count, 1, "Building", error) && !l.item.empty())
                            p.loot.push_back(std::move(l));
                    }
                }
                def.props.push_back(std::move(p));
            }
        }
        if (root.contains("pickups") && root["pickups"].is_array())
        {
            for (const json& jp : root["pickups"])
            {
                BuildingPickupDef p;
                if (!jp.is_object() || !jsonStringOr(jp, "item", p.item, std::string(), "Building", error) || !jsonIntOr(jp, "count", p.count, 1, "Building", error))
                    return fail(error, "bad pickup entry");
                if (!jp.contains("pos") || !readXZ(jp["pos"], p.pos))
                    return fail(error, "pickup '" + p.item + "' needs \"pos\": [x, z]");
                def.pickups.push_back(std::move(p));
            }
        }
        if (root.contains("cameras") && root["cameras"].is_array())
        {
            for (const json& jc : root["cameras"])
            {
                BuildingCameraDef c;
                if (!jc.is_object() || !jsonFloatOr(jc, "height", c.height, 2.8f, "Building", error) || !jsonFloatOr(jc, "yaw", c.yaw, 0.0f, "Building", error)
                    || !jsonFloatOr(jc, "sweep", c.sweep, 60.0f, "Building", error) || !jsonFloatOr(jc, "range", c.range, 14.0f, "Building", error)
                    || !jsonStringOr(jc, "circuit", c.circuit, std::string(), "Building", error))
                    return fail(error, "bad camera entry");
                if (!jc.contains("pos") || !readXZ(jc["pos"], c.pos))
                    return fail(error, "camera needs \"pos\": [x, z]");
                def.cameras.push_back(std::move(c));
            }
        }
        if (root.contains("powerBoxes") && root["powerBoxes"].is_array())
        {
            for (const json& jp : root["powerBoxes"])
            {
                BuildingPowerDef pb;
                if (!jp.is_object() || !jsonStringOr(jp, "circuit", pb.circuit, std::string(), "Building", error) || pb.circuit.empty())
                    return fail(error, "power box needs a \"circuit\"");
                if (!jp.contains("pos") || !readXZ(jp["pos"], pb.pos))
                    return fail(error, "power box needs \"pos\": [x, z]");
                def.powerBoxes.push_back(std::move(pb));
            }
        }
        if (root.contains("guards") && root["guards"].is_array())
        {
            for (const json& jg : root["guards"])
            {
                BuildingGuardDef g;
                if (!jg.is_object() || !jsonStringOr(jg, "squad", g.squad, std::string(), "Building", error))
                    return fail(error, "bad guard entry");
                if (!jg.contains("pos") || !readXZ(jg["pos"], g.pos))
                    return fail(error, "guard needs \"pos\": [x, z]");
                if (jg.contains("patrol") && jg["patrol"].is_array())
                {
                    for (const json& jp : jg["patrol"])
                    {
                        Math::Vector2f p;
                        if (readXZ(jp, p))
                            g.patrol.push_back(p);
                    }
                }
                def.guards.push_back(std::move(g));
            }
        }
        out = std::move(def);
        return true;
    }

    bool loadBuildingKit(const std::filesystem::path& path, BuildingKit& out, std::string* error)
    {
        std::string text;
        return readText(path, text, error) && parseBuildingKit(text, out, error);
    }

    bool loadBuilding(const std::filesystem::path& path, BuildingDef& out, std::string* error)
    {
        std::string text;
        return readText(path, text, error) && parseBuilding(text, out, error);
    }

    bool expandBuilding(const BuildingDef& def, const BuildingKit& kit, const SceneBuildingDesc& place, BuildingExpansion& out, std::string* error)
    {
        if (def.layout.size() < 3)
            return fail(error, "building '" + def.id + "' has no layout");
        const Placer  at{ place, quarterTurns(place.yaw) };
        const float   cell = kit.cell;
        const float   half = kit.thickness * 0.5f;
        const float   tall = kit.height;
        const size_t  rows = def.layout.size();
        const size_t  cols = def.layout[0].size();
        auto          symbol = [&](size_t r, size_t c) { return r < rows && c < cols ? def.layout[r][c] : ' '; };

        for (size_t r = 0; r < rows; ++r)
        {
            for (size_t c = 0; c < cols; ++c)
            {
                const char ch    = symbol(r, c);
                const bool evenR = r % 2 == 0;
                const bool evenC = c % 2 == 0;
                if (ch == ' ')
                    continue;
                if (evenR && evenC)
                {
                    const float x = static_cast<float>(c / 2) * cell;
                    const float z = static_cast<float>(r / 2) * cell;
                    out.objects.push_back(at.box(x - half, 0.0f, z - half, x + half, tall, z + half, kit.wallColor, true));
                    continue;
                }
                if (!evenR && !evenC)
                {
                    const float x = static_cast<float>(c / 2) * cell;
                    const float z = static_cast<float>(r / 2) * cell;
                    out.objects.push_back(at.box(x, -0.02f, z, x + cell, 0.04f, z + cell, kit.floorColor, false));
                    continue;
                }
                const auto name = kit.symbols.find(ch);
                const auto piece = name != kit.symbols.end() ? kit.pieces.find(name->second) : kit.pieces.end();
                if (piece == kit.pieces.end())
                {
                    DE_LOG_WARN("Building '{}': no kit piece for '{}' at row {} column {}", def.id, ch, r, c);
                    continue;
                }
                if (piece->second.hasLeaf)
                {
                    const KitSpan&  l    = piece->second.leaf;
                    const float     x    = static_cast<float>(c / 2) * cell;
                    const float     z    = static_cast<float>(r / 2) * cell;
                    const float     thin = half * 0.6f;
                    SceneObjectData leaf = evenR ? at.box(x + l.a, l.y0, z - thin, x + l.b, l.y1, z + thin, kit.wallColor, false)
                                                 : at.box(x - thin, l.y0, z + l.a, x + thin, l.y1, z + l.b, kit.wallColor, false);
                    out.doors.push_back(BuildingDoor{ leaf.position, leaf.scale, piece->second.lock });
                }
                for (const KitSpan& s : piece->second.spans)
                {
                    const bool block = s.y0 < 1.5f;
                    if (evenR)
                    {
                        const float x = static_cast<float>(c / 2) * cell;
                        const float z = static_cast<float>(r / 2) * cell;
                        out.objects.push_back(at.box(x + s.a, s.y0, z - half, x + s.b, s.y1, z + half, kit.wallColor, block));
                    }
                    else
                    {
                        const float x = static_cast<float>(c / 2) * cell;
                        const float z = static_cast<float>(r / 2) * cell;
                        out.objects.push_back(at.box(x - half, s.y0, z + s.a, x + half, s.y1, z + s.b, kit.wallColor, block));
                    }
                }
            }
        }

        for (const BuildingPropDef& p : def.props)
        {
            const auto it = kit.props.find(p.prop);
            if (it == kit.props.end())
            {
                DE_LOG_WARN("Building '{}': unknown prop '{}'", def.id, p.prop);
                continue;
            }
            const KitProp& kp    = it->second;
            const int      q     = quarterTurns(p.yaw);
            const bool     side  = q % 2 == 1;
            const float    hx    = (side ? kp.size.z : kp.size.x) * 0.5f;
            const float    hz    = (side ? kp.size.x : kp.size.z) * 0.5f;
            out.objects.push_back(at.box(p.pos.x - hx, 0.0f, p.pos.y - hz, p.pos.x + hx, kp.size.y, p.pos.y + hz, kp.color, true));
            const Math::Vector2f facing = turn(Math::Vector2f{ 0.0f, -1.0f }, q);
            const float          reach  = kp.size.z * 0.5f;
            const Math::Vector3f centre = at.point(p.pos.x, 0.0f, p.pos.y);
            const Math::Vector3f ahead  = at.point(p.pos.x + facing.x * (reach + 0.7f), 0.0f, p.pos.y + facing.y * (reach + 0.7f));
            const Math::Vector3f front{ ahead.x - centre.x, 0.0f, ahead.z - centre.z };
            if (kp.hide)
                out.hides.push_back(BuildingHideSpot{ centre, front, kp.label });
            if (!p.loot.empty())
            {
                const Math::Vector3f lootAt = at.point(p.pos.x + facing.x * (reach + 0.35f), 0.9f, p.pos.y + facing.y * (reach + 0.35f));
                out.containers.push_back(BuildingContainer{ lootAt, kp.label, p.loot });
            }
        }

        for (const BuildingPickupDef& p : def.pickups)
        {
            SceneObjectData o;
            o.type        = SceneObjectType::Cube;
            o.position    = at.point(p.pos.x, 0.35f, p.pos.y);
            o.scale       = Math::Vector3f{ 0.4f, 0.4f, 0.4f };
            o.pickup      = p.item;
            o.pickupCount = p.count;
            out.objects.push_back(std::move(o));
        }

        for (const BuildingCameraDef& c : def.cameras)
        {
            const float          rad  = c.yaw * 0.0174533f;
            const Math::Vector3f from = at.point(c.pos.x, c.height, c.pos.y);
            const Math::Vector3f to   = at.point(c.pos.x + std::sin(rad), c.height, c.pos.y + std::cos(rad));
            const float          yaw  = std::atan2(to.x - from.x, to.z - from.z) / 0.0174533f;
            out.cameras.push_back(BuildingCamera{ from, yaw, c.sweep, c.range, c.circuit });
        }

        for (const BuildingPowerDef& pb : def.powerBoxes)
        {
            static const float kYellow[4]{ 0.85f, 0.70f, 0.15f, 1.0f };
            SceneObjectData    o = at.box(pb.pos.x - 0.3f, 0.0f, pb.pos.y - 0.25f, pb.pos.x + 0.3f, 1.3f, pb.pos.y + 0.25f, kYellow, true);
            o.circuit            = pb.circuit;
            out.objects.push_back(std::move(o));
        }

        for (const BuildingGuardDef& g : def.guards)
        {
            SceneObjectData o;
            o.type     = SceneObjectType::Hunter;
            o.position = at.point(g.pos.x, 0.5f, g.pos.y);
            o.squad    = g.squad.empty() ? "building:" + def.id : g.squad;
            for (const Math::Vector2f& p : g.patrol)
            {
                const Math::Vector3f w = at.point(p.x, 0.0f, p.y);
                o.patrol.emplace_back(w.x, w.z);
            }
            out.objects.push_back(std::move(o));
        }
        return true;
    }

    int expandSceneBuildings(SceneFileData& scene, std::vector<BuildingContainer>& containers, std::vector<BuildingHideSpot>& hides,
                             std::vector<BuildingDoor>* doors, std::vector<BuildingCamera>* cameras)
    {
        int                                          placed = 0;
        std::unordered_map<std::string, BuildingKit> kits;
        for (const SceneBuildingDesc& where : scene.buildings)
        {
            BuildingDef def;
            std::string error;
            if (!loadBuilding(resolveContentFile(buildingVirtualPath(where.id)), def, &error))
            {
                DE_LOG_ERROR("Building '{}': {}", where.id, error);
                continue;
            }
            auto kit = kits.find(def.kit);
            if (kit == kits.end())
            {
                BuildingKit loaded;
                if (!loadBuildingKit(resolveContentFile(buildingKitVirtualPath(def.kit)), loaded, &error))
                {
                    DE_LOG_ERROR("Building '{}': kit '{}': {}", where.id, def.kit, error);
                    continue;
                }
                kit = kits.emplace(def.kit, std::move(loaded)).first;
            }
            BuildingExpansion out;
            if (!expandBuilding(def, kit->second, where, out, &error))
            {
                DE_LOG_ERROR("Building '{}': {}", where.id, error);
                continue;
            }
            DE_LOG_INFO("Building '{}': {} pieces, {} searchable, {} hiding spots", where.id, out.objects.size(), out.containers.size(), out.hides.size());
            for (SceneObjectData& o : out.objects)
                scene.objects.push_back(std::move(o));
            for (BuildingContainer& c : out.containers)
                containers.push_back(std::move(c));
            for (BuildingHideSpot& h : out.hides)
                hides.push_back(std::move(h));
            if (doors)
            {
                for (BuildingDoor& d : out.doors)
                    doors->push_back(std::move(d));
            }
            if (cameras)
            {
                for (BuildingCamera& c : out.cameras)
                    cameras->push_back(std::move(c));
            }
            ++placed;
        }
        return placed;
    }

} // namespace Dark
