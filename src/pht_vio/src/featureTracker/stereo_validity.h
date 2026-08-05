#pragma once

// Stereo physical validity contract (plan P1.5 / P1.6).
//
// Before this, a stereo match only had to pass LK plus a 0.5 px left-right cycle
// check. Nothing verified that the match was physically possible, so a match with
// the wrong disparity sign or a negative triangulated depth could reach the stereo
// factor, the depth initialiser, the right-camera GeoDF branch and the backend
// weight evidence.
//
// Two design rules:
//
//  1. Nothing about the rig is hard-coded. In particular the expected disparity
//     direction is DERIVED per feature from the extrinsic: for a ray x0 in cam0 at
//     inverse depth w = 1/Z, the cam1 projection is pi(R x0 + w t), which starts at
//     the point at infinity pi(R x0) for w = 0 and moves along the epipolar line as
//     w grows. A physically possible match must lie on that side. This is exact for
//     any rig geometry, not just a rectified horizontal pair.
//
//  2. There is exactly ONE stereo matcher. GeoDF must read the validated matches
//     produced here (see TrackValidity / validity_by_id) instead of running a second
//     LK of its own, which previously produced measurements inconsistent with the
//     ones the backend used.

#include <Eigen/Dense>

#include <cmath>
#include <map>

namespace stereo_validity
{

enum class Rejection
{
    NONE = 0,
    LK_FAILED,        // optical flow did not converge
    BORDER,           // right observation outside the usable image region
    LR_CYCLE,         // left -> right -> left round trip drifted
    EPIPOLAR,         // off the epipolar line implied by the extrinsic
    DISPARITY_SIGN,   // on the wrong side of the point at infinity
    DISPARITY_RANGE,  // disparity below the noise floor or implausibly large
    NEGATIVE_DEPTH,   // triangulates behind one of the cameras
    REPROJECTION,     // the two rays do not actually meet
};

inline const char *toString(Rejection rejection)
{
    switch (rejection)
    {
        case Rejection::NONE:             return "NONE";
        case Rejection::LK_FAILED:        return "LK_FAILED";
        case Rejection::BORDER:           return "BORDER";
        case Rejection::LR_CYCLE:         return "LR_CYCLE";
        case Rejection::EPIPOLAR:         return "EPIPOLAR";
        case Rejection::DISPARITY_SIGN:   return "DISPARITY_SIGN";
        case Rejection::DISPARITY_RANGE:  return "DISPARITY_RANGE";
        case Rejection::NEGATIVE_DEPTH:   return "NEGATIVE_DEPTH";
        case Rejection::REPROJECTION:     return "REPROJECTION";
    }
    return "UNKNOWN";
}

struct Config
{
    bool enable = true;
    double lr_cycle_max_px = 1.0;
    double epipolar_max_px = 2.0;
    double min_disparity_px = 0.5;
    double max_disparity_px = 200.0;
    double reprojection_max_px = 2.0;
    bool require_positive_depth = true;
};

// Stereo extrinsic, expressed as the transform taking a point from cam0 to cam1.
struct Rig
{
    bool valid = false;
    Eigen::Matrix3d R_c1_c0 = Eigen::Matrix3d::Identity();
    Eigen::Vector3d t_c1_c0 = Eigen::Vector3d::Zero();
    double baseline = 0.0;
    // Essential matrix E = [t]_x R, used for the epipolar residual.
    Eigen::Matrix3d E = Eigen::Matrix3d::Zero();
};

inline Eigen::Matrix3d skew(const Eigen::Vector3d &v)
{
    Eigen::Matrix3d m;
    m <<     0.0, -v.z(),  v.y(),
          v.z(),     0.0, -v.x(),
         -v.y(),  v.x(),     0.0;
    return m;
}

// body_T_cam0 and body_T_cam1 -> cam1_T_cam0.
inline Rig makeRig(const Eigen::Matrix3d &ric0, const Eigen::Vector3d &tic0,
                   const Eigen::Matrix3d &ric1, const Eigen::Vector3d &tic1)
{
    Rig rig;
    rig.R_c1_c0 = ric1.transpose() * ric0;
    rig.t_c1_c0 = ric1.transpose() * (tic0 - tic1);
    rig.baseline = rig.t_c1_c0.norm();
    rig.E = skew(rig.t_c1_c0) * rig.R_c1_c0;
    // A zero baseline is not a stereo rig; refuse rather than divide by it later.
    rig.valid = rig.baseline > 1e-6 && rig.R_c1_c0.allFinite() && rig.t_c1_c0.allFinite();
    return rig;
}

struct Result
{
    Rejection rejection = Rejection::NONE;
    bool valid() const { return rejection == Rejection::NONE; }

