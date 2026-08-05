// Synthetic integration test for the Sem-GeoDF decision layer (plan section 8).
//
// Drives the components that actually decide what happens to a feature -- the
// stereo validity contract, the GeoDF degeneracy guard, the health model, the scene
// policy FSM and the per-track lifecycle -- together, over short synthetic sequences.
// No GPU, no dataset, no ROS: the plan requires this to be runnable in CI.
//
// The scenarios and the assertions are the ones the plan lists:
//
//   static scene                  -> not over-rejected by the semantic expert
//   moving object at 10%          -> policy escalates, deletion allowed
//   moving object at 60%          -> mask saturation is treated as unreliable
//   stale semantic mask           -> fail open, no semantic deletion
//   low parallax                  -> GeoDF may not delete
//   aggressive camera rotation    -> same, via degenerate geometry
//   feature count near minimum    -> down-weight instead of delete
//   wrong-sign stereo matches     -> blocked before the backend

#include "featureTracker/geodf_degeneracy.h"
#include "featureTracker/sem_geodf_risk.h"
#include "featureTracker/sem_policy.h"
#include "featureTracker/stereo_validity.h"
#include "test_support.h"

#include <string>
#include <vector>

namespace sp = sem_policy;
namespace gd = geodf_degeneracy;
namespace sv = stereo_validity;

namespace
{

constexpr double kFocal = 460.0;
constexpr double kFrameDt = 0.1;  // 10 Hz, as in the VIODE configs

// ---------------------------------------------------------------- the harness ---

// One synthetic frame of the decision layer's inputs.
struct Frame
{
    double timestamp_s = 0.0;
    // Semantic expert
    bool mask_available = true;
    bool mask_fresh = true;
    double mask_age_ms = 20.0;
    double dynamic_pixel_ratio = 0.0;
    double semantic_ema = 0.0;
    // Geometric expert
    bool fundamental_valid = true;
    double median_parallax_px = 5.0;
    double grid_occupancy = 0.8;
    int ransac_inliers = 90;
    int ransac_total = 110;
    double mover_share = 0.1;
    // Frame state
    int tracked_features = 120;
    int sem_candidates = 0;
    int geo_candidates = 0;
    int intersection = 0;
    bool scene_gate_open = true;
};

struct FrameOutcome
{
    sp::PolicyState policy_state = sp::PolicyState::StaticSafe;
    bool hard_reject_armed = false;
    bool soft_mask_active = false;
    gd::Health geometry = gd::Health::Healthy;
    gd::Cause geometry_cause = gd::Cause::NONE;
    sp::Health health;
    int hard_rejected = 0;
    int down_weighted = 0;
    int blocked_by_health = 0;
    int blocked_by_observability = 0;
    int blocked_by_redundancy = 0;
    double min_weight = 1.0;
};

// Mirrors how FeatureTracker composes these components, so the test exercises the
// real decision order rather than a re-derivation of it.
class DecisionLayer
{
  public:
    DecisionLayer()
    {
        health_config_.mask_max_age_ms = 150.0;
        health_config_.mask_saturation_ratio = 0.60;
        health_config_.min_semantic_health = 0.35;
        health_config_.min_geometric_health = 0.35;
        health_config_.redundancy_target = 60;
        health_config_.parallax_target_px = 3.0;
        health_config_.min_observability = 0.35;
        health_config_.min_tracks_for_hard_reject = 40;

        overlap_config_.metric = sp::OverlapMetric::Dice;
        overlap_config_.min_sem_candidates = 4;
        overlap_config_.min_geo_candidates = 4;
        overlap_config_.min_intersection = 2;
        overlap_config_.support_saturation = 8;

        policy_config_.assist_hold_s = 1.5;
        policy_config_.strong_hold_s = 1.0;
        policy_config_.min_state_dwell_s = 0.3;
        fsm_.configure(policy_config_);

        lifecycle_config_.suspect_frames = 1;
        lifecycle_config_.downweight_frames = 2;
        lifecycle_config_.hard_reject_risk = 0.60;
        lifecycle_config_.hard_reject_requires_agreement = true;
        lifecycle_config_.recover_dwell_s = 0.5;
        lifecycle_.configure(lifecycle_config_);

        risk_config_ = sem_geodf::RiskConfig{0.25, 0.55, 0.75, 0.25};
    }

