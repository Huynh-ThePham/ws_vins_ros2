#pragma once

// Online expert reliability (no GT). Separates how much to trust Semantic / GeoDF
// from the dynamic risk those experts report. Components are logged individually;
// the product form is the default fusion for q_s / q_g.

#include <algorithm>
#include <cmath>

#include "geodf_degeneracy.h"

namespace expert_reliability
{

inline double clamp01(double v)
{
    if (!std::isfinite(v))
        return 0.0;
    return std::min(1.0, std::max(0.0, v));
}

struct SemanticReliability
{
    double q_fresh = 1.0;
    double q_nonsat = 1.0;
    double q_support = 1.0;
    double q_temporal = 1.0;
    double q_health = 1.0;
    double q_s = 1.0;  // product of the above
};

struct GeoReliability
{
    double q_kappa = 1.0;
    double q_inlier = 1.0;
    double q_parallax = 1.0;
    double q_coverage = 1.0;
    double q_measurable = 1.0;
    double q_health_soft = 1.0;  // Healthy=1, Weak≈0.55, Degenerate≈0.20 (not zero)
    double q_g = 1.0;
};

struct SemanticInputs
{
    bool mask_available = false;
    bool mask_fresh = false;
    double mask_age_ms = 0.0;
    double max_age_ms = 150.0;
    double dynamic_pixel_ratio = 0.0;
    double saturation_ratio = 0.60;
    double activation_ema = 0.0;
    double activate_ratio = 0.08;
    double overlap_ema = 0.0;
    double semantic_health = 1.0;  // from sem_policy::Health
};

struct GeoInputs
{
    geodf_degeneracy::Health health = geodf_degeneracy::Health::Healthy;
    double conditioning = 1.0;       // already [0,1] from degeneracy score
    double inlier_ratio = 1.0;
    double grid_occupancy = 1.0;
    double median_parallax_px = 0.0;
    double min_parallax_px = 1.0;
    double min_grid_occupancy = 0.35;
    double min_inlier_ratio = 0.35;
    double measurable_fraction = 1.0;  // finite Sampson / scored
    // Soft floors: never binary-kill GeoDF soft evidence (benchmark rejected that).
    double degenerate_floor = 0.20;
    double weak_scale = 0.55;
};

inline SemanticReliability computeSemantic(const SemanticInputs &in)
{
    SemanticReliability out;
    if (!in.mask_available)
    {
        out.q_fresh = out.q_nonsat = out.q_support = out.q_temporal = out.q_health = 0.0;
        out.q_s = 0.0;
        return out;
    }

    // Fresher masks → higher trust. Age beyond max_age already fails mask_fresh,
    // but keep a smooth score for near-limit ages.
    if (!in.mask_fresh)
        out.q_fresh = 0.15;
    else if (in.max_age_ms > 1e-6 && in.mask_age_ms >= 0.0)
        out.q_fresh = clamp01(1.0 - 0.5 * (in.mask_age_ms / in.max_age_ms));
    else
        out.q_fresh = 1.0;

    // Saturation: a nearly-full dynamic mask is uninformative.
    const double sat = std::max(1e-6, in.saturation_ratio);
    if (in.dynamic_pixel_ratio >= sat)
        out.q_nonsat = 0.1;
    else
        out.q_nonsat = clamp01(1.0 - (in.dynamic_pixel_ratio / sat));

    // Support: enough activation relative to the scene threshold.
    const double act = std::max(1e-6, in.activate_ratio);
    out.q_support = clamp01(in.activation_ema / act);

    // Temporal: overlap EMA with GeoDF is a consistency cue; pure semantic scenes
    // still get credit via activation_ema folded in at half weight.
    out.q_temporal = clamp01(0.5 * in.overlap_ema + 0.5 * out.q_support);

    out.q_health = clamp01(in.semantic_health);

    out.q_s = clamp01(out.q_fresh * out.q_nonsat * out.q_support * out.q_temporal *
                      out.q_health);
    return out;
}

inline GeoReliability computeGeo(const GeoInputs &in)
{
    GeoReliability out;
    out.q_kappa = clamp01(in.conditioning);

    const double min_in = std::max(1e-6, in.min_inlier_ratio);
    out.q_inlier = clamp01(in.inlier_ratio / min_in);

    const double min_par = std::max(1e-6, in.min_parallax_px);
    out.q_parallax = clamp01(in.median_parallax_px / min_par);

    const double min_grid = std::max(1e-6, in.min_grid_occupancy);
    out.q_coverage = clamp01(in.grid_occupancy / min_grid);

    out.q_measurable = clamp01(in.measurable_fraction);

    switch (in.health)
    {
        case geodf_degeneracy::Health::Healthy:
            out.q_health_soft = 1.0;
            break;
        case geodf_degeneracy::Health::Weak:
            out.q_health_soft = clamp01(in.weak_scale);
            break;
        case geodf_degeneracy::Health::Degenerate:
        default:
            out.q_health_soft = clamp01(in.degenerate_floor);
            break;
    }

    out.q_g = clamp01(out.q_kappa * out.q_inlier * out.q_parallax * out.q_coverage *
                      out.q_measurable * out.q_health_soft);
    return out;
}

}  // namespace expert_reliability
