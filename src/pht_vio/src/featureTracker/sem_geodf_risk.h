#pragma once

#include <algorithm>

namespace sem_geodf
{

struct RiskConfig
{
    double min_weight = 0.25;
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

struct RiskResult
{
    double semantic_risk = 0.0;
    double geo_risk = 0.0;
    double consensus_risk = 0.0;
    double combined_risk = 0.0;
    double target_weight = 1.0;
};

inline double clamp(double value, double lo, double hi)
{
    return std::min(hi, std::max(lo, value));
}

inline RiskResult computeRisk(const RiskEvidence &evidence, const RiskConfig &config)
{
    RiskResult result;

    result.semantic_risk =
        evidence.semantic_hit
            ? (1.0 - config.semantic_weight) *
                  (evidence.semantic_confirmed ? 1.0
                                               : clamp(evidence.semantic_confidence, 0.0, 1.0))
            : 0.0;

    result.geo_risk =
        evidence.geo_hit
            ? (1.0 - config.geo_weight) *
                  std::max(clamp(evidence.geo_scene_confidence, 0.0, 1.0),
                           clamp(evidence.geo_error_confidence, 0.0, 1.0))
            : 0.0;

    result.consensus_risk =
        (evidence.semantic_hit && evidence.geo_hit)
            ? (1.0 - config.agree_weight) *
                  std::max(clamp(evidence.overlap_confidence, 0.0, 1.0),
                           std::min(clamp(evidence.semantic_confidence, 0.0, 1.0),
                                    std::max(clamp(evidence.geo_scene_confidence, 0.0, 1.0),
                                             clamp(evidence.geo_error_confidence, 0.0, 1.0))))
            : 0.0;

    result.combined_risk =
        1.0 - (1.0 - result.semantic_risk) *
                  (1.0 - result.geo_risk) *
                  (1.0 - result.consensus_risk);

    result.target_weight = 1.0 - result.combined_risk;
    if (evidence.semantic_confirmed)
        result.target_weight = std::min(result.target_weight, config.semantic_weight);
    if (evidence.geo_confirmed)
        result.target_weight = std::min(result.target_weight, config.geo_weight);
    if (evidence.semantic_confirmed && evidence.geo_confirmed)
        result.target_weight = std::min(result.target_weight, config.agree_weight);

    result.target_weight = clamp(result.target_weight, config.min_weight, 1.0);
    // Hard-rejection ranking and backend weighting use one confidence model.
    result.combined_risk = 1.0 - result.target_weight;
    return result;
}

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
