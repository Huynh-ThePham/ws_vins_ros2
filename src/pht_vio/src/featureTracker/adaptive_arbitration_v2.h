#pragma once

// Phase 3.5 authority-aware arbitration.  Dynamic truth evidence, expert
// reliability, expert agreement, measurement quality and observability remain
// separate all the way to the returned decision.

#include <algorithm>
#include <cmath>

namespace adaptive_arbitration_v2
{

inline double clamp01(double value)
{
    if (!std::isfinite(value))
        return 0.0;
    return std::min(1.0, std::max(0.0, value));
}

inline double clamp(double value, double lo, double hi)
{
    if (!std::isfinite(value))
        return lo;
    return std::min(hi, std::max(lo, value));
}

inline double smoothstep(double value, double lo, double hi)
{
    if (hi <= lo)
        return value >= hi ? 1.0 : 0.0;
    const double x = clamp01((value - lo) / (hi - lo));
    return x * x * (3.0 - 2.0 * x);
}

enum class Authority
{
    None = 0,
    Semantic,
    GeoDF,
    Joint,
};

inline const char *toString(Authority authority)
{
    switch (authority)
    {
        case Authority::None: return "NO_AUTHORITATIVE_EXPERT";
        case Authority::Semantic: return "SEMANTIC_AUTHORITATIVE";
        case Authority::GeoDF: return "GEODF_AUTHORITATIVE";
        case Authority::Joint: return "JOINT_AUTHORITATIVE";
    }
    return "UNKNOWN";
}

enum class Action
{
    Keep = 0,
    DownWeight,
    Quarantine,
    HardReject,
};

inline const char *toString(Action action)
{
    switch (action)
    {
        case Action::Keep: return "KEEP";
        case Action::DownWeight: return "DOWNWEIGHT";
        case Action::Quarantine: return "QUARANTINE";
        case Action::HardReject: return "HARD_REJECT";
    }
    return "UNKNOWN";
}

enum class Reason
{
    LowRisk = 0,
    MediumRisk,
    TrustedDynamic,
    HealthyDisagreement,
    NoReliableExpert,
    SafetyGuard,
};

enum class FusionMode
{
    ReliabilityNoisyOr = 0,  // F0, Phase-3.4 fusion ablation
    AuthorityWeighted = 1,   // F1
    LogOdds = 2,             // F2
};

struct ExpertAgreement
{
    double agreement = 0.0;
    double disagreement = 0.0;
    bool semantic_only = false;
    bool geo_only = false;
    bool both_dynamic = false;
    bool both_static = false;
};

struct ExpertEvidence
{
    double semantic_risk = 0.0;
    double semantic_reliability = 0.0;
    double geo_risk = 0.0;
    double geo_reliability = 0.0;
    double measurement_quality = 1.0;
    double expert_agreement = -1.0;  // <0 => derive from the two risks
    double observability = 1.0;
    double semantic_persistence = 0.0;
    double geo_persistence = 0.0;
    double track_age = 0.0;          // normalised [0,1]
    double redundancy = 1.0;         // normalised [0,1]
};

struct Config
{
    FusionMode fusion_mode = FusionMode::AuthorityWeighted;
    double min_weight = 0.05;
    double semantic_authority_min = 0.70;
    double geo_authority_min = 0.70;
    double agreement_min = 0.65;
    double dynamic_threshold = 0.55;
    double static_threshold = 0.25;
    double downweight_threshold = 0.30;
    double hard_risk = 0.88;
    double hard_reliability = 0.80;
    double hard_persistence = 0.80;
    double hard_track_age = 0.50;
    double min_observability_for_hard = 0.60;
    double min_redundancy_for_hard = 0.60;
    double dynamic_smooth_lo = 0.25;
    double dynamic_smooth_hi = 0.85;
    double alpha_base = 0.25;
    double alpha_authority_gain = 0.55;
    // F2 coefficients; frozen from TRAIN only.
    double logodds_bias = 0.0;
    double logodds_beta_semantic = 0.65;
    double logodds_beta_geo = 0.65;
    double logodds_beta_agreement = 0.25;
};

struct ArbitrationResult
{
    double fused_dynamic_risk = 0.0;
    double measurement_weight = 1.0;
    double dynamic_weight = 1.0;
    double backend_weight = 1.0;
    double authority_confidence = 0.0;
    ExpertAgreement expert_agreement;
    Authority authority = Authority::None;
    Action action = Action::Keep;
    Reason reason = Reason::LowRisk;
};

inline ExpertAgreement computeAgreement(double semantic_risk, double geo_risk,
                                        double semantic_reliability,
                                        double geo_reliability,
                                        const Config &cfg,
                                        double agreement_override = -1.0)
{
    ExpertAgreement out;
    const double rs = clamp01(semantic_risk);
    const double rg = clamp01(geo_risk);
    const bool sem_reliable = clamp01(semantic_reliability) >= cfg.semantic_authority_min;
    const bool geo_reliable = clamp01(geo_reliability) >= cfg.geo_authority_min;
    out.agreement = agreement_override >= 0.0
                        ? clamp01(agreement_override)
                        : clamp01(1.0 - std::abs(rs - rg));
    out.disagreement = 1.0 - out.agreement;
    out.semantic_only = sem_reliable && !geo_reliable;
    out.geo_only = geo_reliable && !sem_reliable;
    out.both_dynamic = sem_reliable && geo_reliable &&
                       rs >= cfg.dynamic_threshold && rg >= cfg.dynamic_threshold;
    out.both_static = sem_reliable && geo_reliable &&
                      rs <= cfg.static_threshold && rg <= cfg.static_threshold;
    return out;
}

inline Authority selectAuthority(const ExpertEvidence &ev,
                                 const ExpertAgreement &agreement,
                                 const Config &cfg)
{
    const bool sem_reliable = clamp01(ev.semantic_reliability) >= cfg.semantic_authority_min;
    const bool geo_reliable = clamp01(ev.geo_reliability) >= cfg.geo_authority_min;
    if (sem_reliable && geo_reliable)
        return agreement.agreement >= cfg.agreement_min ? Authority::Joint : Authority::None;
    if (sem_reliable)
        return Authority::Semantic;
    if (geo_reliable)
        return Authority::GeoDF;
    return Authority::None;
}

inline double safeLogit(double probability)
{
    const double p = clamp(clamp01(probability), 1e-4, 1.0 - 1e-4);
    return std::log(p / (1.0 - p));
}

inline double sigmoid(double value)
{
    if (value >= 0.0)
        return 1.0 / (1.0 + std::exp(-value));
    const double e = std::exp(value);
    return e / (1.0 + e);
}

inline double fuseDynamicRisk(const ExpertEvidence &ev, Authority authority,
                              const ExpertAgreement &agreement, const Config &cfg)
{
    const double rs = clamp01(ev.semantic_risk);
    const double rg = clamp01(ev.geo_risk);
    const double qs = clamp01(ev.semantic_reliability);
    const double qg = clamp01(ev.geo_reliability);

    if (cfg.fusion_mode == FusionMode::ReliabilityNoisyOr)
        return clamp01(1.0 - (1.0 - qs * rs) * (1.0 - qg * rg));

    if (cfg.fusion_mode == FusionMode::LogOdds)
    {
        // Agreement supports whichever shared verdict the experts make; it does
        // not alter either reliability.  Disagreement contributes zero.
        const double shared_sign = (0.5 * (rs + rg) - 0.5) * 2.0;
        const double agreement_term = agreement.agreement * shared_sign;
        const double log_odds = cfg.logodds_bias +
            cfg.logodds_beta_semantic * qs * safeLogit(rs) +
            cfg.logodds_beta_geo * qg * safeLogit(rg) +
            cfg.logodds_beta_agreement * agreement_term;
        return clamp01(sigmoid(log_odds));
    }

    // F1: a sole authoritative expert is not diluted by an unavailable peer.
    if (authority == Authority::Semantic)
        return rs;
    if (authority == Authority::GeoDF)
        return rg;
    const double denom = qs + qg;
    if (denom <= 1e-9)
        return 0.0;
    return clamp01((qs * rs + qg * rg) / denom);
}

inline double authorityConfidence(Authority authority, const ExpertEvidence &ev)
{
    switch (authority)
    {
        case Authority::Semantic: return clamp01(ev.semantic_reliability);
        case Authority::GeoDF: return clamp01(ev.geo_reliability);
        case Authority::Joint:
            return std::sqrt(clamp01(ev.semantic_reliability) *
                             clamp01(ev.geo_reliability));
        case Authority::None:
        default: return 0.0;
    }
}

inline double dynamicWeight(double fused_risk, double authority_confidence,
                            const Config &cfg)
{
    const double alpha = clamp01(cfg.alpha_base +
                                 cfg.alpha_authority_gain *
                                     clamp01(authority_confidence));
    return clamp01(1.0 - alpha * smoothstep(
        clamp01(fused_risk), cfg.dynamic_smooth_lo, cfg.dynamic_smooth_hi));
}

inline ArbitrationResult arbitrate(const ExpertEvidence &ev, const Config &cfg)
{
    ArbitrationResult out;
    out.measurement_weight = clamp01(ev.measurement_quality);
    out.expert_agreement = computeAgreement(
        ev.semantic_risk, ev.geo_risk, ev.semantic_reliability,
        ev.geo_reliability, cfg, ev.expert_agreement);
    out.authority = selectAuthority(ev, out.expert_agreement, cfg);
    out.authority_confidence = authorityConfidence(out.authority, ev);
    out.fused_dynamic_risk = fuseDynamicRisk(
        ev, out.authority, out.expert_agreement, cfg);
    out.dynamic_weight = dynamicWeight(
        out.fused_dynamic_risk, out.authority_confidence, cfg);
    // KEEP means no dynamic penalty, never "restore full precision".  q_m is
    // present in the final weight for every branch.
    out.backend_weight = clamp(out.measurement_weight * out.dynamic_weight,
                               cfg.min_weight, 1.0);

    const bool both_reliable =
        clamp01(ev.semantic_reliability) >= cfg.semantic_authority_min &&
        clamp01(ev.geo_reliability) >= cfg.geo_authority_min;
    const bool healthy_disagreement = both_reliable &&
        out.expert_agreement.agreement < cfg.agreement_min;
    if (healthy_disagreement)
    {
        out.action = Action::Quarantine;
        out.reason = Reason::HealthyDisagreement;
        return out;
    }

    if (out.fused_dynamic_risk < cfg.downweight_threshold)
    {
        out.action = Action::Keep;
        out.reason = Reason::LowRisk;
        return out;
    }
    if (out.authority == Authority::None)
    {
        out.action = Action::DownWeight;
        out.reason = Reason::NoReliableExpert;
        return out;
    }

    double persistence = 0.0;
    double reliability = 0.0;
    if (out.authority == Authority::Semantic)
    {
        persistence = clamp01(ev.semantic_persistence);
        reliability = clamp01(ev.semantic_reliability);
    }
    else if (out.authority == Authority::GeoDF)
    {
        persistence = clamp01(ev.geo_persistence);
        reliability = clamp01(ev.geo_reliability);
    }
    else
    {
        persistence = std::min(clamp01(ev.semantic_persistence),
                               clamp01(ev.geo_persistence));
        reliability = out.authority_confidence;
    }

    const bool hard_evidence = out.fused_dynamic_risk >= cfg.hard_risk &&
        reliability >= cfg.hard_reliability &&
        persistence >= cfg.hard_persistence;
    const bool hard_safety = clamp01(ev.track_age) >= cfg.hard_track_age &&
        clamp01(ev.observability) >= cfg.min_observability_for_hard &&
        clamp01(ev.redundancy) >= cfg.min_redundancy_for_hard;
    if (hard_evidence && hard_safety)
    {
        out.action = Action::HardReject;
        out.reason = Reason::TrustedDynamic;
    }
    else
    {
        out.action = Action::DownWeight;
        out.reason = hard_evidence ? Reason::SafetyGuard : Reason::MediumRisk;
    }
    return out;
}

}  // namespace adaptive_arbitration_v2
