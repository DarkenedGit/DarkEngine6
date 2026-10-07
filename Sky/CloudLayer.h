#pragma once

#include "Math/Vector2f.h"
#include "Math/Vector3f.h"

namespace Dark
{
namespace Sky
{

struct WeatherState;
class Environment;

// Artist knobs for the distant sky deck. Shading coefficients are shader literals.
struct CloudLayerDesc
{
    float altitude     = 2000.0f; // meters above world Y = 0
    float amount       = 1.0f;    // scales the literal tau
    float windSpeedMps = 8.0f;
};

// Twenty floats appended to SkyFrameConstants and SkyEvalParams. Names match HLSL.
struct CloudLayerGpu
{
    float clLightColor[4]; // rgb direct radiance, a = amount
    float clLightDir[4];   // xyz direction, w = altitude meters
    float clSkyTop[4];     // rgb analytic zenith, a unused
    float clSkyBottom[4];  // rgb analytic horizon, a unused
    float clWind[4];       // xy base offset meters, zw detail offset meters
};

static_assert(sizeof(CloudLayerGpu) == 20 * sizeof(float), "cloud tail is 20 floats");

constexpr float kCloudPlanetRadius = 6.36e6f;

void sanitizeCloudLayer(CloudLayerDesc& layer);

// Sphere hit of the deck. False when the ray points down or the camera is at/above the deck.
// Does not modify cameraY. t is meters along the view ray. Must match CloudShellHit in CloudLayer.hlsli.
bool cloudShellHit(float cameraY, float viewY, float altitude, float planetRadius, float& t);

void writeCloudLayer(CloudLayerGpu& out, const Environment& env);

// Weather-button speeds. Not used when a scene file loads.
float cloudWindSpeedMpsFor(const WeatherState& weather);

} // namespace Sky
} // namespace Dark