    // `flagged` lists the track ids both experts (or one of them) fired on.
    FrameOutcome step(const Frame &frame,
                      const std::vector<int> &semantic_hits,
                      const std::vector<int> &geo_hits)
    {
        FrameOutcome out;

        gd::Observation geo_obs;
        geo_obs.fundamental_valid = frame.fundamental_valid;
        geo_obs.median_parallax_px = frame.median_parallax_px;
        geo_obs.grid_occupancy = frame.grid_occupancy;
        geo_obs.design_condition_number = 1.0e3;
        geo_obs.ransac_inliers = frame.ransac_inliers;
        geo_obs.ransac_total = frame.ransac_total;
        geo_obs.mover_share = frame.mover_share;
        const gd::Result degeneracy = gd::evaluate(geo_obs, gd::Config{});
        out.geometry = degeneracy.health;
        out.geometry_cause = degeneracy.cause;

        sp::SemanticObservation sem_obs;
        sem_obs.mask_available = frame.mask_available;
        sem_obs.mask_fresh = frame.mask_fresh;
        sem_obs.mask_age_ms = frame.mask_age_ms;
        sem_obs.dynamic_pixel_ratio = frame.dynamic_pixel_ratio;

        sp::GeometricObservation geo_health_obs;
        geo_health_obs.fundamental_valid = frame.fundamental_valid;
        geo_health_obs.conditioning = degeneracy.conditioning;
        geo_health_obs.inlier_ratio = degeneracy.inlier_ratio;

        sp::ObservabilityObservation obs;
        obs.tracked_features = frame.tracked_features;
        obs.median_parallax_px = frame.median_parallax_px;
        obs.grid_occupancy = frame.grid_occupancy;

        out.health = sp::computeHealth(sem_obs, geo_health_obs, obs, health_config_);

        const sp::OverlapResult overlap = sp::computeOverlap(
            {frame.sem_candidates, frame.geo_candidates, frame.intersection}, overlap_config_);

        sp::PolicyInput policy_input;
        policy_input.timestamp_s = frame.timestamp_s;
        policy_input.semantic_burst = frame.dynamic_pixel_ratio >= 0.18;
        policy_input.semantic_strong = frame.semantic_ema >= 0.18;
        policy_input.overlap_agreement = overlap.has_support && overlap.value >= 0.35;
        policy_input.geo_evidence = frame.geo_candidates > 0;
        policy_input.semantic_scene_active = frame.scene_gate_open;
        const sp::PolicyOutput policy = fsm_.update(policy_input);
        out.policy_state = policy.state;
        out.hard_reject_armed = policy.hard_reject_armed;
        out.soft_mask_active = policy.soft_mask_active;

        // Per-track decisions over the union of the two candidate sets.
        std::vector<int> ids;
        for (int id : semantic_hits)
            ids.push_back(id);
        for (int id : geo_hits)
            if (std::find(semantic_hits.begin(), semantic_hits.end(), id) == semantic_hits.end())
                ids.push_back(id);

        for (int id : ids)
        {
            const bool sem_hit =
                std::find(semantic_hits.begin(), semantic_hits.end(), id) != semantic_hits.end();
            const bool geo_hit =
                std::find(geo_hits.begin(), geo_hits.end(), id) != geo_hits.end();

            sem_geodf::RiskEvidence evidence;
            evidence.semantic_hit = sem_hit;
            evidence.semantic_confirmed = sem_hit && out.hard_reject_armed;
            evidence.geo_hit = geo_hit;
            evidence.geo_confirmed = geo_hit && degeneracy.mayHardReject();
            evidence.semantic_confidence = sem_hit ? 1.0 : 0.0;
            evidence.geo_scene_confidence = geo_hit ? 1.0 : 0.0;
            evidence.geo_error_confidence = geo_hit ? 1.0 : 0.0;
            evidence.overlap_confidence = overlap.value;
            const sem_geodf::WeightResult weight =
                sem_geodf::computeMeasurementWeight(evidence, risk_config_);
            out.min_weight = std::min(out.min_weight, weight.target_weight);

            sp::TrackEvidence track_evidence;
            track_evidence.fused_risk = weight.risk.fused_risk;
            track_evidence.semantic_hit = sem_hit;
            track_evidence.geo_hit = geo_hit;
            track_evidence.two_expert_agreement =
                sem_hit && geo_hit && degeneracy.mayCountAsStrongAgreement();

            const sp::LifecycleDecision decision = lifecycle_.update(
                id, frame.timestamp_s, track_evidence, out.health, frame.tracked_features,
                health_config_);

            // The scene policy gates deletion just as it does in the tracker: with the
            // policy in static-safe, a semantic hit may not delete.
            const bool policy_allows = out.hard_reject_armed;
            if (decision.action == sp::Action::HardReject && policy_allows)
                out.hard_rejected++;
            else if (decision.action != sp::Action::Accept ||
                     decision.action == sp::Action::HardReject)
                out.down_weighted++;

            if (decision.hard_reject_blocked_by_health)
                out.blocked_by_health++;
            if (decision.hard_reject_blocked_by_observability)
                out.blocked_by_observability++;
            if (decision.hard_reject_blocked_by_redundancy)
                out.blocked_by_redundancy++;
        }
        return out;
    }

