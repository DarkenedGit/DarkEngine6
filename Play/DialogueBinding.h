#pragma once

#include <string>

namespace Dark
{

    // Content-relative graph path. Not saved. An empty path is not a binding.
    struct DialogueBindingComponent
    {
        static constexpr const char* kTypeName = "DialogueBinding";

        std::string graph;
    };

} // namespace Dark
