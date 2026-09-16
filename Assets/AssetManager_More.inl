    AssetRef<Image> AssetManager::loadMemoryImage(const std::string& key, const void* bytes, size_t byteCount)
    {
        if (key.empty() || !bytes || byteCount == 0)
            return {};
        if (AssetRef<Image> existing = tryGetInterned<Image>(key, InternLookup::KeepStale))
            return existing;
        std::vector<uint8_t> copy(static_cast<const uint8_t*>(bytes), static_cast<const uint8_t*>(bytes) + byteCount);
        AssetRef<Image> decoded = m_images.decodeOnce(key,
                                                      [copy]() -> AssetRef<Image> {
                                                          auto img = std::make_shared<Image>();
                                                          if (!img->createFromMemory(copy.data(), copy.size()))
                                                              return {};
                                                          return img;
                                                      });
        return internDecodedImage(key, decoded);
    }

    AssetRef<AnimationSet> AssetManager::internAnimationSetLocked(const std::string& modelKey, const GltfCpuModel& cpu)
    {
        const std::string animKey = modelKey + "#anims";
        if (AssetRef<AnimationSet> existing = findInternedLocked<AnimationSet>(animKey))
            return existing;

        if (cpu.skeleton.joints.empty() && cpu.clips.empty())
            return {};

        auto set = std::make_shared<AnimationSet>();
        set->setFromParsed(cpu);
        AssetRef<AnimationSet> interned = internLoadedLocked<AnimationSet>(animKey, set);
        DE_LOG_INFO("AssetManager: cached AnimationSet '{}' id={} clips={}", animKey, interned->id, interned->clipCount());
        return interned;
    }

    AssetRef<Model> AssetManager::loadModel(const std::string& virtualPath)
    {
        const std::filesystem::path path = resolve(virtualPath);
        if (path.empty())
        {
            DE_LOG_ERROR("AssetManager: model not found '{}'", virtualPath);
            return {};
        }
        return loadModelFile(path);
    }

    AssetRef<Model> AssetManager::loadModelFile(const std::filesystem::path& absPath)
    {
        std::error_code ec;
        if (absPath.empty() || !std::filesystem::is_regular_file(absPath, ec) || ec)
        {
            DE_LOG_ERROR("AssetManager: model file not found '{}'", absPath.string());
            return {};
        }
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(absPath, ec);
        const std::filesystem::path path      = ec ? absPath : canonical;
        const std::string           key       = ImageCache::normalizePath(path);
        if (AssetRef<Model> existing = tryGetInterned<Model>(key))
            return existing;

        GltfCpuModel cpu;
        if (!parseGltfFile(path, cpu))
            return {};

        auto model = std::make_shared<Model>();
        if (!model->createFromParsed(*this, cpu, path))
            return {};

        std::lock_guard<std::mutex> lock(m_mutex);
        if (AssetRef<Model> existing = findInternedLocked<Model>(key))
            return existing;
        AssetRef<Model> interned = internLoadedLocked<Model>(key, model);
        interned->setAnimationSet(internAnimationSetLocked(key, cpu));
        DE_LOG_INFO("AssetManager: cached model '{}' id={}", path.string(), interned->id);
        return interned;
    }

    AssetRef<AnimationSet> AssetManager::loadAnimationSet(const std::string& virtualPath)
    {
        const std::filesystem::path path = resolve(virtualPath);
        if (path.empty())
        {
            DE_LOG_ERROR("AssetManager: animation set source not found '{}'", virtualPath);
            return {};
        }
        const std::string key     = ImageCache::normalizePath(path);
        const std::string animKey = key + "#anims";
        if (AssetRef<AnimationSet> existing = tryGetInterned<AnimationSet>(animKey))
            return existing;

        GltfCpuModel cpu;
        if (!parseGltfFile(path, cpu))
            return {};
        if (cpu.skeleton.joints.empty() && cpu.clips.empty())
        {
            DE_LOG_TRACE("AssetManager: no skeleton or clips in '{}'", virtualPath);
            return {};
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        AssetRef<AnimationSet> set = internAnimationSetLocked(key, cpu);
        if (AssetRef<Model> model = findInternedLocked<Model>(key, InternLookup::KeepStale))
        {
            if (!model->animationSet())
                model->setAnimationSet(set);
        }
        return set;
    }

    AssetRef<AnimGraphDef> AssetManager::loadAnimGraph(const std::string& virtualPath)
    {
        const std::filesystem::path path = resolve(virtualPath);
        if (path.empty())
        {
            DE_LOG_ERROR("AssetManager: anim graph not found '{}'", virtualPath);
            return {};
        }
        return loadAnimGraphFile(path);
    }

    AssetRef<AnimGraphDef> AssetManager::loadAnimGraphFile(const std::filesystem::path& absPath)
    {
        std::error_code ec;
        if (absPath.empty() || !std::filesystem::is_regular_file(absPath, ec) || ec)
        {
            DE_LOG_ERROR("AssetManager: anim graph file not found '{}'", absPath.string());
            return {};
        }
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(absPath, ec);
        const std::filesystem::path path      = ec ? absPath : canonical;
        const std::string           key       = ImageCache::normalizePath(path);
        if (AssetRef<AnimGraphDef> existing = tryGetInterned<AnimGraphDef>(key))
            return existing;

        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            DE_LOG_ERROR("AssetManager: cannot open anim graph '{}'", path.string());
            return {};
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();

        std::string modelPath;
        if (!peekAnimGraphModelPath(text.c_str(), modelPath))
        {
            DE_LOG_ERROR("AssetManager: anim graph '{}' missing model path", path.string());
            return {};
        }

        AssetRef<AnimationSet> set = loadAnimationSet(modelPath);
        if (!set)
        {
            const std::string localVirt = virtualPathFromAbsolute(path);
            if (!localVirt.empty())
            {
                std::filesystem::path gltfVirt(localVirt);
                gltfVirt.replace_extension(".gltf");
                set = loadAnimationSet(gltfVirt.generic_string());
                if (!set)
                {
                    gltfVirt.replace_extension(".glb");
                    set = loadAnimationSet(gltfVirt.generic_string());
                }
            }
        }
        if (!set)
        {
            DE_LOG_ERROR("AssetManager: anim graph '{}' could not load model '{}'", path.string(), modelPath);
            return {};
        }

        auto graph = std::make_shared<AnimGraphDef>();
        if (!parseAnimGraphJson(text.c_str(), *set, *graph))
            return {};
        graph->animSet = set;

        std::lock_guard<std::mutex> lock(m_mutex);
        if (AssetRef<AnimGraphDef> existing = findInternedLocked<AnimGraphDef>(key))
            return existing;
        AssetRef<AnimGraphDef> interned = internLoadedLocked<AnimGraphDef>(key, graph);
        DE_LOG_INFO("AssetManager: cached AnimGraph '{}' id={}", path.string(), interned->id);
        return interned;
    }

    AssetRef<AnimGraphDef> AssetManager::tryLoadAnimGraphForModel(const std::string& gltfVirtualPath)
    {
        std::filesystem::path vp(gltfVirtualPath);
        vp.replace_extension(".anim.json");
        const std::string jsonPath = vp.generic_string();
        if (resolve(jsonPath).empty())
            return {};
        return loadAnimGraph(jsonPath);
    }

    AssetRef<HsmGraphDef> AssetManager::tryLoadHsmGraph(const std::string& virtualPath)
    {
        if (resolve(virtualPath).empty())
            return {};
        return loadHsmGraph(virtualPath);
    }

    AssetRef<HsmGraphDef> AssetManager::loadHsmGraph(const std::string& virtualPath)
    {
        const std::filesystem::path path = resolve(virtualPath);
        if (path.empty())
        {
            DE_LOG_ERROR("AssetManager: HSM graph not found '{}'", virtualPath);
            return {};
        }
        AssetRef<HsmGraphDef> graph = loadHsmGraphFile(path);
        if (graph && graph->sourcePath.empty())
            graph->sourcePath = virtualPath;
        return graph;
    }

    AssetRef<HsmGraphDef> AssetManager::loadHsmGraphFile(const std::filesystem::path& absPath)
    {
        std::error_code ec;
        if (absPath.empty() || !std::filesystem::is_regular_file(absPath, ec) || ec)
        {
            DE_LOG_ERROR("AssetManager: HSM graph file not found '{}'", absPath.string());
            return {};
        }
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(absPath, ec);
        const std::filesystem::path path      = ec ? absPath : canonical;
        const std::string           key       = ImageCache::normalizePath(path);
        if (AssetRef<HsmGraphDef> existing = tryGetInterned<HsmGraphDef>(key))
            return existing;

        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            DE_LOG_ERROR("AssetManager: cannot open HSM graph '{}'", path.string());
            return {};
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();

        auto graph = std::make_shared<HsmGraphDef>();
        if (!parseHsmGraphJson(text.c_str(), *graph))
            return {};
        graph->sourcePath = path.generic_string();

        std::lock_guard<std::mutex> lock(m_mutex);
        if (AssetRef<HsmGraphDef> existing = findInternedLocked<HsmGraphDef>(key))
            return existing;
        AssetRef<HsmGraphDef> interned = internLoadedLocked<HsmGraphDef>(key, graph);
        DE_LOG_INFO("AssetManager: cached HsmGraph '{}' id={}", path.string(), interned->id);
        return interned;
    }
