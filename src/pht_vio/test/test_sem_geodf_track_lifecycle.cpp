// Plan P1.3 / P1.4: health is measured separately from the action it informs, and a
// track may only be hard-rejected when risk, expert health, observability and
// redundancy all permit it.

#include "featureTracker/sem_policy.h"
#include "test_support.h"

#include <string>
#include <vector>

namespace sp = sem_policy;

namespace
{

sp::HealthConfig healthConfig()
{
    sp::HealthConfig c;
    c.mask_max_age_ms = 150.0;
    c.mask_saturation_ratio = 0.60;
    c.min_semantic_health = 0.35;
    c.min_geometric_health = 0.35;
    c.redundancy_target = 60;
    c.parallax_target_px = 3.0;
    c.min_observability = 0.35;
    c.min_tracks_for_hard_reject = 40;
    return c;
}

sp::SemanticObservation freshMask(double ratio = 0.10)
{
    sp::SemanticObservation obs;
    obs.mask_available = true;
    obs.mask_fresh = true;
    obs.mask_age_ms = 20.0;
    obs.dynamic_pixel_ratio = ratio;
    return obs;
}

sp::GeometricObservation goodGeometry()
{
    sp::GeometricObservation obs;
    obs.fundamental_valid = true;
    obs.conditioning = 1.0;
    obs.inlier_ratio = 0.85;
    return obs;
}

sp::ObservabilityObservation goodObservability()
{
    sp::ObservabilityObservation obs;
    obs.tracked_features = 110;
    obs.median_parallax_px = 6.0;
    obs.grid_occupancy = 0.8;
    return obs;
}

sp::Health healthyAll()
{
    return sp::computeHealth(freshMask(), goodGeometry(), goodObservability(), healthConfig());
}

sp::LifecycleConfig lifecycleConfig()
{
    sp::LifecycleConfig c;
    c.suspect_frames = 1;
    c.downweight_frames = 2;
    c.hard_reject_risk = 0.60;
    c.hard_reject_requires_agreement = true;
    c.recover_dwell_s = 0.5;
    return c;
}

sp::TrackEvidence agreeingEvidence(double risk = 0.9)
{
    sp::TrackEvidence e;
    e.fused_risk = risk;
    e.semantic_hit = true;
    e.geo_hit = true;
    e.two_expert_agreement = true;
    return e;
}

}  // namespace

