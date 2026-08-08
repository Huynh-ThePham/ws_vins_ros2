#include "featureTracker/adaptive_arbitration_v2.h"
#include "featureTracker/expert_reliability_v2.h"
#include "test_support.h"

#include <cmath>

namespace aa = adaptive_arbitration_v2;
namespace er = expert_reliability_v2;

namespace {

aa::ExpertEvidence evidence(double rs, double qs, double rg, double qg,
                            double qm = 1.0, double qobs = 1.0)
{
    aa::ExpertEvidence ev;
    ev.semantic_risk = rs;
    ev.semantic_reliability = qs;
    ev.geo_risk = rg;
    ev.geo_reliability = qg;
    ev.measurement_quality = qm;
    ev.observability = qobs;
    ev.semantic_persistence = 1.0;
    ev.geo_persistence = 1.0;
    ev.track_age = 1.0;
    ev.redundancy = 1.0;
    return ev;
}

er::SemanticInputs healthySemantic()
{
    er::SemanticInputs in;
    in.mask_available = true;
    in.mask_fresh = true;
    in.mask_age_ms = 10.0;
    in.max_age_ms = 150.0;
    in.dynamic_pixel_ratio = 0.30;
    in.temporal_consistency = 0.95;
    in.support = 1.0;
    in.semantic_confidence = 0.95;
    return in;
}

}  // namespace

