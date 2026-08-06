#pragma once

#include <algorithm>

// Semantic-GeoDF evidence fusion.
//
// Three distinct quantities are kept separate on purpose (see the paper's
// backend-weighting section); collapsing them into one variable is what
// previously let a risk expression be published as a weight expression:
//
//   fused_risk      r_i = 1 - prod_b (1 - rho_i^b)          in [0, 1]
//   target_weight   w_i = clip(1 - r_i, w_min, 1), then confirmation caps
//   applied_weight  target_weight after per-id recovery / hysteresis
//
// The fusion is multiplicative ("noisy-OR-inspired"): it is NOT a probabilistic
// OR, because the semantic, geometric and agreement terms are not independent.
namespace sem_geodf
{

struct RiskConfig
{
    // Lower bound on the exported weight; a suspicious track is never silenced.
    double min_weight = 0.25;
    // Weight ceilings applied when the corresponding expert *confirms* a track.
    double semantic_weight = 0.55;
    double geo_weight = 0.75;
    double agree_weight = 0.25;
};

struct RiskEvidence
{
    bool semantic_hit = false;
    bool semantic_confirmed = false;
    bool geo_hit = false;
    bool geo_confirmed = false;
    double semantic_confidence = 0.0;
    double geo_scene_confidence = 0.0;
    double geo_error_confidence = 0.0;
    double overlap_confidence = 0.0;
};

// Per-expert risk terms and their multiplicative fusion. No weight mapping and
// no caps are applied here.
struct FusedRisk
{
    double semantic_risk = 0.0;
    double geo_risk = 0.0;
    double consensus_risk = 0.0;
    double fused_risk = 0.0;
};

struct WeightResult
{
    FusedRisk risk;
    // Weight the mapping asks for, before recovery/hysteresis.
    double target_weight = 1.0;
    // Severity used to rank candidates competing for the shared hard-rejection
    // budget. This is 1 - target_weight (so confirmation caps are respected),
    // deliberately NOT the raw fused risk.
    double ranking_risk = 0.0;
};

inline double clamp(double value, double lo, double hi)
{
    return std::min(hi, std::max(lo, value));
}

// rho_i^b for each expert b, then r_i = 1 - prod_b (1 - rho_i^b).
inline FusedRisk computeFusedRisk(const RiskEvidence &evidence, const RiskConfig &config)
{
    FusedRisk out;

    out.semantic_risk =
        evidence.semantic_hit
            ? (1.0 - config.semantic_weight) *
                  (evidence.semantic_confirmed ? 1.0
                                               : clamp(evidence.semantic_confidence, 0.0, 1.0))
            : 0.0;

    out.geo_risk =
        evidence.geo_hit
            ? (1.0 - config.geo_weight) *
                  std::max(clamp(evidence.geo_scene_confidence, 0.0, 1.0),
                           clamp(evidence.geo_error_confidence, 0.0, 1.0))
            : 0.0;

    // Agreement is only charged when both experts fire on the same track.
    out.consensus_risk =
        (evidence.semantic_hit && evidence.geo_hit)
            ? (1.0 - config.agree_weight) *
                  std::max(clamp(evidence.overlap_confidence, 0.0, 1.0),
                           std::min(clamp(evidence.semantic_confidence, 0.0, 1.0),
                                    std::max(clamp(evidence.geo_scene_confidence, 0.0, 1.0),
                                             clamp(evidence.geo_error_confidence, 0.0, 1.0))))
            : 0.0;

    out.fused_risk = 1.0 - (1.0 - out.semantic_risk) *
                               (1.0 - out.geo_risk) *
                               (1.0 - out.consensus_risk);
    return out;
}

// w_i = clip(1 - r_i, w_min, 1), then tightened by the confirmation caps.
//
// Publication semantics (precision interpretation, Hướng A):
//   The resulting w is a measurement-precision multiplier consumed by the
//   projection factors as Σ_w = Σ / w (implemented via √w residual/Jacobian
//   scaling). It is derived from fused risk heuristics and confirmation caps;
//   it is NOT a calibrated posterior inlier probability unless an explicit
//   calibration study says otherwise. Do not mix this with Huber ρ(·): Ceres
//   applies the robust loss on top of the already √w-scaled residual.
//
// Combining two observation qualities with min(w_i, w_j) is a conservative
// heuristic, not covariance propagation. Harmonic / full-covariance fusion is
// an optional ablation, not the main publication method.
inline double riskToWeight(double fused_risk,
                           const RiskEvidence &evidence,
                           const RiskConfig &config)
{
    double weight = clamp(1.0 - fused_risk, config.min_weight, 1.0);
    if (evidence.semantic_confirmed)
        weight = std::min(weight, config.semantic_weight);
    if (evidence.geo_confirmed)
        weight = std::min(weight, config.geo_weight);
    if (evidence.semantic_confirmed && evidence.geo_confirmed)
        weight = std::min(weight, config.agree_weight);
    // Re-clamp: a cap below min_weight must not push the track below the floor.
    return clamp(weight, config.min_weight, 1.0);
}

inline WeightResult computeMeasurementWeight(const RiskEvidence &evidence,
                                             const RiskConfig &config)
{
    WeightResult out;
    out.risk = computeFusedRisk(evidence, config);
    out.target_weight = riskToWeight(out.risk.fused_risk, evidence, config);
    out.ranking_risk = 1.0 - out.target_weight;
    return out;
}

// applied_weight: recovery only slows the rise back toward a higher target, so a
// transient false positive does not permanently suppress a track. Drops are
// immediate.
inline double recoverWeight(double previous_weight,
                            double target_weight,
                            double recovery_rate,
                            double min_weight)
{
    double weight = target_weight;
    if (target_weight > previous_weight)
        weight = previous_weight +
                 clamp(recovery_rate, 0.0, 1.0) * (target_weight - previous_weight);
    return clamp(weight, min_weight, 1.0);
}

}  // namespace sem_geodf
