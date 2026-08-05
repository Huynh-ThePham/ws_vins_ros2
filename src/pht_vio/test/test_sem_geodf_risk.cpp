#include "featureTracker/sem_geodf_risk.h"

#include <cassert>
#include <cmath>

namespace
{

bool near(double lhs, double rhs, double eps = 1e-12)
{
    return std::abs(lhs - rhs) <= eps;
}

}  // namespace

int main()
{
    const sem_geodf::RiskConfig config{0.25, 0.55, 0.75, 0.25};

    const auto clean = sem_geodf::computeRisk({}, config);
    assert(near(clean.combined_risk, 0.0));
    assert(near(clean.target_weight, 1.0));

    sem_geodf::RiskEvidence semantic;
    semantic.semantic_hit = true;
    semantic.semantic_confirmed = true;
    semantic.semantic_confidence = 1.0;
    const auto semantic_result = sem_geodf::computeRisk(semantic, config);
    assert(near(semantic_result.target_weight, config.semantic_weight));

    sem_geodf::RiskEvidence geo;
    geo.geo_hit = true;
    geo.geo_confirmed = true;
    geo.geo_scene_confidence = 1.0;
    geo.geo_error_confidence = 1.0;
    const auto geo_result = sem_geodf::computeRisk(geo, config);
    assert(near(geo_result.target_weight, config.geo_weight));

    sem_geodf::RiskEvidence consensus = semantic;
    consensus.geo_hit = true;
    consensus.geo_confirmed = true;
    consensus.geo_scene_confidence = 1.0;
    consensus.geo_error_confidence = 1.0;
    consensus.overlap_confidence = 1.0;
    const auto consensus_result = sem_geodf::computeRisk(consensus, config);
    assert(near(consensus_result.target_weight, config.agree_weight));
    assert(consensus_result.combined_risk > semantic_result.combined_risk);
    assert(semantic_result.combined_risk > geo_result.combined_risk);

    assert(near(sem_geodf::recoverWeight(0.25, 1.0, 0.2, 0.25), 0.40));
    assert(near(sem_geodf::recoverWeight(0.75, 0.25, 0.2, 0.25), 0.25));
    assert(near(sem_geodf::recoverWeight(0.25, 1.0, 2.0, 0.25), 1.0));

    return 0;
}
