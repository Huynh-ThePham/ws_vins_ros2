#pragma once

// Local pixel scale of a camodocal camera around an image point.
// Used to convert normalized-plane residuals (Sampson, reprojection) into
// pixels without hard-coding a global FOCAL_LENGTH for every camera.

#include <camodocal/camera_models/Camera.h>

#include <Eigen/Dense>
#include <cmath>

namespace camera_focal
{

inline double localFocalPx(const camodocal::CameraPtr &cam,
                           const Eigen::Vector2d &uv_px,
                           double fallback_focal_px)
{
    if (!cam)
        return fallback_focal_px;

    Eigen::Vector3d ray;
    cam->liftProjective(uv_px, ray);
    if (!(std::abs(ray.z()) > 1e-9) || !ray.allFinite())
        return fallback_focal_px;
    ray /= ray.z();

    constexpr double kDelta = 1e-3;
    Eigen::Vector2d u0, u1;
    cam->spaceToPlane(ray, u0);
    cam->spaceToPlane(Eigen::Vector3d(ray.x() + kDelta, ray.y(), 1.0), u1);
    if (!u0.allFinite() || !u1.allFinite())
        return fallback_focal_px;
    const double scale = (u1 - u0).norm() / kDelta;
    if (!(scale > 1.0) || !std::isfinite(scale))
        return fallback_focal_px;
    return scale;
}

}  // namespace camera_focal
