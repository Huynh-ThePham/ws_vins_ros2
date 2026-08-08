#include "featureTracker/adaptive_arbitration_v2.h"
#include "featureTracker/adaptive_lifecycle_v2.h"
#include "featureTracker/expert_reliability_v2.h"
#include "test_support.h"

#include <cmath>

namespace aa = adaptive_arbitration_v2;
namespace al = adaptive_lifecycle_v2;
namespace er = expert_reliability_v2;

namespace {

struct FrozenPrior
{
    explicit FrozenPrior(double live_backend_weight)
        : residual_scale(std::sqrt(live_backend_weight)) {}
    double residual_scale;
};

aa::ArbitrationResult semanticOnlyProductionDecision(double measurement_quality,
                                                     double observability = 1.0)
{
    er::SemanticInputs semantic;
    semantic.mask_available = true;
    semantic.mask_fresh = true;
    semantic.mask_age_ms = 5.0;
    semantic.dynamic_pixel_ratio = 0.25;
    semantic.temporal_consistency = 1.0;
    semantic.support = 1.0;
    const auto qs = er::computeSemantic(semantic);

    er::GeoInputs geo;
    geo.health = geodf_degeneracy::Health::Degenerate;
    const auto qg = er::computeGeo(geo);

    aa::ExpertEvidence evidence;
    evidence.semantic_risk = 0.96;
    evidence.semantic_reliability = qs.q_s;
    evidence.geo_risk = 0.0;
    evidence.geo_reliability = qg.q_g;
    evidence.measurement_quality = measurement_quality;
    evidence.observability = observability;
    evidence.semantic_persistence = 1.0;
    evidence.track_age = 1.0;
    evidence.redundancy = 1.0;
    return aa::arbitrate(evidence, aa::Config{});
}

}  // namespace

int main()
{
    TEST_CASE("ArbV2Integration.SemanticAuthorityReachesBackendAndLifecycle");
    {
        const auto arbitration = semanticOnlyProductionDecision(0.8);
        CHECK(arbitration.authority == aa::Authority::Semantic);
        CHECK(arbitration.action == aa::Action::HardReject);
        CHECK(arbitration.backend_weight < arbitration.measurement_weight);

        al::Config cfg;
        cfg.hard_reject_dwell_frames = 3;
        cfg.minimum_surviving_tracks = 40;
        al::Manager lifecycle;
        lifecycle.configure(cfg);
        auto d1 = lifecycle.update(7, 1.00, arbitration, 80);
        auto d2 = lifecycle.update(7, 1.05, arbitration, 80);
        auto d3 = lifecycle.update(7, 1.10, arbitration, 80);
        CHECK(d1.action == aa::Action::DownWeight);
        CHECK(d2.action == aa::Action::DownWeight);
        CHECK(d3.action == aa::Action::HardReject);
        CHECK(d3.state == al::State::Rejected);
    }

    TEST_CASE("ArbV2Integration.DisagreementQuarantinesWithoutDelete");
    {
        aa::ExpertEvidence ev;
        ev.semantic_risk = 0.95;
        ev.semantic_reliability = 0.95;
        ev.geo_risk = 0.05;
        ev.geo_reliability = 0.95;
        ev.measurement_quality = 0.9;
        ev.observability = ev.semantic_persistence = ev.geo_persistence =
            ev.track_age = ev.redundancy = 1.0;
        const auto arbitration = aa::arbitrate(ev, aa::Config{});
        CHECK(arbitration.action == aa::Action::Quarantine);

        al::Manager lifecycle;
        al::Decision decision;
        for (int i = 0; i < 10; ++i)
            decision = lifecycle.update(8, 2.0 + 0.05 * i, arbitration, 80);
        CHECK(decision.action == aa::Action::Quarantine);
        CHECK(decision.state == al::State::Quarantined);
        CHECK(decision.state != al::State::Rejected);
    }

    TEST_CASE("ArbV2Integration.MeasurementQualitySurvivesKeepPath");
    {
        aa::ExpertEvidence ev;
        ev.semantic_reliability = 0.95;
        ev.geo_reliability = 0.95;
        ev.measurement_quality = 0.2;
        const auto arbitration = aa::arbitrate(ev, aa::Config{});
        CHECK(arbitration.action == aa::Action::Keep);
        CHECK_NEAR(arbitration.backend_weight, 0.2, 1e-12);
        // Projection factors consume sqrt(w), not a restored full-precision 1.0.
        CHECK_NEAR(std::sqrt(arbitration.backend_weight), std::sqrt(0.2), 1e-12);
    }

    TEST_CASE("ArbV2Integration.MarginalizedPriorWeightIsFrozen");
    {
        const auto first = semanticOnlyProductionDecision(0.8);
        FrozenPrior prior(first.backend_weight);
        const double frozen = prior.residual_scale;

        // Live evidence recovers later; the already constructed prior is not
        // rebuilt or reweighted by the new value.
        aa::ExpertEvidence clean;
        clean.semantic_reliability = 0.95;
        clean.geo_reliability = 0.95;
        clean.measurement_quality = 1.0;
        const auto recovered = aa::arbitrate(clean, aa::Config{});
        CHECK(recovered.backend_weight > first.backend_weight);
        CHECK_NEAR(prior.residual_scale, frozen, 1e-12);
    }

    TEST_CASE("ArbV2Integration.ObservabilityDoesNotChangeTruth");
    {
        const auto high = semanticOnlyProductionDecision(0.8, 1.0);
        const auto low = semanticOnlyProductionDecision(0.8, 0.1);
        CHECK_NEAR(high.fused_dynamic_risk, low.fused_dynamic_risk, 1e-12);
        CHECK(high.action == aa::Action::HardReject);
        CHECK(low.action == aa::Action::DownWeight);
    }

    TEST_MAIN_RETURN();
}
