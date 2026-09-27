#include "Physics/CollisionCook.h"
#include "Assets/AssetManager.h"
#include "Assets/Model.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Math/Vector4f.h"
#include "Physics/PhysicsMath.h"

#include "third_party/nlohmann/json.hpp"

#include <cctype>
#include <climits>
#include <cmath>
#include <fstream>
#include <sstream>

namespace Dark::Physics
{
    namespace
    {
        using json = nlohmann::json;

        constexpr std::uintmax_t kMaxJsonBytes   = 256u * 1024u;
        constexpr uint32_t       kMaxHullPoints  = 4096;
        constexpr int            kMaxHullVerts   = 64;
        constexpr float          kPointEps       = 1.0e-5f;

        bool jsonToFloat(const json& v, float& out)
        {
            if (const auto* f = v.get_ptr<const json::number_float_t*>())
            {
                out = static_cast<float>(*f);
                return true;
            }
            if (const auto* i = v.get_ptr<const json::number_integer_t*>())
            {
                out = static_cast<float>(*i);
                return true;
            }
            if (const auto* u = v.get_ptr<const json::number_unsigned_t*>())
            {
                out = static_cast<float>(*u);
                return true;
            }
            return false;
        }

        bool jsonToInt(const json& v, int& out)
        {
            if (const auto* i = v.get_ptr<const json::number_integer_t*>())
            {
                out = static_cast<int>(*i);
                return true;
            }
            if (const auto* u = v.get_ptr<const json::number_unsigned_t*>())
            {
                if (*u > static_cast<json::number_unsigned_t>(INT_MAX))
                    return false;
                out = static_cast<int>(*u);
                return true;
            }
            return false;
        }

        bool readString(const json& obj, const char* key, std::string& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end() || !it->is_string())
                return false;
            if (const auto* s = it->get_ptr<const json::string_t*>())
            {
                out = *s;
                return true;
            }
            return false;
        }