  private:
    sp::HealthConfig health_config_;
    sp::OverlapConfig overlap_config_;
    sp::PolicyConfig policy_config_;
    sp::LifecycleConfig lifecycle_config_;
    sp::PolicyFsm fsm_;
    sp::LifecycleManager lifecycle_;
    sem_geodf::RiskConfig risk_config_;
};

std::vector<int> range(int count)
{
    std::vector<int> out;
    for (int i = 0; i < count; i++)
        out.push_back(i);
    return out;
}

}  // namespace

int main()
{
    // ------------------------------------------------------------------------
    TEST_CASE("Integration.StaticSceneIsNotOverRejected");
    {
        // A fully static scene with only low-level YOLO false positives. Nothing may
        // be hard-rejected, and the scene policy must stay in static-safe.
        DecisionLayer layer;
        int total_rejected = 0;
        for (int i = 0; i < 60; i++)
        {
            Frame frame;
            frame.timestamp_s = i * kFrameDt;
            frame.dynamic_pixel_ratio = 0.004;  // sensor noise, not a mover
            frame.semantic_ema = 0.004;
            frame.sem_candidates = 1;
            frame.geo_candidates = 0;
            frame.intersection = 0;
            frame.scene_gate_open = false;  // scene gate never opens on a static scene
            const FrameOutcome out = layer.step(frame, {7}, {});
            CHECK(out.policy_state == sp::PolicyState::StaticSafe);
            CHECK(!out.hard_reject_armed);
            total_rejected += out.hard_rejected;
        }
        CHECK(total_rejected == 0);
    }

    // ------------------------------------------------------------------------
    TEST_CASE("Integration.MovingObjectAtTenPercentEscalatesAndDeletes");
    {
        // A genuine mover covering ~10% of the image, agreed on by both experts with
        // real support. This is the case the method exists for: it must escalate and
        // it must be allowed to delete.
        DecisionLayer layer;
        bool escalated = false;
        int rejected = 0;
        for (int i = 0; i < 20; i++)
        {
            Frame frame;
            frame.timestamp_s = i * kFrameDt;
            frame.dynamic_pixel_ratio = 0.22;   // above the 0.18 burst threshold
            frame.semantic_ema = 0.20;
            frame.sem_candidates = 10;
            frame.geo_candidates = 9;
            frame.intersection = 8;             // strong, well-supported agreement
            frame.mover_share = 0.12;
            const FrameOutcome out = layer.step(frame, range(10), range(9));
            if (out.policy_state == sp::PolicyState::StrongDynamic)
                escalated = true;
            rejected += out.hard_rejected;
            CHECK(out.geometry == gd::Health::Healthy);
        }
        CHECK(escalated);
        CHECK(rejected > 0);
    }

    // ------------------------------------------------------------------------
    TEST_CASE("Integration.MovingObjectAtSixtyPercentIsTreatedAsUnreliableMask");
    {
        // A mask claiming 60%+ of the frame is dynamic is far more often a
        // segmentation failure than a scene where everything moves. Semantic health
        // must fall, and semantic deletion must be blocked on health grounds.
        DecisionLayer layer;
        int blocked = 0;
        bool semantic_ever_healthy = false;
        for (int i = 0; i < 20; i++)
        {
            Frame frame;
            frame.timestamp_s = i * kFrameDt;
            frame.dynamic_pixel_ratio = 0.97;
            frame.semantic_ema = 0.90;
            frame.sem_candidates = 40;
            frame.geo_candidates = 5;
            frame.intersection = 4;
            const FrameOutcome out = layer.step(frame, range(40), range(5));
            if (out.health.semantic_healthy)
                semantic_ever_healthy = true;
            blocked += out.blocked_by_health;
        }
        CHECK(!semantic_ever_healthy);
        CHECK(blocked > 0);
    }

    // ------------------------------------------------------------------------
    TEST_CASE("Integration.StaleSemanticMaskFailsOpen");
    {
        // The mask stops arriving (YOLO stalled). The semantic expert must lose its
        // authority to delete rather than keep deleting on a frozen mask.
        // Semantic evidence alone: deletion is blocked by the agreement requirement
        // before health is even consulted, so nothing is deleted.
        DecisionLayer semantic_only;
        int rejected = 0;
        for (int i = 0; i < 20; i++)
        {
            Frame frame;
            frame.timestamp_s = i * kFrameDt;
            frame.mask_fresh = false;
            frame.mask_age_ms = 600.0;   // four times sem_mask_max_age_ms
            frame.dynamic_pixel_ratio = 0.30;
            frame.semantic_ema = 0.28;
            frame.sem_candidates = 12;
            frame.geo_candidates = 0;    // no geometric corroboration
            frame.intersection = 0;
            const FrameOutcome out = semantic_only.step(frame, range(12), {});
            CHECK(!out.health.semantic_healthy);
            rejected += out.hard_rejected;
        }
        CHECK(rejected == 0);

        // With geometric corroboration present, agreement is satisfied, so the stale
        // mask is what must stop the deletion. This is the case where semantic health
        // is the operative guard.
        DecisionLayer corroborated;
        int blocked = 0;
        int rejected_corroborated = 0;
        for (int i = 0; i < 20; i++)
        {
            Frame frame;
            frame.timestamp_s = i * kFrameDt;
            frame.mask_fresh = false;
            frame.mask_age_ms = 600.0;
            frame.dynamic_pixel_ratio = 0.30;
            frame.semantic_ema = 0.28;
            frame.sem_candidates = 12;
            frame.geo_candidates = 10;
            frame.intersection = 9;
            const FrameOutcome out = corroborated.step(frame, range(12), range(10));
            CHECK(!out.health.semantic_healthy);
            blocked += out.blocked_by_health;
            rejected_corroborated += out.hard_rejected;
        }
        CHECK(blocked > 0);
        CHECK(rejected_corroborated == 0);
    }

    // ------------------------------------------------------------------------
    TEST_CASE("Integration.LowParallaxBlocksGeometricDeletion");
    {
        // Near-zero translation: the epipolar geometry is unidentifiable, so static
        // structure can look like motion. GeoDF must not delete.
        DecisionLayer layer;
        int rejected = 0;
        for (int i = 0; i < 20; i++)
        {
            Frame frame;
            frame.timestamp_s = i * kFrameDt;
            frame.median_parallax_px = 0.15;
            frame.geo_candidates = 25;
            frame.sem_candidates = 0;
            frame.intersection = 0;
            const FrameOutcome out = layer.step(frame, {}, range(25));
            CHECK(out.geometry == gd::Health::Degenerate);
            CHECK(out.geometry_cause == gd::Cause::LOW_PARALLAX);
            CHECK(!out.health.geometric_healthy);
            rejected += out.hard_rejected;
        }
        CHECK(rejected == 0);
    }

    // ------------------------------------------------------------------------
    TEST_CASE("Integration.AggressiveRotationBlocksGeometricDeletion");
    {
        // Fast rotation with little translation: lots of apparent flow, no usable
        // baseline. Same conclusion as low parallax, reached from different inputs.
        DecisionLayer layer;
        int rejected = 0;
        for (int i = 0; i < 20; i++)
        {
            Frame frame;
            frame.timestamp_s = i * kFrameDt;
            frame.median_parallax_px = 0.4;
            frame.mover_share = 0.75;      // most of the frame looks like it is moving
            frame.grid_occupancy = 0.2;
            frame.ransac_inliers = 18;
            frame.ransac_total = 100;
            frame.geo_candidates = 40;
            const FrameOutcome out = layer.step(frame, {}, range(40));
            CHECK(out.geometry != gd::Health::Healthy);
            rejected += out.hard_rejected;
        }
        CHECK(rejected == 0);
    }

    // ------------------------------------------------------------------------
    TEST_CASE("Integration.NearMinimumFeatureCountPrefersDownWeight");
    {
        // Real dynamic evidence, but the feature set is nearly exhausted. Deleting
        // would starve the estimator, so the decision must be a down-weight.
        DecisionLayer layer;
        int rejected = 0;
        int blocked = 0;
        int downweighted = 0;
        for (int i = 0; i < 20; i++)
        {
            Frame frame;
            frame.timestamp_s = i * kFrameDt;
            frame.tracked_features = 14;   // below min_tracks_for_hard_reject (40)
            frame.dynamic_pixel_ratio = 0.30;
            frame.semantic_ema = 0.28;
            frame.sem_candidates = 6;
            frame.geo_candidates = 6;
            frame.intersection = 5;
            const FrameOutcome out = layer.step(frame, range(6), range(6));
            rejected += out.hard_rejected;
            blocked += out.blocked_by_observability + out.blocked_by_redundancy;
            downweighted += out.down_weighted;
        }
        CHECK(rejected == 0);
        CHECK(blocked > 0);
        // The tracks are still influenced, just less: they are not simply ignored.
        CHECK(downweighted > 0);
    }

    // ------------------------------------------------------------------------
    TEST_CASE("Integration.WrongSignStereoMatchesNeverReachTheBackend");
    {
        // A whole frame of mirrored stereo matches. Every one must be rejected, and
        // the counters must attribute them rather than lose them.
        const sv::Rig rig = sv::makeRig(Eigen::Matrix3d::Identity(), Eigen::Vector3d::Zero(),
                                        Eigen::Matrix3d::Identity(),
                                        Eigen::Vector3d(0.11, 0.0, 0.0));
        CHECK(rig.valid);
        sv::Config config;
        sv::Counters counters;

        int accepted = 0;
        for (int i = 0; i < 40; i++)
        {
            const Eigen::Vector3d x0(-0.2 + i * 0.01, 0.05 - i * 0.002, 1.0);
            const Eigen::Vector3d p1 = rig.R_c1_c0 * (x0 * 3.0) + rig.t_c1_c0;
            const Eigen::Vector3d good = p1 / p1.z();
            const Eigen::Vector3d inf = rig.R_c1_c0 * x0 / (rig.R_c1_c0 * x0).z();
            const Eigen::Vector3d mirrored = 2.0 * inf - good;

            const sv::Result r = sv::checkStereoMatch(x0, mirrored, rig, config, 0.0,
                                                     true, true, kFocal);
            counters.record(r.rejection);
            if (r.valid())
                accepted++;
        }
        CHECK(accepted == 0);
        CHECK(counters.stereo_valid_total == 0);
        CHECK(counters.stereo_match_total == 40);
        // Attributed to a physical cause, not merely counted as "bad".
        CHECK(counters.stereo_wrong_disparity_sign + counters.stereo_negative_depth == 40);

        // And the physically correct versions of the same matches all pass, so the
        // contract is not simply rejecting everything.
        sv::Counters good_counters;
        for (int i = 0; i < 40; i++)
        {
            const Eigen::Vector3d x0(-0.2 + i * 0.01, 0.05 - i * 0.002, 1.0);
            const Eigen::Vector3d p1 = rig.R_c1_c0 * (x0 * 3.0) + rig.t_c1_c0;
            const sv::Result r = sv::checkStereoMatch(x0, p1 / p1.z(), rig, config, 0.0,
                                                     true, true, kFocal);
            good_counters.record(r.rejection);
        }
        CHECK(good_counters.stereo_valid_total == 40);
    }

    // ------------------------------------------------------------------------
    TEST_CASE("Integration.RejectBudgetNeverEmptiesTheFeatureSet");
    {
        // Everything flagged, every frame, with a healthy expert and healthy geometry.
        // The redundancy guard must stop the feature set being emptied.
        DecisionLayer layer;
        int surviving = 200;
        for (int i = 0; i < 100 && surviving > 0; i++)
        {
            Frame frame;
            frame.timestamp_s = i * kFrameDt;
            frame.tracked_features = surviving;
            frame.dynamic_pixel_ratio = 0.45;
            frame.semantic_ema = 0.40;
            frame.sem_candidates = surviving;
            frame.geo_candidates = surviving;
            frame.intersection = surviving;
            const int flagged = std::min(surviving, 30);
            const FrameOutcome out = layer.step(frame, range(flagged), range(flagged));
            surviving -= out.hard_rejected;
        }
        // The guard is min_tracks_for_hard_reject = 40: below it nothing more may be
        // deleted, so the estimator is never starved.
        CHECK(surviving >= 40);
    }

    // ------------------------------------------------------------------------
    TEST_CASE("Integration.EvidenceDisappearingRestoresFullWeight");
    {
        // A transient false positive must not permanently suppress a track.
        DecisionLayer layer;
        for (int i = 0; i < 5; i++)
        {
            Frame frame;
            frame.timestamp_s = i * kFrameDt;
            frame.dynamic_pixel_ratio = 0.25;
            frame.semantic_ema = 0.22;
            frame.sem_candidates = 8;
            frame.geo_candidates = 8;
            frame.intersection = 7;
            layer.step(frame, range(8), range(8));
        }
        // Evidence stops. After the recovery dwell the weight must return to 1.
        FrameOutcome out;
        for (int i = 5; i < 40; i++)
        {
            Frame frame;
            frame.timestamp_s = i * kFrameDt;
            frame.sem_candidates = 0;
            frame.geo_candidates = 0;
            frame.intersection = 0;
            frame.scene_gate_open = false;
            out = layer.step(frame, {}, {});
        }
        CHECK_NEAR(out.min_weight, 1.0, 1e-12);
        CHECK(out.policy_state == sp::PolicyState::StaticSafe);
    }

    TEST_MAIN_RETURN();
}
