// Property tests for the Semantic-GeoDF risk -> weight mapping (plan P0.2).
//
// The published equation and the implementation must agree, and the mapping must
// be monotone: more evidence of a dynamic feature can never raise its weight.

#include "featureTracker/sem_geodf_risk.h"
#include "test_support.h"

#include <vector>

namespace
{

const sem_geodf::RiskConfig kConfig{0.25, 0.55, 0.75, 0.25};

// Independent re-derivation of the paper equation, written from the manuscript
// rather than from the implementation:
//   rho_sem = 1{sem} (1 - w_sem) c_sem
//   rho_geo = 1{geo} (1 - w_geo) max(c_geo_scene, c_geo_err)
//   rho_agr = 1{sem}1{geo} (1 - w_agr) max(o, min(c_sem, max(c_geo_scene, c_geo_err)))
//   r       = 1 - (1-rho_sem)(1-rho_geo)(1-rho_agr)
//   w       = clip(1 - r, w_min, 1), then capped by confirmation flags
double paperRisk(const sem_geodf::RiskEvidence &e, const sem_geodf::RiskConfig &c)
{
    const double cs = sem_geodf::clamp(e.semantic_confidence, 0.0, 1.0);
    const double cg = std::max(sem_geodf::clamp(e.geo_scene_confidence, 0.0, 1.0),
                               sem_geodf::clamp(e.geo_error_confidence, 0.0, 1.0));
    const double ov = sem_geodf::clamp(e.overlap_confidence, 0.0, 1.0);

    const double rho_sem =
        e.semantic_hit ? (1.0 - c.semantic_weight) * (e.semantic_confirmed ? 1.0 : cs) : 0.0;
    const double rho_geo = e.geo_hit ? (1.0 - c.geo_weight) * cg : 0.0;
    const double rho_agr = (e.semantic_hit && e.geo_hit)
                               ? (1.0 - c.agree_weight) * std::max(ov, std::min(cs, cg))
                               : 0.0;
    return 1.0 - (1.0 - rho_sem) * (1.0 - rho_geo) * (1.0 - rho_agr);
}

double paperWeight(const sem_geodf::RiskEvidence &e, const sem_geodf::RiskConfig &c)
{
    double w = sem_geodf::clamp(1.0 - paperRisk(e, c), c.min_weight, 1.0);
    if (e.semantic_confirmed)
        w = std::min(w, c.semantic_weight);
    if (e.geo_confirmed)
        w = std::min(w, c.geo_weight);
    if (e.semantic_confirmed && e.geo_confirmed)
        w = std::min(w, c.agree_weight);
    return sem_geodf::clamp(w, c.min_weight, 1.0);
}

// A representative sweep over the evidence space used by several properties.
std::vector<sem_geodf::RiskEvidence> evidenceSweep()
{
    std::vector<sem_geodf::RiskEvidence> out;
    const double levels[] = {0.0, 0.25, 0.5, 0.75, 1.0};
    for (int sem_hit = 0; sem_hit < 2; sem_hit++)
        for (int sem_conf = 0; sem_conf < 2; sem_conf++)
            for (int geo_hit = 0; geo_hit < 2; geo_hit++)
                for (int geo_conf = 0; geo_conf < 2; geo_conf++)
                    for (double cs : levels)
                        for (double cg : levels)
                            for (double ov : levels)
                            {
                                if (sem_conf && !sem_hit)
                                    continue;
                                if (geo_conf && !geo_hit)
                                    continue;
                                sem_geodf::RiskEvidence e;
                                e.semantic_hit = sem_hit != 0;
                                e.semantic_confirmed = sem_conf != 0;
                                e.geo_hit = geo_hit != 0;
                                e.geo_confirmed = geo_conf != 0;
                                e.semantic_confidence = cs;
                                e.geo_scene_confidence = cg;
                                e.geo_error_confidence = cg;
                                e.overlap_confidence = ov;
                                out.push_back(e);
                            }
    return out;
}

}  // namespace