        bool readBool(const json& obj, const char* key, bool& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end() || !it->is_boolean())
                return false;
            if (const auto* b = it->get_ptr<const json::boolean_t*>())
            {
                out = *b;
                return true;
            }
            return false;
        }

        bool readInt(const json& obj, const char* key, int& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end())
                return false;
            return jsonToInt(*it, out);
        }

        Math::Vector3f transformPoint(const Math::Matrix4f& m, const Math::Vector3f& p)
        {
            const Math::Vector4f wp = m * Math::Vector4f(p.x, p.y, p.z, 1.0f);
            return Math::Vector3f(wp.x, wp.y, wp.z);
        }

        Math::Vector3f scalePoint(const Math::Vector3f& p, const Math::Vector3f& scale)
        {
            return Math::Vector3f(p.x * scale.x, p.y * scale.y, p.z * scale.z);
        }

        bool nearPoint(const Math::Vector3f& a, const Math::Vector3f& b)
        {
            return std::fabs(a.x - b.x) <= kPointEps && std::fabs(a.y - b.y) <= kPointEps && std::fabs(a.z - b.z) <= kPointEps;
        }

        std::vector<Math::Vector3f> uniquePoints(const std::vector<Math::Vector3f>& pts)
        {
            std::vector<Math::Vector3f> out;
            out.reserve(pts.size());
            for (const Math::Vector3f& p : pts)
            {
                bool dup = false;
                for (const Math::Vector3f& e : out)
                {
                    if (nearPoint(e, p))
                    {
                        dup = true;
                        break;
                    }
                }
                if (!dup)
                    out.push_back(p);
                if (out.size() >= kMaxHullPoints)
                    break;
            }
            return out;
        }

        Math::AABox3f aabbOf(const std::vector<Math::Vector3f>& pts)
        {
            Math::AABox3f box = Math::AABox3f::Empty();
            for (const Math::Vector3f& p : pts)
                box.ExpandToInclude(p);
            return box;
        }

        std::vector<Math::Vector3f> partPointsRoot(const Model::Part& part, const Math::Vector3f& scale)
        {
            std::vector<Math::Vector3f> pts;
            pts.reserve(part.mesh.positions.size());
            for (const Math::Vector3f& p : part.mesh.positions)
                pts.push_back(scalePoint(transformPoint(part.localToRoot, p), scale));
            return pts;
        }

        CollisionGeom geomFromString(std::string_view s)
        {
            if (s == "box")
                return CollisionGeom::Box;
            if (s == "sphere")
                return CollisionGeom::Sphere;
            if (s == "capsule")
                return CollisionGeom::Capsule;
            if (s == "hull")
                return CollisionGeom::Hull;
            if (s == "mesh")
                return CollisionGeom::Mesh;
            return CollisionGeom::None;
        }

        CollisionGeom classifyFromName(std::string_view name)
        {
            std::string lower(name);
            for (char& c : lower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            auto has = [&](const char* tok) { return lower.find(tok) != std::string::npos; };
            if (has("_colcapsule"))
                return CollisionGeom::Capsule;
            if (has("_colsphere"))
                return CollisionGeom::Sphere;
            if (has("_colbox"))
                return CollisionGeom::Box;
            if (has("_colmesh") || has("_trimesh"))
                return CollisionGeom::Mesh;
            if (has("_colconv") || has("_hull"))
                return CollisionGeom::Hull;
            return CollisionGeom::Hull;
        }

        CollisionBodyHint bodyHintFromString(std::string_view s)
        {
            if (s == "dynamic")
                return CollisionBodyHint::Dynamic;
            if (s == "kinematic")
                return CollisionBodyHint::Kinematic;
            return CollisionBodyHint::Static;
        }

        uint64_t categoryFromString(std::string_view s)
        {
            if (s == "World")
                return static_cast<uint64_t>(PhysicsLayer::World);
            if (s == "Player")
                return static_cast<uint64_t>(PhysicsLayer::Player);
            if (s == "Npc")
                return static_cast<uint64_t>(PhysicsLayer::Npc);
            if (s == "Projectile")
                return static_cast<uint64_t>(PhysicsLayer::Projectile);
            if (s == "Trigger")
                return static_cast<uint64_t>(PhysicsLayer::Trigger);
            if (s == "Debris")
                return static_cast<uint64_t>(PhysicsLayer::Debris);
            if (s == "Pickup")
                return static_cast<uint64_t>(PhysicsLayer::Pickup);
            return 0;
        }

        uint32_t resolveSurface(const PhysicsSurfaceCatalog* cat, std::string_view name)
        {
            if (!cat)
                return kNullPhysicsSurfaceId;
            if (!name.empty())
            {
                if (const PhysicsSurface* s = cat->find(name))
                    return s->internId;
                DE_LOG_WARN(LogCategory::Collision, "CollisionCook: unknown surface '{}'; using default", name);
            }
            return cat->getDefault().internId;
        }

        bool nodeMatches(std::string_view partName, std::string_view node)
        {
            if (node.empty())
                return false;
            if (partName == node)
                return true;
            const size_t slash = partName.find(" / ");
            const std::string_view stem = (slash == std::string_view::npos) ? partName : partName.substr(0, slash);
            return stem == node;
        }

        const Model::Part* findNode(const Model& model, std::string_view node)
        {
            auto search = [&](const std::vector<Model::Part>& parts) -> const Model::Part* {
                for (const Model::Part& p : parts)
                {
                    if (nodeMatches(p.name, node))
                        return &p;
                }
                return nullptr;
            };
            if (const Model::Part* p = search(model.collisionParts()))
                return p;
            if (const Model::Part* p = search(model.opaque()))
                return p;
            return search(model.translucent());
        }

        CollisionPart makeBoxFromAabb(const Math::AABox3f& box)
        {
            CollisionPart part;
            part.geom        = CollisionGeom::Box;
            part.localPos    = box.Center();
            part.halfExtents = box.Extents();
            if (part.halfExtents.x < 1.0e-4f)
                part.halfExtents.x = 1.0e-4f;
            if (part.halfExtents.y < 1.0e-4f)
                part.halfExtents.y = 1.0e-4f;
            if (part.halfExtents.z < 1.0e-4f)
                part.halfExtents.z = 1.0e-4f;
            return part;
        }

        CollisionPart makeSphereFromAabb(const Math::AABox3f& box)
        {
            CollisionPart part;
            part.geom     = CollisionGeom::Sphere;
            part.localPos = box.Center();
            const Math::Vector3f e = box.Extents();
            part.radius = Math::Max(Math::Max(e.x, e.y), e.z);
            if (part.radius < 1.0e-4f)
                part.radius = 1.0e-4f;
            return part;
        }

        CollisionPart makeCapsuleFromAabb(const Math::AABox3f& box)
        {
            CollisionPart part;
            part.geom     = CollisionGeom::Capsule;
            part.localPos = box.Center();
            const Math::Vector3f e = box.Extents();
            part.radius            = Math::Max(e.x, e.z);
            if (part.radius < 1.0e-4f)
                part.radius = 1.0e-4f;
            float halfSeg = e.y - part.radius;
            if (halfSeg < 0.0f)
                halfSeg = 0.0f;
            part.capsuleA = Math::Vector3f(0.0f, -halfSeg, 0.0f);
            part.capsuleB = Math::Vector3f(0.0f, halfSeg, 0.0f);
            return part;
        }

        CollisionPart makeHullOrFallback(std::vector<Math::Vector3f> pts, std::string_view name, int maxVerts)
        {
            if (maxVerts < 4)
                maxVerts = 4;
            if (maxVerts > kMaxHullVerts)
                maxVerts = kMaxHullVerts;
            std::vector<Math::Vector3f> unique = uniquePoints(pts);
            if (unique.size() > static_cast<size_t>(maxVerts))
                unique.resize(static_cast<size_t>(maxVerts));
            if (unique.size() < 4)
            {
                DE_LOG_WARN(LogCategory::Collision, "CollisionCook: hull degenerate on '{}'; falling back to AABB box", name);
                const Math::AABox3f box = pts.empty() ? aabbOf(unique) : aabbOf(pts);
                CollisionPart part      = makeBoxFromAabb(box.IsValid() ? box : Math::AABox3f(Math::Vector3f(-0.5f, -0.5f, -0.5f), Math::Vector3f(0.5f, 0.5f, 0.5f)));
                part.nodeName           = std::string(name);
                part.maxHullVerts       = maxVerts;
                return part;
            }
            CollisionPart part;
            part.geom         = CollisionGeom::Hull;
            part.hullPoints   = std::move(unique);
            part.maxHullVerts = maxVerts;
            part.nodeName     = std::string(name);
            return part;
        }

        CollisionPart makeMesh(const Model::Part& src, const Math::Vector3f& scale)
        {
            CollisionPart part;
            part.geom     = CollisionGeom::Mesh;
            part.nodeName = src.name;
            part.meshPositions.reserve(src.mesh.positions.size());
            for (const Math::Vector3f& p : src.mesh.positions)
                part.meshPositions.push_back(scalePoint(transformPoint(src.localToRoot, p), scale));
            part.meshIndices = src.mesh.indices;
            return part;
        }

        CollisionPart cookNamedPart(const Model::Part& src, CollisionGeom geom, const Math::Vector3f& scale, int maxVerts)
        {
            const std::vector<Math::Vector3f> pts = partPointsRoot(src, scale);
            const Math::AABox3f               box = aabbOf(pts);
            CollisionPart                     part;
            switch (geom)
            {
            case CollisionGeom::Box:
                part = makeBoxFromAabb(box);
                break;
            case CollisionGeom::Sphere:
                part = makeSphereFromAabb(box);
                break;
            case CollisionGeom::Capsule:
                part = makeCapsuleFromAabb(box);
                break;
            case CollisionGeom::Mesh:
                part = makeMesh(src, scale);
                break;
            case CollisionGeom::Hull:
            default:
                part = makeHullOrFallback(pts, src.name, maxVerts);
                break;
            }
            part.nodeName = src.name;
            return part;
        }

        void applySurfaceSensorCategory(CollisionPart& part, const CollisionCookDesc& desc, std::string_view surface, bool sensor, uint64_t categoryBits)
        {
            part.surfaceId    = resolveSurface(desc.surfaces, surface.empty() ? desc.surfaceName : std::string(surface));
            part.sensor       = sensor || desc.sensor;
            part.categoryBits = categoryBits;
        }

        bool cookCube(const CollisionCookDesc& desc, CollisionShape& out)
        {
            CollisionPart part;
            part.geom        = CollisionGeom::Box;
            part.halfExtents = bakeBoxHalfExtents(Math::Vector3f(0.5f, 0.5f, 0.5f), desc.scale);
            applySurfaceSensorCategory(part, desc, desc.surfaceName, desc.sensor, static_cast<uint64_t>(PhysicsLayer::World));
            out.addPart(std::move(part));
            return true;
        }

        bool cookSphere(const CollisionCookDesc& desc, CollisionShape& out)
        {
            CollisionPart part;
            part.geom   = CollisionGeom::Sphere;
            part.radius = bakeSphereRadius(0.5f, desc.scale);
            applySurfaceSensorCategory(part, desc, desc.surfaceName, desc.sensor, static_cast<uint64_t>(PhysicsLayer::World));
            out.addPart(std::move(part));
            return true;
        }

        bool cookPawn(const CollisionCookDesc& desc, CollisionShape& out)
        {
            CollisionPart part;
            part.geom   = CollisionGeom::Capsule;
            part.radius = kPawnCapsuleRadius;
            float height = kPawnDefaultHeight;
            if (desc.bounds.IsValid())
            {
                const float hy = desc.bounds.Size().y;
                if (hy > 2.0f * kPawnCapsuleRadius)
                    height = hy;
            }
            float segment = height - 2.0f * kPawnCapsuleRadius;
            if (segment < 0.0f)
                segment = 0.0f;
            part.capsuleA = Math::Vector3f(0.0f, 0.0f, 0.0f);
            part.capsuleB = Math::Vector3f(0.0f, segment, 0.0f);
            applySurfaceSensorCategory(part, desc, desc.surfaceName, desc.sensor, static_cast<uint64_t>(PhysicsLayer::Player));
            out.addPart(std::move(part));
            return true;
        }

        bool parseSidecarInto(std::string_view jsonText, CollisionSidecar& parsed)
        {
            parsed = CollisionSidecar{};
            if (jsonText.empty() || jsonText.size() > kMaxJsonBytes)
            {
                DE_LOG_ERROR(LogCategory::Collision, "CollisionCook: empty or oversized sidecar json");
                return false;
            }
            const json root = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
            if (root.is_discarded() || !root.is_object())
            {
                DE_LOG_ERROR(LogCategory::Collision, "CollisionCook: discarded or non-object sidecar json");
                return false;
            }
            const auto verIt = root.find("version");
            if (verIt != root.end())
            {
                float ver = 0.0f;
                if (!jsonToFloat(*verIt, ver) || static_cast<int>(ver) != 1)
                {
                    DE_LOG_ERROR(LogCategory::Collision, "CollisionCook: unsupported sidecar version");
                    return false;
                }
                parsed.version = 1;
            }
            const json* rootObj = &root;
            const auto  rootIt  = root.find("root");
            if (rootIt != root.end() && rootIt->is_object())
                rootObj = &*rootIt;
            readString(*rootObj, "body", parsed.body);
            const auto partsIt = rootObj->find("parts");
            if (partsIt == rootObj->end() || !partsIt->is_array())
            {
                DE_LOG_ERROR(LogCategory::Collision, "CollisionCook: sidecar missing parts array");
                return false;
            }
            for (const json& item : *partsIt)
            {
                if (!item.is_object())
                {
                    DE_LOG_WARN(LogCategory::Collision, "CollisionCook: skipping non-object sidecar part");
                    continue;
                }
                CollisionSidecarPart p;
                if (!readString(item, "node", p.node) || p.node.empty())
                {
                    DE_LOG_WARN(LogCategory::Collision, "CollisionCook: sidecar part missing node");
                    continue;
                }
                readString(item, "geom", p.geom);
                readString(item, "surface", p.surface);
                readString(item, "category", p.category);
                readBool(item, "sensor", p.sensor);
                readInt(item, "maxVerts", p.maxVerts);
                if (p.maxVerts <= 0)
                    p.maxVerts = kDefaultMaxHullVerts;
                parsed.parts.push_back(std::move(p));
            }
            return true;
        }

        bool loadSidecarFile(const std::filesystem::path& path, CollisionSidecar& out)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec) || ec)
                return false;
            const auto sz = std::filesystem::file_size(path, ec);
            if (ec || sz > kMaxJsonBytes)
            {
                DE_LOG_ERROR(LogCategory::Collision, "CollisionCook: sidecar unreadable or too large '{}'", path.string());
                return false;
            }
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            std::ostringstream ss;
            ss << in.rdbuf();
            return parseCollisionSidecar(ss.str(), out);
        }

        bool cookFromSidecar(const CollisionCookDesc& desc, const CollisionSidecar& sidecar, CollisionShape& out)
        {
            out.setBodyHint(bodyHintFromString(sidecar.body));
            for (const CollisionSidecarPart& sp : sidecar.parts)
            {
                const Model::Part* node = findNode(*desc.model, sp.node);
                if (!node)
                {
                    DE_LOG_WARN(LogCategory::Collision, "CollisionCook: sidecar node '{}' not found", sp.node);
                    continue;
                }
                CollisionGeom geom = geomFromString(sp.geom);
                if (geom == CollisionGeom::None)
                    geom = classifyFromName(node->name);
                CollisionPart part = cookNamedPart(*node, geom, desc.scale, sp.maxVerts);
                applySurfaceSensorCategory(part, desc, sp.surface, sp.sensor, categoryFromString(sp.category));
                out.addPart(std::move(part));
            }
            return out.valid();
        }

        bool cookFromCollisionParts(const CollisionCookDesc& desc, CollisionShape& out)
        {
            for (const Model::Part& src : desc.model->collisionParts())
            {
                const CollisionGeom geom = classifyFromName(src.name);
                CollisionPart       part = cookNamedPart(src, geom, desc.scale, kDefaultMaxHullVerts);
                applySurfaceSensorCategory(part, desc, desc.surfaceName, desc.sensor, static_cast<uint64_t>(PhysicsLayer::World));
                out.addPart(std::move(part));
            }
            return out.valid();
        }

        bool cookModel(const CollisionCookDesc& desc, CollisionShape& out)
        {
            if (!desc.model)
            {
                DE_LOG_ERROR(LogCategory::Collision, "CollisionCook: Model cook missing model");
                return false;
            }
            out.setSourcePath(desc.model->sourcePath());

            CollisionSidecar sidecar;
            bool             haveSidecar = false;
            if (!desc.sidecarJson.empty())
            {
                haveSidecar = parseCollisionSidecar(desc.sidecarJson, sidecar);
                if (!haveSidecar)
                    DE_LOG_WARN(LogCategory::Collision, "CollisionCook: sidecar json failed; falling back to named _col parts");
            }
            else if (desc.loadSidecarFile && !desc.model->sourcePath().empty())
            {
                const std::filesystem::path path = desc.model->sourcePath().parent_path() / (desc.model->sourcePath().stem().string() + ".collision.json");
                std::error_code             ec;
                if (std::filesystem::is_regular_file(path, ec) && !ec)
                {
                    haveSidecar = loadSidecarFile(path, sidecar);
                    if (!haveSidecar)
                        DE_LOG_WARN(LogCategory::Collision, "CollisionCook: sidecar file '{}' failed; falling back to named _col parts", path.string());
                }
            }

            if (haveSidecar)
            {
                if (cookFromSidecar(desc, sidecar, out))
                    return true;
                DE_LOG_WARN(LogCategory::Collision, "CollisionCook: sidecar produced no parts; falling back to named _col parts");
                out.clear();
                out.setSourcePath(desc.model->sourcePath());
            }

            if (cookFromCollisionParts(desc, out))
                return true;

            DE_LOG_INFO(LogCategory::Collision, "CollisionCook: model has no _col parts or sidecar; opt-in skipped");
            return false;
        }
    } // namespace

    bool parseCollisionSidecar(std::string_view jsonText, CollisionSidecar& out)
    {
        CollisionSidecar parsed;
        if (!parseSidecarInto(jsonText, parsed))
            return false;
        out = std::move(parsed);
        return true;
    }

    bool cookCollisionShape(const CollisionCookDesc& desc, CollisionShape& out)
    {
        out.clear();
        bool ok = false;
        switch (desc.kind)
        {
        case CollisionCookKind::Cube:
            ok = cookCube(desc, out);
            break;
        case CollisionCookKind::Sphere:
            ok = cookSphere(desc, out);
            break;
        case CollisionCookKind::Pawn:
            ok = cookPawn(desc, out);
            break;
        case CollisionCookKind::Model:
            ok = cookModel(desc, out);
            break;
        }
        if (!ok)
        {
            out.clear();
            return false;
        }
        return out.valid();
    }

    AssetRef<CollisionShape> internCollisionShape(AssetManager& assets, AssetRef<CollisionShape> shape, const std::string& cacheKey)
    {
        if (!shape || !shape->valid())
        {
            DE_LOG_ERROR(LogCategory::Collision, "CollisionCook: intern of empty CollisionShape");
            return {};
        }
        if (assets.registerAsset(shape, cacheKey) == NULL_ASSET)
        {
            DE_LOG_ERROR(LogCategory::Collision, "CollisionCook: registerAsset failed");
            return {};
        }
        return shape;
    }
} // namespace Dark::Physics