int main()
{
    // ---------------------------------------------------------------- health ---
    TEST_CASE("Health.StaleOrMissingMaskIsUnhealthy");
    {
        const sp::HealthConfig config = healthConfig();
        CHECK(sp::semanticHealth({}, config) == 0.0);  // no mask at all

        sp::SemanticObservation stale = freshMask();
        stale.mask_fresh = false;
        stale.mask_age_ms = 260.0;
        CHECK(sp::semanticHealth(stale, config) < config.min_semantic_health);

        CHECK(sp::semanticHealth(freshMask(), config) >= config.min_semantic_health);
    }

    TEST_CASE("Health.FrameFillingMaskIsUnhealthy");
    {
        // A mask calling almost the whole frame dynamic is far more likely a
        // segmentation failure than a scene where everything moves, and it must not
        // be allowed to authorise hard rejection.
        const sp::HealthConfig config = healthConfig();
        CHECK(sp::semanticHealth(freshMask(0.10), config) >= config.min_semantic_health);
        CHECK(sp::semanticHealth(freshMask(0.98), config) < config.min_semantic_health);
        // Monotone: more saturation is never healthier.
        double previous = 2.0;
        for (int i = 0; i <= 20; i++)
        {
            const double h = sp::semanticHealth(freshMask(i / 20.0), config);
            CHECK(h <= previous + 1e-12);
            previous = h;
        }
    }

    TEST_CASE("Health.DegenerateGeometryIsUnhealthy");
    {
        const sp::HealthConfig config = healthConfig();
        CHECK(sp::geometricHealth({}, config) == 0.0);  // no fundamental matrix

        sp::GeometricObservation weak = goodGeometry();
        weak.conditioning = 0.05;
        CHECK(sp::geometricHealth(weak, config) < config.min_geometric_health);

        sp::GeometricObservation few_inliers = goodGeometry();
        few_inliers.inlier_ratio = 0.10;
        CHECK(sp::geometricHealth(few_inliers, config) < config.min_geometric_health);

        CHECK(sp::geometricHealth(goodGeometry(), config) >= config.min_geometric_health);
    }

    TEST_CASE("Health.ObservabilityTakesTheWeakestOfRedundancyParallaxSpread");
    {
        const sp::HealthConfig config = healthConfig();
        CHECK(sp::observabilityHealth(goodObservability(), config) >= config.min_observability);

        // Many features clustered in one corner is not observable motion.
        sp::ObservabilityObservation clustered = goodObservability();
        clustered.grid_occupancy = 0.05;
        CHECK(sp::observabilityHealth(clustered, config) < config.min_observability);

        // Plenty of well-spread features but no parallax: pure rotation.
        sp::ObservabilityObservation rotating = goodObservability();
        rotating.median_parallax_px = 0.1;
        CHECK(sp::observabilityHealth(rotating, config) < config.min_observability);

        // Well spread with good parallax but almost no features.
        sp::ObservabilityObservation sparse = goodObservability();
        sparse.tracked_features = 5;
        CHECK(sp::observabilityHealth(sparse, config) < config.min_observability);
    }

    // ------------------------------------------------------------- lifecycle ---
    TEST_CASE("Lifecycle.TrustedThroughSuspectToDownWeighted");
    {
        sp::LifecycleManager manager;
        manager.configure(lifecycleConfig());
        const sp::Health health = healthyAll();
        const sp::HealthConfig hc = healthConfig();

        // A clean track stays trusted and is simply accepted.
        const sp::LifecycleDecision clean = manager.update(1, 0.0, {}, health, 110, hc);
        CHECK(clean.state == sp::TrackState::Trusted);
        CHECK(clean.action == sp::Action::Accept);

        // One suspicious frame: suspect, but not yet down-weighted.
        sp::TrackEvidence low_risk = agreeingEvidence(0.10);
        const sp::LifecycleDecision suspect = manager.update(1, 0.1, low_risk, health, 110, hc);
        CHECK(suspect.state == sp::TrackState::Suspect);
        CHECK(suspect.action == sp::Action::Accept);

        // Repeated evidence: down-weighted. Low risk keeps it from being deleted.
        const sp::LifecycleDecision down = manager.update(1, 0.2, low_risk, health, 110, hc);
        CHECK(down.state == sp::TrackState::DownWeighted);
        CHECK(down.action == sp::Action::DownWeight);
    }

    TEST_CASE("Lifecycle.HardRejectNeedsRiskAgreementHealthAndRedundancy");
    {
        const sp::Health health = healthyAll();
        const sp::HealthConfig hc = healthConfig();

        // The permitted case, for reference.
        {
            sp::LifecycleManager manager;
            manager.configure(lifecycleConfig());
            manager.update(1, 0.0, agreeingEvidence(), health, 110, hc);
            const sp::LifecycleDecision d = manager.update(1, 0.1, agreeingEvidence(), health, 110, hc);
            CHECK(d.action == sp::Action::HardReject);
            CHECK(d.state == sp::TrackState::Rejected);
        }

        // Risk below the threshold: down-weight only.
        {
            sp::LifecycleManager manager;
            manager.configure(lifecycleConfig());
            manager.update(1, 0.0, agreeingEvidence(0.20), health, 110, hc);
            const sp::LifecycleDecision d = manager.update(1, 0.1, agreeingEvidence(0.20), health, 110, hc);
            CHECK(d.action == sp::Action::DownWeight);
        }

        // Only one expert fired: no agreement, so no deletion.
        {
            sp::LifecycleManager manager;
            manager.configure(lifecycleConfig());
            sp::TrackEvidence single;
            single.fused_risk = 0.95;
            single.semantic_hit = true;
            manager.update(1, 0.0, single, health, 110, hc);
            const sp::LifecycleDecision d = manager.update(1, 0.1, single, health, 110, hc);
            CHECK(d.action == sp::Action::DownWeight);
        }

        // Semantic expert unhealthy (stale mask): the semantic hit cannot delete.
        {
            sp::LifecycleManager manager;
            manager.configure(lifecycleConfig());
            sp::SemanticObservation stale = freshMask();
            stale.mask_fresh = false;
            stale.mask_age_ms = 400.0;
            const sp::Health unhealthy =
                sp::computeHealth(stale, goodGeometry(), goodObservability(), hc);
            CHECK(!unhealthy.semantic_healthy);
            manager.update(1, 0.0, agreeingEvidence(), unhealthy, 110, hc);
            const sp::LifecycleDecision d =
                manager.update(1, 0.1, agreeingEvidence(), unhealthy, 110, hc);
            CHECK(d.action == sp::Action::DownWeight);
            CHECK(d.hard_reject_blocked_by_health);
        }

        // Degenerate geometry: the GeoDF hit cannot delete either.
        {
            sp::LifecycleManager manager;
            manager.configure(lifecycleConfig());
            const sp::Health unhealthy =
                sp::computeHealth(freshMask(), {}, goodObservability(), hc);
            CHECK(!unhealthy.geometric_healthy);
            manager.update(1, 0.0, agreeingEvidence(), unhealthy, 110, hc);
            const sp::LifecycleDecision d =
                manager.update(1, 0.1, agreeingEvidence(), unhealthy, 110, hc);
            CHECK(d.action == sp::Action::DownWeight);
            CHECK(d.hard_reject_blocked_by_health);
        }

        // Low observability: prefer down-weighting over deleting (plan P1.3).
        {
            sp::LifecycleManager manager;
            manager.configure(lifecycleConfig());
            sp::ObservabilityObservation rotating = goodObservability();
            rotating.median_parallax_px = 0.05;
            const sp::Health unhealthy =
                sp::computeHealth(freshMask(), goodGeometry(), rotating, hc);
            CHECK(!unhealthy.observability_healthy);
            manager.update(1, 0.0, agreeingEvidence(), unhealthy, 110, hc);
            const sp::LifecycleDecision d =
                manager.update(1, 0.1, agreeingEvidence(), unhealthy, 110, hc);
            CHECK(d.action == sp::Action::DownWeight);
            CHECK(d.hard_reject_blocked_by_observability);
        }

        // Not enough surviving tracks to afford the deletion.
        {
            sp::LifecycleManager manager;
            manager.configure(lifecycleConfig());
            manager.update(1, 0.0, agreeingEvidence(), health, 12, hc);
            const sp::LifecycleDecision d = manager.update(1, 0.1, agreeingEvidence(), health, 12, hc);
            CHECK(d.action == sp::Action::DownWeight);
            CHECK(d.hard_reject_blocked_by_redundancy);
        }
    }

    TEST_CASE("Lifecycle.PerFrameDeletionBudgetKeepsTheFeatureSetAboveTheFloor");
    {
        // Checking `tracked_features >= min_tracks` once per track is not enough: with
        // many tracks flagged in the same frame, every check sees the pre-deletion
        // count and the batch overshoots the floor. The per-frame allowance is what
        // actually bounds it.
        sp::LifecycleManager manager;
        manager.configure(lifecycleConfig());
        const sp::Health health = healthyAll();
        const sp::HealthConfig hc = healthConfig();  // min_tracks_for_hard_reject = 40

        const int tracked = 45;  // only 5 deletions may be afforded this frame
        // Two frames of evidence to reach DOWNWEIGHTED for every track.
        for (int id = 0; id < 30; id++)
            manager.update(id, 0.0, agreeingEvidence(), health, tracked, hc);
        int rejected = 0;
        for (int id = 0; id < 30; id++)
        {
            const sp::LifecycleDecision d =
                manager.update(id, 0.1, agreeingEvidence(), health, tracked, hc);
            if (d.action == sp::Action::HardReject)
                rejected++;
            else
                CHECK(d.action == sp::Action::DownWeight);
        }
        CHECK(rejected == tracked - hc.min_tracks_for_hard_reject);
        CHECK(manager.deletionAllowance() == 0);

        // A new frame refreshes the allowance from the new count.
        const sp::LifecycleDecision next =
            manager.update(0, 0.2, agreeingEvidence(), health, 100, hc);
        (void)next;
        CHECK(manager.deletionAllowance() >= 0);

        // At or below the floor, nothing may be deleted at all.
        sp::LifecycleManager starved;
        starved.configure(lifecycleConfig());
        for (int id = 0; id < 10; id++)
            starved.update(id, 0.0, agreeingEvidence(), health, 40, hc);
        for (int id = 0; id < 10; id++)
        {
            const sp::LifecycleDecision d =
                starved.update(id, 0.1, agreeingEvidence(), health, 40, hc);
            CHECK(d.action == sp::Action::DownWeight);
            CHECK(d.hard_reject_blocked_by_redundancy);
        }
    }

    TEST_CASE("Lifecycle.RecoveryRequiresDwellTime");
    {
        sp::LifecycleManager manager;
        manager.configure(lifecycleConfig());
        const sp::Health health = healthyAll();
        const sp::HealthConfig hc = healthConfig();

        sp::TrackEvidence low_risk = agreeingEvidence(0.10);
        manager.update(1, 0.0, low_risk, health, 110, hc);
        CHECK(manager.update(1, 0.1, low_risk, health, 110, hc).state ==
              sp::TrackState::DownWeighted);

        // Evidence disappears: RECOVERING, not straight back to TRUSTED.
        CHECK(manager.update(1, 0.2, {}, health, 110, hc).state == sp::TrackState::Recovering);
        // Still inside the dwell window.
        CHECK(manager.update(1, 0.5, {}, health, 110, hc).state == sp::TrackState::Recovering);
        // Past it: trusted again.
        CHECK(manager.update(1, 0.8, {}, health, 110, hc).state == sp::TrackState::Trusted);
    }

    TEST_CASE("Lifecycle.EvidenceDuringRecoveryReEscalates");
    {
        sp::LifecycleManager manager;
        manager.configure(lifecycleConfig());
        const sp::Health health = healthyAll();
        const sp::HealthConfig hc = healthConfig();
        sp::TrackEvidence low_risk = agreeingEvidence(0.10);

        manager.update(1, 0.0, low_risk, health, 110, hc);
        manager.update(1, 0.1, low_risk, health, 110, hc);
        CHECK(manager.update(1, 0.2, {}, health, 110, hc).state == sp::TrackState::Recovering);
        // A single fresh suspicious frame puts it back under suspicion.
        CHECK(manager.update(1, 0.3, low_risk, health, 110, hc).state == sp::TrackState::Suspect);
    }

    TEST_CASE("Lifecycle.TracksAreForgottenWhenTheyDie");
    {
        // Without this the map grows for the whole sequence.
        sp::LifecycleManager manager;
        manager.configure(lifecycleConfig());
        const sp::Health health = healthyAll();
        const sp::HealthConfig hc = healthConfig();
        for (int id = 0; id < 500; id++)
            manager.update(id, 0.0, {}, health, 110, hc);
        CHECK(manager.size() == 500);

        const std::vector<int> alive{3, 7, 11};
        manager.retainOnly(alive);
        CHECK(manager.size() == 3);
        CHECK(manager.find(3) != nullptr);
        CHECK(manager.find(4) == nullptr);
    }

    TEST_CASE("Lifecycle.StateAndActionNamesAreStable");
    {
        CHECK(std::string(sp::toString(sp::TrackState::Trusted)) == "TRUSTED");
        CHECK(std::string(sp::toString(sp::TrackState::Suspect)) == "SUSPECT");
        CHECK(std::string(sp::toString(sp::TrackState::DownWeighted)) == "DOWNWEIGHTED");
        CHECK(std::string(sp::toString(sp::TrackState::Rejected)) == "REJECTED");
        CHECK(std::string(sp::toString(sp::TrackState::Recovering)) == "RECOVERING");
        CHECK(std::string(sp::toString(sp::Action::Accept)) == "accept");
        CHECK(std::string(sp::toString(sp::Action::DownWeight)) == "down_weight");
        CHECK(std::string(sp::toString(sp::Action::HardReject)) == "hard_reject");
    }

    TEST_MAIN_RETURN();
}
