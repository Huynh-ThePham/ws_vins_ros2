#pragma once

// Degradation-aware expert arbitration (no GT, no sequence labels).
//
// Separates:
//   q_m  — measurement quality
//   r_s, r_g — expert dynamic risks in [0,1]
//   q_s, q_g — expert reliability
//   r_d  — fused dynamic risk
//   w    — backend precision weight
//   action — KEEP_FULL / DOWNWEIGHT / HARD_REJECT
//
// Default fusion is reliability-gated noisy-OR (interpretable baseline).
// Coefficients are frozen protocol constants (fit only on TRAIN offline).

#include <algorithm>
#include <cmath>
#include <string>

#include "expert_reliability.h"

namespace adaptive_arbitration
{

inline double clamp01(double v)
{
    if (!std::isfinite(v))
        return 0.0;
    return std::min(1.0, std::max(0.0, v));
}

inline double clamp(double v, double lo, double hi)
{
    return std::min(hi, std::max(lo, v));
}

enum class Action
{
    KeepFull = 0,
    DownWeight,
    HardReject,
};

inline const char *toString(Action a)
{
    switch (a)
    {
        case Action::KeepFull: return "KEEP_FULL";
        case Action::DownWeight: return "DOWNWEIGHT";
        case Action::HardReject: return "HARD_REJECT";
    }
    return "UNKNOWN";
}

enum class Reason
{
    LowRisk = 0,
    MediumRisk,
    HighRiskTrusted,
    HighRiskUntrusted,
    LowObservability,
    LowMeasurementQuality,
};

inline const char *toString(Reason r)
{
    switch (r)
    {
        case Reason::LowRisk: return "low_risk";
        case Reason::MediumRisk: return "medium_risk";
        case Reason::HighRiskTrusted: return "high_risk_trusted";
        case Reason::HighRiskUntrusted: return "high_risk_untrusted";
        case Reason::LowObservability: return "low_observability";
        case Reason::LowMeasurementQuality: return "low_measurement_quality";
    }
    return "unknown";
}

// Frozen on TRAIN before HOLD-OUT. Documented in RUN_META / paper commons.
struct Config
{
    double min_weight = 0.25;
    // w = q_m * (1 - r_d * (lambda0 + lambda1 * q_expert))
    double lambda0 = 0.40;
    double lambda1 = 0.60;
    // Action thresholds on fused dynamic risk.
    double rd_downweight = 0.20;
    double rd_hard = 0.65;
    // Expert must be this reliable to authorise HARD_REJECT.
    double min_expert_for_hard = 0.35;
    // Observability gate for HARD_REJECT (uses q_obs from health).
    double min_obs_for_hard = 0.35;
};

struct ExpertEvidence
{
    double semantic_risk = 0.0;       // r_s before reliability gating
    double semantic_reliability = 1.0;  // q_s
    double geo_risk = 0.0;            // r_g
    double geo_reliability = 1.0;     // q_g
    double measurement_quality = 1.0; // q_m
    double observability = 1.0;       // q_obs
};

struct ArbitrationResult
{
    double fused_dynamic_risk = 0.0;  // r_d
    double backend_weight = 1.0;      // w
    double ranking_risk = 0.0;        // 1 - w (for shared reject budget)
    double q_expert = 0.0;
    Action action = Action::KeepFull;
    Reason reason = Reason::LowRisk;
    // Telemetry mirrors.
    double semantic_reliability = 1.0;
    double geo_reliability = 1.0;
    double measurement_quality = 1.0;
    double observability = 1.0;
};

// Reliability-gated noisy-OR:
//   r_d = 1 - (1 - q_s r_s)(1 - q_g r_g)
inline double fuseDynamicRisk(double r_s, double q_s, double r_g, double q_g)
{
    const double as = clamp01(q_s) * clamp01(r_s);
    const double ag = clamp01(q_g) * clamp01(r_g);
    return clamp01(1.0 - (1.0 - as) * (1.0 - ag));
}

inline double expertConfidence(double q_s, double r_s, double q_g, double r_g)
{
    // How much we trust the *active* dynamic evidence.
    const double as = clamp01(q_s) * clamp01(r_s);
    const double ag = clamp01(q_g) * clamp01(r_g);
    return clamp01(std::max(as, ag));
}

inline double backendWeight(double q_m, double r_d, double q_expert, const Config &cfg)
{
    const double gate = clamp01(cfg.lambda0 + cfg.lambda1 * clamp01(q_expert));
    const double raw = clamp01(q_m) * (1.0 - clamp01(r_d) * gate);
    return clamp(raw, cfg.min_weight, 1.0);
}

inline ArbitrationResult arbitrate(const ExpertEvidence &ev, const Config &cfg)
{
    ArbitrationResult out;
    out.semantic_reliability = clamp01(ev.semantic_reliability);
    out.geo_reliability = clamp01(ev.geo_reliability);
    out.measurement_quality = clamp01(ev.measurement_quality);
    out.observability = clamp01(ev.observability);

    out.fused_dynamic_risk = fuseDynamicRisk(
        ev.semantic_risk, out.semantic_reliability,
        ev.geo_risk, out.geo_reliability);
    out.q_expert = expertConfidence(
        out.semantic_reliability, ev.semantic_risk,
        out.geo_reliability, ev.geo_risk);

    out.backend_weight = backendWeight(
        out.measurement_quality, out.fused_dynamic_risk, out.q_expert, cfg);
    out.ranking_risk = 1.0 - out.backend_weight;

    // Action selection: redundancy / observability can only *downgrade* hard reject.
    if (out.fused_dynamic_risk < cfg.rd_downweight)
    {
        out.action = Action::KeepFull;
        out.reason = Reason::LowRisk;
        // Near-clean tracks keep full precision regardless of small numeric noise.
        out.backend_weight = std::max(out.backend_weight, 0.999);
        out.ranking_risk = 1.0 - out.backend_weight;
        return out;
    }

    if (out.fused_dynamic_risk >= cfg.rd_hard &&
        out.q_expert >= cfg.min_expert_for_hard)
    {
        if (out.observability < cfg.min_obs_for_hard)
        {
            out.action = Action::DownWeight;
            out.reason = Reason::LowObservability;
        }
        else if (out.measurement_quality < 0.35)
        {
            // Bad measurement → prefer downweight over deleting the only cue.
            out.action = Action::DownWeight;
            out.reason = Reason::LowMeasurementQuality;
        }
        else
        {
            out.action = Action::HardReject;
            out.reason = Reason::HighRiskTrusted;
        }
        return out;
    }

    if (out.fused_dynamic_risk >= cfg.rd_hard)
    {
        out.action = Action::DownWeight;
        out.reason = Reason::HighRiskUntrusted;
        return out;
    }

    out.action = Action::DownWeight;
    out.reason = Reason::MediumRisk;
    return out;
}

}  // namespace adaptive_arbitration
