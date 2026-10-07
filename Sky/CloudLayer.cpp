#include "Sky/CloudLayer.h"

#include "Sky/Environment.h"
#include "Math/MathHelper.h"

#include <cmath>

namespace Dark
{
namespace Sky
{

void sanitizeCloudLayer(CloudLayerDesc& layer)
{
    layer.altitude     = Math::Clamp(layer.altitude, 500.0f, 8000.0f);
    layer.amount       = Math::Clamp(layer.amount, 0.0f, 2.0f);
    layer.windSpeedMps = Math::Clamp(layer.windSpeedMps, 0.0f, 20.0f);
}

bool cloudShellHit(float cameraY, float viewY, float altitude, float planetRadius, float& t)
{
    if (viewY <= 0.0f || cameraY >= altitude - 1.0f)
        return false;

    const float y0 = cameraY;
    const float k  = (altitude - y0) * (2.0f * planetRadius + altitude + y0);
    const float b  = (planetRadius + y0) * viewY;
    const float s  = sqrtf(b * b + k);
    t              = (b >= 0.0f) ? (k / (s + b)) : (s - b);
    return true;
}

void writeCloudLayer(CloudLayerGpu& out, const Environment& env)
{
    const Math::Vector3f& color   = env.cloudLightColor();
    const Math::Vector3f& dir     = env.cloudLightDir();
    const Math::Vector3f& zenith  = env.skyZenith();
    const Math::Vector3f& horizon = env.skyHorizon();

    out.clLightColor[0] = color.x;
    out.clLightColor[1] = color.y;
    out.clLightColor[2] = color.z;
    out.clLightColor[3] = env.cloudLayer.amount;

    out.clLightDir[0] = dir.x;
    out.clLightDir[1] = dir.y;
    out.clLightDir[2] = dir.z;
    out.clLightDir[3] = env.cloudLayer.altitude;

    out.clSkyTop[0] = zenith.x;
    out.clSkyTop[1] = zenith.y;
    out.clSkyTop[2] = zenith.z;
    out.clSkyTop[3] = 0.0f;

    out.clSkyBottom[0] = horizon.x;
    out.clSkyBottom[1] = horizon.y;
    out.clSkyBottom[2] = horizon.z;
    out.clSkyBottom[3] = 0.0f;

    out.clWind[0] = env.cloudWindBase().x;
    out.clWind[1] = env.cloudWindBase().y;
    out.clWind[2] = env.cloudWindDetail().x;
    out.clWind[3] = env.cloudWindDetail().y;
}

float cloudWindSpeedMpsFor(const WeatherState& weather)
{
    if (weather.cloudCoverage < 0.2f && weather.rain < 0.05f)
        return 3.0f;
    if (weather.cloudCoverage < 0.6f && weather.rain < 0.05f)
        return 8.0f;
    if (weather.rain < 0.5f)
        return 10.0f;
    return 16.0f;
}

} // namespace Sky
} // namespace Dark
