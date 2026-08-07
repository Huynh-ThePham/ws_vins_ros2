#pragma once

// GeoDF Sampson residual helper (plan P0.2).
//
// Returns the Sampson SQUARED distance:
//   S^2 = (x'^T F x)^2 / (||F x||_{1:2}^2 + ||F^T x'||_{1:2}^2)
// with x = (x1,y1,1), x' = (x2,y2,1).
//
// Config keys geodf_sampson_th / geodf_stereo_sampson_th are calibrated against
// this squared quantity (not the unsquared Sampson distance used in
// stereo_validity.h). Keep thresholds in the same units.
//
// Fail-closed: a near-zero / non-finite denominator is INVALID geometry, never
// reported as S^2 = 0 (which would look like a perfect inlier).

#include <cmath>
#include <limits>

namespace geodf_sampson
{

struct SampsonResult
{
    bool valid = false;
    // Squared Sampson distance. Infinity when !valid.
    double squared_distance = std::numeric_limits<double>::infinity();
};

inline SampsonResult sampsonSquaredDistance(double f11, double f12, double f13,
                                            double f21, double f22, double f23,
                                            double f31, double f32, double f33,
                                            double x1, double y1,
                                            double x2, double y2)
{
    SampsonResult out;
    out.valid = false;
    out.squared_distance = std::numeric_limits<double>::infinity();

    if (!std::isfinite(x1) || !std::isfinite(y1) ||
        !std::isfinite(x2) || !std::isfinite(y2))
        return out;

    const double Fx1x = f11 * x1 + f12 * y1 + f13;
    const double Fx1y = f21 * x1 + f22 * y1 + f23;
    const double Ftx2x = f11 * x2 + f21 * y2 + f31;
    const double Ftx2y = f12 * x2 + f22 * y2 + f32;

    const double num = x2 * Fx1x + y2 * Fx1y + f31 * x1 + f32 * y1 + f33;
    const double denom =
        Fx1x * Fx1x + Fx1y * Fx1y + Ftx2x * Ftx2x + Ftx2y * Ftx2y;

    // Near-zero or non-finite denom => epipolar lines are unobservable.
    // Must NOT return 0 (old bug: treated as perfect inlier).
    if (!(denom >= 1e-12) || !std::isfinite(denom) || !std::isfinite(num))
        return out;

    const double d2 = (num * num) / denom;
    if (!std::isfinite(d2))
        return out;

    out.valid = true;
    out.squared_distance = d2;
    return out;
}

}  // namespace geodf_sampson