    double lr_cycle_px = 0.0;
    double epipolar_px = 0.0;
    double disparity_px = 0.0;
    double depth_cam0 = 0.0;
    double depth_cam1 = 0.0;
    double reprojection_px = 0.0;
};

// What GeoDF (and anything else downstream) is allowed to read: a match that has
// already passed the contract, with its measured quantities.
struct TrackValidity
{
    bool valid = false;
    double disparity_px = 0.0;
    double depth_cam0 = 0.0;
    double epipolar_px = 0.0;
    double reprojection_px = 0.0;
};

struct Counters
{
    long long stereo_match_total = 0;
    long long stereo_lk_failed = 0;
    long long stereo_border_failed = 0;
    long long stereo_lr_cycle_failed = 0;
    long long stereo_epipolar_failed = 0;
    long long stereo_wrong_disparity_sign = 0;
    long long stereo_disparity_range_failed = 0;
    long long stereo_negative_depth = 0;
    long long stereo_reprojection_failed = 0;
    long long stereo_valid_total = 0;

    void reset() { *this = Counters(); }

    void record(Rejection rejection)
    {
        stereo_match_total++;
        switch (rejection)
        {
            case Rejection::NONE:            stereo_valid_total++; break;
            case Rejection::LK_FAILED:       stereo_lk_failed++; break;
            case Rejection::BORDER:          stereo_border_failed++; break;
            case Rejection::LR_CYCLE:        stereo_lr_cycle_failed++; break;
            case Rejection::EPIPOLAR:        stereo_epipolar_failed++; break;
            case Rejection::DISPARITY_SIGN:  stereo_wrong_disparity_sign++; break;
            case Rejection::DISPARITY_RANGE: stereo_disparity_range_failed++; break;
            case Rejection::NEGATIVE_DEPTH:  stereo_negative_depth++; break;
            case Rejection::REPROJECTION:    stereo_reprojection_failed++; break;
        }
    }
};

// Perspective division guarded against a point in the camera plane.
inline bool project(const Eigen::Vector3d &p, Eigen::Vector2d &out)
{
    if (!(std::abs(p.z()) > 1e-9) || !p.allFinite())
        return false;
    out = p.head<2>() / p.z();
    return true;
}

// Triangulate two normalized rays. Solves Z1 * x1 = R * (Z0 * x0) + t in the least
// squares sense, so depth_cam0/depth_cam1 are the ray parameters and the residual
// is the distance between the two rays at their closest approach.
inline bool triangulate(const Rig &rig, const Eigen::Vector3d &x0, const Eigen::Vector3d &x1,
                        double &depth_cam0, double &depth_cam1, Eigen::Vector3d &residual)
{
    const Eigen::Vector3d a = rig.R_c1_c0 * x0;
    Eigen::Matrix<double, 3, 2> A;
    A.col(0) = a;
    A.col(1) = -x1;
    const Eigen::Vector2d z = A.colPivHouseholderQr().solve(-rig.t_c1_c0);
    if (!z.allFinite())
        return false;
    depth_cam0 = z(0);
    depth_cam1 = z(1);
    residual = a * depth_cam0 + rig.t_c1_c0 - x1 * depth_cam1;
    return residual.allFinite();
}

// The full contract for one match.
//
//   x0, x1        normalized (lifted) rays in cam0 / cam1, z-normalized by caller
//   lr_cycle_px   left -> right -> left round-trip error in pixels (0 if unavailable)
//   lk_ok         optical flow status
//   in_border     right observation inside the usable region
//   focal_px      pixel scale used to express normalized residuals in pixels
inline Result checkStereoMatch(const Eigen::Vector3d &x0, const Eigen::Vector3d &x1,
                               const Rig &rig, const Config &config,
                               double lr_cycle_px, bool lk_ok, bool in_border,
                               double focal_px)
{
    Result out;
    out.lr_cycle_px = lr_cycle_px;

    if (!lk_ok)
    {
        out.rejection = Rejection::LK_FAILED;
        return out;
    }
    if (!in_border)
    {
        out.rejection = Rejection::BORDER;
        return out;
    }
    if (!config.enable)
        return out;  // contract disabled: LK + border only, as before

    if (!rig.valid || !x0.allFinite() || !x1.allFinite())
    {
        // Without a usable extrinsic no physical claim can be made. Refusing is the
        // fail-closed choice: an unverifiable match must not reach the backend.
        out.rejection = Rejection::EPIPOLAR;
        return out;
    }

    if (lr_cycle_px > config.lr_cycle_max_px)
    {
        out.rejection = Rejection::LR_CYCLE;
        return out;
    }

    // Epipolar residual: point-to-line distance in cam1, scaled to pixels.
    const Eigen::Vector3d line = rig.E * x0;
    const double line_norm = line.head<2>().norm();
    if (line_norm > 1e-12)
        out.epipolar_px = std::abs(x1.dot(line)) / line_norm * focal_px;
    else
        out.epipolar_px = 0.0;
    if (out.epipolar_px > config.epipolar_max_px)
    {
        out.rejection = Rejection::EPIPOLAR;
        return out;
    }

    // Disparity relative to the point at infinity pi(R x0), and the direction the
    // match is expected to move as inverse depth grows. Both derived from the rig.
    Eigen::Vector2d p_inf;
    if (!project(rig.R_c1_c0 * x0, p_inf))
    {
        out.rejection = Rejection::NEGATIVE_DEPTH;
        return out;
    }
    Eigen::Vector2d p_near;
    // A small positive inverse-depth step; its magnitude cancels in the direction.
    const double kInverseDepthStep = 1e-3 / std::max(1e-9, rig.baseline);
    if (!project(rig.R_c1_c0 * x0 + kInverseDepthStep * rig.t_c1_c0, p_near))
    {
        out.rejection = Rejection::NEGATIVE_DEPTH;
        return out;
    }
    const Eigen::Vector2d expected = p_near - p_inf;
    Eigen::Vector2d observed_2d;
    if (!project(x1, observed_2d))
    {
        out.rejection = Rejection::NEGATIVE_DEPTH;
        return out;
    }
    const Eigen::Vector2d observed = observed_2d - p_inf;
    out.disparity_px = observed.norm() * focal_px;

    if (expected.norm() > 1e-12 && observed.norm() > 1e-12 &&
        observed.dot(expected) <= 0.0)
    {
        out.rejection = Rejection::DISPARITY_SIGN;
        return out;
    }

    if (out.disparity_px < config.min_disparity_px ||
        out.disparity_px > config.max_disparity_px)
    {
        out.rejection = Rejection::DISPARITY_RANGE;
        return out;
    }

    Eigen::Vector3d residual;
    if (!triangulate(rig, x0, x1, out.depth_cam0, out.depth_cam1, residual))
    {
        out.rejection = Rejection::NEGATIVE_DEPTH;
        return out;
    }
    if (config.require_positive_depth && (out.depth_cam0 <= 0.0 || out.depth_cam1 <= 0.0))
    {
        out.rejection = Rejection::NEGATIVE_DEPTH;
        return out;
    }

    // Distance between the two rays at closest approach, expressed in cam1 pixels.
    const double depth_scale = std::max(1e-6, std::abs(out.depth_cam1));
    out.reprojection_px = residual.norm() / depth_scale * focal_px;
    if (out.reprojection_px > config.reprojection_max_px)
    {
        out.rejection = Rejection::REPROJECTION;
        return out;
    }

    return out;
}

inline TrackValidity toTrackValidity(const Result &result)
{
    TrackValidity out;
    out.valid = result.valid();
    out.disparity_px = result.disparity_px;
    out.depth_cam0 = result.depth_cam0;
    out.epipolar_px = result.epipolar_px;
    out.reprojection_px = result.reprojection_px;
    return out;
}

using ValidityById = std::map<int, TrackValidity>;

}  // namespace stereo_validity
