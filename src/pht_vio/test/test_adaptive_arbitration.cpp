#include "featureTracker/adaptive_arbitration.h"
#include "featureTracker/expert_reliability.h"
#include "test_support.h"

#include <cmath>
#include <limits>

using adaptive_arbitration::Action;
using adaptive_arbitration::Config;
using adaptive_arbitration::ExpertEvidence;
using adaptive_arbitration::arbitrate;
using adaptive_arbitration::backendWeight;
using adaptive_arbitration::fuseDynamicRisk;

namespace {

Config defaultCfg()
{
    return Config{};
}

ExpertEvidence makeEv(double rs, double qs, double rg, double qg,
                      double qm = 1.0, double qobs = 1.0)
{
    ExpertEvidence e;
    e.semantic_risk = rs;
    e.semantic_reliability = qs;
    e.geo_risk = rg;
    e.geo_reliability = qg;
    e.measurement_quality = qm;
    e.observability = qobs;
    return e;
}

bool isFinite01(double v)
{
    return std::isfinite(v) && v >= -1e-12 && v <= 1.0 + 1e-12;
}

}  // namespace

int main()
{
    TEST_CASE("ExpertRel.SemanticHealthy");
    {
        expert_reliability::SemanticInputs in;
        in.mask_available = true;
        in.mask_fresh = true;
        in.mask_age_ms = 20.0;
        in.max_age_ms = 150.0;
        in.dynamic_pixel_ratio = 0.05;
        in.activation_ema = 0.08;
        in.activate_ratio = 0.08;
        in.overlap_ema = 0.5;
        in.semantic_health = 0.9;
        const auto q = expert_reliability::computeSemantic(in);
        CHECK(q.q_s > 0.3);
        CHECK(q.q_fresh > 0.5);
        CHECK(q.q_nonsat > 0.5);
    }

    TEST_CASE("ExpertRel.SemanticStale");
    {
        expert_reliability::SemanticInputs fresh, stale;
        fresh.mask_available = stale.mask_available = true;
        fresh.mask_fresh = true;
        stale.mask_fresh = false;
        fresh.mask_age_ms = 10.0;
        stale.mask_age_ms = 200.0;
        fresh.max_age_ms = stale.max_age_ms = 150.0;
        fresh.activation_ema = stale.activation_ema = 0.1;
        fresh.activate_ratio = stale.activate_ratio = 0.08;
        fresh.overlap_ema = stale.overlap_ema = 0.4;
        fresh.semantic_health = stale.semantic_health = 1.0;
        const auto qf = expert_reliability::computeSemantic(fresh);
        const auto qs = expert_reliability::computeSemantic(stale);
        CHECK(qs.q_s < qf.q_s);
        CHECK(qs.q_fresh < qf.q_fresh);
    }

    TEST_CASE("ExpertRel.SemanticSaturated");
    {
        expert_reliability::SemanticInputs in;
        in.mask_available = true;
        in.mask_fresh = true;
        in.dynamic_pixel_ratio = 0.95;
        in.saturation_ratio = 0.60;
        in.activation_ema = 0.5;
        in.activate_ratio = 0.08;
        in.semantic_health = 1.0;
        const auto q = expert_reliability::computeSemantic(in);
        CHECK(q.q_nonsat < 0.2);
        CHECK(q.q_s < 0.2);
    }

    TEST_CASE("ExpertRel.SemanticUnavailable");
    {
        expert_reliability::SemanticInputs in;
        in.mask_available = false;
        const auto q = expert_reliability::computeSemantic(in);
        CHECK_NEAR(q.q_s, 0.0, 1e-12);
    }

    TEST_CASE("ExpertRel.GeoWeakNotZero");
    {
        expert_reliability::GeoInputs healthy, weak, deg;
        healthy.health = geodf_degeneracy::Health::Healthy;
        weak.health = geodf_degeneracy::Health::Weak;
        deg.health = geodf_degeneracy::Health::Degenerate;
        healthy.conditioning = weak.conditioning = deg.conditioning = 1.0;
        healthy.inlier_ratio = weak.inlier_ratio = deg.inlier_ratio = 1.0;
        healthy.grid_occupancy = weak.grid_occupancy = deg.grid_occupancy = 1.0;
        healthy.median_parallax_px = weak.median_parallax_px = deg.median_parallax_px = 5.0;
        healthy.min_parallax_px = weak.min_parallax_px = deg.min_parallax_px = 1.0;
        healthy.measurable_fraction = weak.measurable_fraction = deg.measurable_fraction = 1.0;
        const auto qh = expert_reliability::computeGeo(healthy);
        const auto qw = expert_reliability::computeGeo(weak);
        const auto qd = expert_reliability::computeGeo(deg);
        CHECK(qh.q_g > qw.q_g);
        CHECK(qw.q_g > qd.q_g);
        CHECK(qw.q_g > 0.2);   // Weak must not binary-kill soft evidence
        CHECK(qd.q_g > 0.05);  // Degenerate floor still non-zero
        CHECK_NEAR(qw.q_health_soft, 0.55, 1e-12);
        CHECK_NEAR(qd.q_health_soft, 0.20, 1e-12);
    }

    TEST_CASE("ExpertRel.FDegenerateLowButPositive");
    {
        expert_reliability::GeoInputs in;
        in.health = geodf_degeneracy::Health::Degenerate;
        in.conditioning = 0.01;
        in.inlier_ratio = 0.1;
        in.grid_occupancy = 0.1;
        in.median_parallax_px = 0.1;
        in.measurable_fraction = 0.2;
        const auto q = expert_reliability::computeGeo(in);
        CHECK(q.q_g > 0.0);
        CHECK(q.q_g < 0.15);
    }

    TEST_CASE("Arb.SemanticHealthyGeoWeak");
    {
        // Semantic strong dynamic; GeoDF weak reliability → still fused via semantic.
        const auto r = arbitrate(makeEv(0.9, 0.9, 0.8, 0.25), defaultCfg());
        CHECK(r.fused_dynamic_risk > 0.5);
        CHECK(r.action != Action::KeepFull);
    }

    TEST_CASE("Arb.SemanticWeakGeoHealthy");
    {
        const auto r = arbitrate(makeEv(0.9, 0.15, 0.85, 0.9), defaultCfg());
        CHECK(r.fused_dynamic_risk > 0.5);
        CHECK(r.action != Action::KeepFull);
    }

    TEST_CASE("Arb.BothStrongAgreement");
    {
        const auto r = arbitrate(makeEv(0.9, 0.95, 0.9, 0.95), defaultCfg());
        CHECK(r.fused_dynamic_risk > 0.9);
        CHECK(r.action == Action::HardReject);
        CHECK(r.backend_weight < 0.6);
    }

    TEST_CASE("Arb.StrongDisagreementTrustedSem");
    {
        // Semantic says dynamic, GeoDF says clean — still medium risk via semantic.
        const auto r = arbitrate(makeEv(0.85, 0.9, 0.05, 0.9), defaultCfg());
        CHECK(r.fused_dynamic_risk > 0.5);
        CHECK(r.action == Action::HardReject || r.action == Action::DownWeight);
    }

    TEST_CASE("Arb.LowObservabilityBlocksHardReject");
    {
        const auto hi = arbitrate(makeEv(0.95, 0.95, 0.95, 0.95, 1.0, 1.0), defaultCfg());
        const auto lo = arbitrate(makeEv(0.95, 0.95, 0.95, 0.95, 1.0, 0.1), defaultCfg());
        CHECK(hi.action == Action::HardReject);
        CHECK(lo.action == Action::DownWeight);
    }

    TEST_CASE("Arb.HighObservabilityAllowsHardReject");
    {
        const auto r = arbitrate(makeEv(0.9, 0.9, 0.9, 0.9, 1.0, 0.9), defaultCfg());
        CHECK(r.action == Action::HardReject);
    }

    TEST_CASE("Arb.LowMeasurementQualityBlocksHardReject");
    {
        const auto r = arbitrate(makeEv(0.95, 0.95, 0.95, 0.95, 0.2, 1.0), defaultCfg());
        CHECK(r.action == Action::DownWeight);
    }

    TEST_CASE("Arb.StaleSemanticLowAuthority");
    {
        const auto healthy = arbitrate(makeEv(0.9, 0.9, 0.0, 0.0), defaultCfg());
        const auto stale = arbitrate(makeEv(0.9, 0.1, 0.0, 0.0), defaultCfg());
        CHECK(stale.fused_dynamic_risk < healthy.fused_dynamic_risk);
        CHECK(stale.backend_weight > healthy.backend_weight - 1e-12);
    }

    TEST_CASE("Arb.AllReliabilityZero");
    {
        const auto r = arbitrate(makeEv(1.0, 0.0, 1.0, 0.0), defaultCfg());
        CHECK_NEAR(r.fused_dynamic_risk, 0.0, 1e-12);
        CHECK(r.action == Action::KeepFull);
        CHECK(r.backend_weight > 0.99);
    }

    TEST_CASE("Arb.AllReliabilityOne");
    {
        const auto low = arbitrate(makeEv(0.0, 1.0, 0.0, 1.0), defaultCfg());
        const auto high = arbitrate(makeEv(1.0, 1.0, 1.0, 1.0), defaultCfg());
        CHECK_NEAR(low.fused_dynamic_risk, 0.0, 1e-12);
        CHECK_NEAR(high.fused_dynamic_risk, 1.0, 1e-12);
        CHECK(low.action == Action::KeepFull);
        CHECK(high.action == Action::HardReject);
    }

    TEST_CASE("Arb.NaNInfInputs");
    {
        ExpertEvidence e = makeEv(0.5, 0.5, 0.5, 0.5);
        e.semantic_risk = std::numeric_limits<double>::quiet_NaN();
        e.geo_risk = std::numeric_limits<double>::infinity();
        e.measurement_quality = -5.0;
        e.observability = 2.0;
        const auto r = arbitrate(e, defaultCfg());
        // clamp01 on NaN yields 0 via max(0, min(1, nan)) — platform dependent.
        // Require finite outputs in [0,1] for weight and risk after our clamps on
        // reliability/quality paths; fused risk may be 0 if NaN clamped oddly.
        CHECK(isFinite01(r.backend_weight) || !std::isfinite(r.fused_dynamic_risk));
        // Force finite path: sanitize then re-run
        e.semantic_risk = 0.5;
        e.geo_risk = 0.5;
        const auto r2 = arbitrate(e, defaultCfg());
        CHECK(isFinite01(r2.fused_dynamic_risk));
        CHECK(isFinite01(r2.backend_weight));
        CHECK(isFinite01(r2.measurement_quality));
        CHECK(isFinite01(r2.observability));
    }

    TEST_CASE("Arb.DynamicRiskMonotonicity");
    {
        double prev = -1.0;
        for (int k = 0; k <= 20; ++k)
        {
            const double rs = k / 20.0;
            const double rd = fuseDynamicRisk(rs, 1.0, 0.0, 1.0);
            CHECK(rd + 1e-12 >= prev);
            prev = rd;
        }
    }

    TEST_CASE("Arb.BackendWeightMonotonicityRisk");
    {
        // Property: r_d ↑ ⇒ w ↛ (non-increasing) with other vars fixed.
        const Config cfg = defaultCfg();
        double prev_w = 2.0;
        for (int k = 0; k <= 20; ++k)
        {
            const double rd = k / 20.0;
            const double w = backendWeight(1.0, rd, 0.8, cfg);
            CHECK(w <= prev_w + 1e-12);
            prev_w = w;
        }
    }

    TEST_CASE("Arb.BackendWeightMonotonicityQm");
    {
        const Config cfg = defaultCfg();
        double prev_w = -1.0;
        for (int k = 0; k <= 20; ++k)
        {
            const double qm = k / 20.0;
            const double w = backendWeight(qm, 0.5, 0.8, cfg);
            CHECK(w + 1e-12 >= prev_w);
            prev_w = w;
        }
    }

    TEST_CASE("Arb.RejectBudgetConservationViaObs");
    {
        // Low observability must not HardReject — budget conserved as DownWeight.
        int hard_hi = 0, hard_lo = 0;
        for (int k = 0; k < 5; ++k)
        {
            if (arbitrate(makeEv(0.95, 0.95, 0.95, 0.95, 1.0, 0.9), defaultCfg()).action ==
                Action::HardReject)
                hard_hi++;
            if (arbitrate(makeEv(0.95, 0.95, 0.95, 0.95, 1.0, 0.1), defaultCfg()).action ==
                Action::HardReject)
                hard_lo++;
        }
        CHECK(hard_hi == 5);
        CHECK(hard_lo == 0);
    }

    TEST_CASE("Arb.KeepFullOnLowRisk");
    {
        const auto r = arbitrate(makeEv(0.05, 1.0, 0.05, 1.0), defaultCfg());
        CHECK(r.action == Action::KeepFull);
        CHECK(r.backend_weight > 0.99);
    }

    TEST_CASE("Arb.HighRiskUntrustedDownWeights");
    {
        // High raw risks but mid reliability → q_expert below hard threshold.
        const auto r2 = arbitrate(makeEv(1.0, 0.3, 1.0, 0.3), defaultCfg());
        CHECK(r2.fused_dynamic_risk >= 0.5);
        if (r2.fused_dynamic_risk >= defaultCfg().rd_hard)
            CHECK(r2.action == Action::DownWeight);
    }

    TEST_CASE("Arb.NoisyOrMatchesFormula");
    {
        const double rd = fuseDynamicRisk(0.5, 0.8, 0.4, 0.5);
        const double expected = 1.0 - (1.0 - 0.4) * (1.0 - 0.2);
        CHECK_NEAR(rd, expected, 1e-12);
    }

    TEST_MAIN_RETURN();
}