int main()
{
    TEST_CASE("SemGeoDFRisk.CleanEvidenceHasUnitWeight");
    {
        const auto clean = sem_geodf::computeMeasurementWeight({}, kConfig);
        CHECK_NEAR(clean.risk.fused_risk, 0.0, 1e-12);
        CHECK_NEAR(clean.target_weight, 1.0, 1e-12);
        CHECK_NEAR(clean.ranking_risk, 0.0, 1e-12);
    }

    TEST_CASE("SemGeoDFRisk.RiskIncreaseNeverIncreasesWeight");
    {
        // Sweep each confidence channel upward; weight must be non-increasing and
        // fused risk non-decreasing.
        for (int sem_hit = 0; sem_hit < 2; sem_hit++)
            for (int geo_hit = 0; geo_hit < 2; geo_hit++)
            {
                double prev_weight = 2.0;
                double prev_risk = -1.0;
                for (int step = 0; step <= 20; step++)
                {
                    const double level = step / 20.0;
                    sem_geodf::RiskEvidence e;
                    e.semantic_hit = sem_hit != 0;
                    e.geo_hit = geo_hit != 0;
                    e.semantic_confidence = level;
                    e.geo_scene_confidence = level;
                    e.geo_error_confidence = level;
                    e.overlap_confidence = level;
                    const auto r = sem_geodf::computeMeasurementWeight(e, kConfig);
                    CHECK(r.target_weight <= prev_weight + 1e-12);
                    CHECK(r.risk.fused_risk >= prev_risk - 1e-12);
                    prev_weight = r.target_weight;
                    prev_risk = r.risk.fused_risk;
                }
            }
        // Adding a confirmation flag can only tighten the weight.
        for (const auto &base : evidenceSweep())
        {
            if (base.semantic_confirmed || !base.semantic_hit)
                continue;
            sem_geodf::RiskEvidence confirmed = base;
            confirmed.semantic_confirmed = true;
            CHECK(sem_geodf::computeMeasurementWeight(confirmed, kConfig).target_weight <=
                  sem_geodf::computeMeasurementWeight(base, kConfig).target_weight + 1e-12);
        }
    }

    TEST_CASE("SemGeoDFRisk.AgreementIsNoLessSevereThanSingleExpert");
    {
        for (const auto &both : evidenceSweep())
        {
            if (!both.semantic_hit || !both.geo_hit)
                continue;
            sem_geodf::RiskEvidence sem_only = both;
            sem_only.geo_hit = false;
            sem_only.geo_confirmed = false;
            sem_geodf::RiskEvidence geo_only = both;
            geo_only.semantic_hit = false;
            geo_only.semantic_confirmed = false;

            const double w_both = sem_geodf::computeMeasurementWeight(both, kConfig).target_weight;
            CHECK(w_both <=
                  sem_geodf::computeMeasurementWeight(sem_only, kConfig).target_weight + 1e-12);
            CHECK(w_both <=
                  sem_geodf::computeMeasurementWeight(geo_only, kConfig).target_weight + 1e-12);
        }
    }

    TEST_CASE("SemGeoDFRisk.WeightNeverBelowMinimum");
    {
        for (const auto &e : evidenceSweep())
        {
            const auto r = sem_geodf::computeMeasurementWeight(e, kConfig);
            CHECK(r.target_weight >= kConfig.min_weight - 1e-12);
            CHECK(r.target_weight <= 1.0 + 1e-12);
            CHECK(r.risk.fused_risk >= -1e-12);
            CHECK(r.risk.fused_risk <= 1.0 + 1e-12);
        }
        // A confirmation cap tighter than the floor must not breach the floor.
        const sem_geodf::RiskConfig tight{0.5, 0.1, 0.1, 0.1};
        sem_geodf::RiskEvidence worst;
        worst.semantic_hit = worst.semantic_confirmed = true;
        worst.geo_hit = worst.geo_confirmed = true;
        worst.semantic_confidence = worst.geo_scene_confidence = 1.0;
        worst.geo_error_confidence = worst.overlap_confidence = 1.0;
        CHECK_NEAR(sem_geodf::computeMeasurementWeight(worst, tight).target_weight, 0.5, 1e-12);
    }

    TEST_CASE("SemGeoDFRisk.RecoveryIsMonotonicAndBounded");
    {
        // Rising toward a higher target is gradual; dropping is immediate.
        CHECK_NEAR(sem_geodf::recoverWeight(0.25, 1.0, 0.2, 0.25), 0.40, 1e-12);
        CHECK_NEAR(sem_geodf::recoverWeight(0.75, 0.25, 0.2, 0.25), 0.25, 1e-12);
        CHECK_NEAR(sem_geodf::recoverWeight(0.25, 1.0, 2.0, 0.25), 1.0, 1e-12);
        CHECK_NEAR(sem_geodf::recoverWeight(0.25, 1.0, -1.0, 0.25), 0.25, 1e-12);

        // Repeated recovery is monotone non-decreasing and converges into [min, 1].
        double w = 0.25;
        for (int i = 0; i < 200; i++)
        {
            const double next = sem_geodf::recoverWeight(w, 1.0, 0.2, 0.25);
            CHECK(next >= w - 1e-12);
            CHECK(next <= 1.0 + 1e-12);
            w = next;
        }
        CHECK(w > 0.99);

        // A higher target never yields a lower applied weight.
        for (int step = 0; step <= 20; step++)
        {
            const double target = 0.25 + 0.75 * step / 20.0;
            const double lo = sem_geodf::recoverWeight(0.30, target, 0.2, 0.25);
            const double hi = sem_geodf::recoverWeight(0.30, std::min(1.0, target + 0.05), 0.2, 0.25);
            CHECK(hi >= lo - 1e-12);
        }
    }

    TEST_CASE("SemGeoDFRisk.PaperEquationMatchesImplementation");
    {
        for (const auto &e : evidenceSweep())
        {
            const auto r = sem_geodf::computeMeasurementWeight(e, kConfig);
            CHECK_NEAR(r.risk.fused_risk, paperRisk(e, kConfig), 1e-12);
            CHECK_NEAR(r.target_weight, paperWeight(e, kConfig), 1e-12);
            // riskToWeight must be the only place the mapping lives.
            CHECK_NEAR(sem_geodf::riskToWeight(r.risk.fused_risk, e, kConfig),
                       r.target_weight, 1e-12);
            // ranking_risk is the post-cap severity, not the raw fused risk.
            CHECK_NEAR(r.ranking_risk, 1.0 - r.target_weight, 1e-12);
        }

        // Regression guard on the exact defect this test exists for: the weight is
        // the product form, never the risk expression itself.
        sem_geodf::RiskEvidence dynamic;
        dynamic.semantic_hit = true;
        dynamic.geo_hit = true;
        dynamic.semantic_confidence = 1.0;
        dynamic.geo_scene_confidence = 1.0;
        dynamic.geo_error_confidence = 1.0;
        dynamic.overlap_confidence = 1.0;
        const auto r = sem_geodf::computeMeasurementWeight(dynamic, kConfig);
        const double product = (1.0 - 0.45) * (1.0 - 0.25) * (1.0 - 0.75);
        CHECK_NEAR(r.target_weight, std::max(product, kConfig.min_weight), 1e-12);
        CHECK(r.target_weight < r.risk.fused_risk);
    }

    TEST_MAIN_RETURN();
}