int main()
{
    TEST_CASE("ArbV2.SemanticReliabilityIndependentOfGeoDF");
    {
        const er::SemanticInputs in = healthySemantic();
        const double q_before = er::computeSemantic(in).q_s;
        // Agreement/GeoDF are intentionally in a different object and function.
        aa::ExpertEvidence ev = evidence(0.9, q_before, 0.0, 0.0);
        const auto a = aa::arbitrate(ev, aa::Config{});
        ev.geo_risk = 1.0;
        ev.geo_reliability = 1.0;
        const auto b = aa::arbitrate(ev, aa::Config{});
        CHECK_NEAR(er::computeSemantic(in).q_s, q_before, 1e-12);
        CHECK_NEAR(a.authority == aa::Authority::Semantic ? q_before : -1.0,
                   q_before, 1e-12);
        CHECK(std::abs(b.expert_agreement.agreement - a.expert_agreement.agreement) > 0.5);
    }

    TEST_CASE("ArbV2.SemanticSoftSaturation");
    {
        er::SemanticInputs ordinary = healthySemantic();
        ordinary.dynamic_pixel_ratio = 0.40;
        er::SemanticInputs saturated = ordinary;
        saturated.dynamic_pixel_ratio = 0.96;
        const auto qo = er::computeSemantic(ordinary);
        const auto qs = er::computeSemantic(saturated);
        CHECK_NEAR(qo.q_saturation, 1.0, 1e-12);
        CHECK(qs.q_saturation <= 0.151);
        CHECK(qo.q_s > qs.q_s);
    }

    TEST_CASE("ArbV2.GeoNoDoubleCount");
    {
        er::GeoInputs in;
        in.health = geodf_degeneracy::Health::Healthy;
        in.q_kappa = in.q_inlier = in.q_parallax = in.q_coverage =
            in.q_measurable = 0.60;
        in.combination = er::GeoCombination::WeightedGeometricMean;
        const auto q = er::computeGeo(in);
        CHECK_NEAR(q.q_g, 0.60, 1e-9);
        CHECK(q.q_g > 0.5);  // product would have collapsed to 0.6^5 ~= 0.078
    }

    TEST_CASE("ArbV2.GeoCombinationAblationsFinite");
    {
        for (int mode = 0; mode <= 2; ++mode)
        {
            er::GeoInputs in;
            in.health = geodf_degeneracy::Health::Healthy;
            in.q_kappa = 0.5;
            in.q_inlier = 0.7;
            in.q_parallax = 0.8;
            in.q_coverage = 0.6;
            in.q_measurable = 0.9;
            in.combination = static_cast<er::GeoCombination>(mode);
            const auto q = er::computeGeo(in);
            CHECK(std::isfinite(q.q_g));
            CHECK(q.q_g >= 0.0 && q.q_g <= 1.0);
            CHECK(q.q_g > 0.45);
        }
    }

    TEST_CASE("ArbV2.StaticBadMeasurementKeepsQm");
    {
        const auto out = aa::arbitrate(evidence(0.0, 0.95, 0.0, 0.95, 0.2),
                                       aa::Config{});
        CHECK(out.action == aa::Action::Keep);
        CHECK_NEAR(out.measurement_weight, 0.2, 1e-12);
        CHECK_NEAR(out.dynamic_weight, 1.0, 1e-12);
        CHECK_NEAR(out.backend_weight, 0.2, 1e-12);
    }

    TEST_CASE("ArbV2.SemanticOnlyAuthority");
    {
        const auto out = aa::arbitrate(evidence(0.95, 0.95, 0.1, 0.2), aa::Config{});
        CHECK(out.authority == aa::Authority::Semantic);
        CHECK_NEAR(out.fused_dynamic_risk, 0.95, 1e-12);
    }

    TEST_CASE("ArbV2.GeoOnlyAuthority");
    {
        const auto out = aa::arbitrate(evidence(0.1, 0.2, 0.95, 0.95), aa::Config{});
        CHECK(out.authority == aa::Authority::GeoDF);
        CHECK_NEAR(out.fused_dynamic_risk, 0.95, 1e-12);
    }

    TEST_CASE("ArbV2.HealthyDisagreementNeverImmediateReject");
    {
        const auto out = aa::arbitrate(evidence(0.95, 0.95, 0.05, 0.95), aa::Config{});
        CHECK(out.authority == aa::Authority::None);
        CHECK(out.action == aa::Action::Quarantine);
        CHECK(out.action != aa::Action::HardReject);
    }

    TEST_CASE("ArbV2.JointDynamicAgreement");
    {
        const auto out = aa::arbitrate(evidence(0.95, 0.95, 0.92, 0.90), aa::Config{});
        CHECK(out.authority == aa::Authority::Joint);
        CHECK(out.expert_agreement.both_dynamic);
        CHECK(out.action == aa::Action::HardReject);
    }

    TEST_CASE("ArbV2.LowObservabilityChangesActionNotTruth");
    {
        const auto hi = aa::arbitrate(evidence(0.95, 0.95, 0.0, 0.1, 1.0, 1.0),
                                      aa::Config{});
        const auto lo = aa::arbitrate(evidence(0.95, 0.95, 0.0, 0.1, 1.0, 0.1),
                                      aa::Config{});
        CHECK_NEAR(hi.fused_dynamic_risk, lo.fused_dynamic_risk, 1e-12);
        CHECK(hi.action == aa::Action::HardReject);
        CHECK(lo.action == aa::Action::DownWeight);
    }

    TEST_CASE("ArbV2.MeasurementMonotonicity");
    {
        double previous = -1.0;
        for (int i = 0; i <= 20; ++i)
        {
            const double qm = i / 20.0;
            const auto out = aa::arbitrate(evidence(0.6, 0.9, 0.0, 0.1, qm),
                                           aa::Config{});
            CHECK(out.backend_weight + 1e-12 >= previous);
            previous = out.backend_weight;
        }
    }

    TEST_CASE("ArbV2.DynamicWeightMonotonicity");
    {
        double previous = 2.0;
        for (int i = 0; i <= 20; ++i)
        {
            const double risk = i / 20.0;
            const double weight = aa::dynamicWeight(risk, 0.9, aa::Config{});
            CHECK(weight <= previous + 1e-12);
            previous = weight;
        }
    }

    TEST_CASE("ArbV2.ExpertIsolation");
    {
        const double qs = er::computeSemantic(healthySemantic()).q_s;
        er::GeoInputs geo;
        geo.health = geodf_degeneracy::Health::Healthy;
        geo.q_kappa = geo.q_inlier = geo.q_parallax = geo.q_coverage = geo.q_measurable = 0.9;
        const double qg = er::computeGeo(geo).q_g;
        geo.q_inlier = 0.1;
        CHECK_NEAR(er::computeSemantic(healthySemantic()).q_s, qs, 1e-12);
        CHECK(er::computeGeo(geo).q_g < qg);
    }

    TEST_CASE("ArbV2.AllFusionAblations");
    {
        for (int mode = 0; mode <= 2; ++mode)
        {
            aa::Config cfg;
            cfg.fusion_mode = static_cast<aa::FusionMode>(mode);
            const auto out = aa::arbitrate(evidence(0.8, 0.9, 0.7, 0.8), cfg);
            CHECK(std::isfinite(out.fused_dynamic_risk));
            CHECK(out.fused_dynamic_risk >= 0.0 && out.fused_dynamic_risk <= 1.0);
        }
    }

    TEST_MAIN_RETURN();
}
