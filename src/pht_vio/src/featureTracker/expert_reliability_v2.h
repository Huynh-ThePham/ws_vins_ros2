#pragma once

// Phase 3.5 expert reliability.  Reliability is deliberately computed from
// evidence internal to each expert.  Semantic/GeoDF agreement belongs to the
// arbitrator and is not an input to either function in this file.

#include <algorithm>
#include <array>
#include <cmath>

#include "geodf_degeneracy.h"

namespace expert_reliability_v2
{

inline double clamp01(double value)
{
    if (!std::isfinite(value))
        return 0.0;
    return std::min(1.0, std::max(0.0, value));
}

inline double smoothstep01(double value)
{
    const double x = clamp01(value);
    return x * x * (3.0 - 2.0 * x);
}

struct SemanticInputs
{
    bool mask_available = false;
    bool mask_fresh = false;
    double mask_age_ms = 0.0;
    double max_age_ms = 150.0;
    double dynamic_pixel_ratio = 0.0;
    // TRAIN-selected soft saturation gate.  Ordinary 20--40% dynamic masks are
    // not penalised; only a nearly frame-filling mask loses authority.
    double saturation_start = 0.65;
    double saturation_end = 0.92;
    double saturation_floor = 0.15;
    double semantic_confidence = 1.0;
    double temporal_consistency = 1.0;
    double support = 1.0;
    double mask_health = 1.0;
};

struct SemanticReliability
{
    double q_availability = 0.0;
    double q_fresh = 0.0;
    double q_saturation = 0.0;
    double q_confidence = 0.0;
    double q_temporal = 0.0;
    double q_support = 0.0;
    double q_health = 0.0;
    double q_s = 0.0;
};

inline double softSaturationScore(double ratio, double start, double end, double floor)
{
    const double r = clamp01(ratio);
    const double r0 = clamp01(start);
    const double r1 = std::max(r0 + 1e-6, clamp01(end));
    const double q_min = clamp01(floor);
    if (r <= r0)
        return 1.0;
    if (r >= r1)
        return q_min;
    const double transition = smoothstep01((r - r0) / (r1 - r0));
    return 1.0 - (1.0 - q_min) * transition;
}

inline SemanticReliability computeSemantic(const SemanticInputs &in)
{
    SemanticReliability out;
    out.q_availability = in.mask_available ? 1.0 : 0.0;
    if (!in.mask_available)
        return out;

    if (!in.mask_fresh)
        out.q_fresh = 0.10;
    else if (in.max_age_ms > 1e-6)
        out.q_fresh = clamp01(1.0 - 0.25 * std::max(0.0, in.mask_age_ms) /
                                        in.max_age_ms);
    else
        out.q_fresh = 1.0;

    out.q_saturation = softSaturationScore(
        in.dynamic_pixel_ratio, in.saturation_start, in.saturation_end,
        in.saturation_floor);
    out.q_confidence = clamp01(in.semantic_confidence);
    out.q_temporal = clamp01(in.temporal_consistency);
    out.q_support = clamp01(in.support);
    out.q_health = clamp01(in.mask_health);

    // Weighted geometric mean: one moderately weak component reduces authority
    // without the collapse caused by multiplying every component directly.
    constexpr double eps = 1e-6;
    const std::array<double, 7> q = {
        out.q_availability, out.q_fresh, out.q_saturation, out.q_confidence,
        out.q_temporal, out.q_support, out.q_health};
    const std::array<double, 7> alpha = {0.10, 0.22, 0.13, 0.10, 0.20, 0.15, 0.10};
    double log_q = 0.0;
    for (size_t i = 0; i < q.size(); ++i)
        log_q += alpha[i] * std::log(std::max(eps, q[i]));
    out.q_s = clamp01(std::exp(log_q));
    return out;
}

enum class GeoCombination
{
    GeometricMean = 0,
    WeightedGeometricMean = 1,
    SoftMinimum = 2,
};

struct GeoInputs
{
    // Independent raw quality components, already normalised to [0,1].
    double q_kappa = 0.0;
    double q_inlier = 0.0;
    double q_parallax = 0.0;
    double q_coverage = 0.0;
    double q_measurable = 0.0;
    geodf_degeneracy::Health health = geodf_degeneracy::Health::Degenerate;
    GeoCombination combination = GeoCombination::WeightedGeometricMean;
    double softmin_beta = 6.0;
    double weak_cap = 0.65;
    double degenerate_cap = 0.25;
};

struct GeoReliability
{
    double q_kappa = 0.0;
    double q_inlier = 0.0;
    double q_parallax = 0.0;
    double q_coverage = 0.0;
    double q_measurable = 0.0;
    double health_cap = 0.0;
    double q_g_uncapped = 0.0;
    double q_g = 0.0;
};

inline double geometricMean(const std::array<double, 5> &values,
                            const std::array<double, 5> &weights)
{
    constexpr double eps = 1e-6;
    double log_q = 0.0;
    double weight_sum = 0.0;
    for (size_t i = 0; i < values.size(); ++i)
    {
        const double weight = std::max(0.0, weights[i]);
        log_q += weight * std::log(std::max(eps, clamp01(values[i])));
        weight_sum += weight;
    }
    return weight_sum > 0.0 ? clamp01(std::exp(log_q / weight_sum)) : 0.0;
}

inline double softMinimum(const std::array<double, 5> &values, double beta)
{
    const double b = std::max(1e-3, beta);
    double sum = 0.0;
    for (double value : values)
        sum += std::exp(-b * clamp01(value));
    // log(mean(.)) keeps equal inputs unchanged and maps the range to [0,1].
    return clamp01(-std::log(sum / static_cast<double>(values.size())) / b);
}

inline GeoReliability computeGeo(const GeoInputs &in)
{
    GeoReliability out;
    out.q_kappa = clamp01(in.q_kappa);
    out.q_inlier = clamp01(in.q_inlier);
    out.q_parallax = clamp01(in.q_parallax);
    out.q_coverage = clamp01(in.q_coverage);
    out.q_measurable = clamp01(in.q_measurable);
    const std::array<double, 5> values = {
        out.q_kappa, out.q_inlier, out.q_parallax, out.q_coverage,
        out.q_measurable};

    switch (in.combination)
    {
        case GeoCombination::GeometricMean:
            out.q_g_uncapped = geometricMean(values, {1.0, 1.0, 1.0, 1.0, 1.0});
            break;
        case GeoCombination::SoftMinimum:
            out.q_g_uncapped = softMinimum(values, in.softmin_beta);
            break;
        case GeoCombination::WeightedGeometricMean:
        default:
            out.q_g_uncapped = geometricMean(values, {0.25, 0.25, 0.20, 0.15, 0.15});
            break;
    }

    switch (in.health)
    {
        case geodf_degeneracy::Health::Healthy:
            out.health_cap = 1.0;
            break;
        case geodf_degeneracy::Health::Weak:
            out.health_cap = clamp01(in.weak_cap);
            break;
        case geodf_degeneracy::Health::Degenerate:
        default:
            out.health_cap = clamp01(in.degenerate_cap);
            break;
    }
    // Health is a safety ceiling, not another multiplicative copy of the raw
    // geometric degradation.
    out.q_g = std::min(out.q_g_uncapped, out.health_cap);
    return out;
}

}  // namespace expert_reliability_v2
