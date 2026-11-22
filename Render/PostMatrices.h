#pragma once

#include "Math/Matrix4f.h"

namespace Dark
{
    // The camera matrices a declared Post pass needs (TAA, motion blur). SceneRenderer::addPostPasses
    // builds one while execute() builds the graph, before any pass executes; hosts write the jittered
    // view-proj in their GBuffer pass, so only a pointer to it is taken then and fill() reads it from
    // the pass exec. (Render-graph plan Phase 6 part 2 bug: Post inverted the matrix at setup.) The
    // previous view-proj is captured by value: hosts call endCameraFrame after execute().
    class PostMatrices
    {
    public:
        PostMatrices(const Math::Matrix4f& viewProj, bool havePrev, const Math::Matrix4f& prev) :
            m_viewProj(&viewProj),
            m_havePrev(havePrev),
            m_prev(prev)
        {
        }

        // Call from a pass exec only.
        void fill(float invViewProj[16], float prevViewProj[16]) const
        {
            m_viewProj->Inverse().store(invViewProj);
            (m_havePrev ? m_prev : *m_viewProj).store(prevViewProj);
        }

    private:
        const Math::Matrix4f* m_viewProj;
        bool                  m_havePrev;
        Math::Matrix4f        m_prev;
    };
} // namespace Dark
