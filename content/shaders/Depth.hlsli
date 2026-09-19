#ifndef DE_DEPTH_HLSLI
#define DE_DEPTH_HLSLI
static const float kReconstructMinDepth = 1.0e-7f;

// Cleared / sky EQUAL is exactly 0; do not use a 1e-8 epsilon.
bool IsSkyDepth(float d) { return d <= 0.0f; }

float ClampDepthForReconstruct(float d) { return max(d, kReconstructMinDepth); }

// Infinite reverse-Z: viewZ = near / ndcZ. nearZ is Camera3D::GetNearZ().
float LinearizeViewZ(float depth, float nearZ)
{
    return nearZ / ClampDepthForReconstruct(depth);
}
#endif
