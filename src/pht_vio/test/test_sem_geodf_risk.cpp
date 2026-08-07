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

    TEST_MAIN_RETURN();
}
