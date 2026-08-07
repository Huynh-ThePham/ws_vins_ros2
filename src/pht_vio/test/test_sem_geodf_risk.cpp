// Specification tests for the Semantic-GeoDF confirmation caps and recovery.
// Monotonicity / paper-equation properties live in
// test_sem_geodf_risk_properties.cpp.

#include "featureTracker/sem_geodf_risk.h"
#include "test_support.h"

int main()
{
    const sem_geodf::RiskConfig config{0.25, 0.55, 0.75, 0.25};

    TEST_CASE("SemGeoDFRisk.CleanEvidence");
    {
        const auto clean = sem_geodf::computeMeasurementWeight({}, config);
        CHECK_NEAR(clean.risk.fused_risk, 0.0, 1e-12);
        CHECK_NEAR(clean.target_weight, 1.0, 1e-12);
    }

    TEST_CASE("SemGeoDFRisk.SemanticConfirmationCap");
    {
        sem_geodf::RiskEvidence semantic;
        semantic.semantic_hit = true;
        semantic.semantic_confirmed = true;
        semantic.semantic_confidence = 1.0;
        const auto result = sem_geodf::computeMeasurementWeight(semantic, config);
        CHECK_NEAR(result.target_weight, config.semantic_weight, 1e-12);
    }

    TEST_CASE("SemGeoDFRisk.GeometricConfirmationCap");
    {
        sem_geodf::RiskEvidence geo;
        geo.geo_hit = true;
        geo.geo_confirmed = true;
        geo.geo_scene_confidence = 1.0;
        geo.geo_error_confidence = 1.0;
        const auto result = sem_geodf::computeMeasurementWeight(geo, config);
        CHECK_NEAR(result.target_weight, config.geo_weight, 1e-12);
    }

    TEST_CASE("SemGeoDFRisk.AgreementCapIsStrictest");
    {
        sem_geodf::RiskEvidence semantic;
        semantic.semantic_hit = true;
        semantic.semantic_confirmed = true;
        semantic.semantic_confidence = 1.0;
        sem_geodf::RiskEvidence geo;
        geo.geo_hit = true;
        geo.geo_confirmed = true;
        geo.geo_scene_confidence = 1.0;
        geo.geo_error_confidence = 1.0;

        sem_geodf::RiskEvidence consensus = semantic;
        consensus.geo_hit = true;
        consensus.geo_confirmed = true;
        consensus.geo_scene_confidence = 1.0;
        consensus.geo_error_confidence = 1.0;
        consensus.overlap_confidence = 1.0;

        const auto s = sem_geodf::computeMeasurementWeight(semantic, config);
        const auto g = sem_geodf::computeMeasurementWeight(geo, config);
        const auto c = sem_geodf::computeMeasurementWeight(consensus, config);
        CHECK_NEAR(c.target_weight, config.agree_weight, 1e-12);
        CHECK(c.target_weight <= s.target_weight);
        CHECK(s.target_weight <= g.target_weight);
    }

    TEST_CASE("SemGeoDFRisk.Recovery");
    {
        CHECK_NEAR(sem_geodf::recoverWeight(0.25, 1.0, 0.2, 0.25), 0.40, 1e-12);
        CHECK_NEAR(sem_geodf::recoverWeight(0.75, 0.25, 0.2, 0.25), 0.25, 1e-12);
        CHECK_NEAR(sem_geodf::recoverWeight(0.25, 1.0, 2.0, 0.25), 1.0, 1e-12);
    }

    TEST_CASE("SemGeoDFRisk.UnhealthyGeometryDoesNotEnterDynamicRisk");
    {
        // Phase 3.1: GeoDF evidence against a non-Healthy F must not lower w_i.
        // Semantic evidence alone still may.
        sem_geodf::RiskEvidence geo_only;
        geo_only.geo_hit = true;
        geo_only.geo_confirmed = true;
        geo_only.geo_scene_confidence = 1.0;
        geo_only.geo_error_confidence = 1.0;
        geo_only.overlap_confidence = 1.0;

        const auto gated =
            sem_geodf::gateGeoDynamicEvidence(geo_only, /*geometry_healthy=*/false);
        CHECK(!gated.geo_hit);
        CHECK(!gated.geo_confirmed);
        CHECK_NEAR(gated.geo_scene_confidence, 0.0, 1e-12);
        CHECK_NEAR(gated.geo_error_confidence, 0.0, 1e-12);
        CHECK_NEAR(gated.overlap_confidence, 0.0, 1e-12);

        const auto w_gated = sem_geodf::computeMeasurementWeight(gated, config);
        CHECK_NEAR(w_gated.risk.fused_risk, 0.0, 1e-12);
        CHECK_NEAR(w_gated.target_weight, 1.0, 1e-12);

        const auto w_healthy = sem_geodf::computeMeasurementWeight(
            sem_geodf::gateGeoDynamicEvidence(geo_only, /*geometry_healthy=*/true),
            config);
        CHECK(w_healthy.target_weight < 1.0 - 1e-9);
        CHECK_NEAR(w_healthy.target_weight, config.geo_weight, 1e-12);
    }

    TEST_CASE("SemGeoDFRisk.UnhealthyGeometryPreservesSemanticRisk");
    {
        sem_geodf::RiskEvidence both;
        both.semantic_hit = true;
        both.semantic_confirmed = true;
        both.semantic_confidence = 1.0;
        both.geo_hit = true;
        both.geo_confirmed = true;
        both.geo_scene_confidence = 1.0;
        both.geo_error_confidence = 1.0;
        both.overlap_confidence = 1.0;

        const auto gated =
            sem_geodf::gateGeoDynamicEvidence(both, /*geometry_healthy=*/false);
        CHECK(gated.semantic_hit);
        CHECK(gated.semantic_confirmed);
        CHECK(!gated.geo_hit);

        const auto w = sem_geodf::computeMeasurementWeight(gated, config);
        // Same as semantic-only confirmation, not the stricter agree_weight.
        CHECK_NEAR(w.target_weight, config.semantic_weight, 1e-12);
    }

    TEST_MAIN_RETURN();
}
