#pragma once

// Semantic-GeoDF policy correctness (plan P1.1 - P1.4).
//
// Four separate problems are fixed here, and each is a distinct component so that
// health measurement, action choice and per-track lifecycle cannot be conflated:
//
//  P1.1  Overlap used max(|I|/|G|, |I|/|S|), so a two-element candidate set sharing
//        one feature scored 0.5 overlap. Dice with a minimum-support requirement and
//        an optional support-confidence factor replaces it.
//  P1.2  The hold timer counted FRAMES (sem_policy_hold_frames: 180), so its real
//        duration depended on camera rate and on dropped frames. It is now a
//        duration in seconds, driven by sensor timestamps, never wall-clock.
//  P1.3  The dynamic pixel ratio decided hard rejection directly. Health scores are
//        now measured first, and the action policy consumes them: an unhealthy
//        semantic expert cannot hard-reject, a degenerate geometry cannot hard-reject,
//        and low observability downgrades deletion to down-weighting.
//  P1.4  A single three-state scene FSM said nothing about individual tracks. Each
//        track now has a lifecycle, and hard rejection requires risk AND redundancy
//        AND observability.

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace sem_policy
{

inline double clamp01(double v)
{
    return std::min(1.0, std::max(0.0, v));
}

// ---------------------------------------------------------------------------
// P1.1 Overlap between the semantic and geometric candidate sets
// ---------------------------------------------------------------------------

enum class OverlapMetric
{
    DirectionalMax,  // legacy behaviour, kept only for the ablation
    DirectionalMin,
    Dice,
    Jaccard,
    DiceSupport,     // Dice scaled by confidence in the size of the intersection
};

inline OverlapMetric parseOverlapMetric(const std::string &name, bool *ok = nullptr)
{
    if (ok)
        *ok = true;
    if (name == "max" || name == "directional_max")
        return OverlapMetric::DirectionalMax;
    if (name == "min" || name == "directional_min")
        return OverlapMetric::DirectionalMin;
    if (name == "dice")
        return OverlapMetric::Dice;
    if (name == "jaccard")
        return OverlapMetric::Jaccard;
    if (name == "dice_support")
        return OverlapMetric::DiceSupport;
    if (ok)
        *ok = false;
    return OverlapMetric::Dice;
}

inline const char *toString(OverlapMetric metric)
{
    switch (metric)
    {
        case OverlapMetric::DirectionalMax: return "directional_max";
        case OverlapMetric::DirectionalMin: return "directional_min";
        case OverlapMetric::Dice:           return "dice";
        case OverlapMetric::Jaccard:        return "jaccard";
        case OverlapMetric::DiceSupport:    return "dice_support";
    }
    return "unknown";
}

struct OverlapConfig
{
    OverlapMetric metric = OverlapMetric::Dice;
    // Minimum support. Without these, one shared feature between two tiny sets
    // produced a large overlap and armed the policy on noise.
    int min_sem_candidates = 4;
    int min_geo_candidates = 4;
    int min_intersection = 2;
    // Intersection size at which support confidence saturates.
    int support_saturation = 8;
};

struct OverlapCounts
{
    int sem_candidates = 0;
    int geo_candidates = 0;
    int intersection = 0;
};

struct OverlapResult
{
    // False when the candidate sets are too small for the number to mean anything.
    bool has_support = false;
    double raw = 0.0;         // the chosen metric, before support scaling
    double support = 0.0;     // confidence in the intersection size, in [0, 1]
    double value = 0.0;       // what the policy should use
};

inline OverlapResult computeOverlap(const OverlapCounts &counts, const OverlapConfig &config)
{
    OverlapResult out;
    const double s = static_cast<double>(std::max(0, counts.sem_candidates));
    const double g = static_cast<double>(std::max(0, counts.geo_candidates));
    const double i = static_cast<double>(std::max(0, counts.intersection));

    out.has_support = counts.sem_candidates >= config.min_sem_candidates &&
                      counts.geo_candidates >= config.min_geo_candidates &&
                      counts.intersection >= config.min_intersection;

    const int saturation = std::max(1, config.support_saturation);
    out.support = clamp01(i / static_cast<double>(saturation));

    if (s <= 0.0 || g <= 0.0)
        return out;

    switch (config.metric)
    {
        case OverlapMetric::DirectionalMax:
            out.raw = std::max(i / g, i / s);
            break;
        case OverlapMetric::DirectionalMin:
            out.raw = std::min(i / g, i / s);
            break;
        case OverlapMetric::Dice:
        case OverlapMetric::DiceSupport:
            out.raw = 2.0 * i / (g + s);
            break;
        case OverlapMetric::Jaccard:
        {
            const double union_size = g + s - i;
            out.raw = union_size > 0.0 ? i / union_size : 0.0;
            break;
        }
    }
    out.raw = clamp01(out.raw);

    // No support means the number is not evidence, so it must not reach a threshold
    // comparison as if it were.
    if (!out.has_support)
    {
        out.value = 0.0;
        return out;
    }
    out.value = config.metric == OverlapMetric::DiceSupport ? out.raw * out.support : out.raw;
    return out;
}

// ---------------------------------------------------------------------------
// P1.2 / P1.4 Scene policy FSM, driven by sensor timestamps
// ---------------------------------------------------------------------------

enum class PolicyState
{
    StaticSafe = 0,
    DynamicAssist = 1,
    StrongDynamic = 2,
};

inline const char *toString(PolicyState state)
{
    switch (state)
    {
        case PolicyState::StaticSafe:    return "static_safe";
        case PolicyState::DynamicAssist: return "dynamic_assist";
        case PolicyState::StrongDynamic: return "strong_dynamic";
    }
    return "unknown";
}

struct PolicyConfig
{
    // Durations, not frame counts: a frame count silently changes meaning with the
    // camera rate and with dropped frames.
    double assist_hold_s = 1.5;
    double strong_hold_s = 1.0;
    // Minimum time in a state before it may be LEFT DOWNWARD. Escalation is never
    // delayed: waiting to protect against real dynamic content is the unsafe
    // direction.
    double min_state_dwell_s = 0.3;
};

struct PolicyInput
{
    double timestamp_s = 0.0;  // sensor timestamp, never wall-clock
    bool semantic_burst = false;
    bool semantic_strong = false;
    bool overlap_agreement = false;
    bool geo_evidence = false;
    bool semantic_scene_active = false;
};

struct PolicyOutput
{
    PolicyState state = PolicyState::StaticSafe;
    bool assist_hold_active = false;
    bool strong_hold_active = false;
    double assist_hold_remaining_s = 0.0;
    bool soft_mask_active = false;
    bool hard_reject_armed = false;
    bool dwell_blocked_downgrade = false;
};

class PolicyFsm
{
  public:
    PolicyFsm() = default;
    explicit PolicyFsm(const PolicyConfig &config) : config_(config) {}

    void configure(const PolicyConfig &config) { config_ = config; }
    const PolicyConfig &config() const { return config_; }

    void reset()
    {
        state_ = PolicyState::StaticSafe;
        assist_hold_until_s_ = -1.0;
        strong_hold_until_s_ = -1.0;
        state_entered_s_ = -1.0;
        initialized_ = false;
    }

    PolicyState state() const { return state_; }

    PolicyOutput update(const PolicyInput &in)
    {
        if (!initialized_)
        {
            state_entered_s_ = in.timestamp_s;
            initialized_ = true;
        }
        // A bag restart or a backwards timestamp is a discontinuity, not a state
        // transition: clear the holds AND the state, or the dwell timer would keep the
        // policy armed across the restart.
        if (in.timestamp_s < last_timestamp_s_)
        {
            state_ = PolicyState::StaticSafe;
            assist_hold_until_s_ = -1.0;
            strong_hold_until_s_ = -1.0;
            // Backdate so the dwell guard cannot block the first honest downgrade.
            state_entered_s_ = in.timestamp_s - config_.min_state_dwell_s;
        }
        last_timestamp_s_ = in.timestamp_s;

        const bool any_trigger =
            in.semantic_burst || in.semantic_strong || in.overlap_agreement;
        if (any_trigger)
            assist_hold_until_s_ =
                std::max(assist_hold_until_s_, in.timestamp_s + std::max(0.0, config_.assist_hold_s));
        if (in.semantic_strong || in.overlap_agreement)
            strong_hold_until_s_ =
                std::max(strong_hold_until_s_, in.timestamp_s + std::max(0.0, config_.strong_hold_s));

        PolicyOutput out;
        out.assist_hold_active = in.timestamp_s < assist_hold_until_s_;
        out.strong_hold_active = in.timestamp_s < strong_hold_until_s_;
        out.assist_hold_remaining_s =
            std::max(0.0, assist_hold_until_s_ - in.timestamp_s);

        PolicyState desired = PolicyState::StaticSafe;
        if (in.semantic_strong || in.overlap_agreement || out.strong_hold_active ||
            (out.assist_hold_active && in.geo_evidence))
            desired = PolicyState::StrongDynamic;
        else if (out.assist_hold_active)
            desired = PolicyState::DynamicAssist;

        const double dwell = in.timestamp_s - state_entered_s_;
        if (desired < state_ && dwell < config_.min_state_dwell_s)
        {
            // Hold the higher state a little longer rather than flickering.
            out.dwell_blocked_downgrade = true;
            desired = state_;
        }
        if (desired != state_)
        {
            state_ = desired;
            state_entered_s_ = in.timestamp_s;
        }

        out.state = state_;
        // Static-safe still permits scene-gated soft masking, which is what keeps the
        // fully static sequences from regressing.
        out.soft_mask_active = in.semantic_scene_active || out.assist_hold_active;
        out.hard_reject_armed =
            in.semantic_scene_active && state_ != PolicyState::StaticSafe;
        return out;
    }

  private:
    PolicyConfig config_;
    PolicyState state_ = PolicyState::StaticSafe;
    double assist_hold_until_s_ = -1.0;
    double strong_hold_until_s_ = -1.0;
    double state_entered_s_ = -1.0;
    double last_timestamp_s_ = -1.0;
    bool initialized_ = false;
};

// ---------------------------------------------------------------------------
// P1.3 Health scores, measured separately from the action they inform
// ---------------------------------------------------------------------------

struct HealthConfig
{
    // Semantic health
    double mask_max_age_ms = 150.0;
    double mask_saturation_ratio = 0.60;  // a mask covering most of the frame is suspect
    double min_semantic_health = 0.35;
    // Geometric health
    double min_geometric_health = 0.35;
    // Observability
    int redundancy_target = 60;      // tracked features at which redundancy saturates
    double parallax_target_px = 3.0; // median parallax at which motion is well observed
    double min_observability = 0.35;
    // Hard rejection needs genuine redundancy, not just a healthy expert.
    int min_tracks_for_hard_reject = 40;
};

struct SemanticObservation
{
    bool mask_available = false;
    bool mask_fresh = false;
    double mask_age_ms = 0.0;
    double dynamic_pixel_ratio = 0.0;
};

struct GeometricObservation
{
    bool fundamental_valid = false;
    // From the degeneracy checks (see geodf_degeneracy.h). 1.0 = well conditioned.
    double conditioning = 0.0;
    double inlier_ratio = 0.0;
};

struct ObservabilityObservation
{
    int tracked_features = 0;
    double median_parallax_px = 0.0;
    double grid_occupancy = 0.0;  // spatial spread of the surviving tracks, in [0, 1]
};

struct Health
{
    double semantic = 0.0;
    double geometric = 0.0;
    double observability = 0.0;
    bool semantic_healthy = false;
    bool geometric_healthy = false;
    bool observability_healthy = false;
};

// A stale, missing or frame-filling mask is not a trustworthy expert. Note the
// intent: this measures whether the SEMANTIC EXPERT can be believed, not whether the
// scene is dynamic.
inline double semanticHealth(const SemanticObservation &obs, const HealthConfig &config)
{
    if (!obs.mask_available)
        return 0.0;
    double health = obs.mask_fresh ? 1.0 : 0.5;
    if (config.mask_max_age_ms > 0.0)
        health *= clamp01(1.0 - obs.mask_age_ms / (2.0 * config.mask_max_age_ms));
    // A mask that calls most of the frame dynamic is far more likely to be a
    // segmentation failure than a scene where everything moves.
    if (config.mask_saturation_ratio > 0.0 &&
        obs.dynamic_pixel_ratio > config.mask_saturation_ratio)
    {
        const double excess = (obs.dynamic_pixel_ratio - config.mask_saturation_ratio) /
                              std::max(1e-6, 1.0 - config.mask_saturation_ratio);
        health *= clamp01(1.0 - excess);
    }
    return clamp01(health);
}

inline double geometricHealth(const GeometricObservation &obs, const HealthConfig &)
{
    if (!obs.fundamental_valid)
        return 0.0;
    return clamp01(std::min(clamp01(obs.conditioning), clamp01(obs.inlier_ratio)));
}

inline double observabilityHealth(const ObservabilityObservation &obs,
                                 const HealthConfig &config)
{
    const double redundancy =
        clamp01(static_cast<double>(obs.tracked_features) /
                std::max(1.0, static_cast<double>(config.redundancy_target)));
    const double parallax =
        clamp01(obs.median_parallax_px / std::max(1e-6, config.parallax_target_px));
    const double spread = clamp01(obs.grid_occupancy);
    // The weakest of the three governs: plenty of features clustered in one corner
    // does not make camera motion observable.
    return std::min(redundancy, std::min(parallax, spread));
}

inline Health computeHealth(const SemanticObservation &sem, const GeometricObservation &geo,
                            const ObservabilityObservation &obs, const HealthConfig &config)
{
    Health out;
    out.semantic = semanticHealth(sem, config);
    out.geometric = geometricHealth(geo, config);
    out.observability = observabilityHealth(obs, config);
    out.semantic_healthy = out.semantic >= config.min_semantic_health;
    out.geometric_healthy = out.geometric >= config.min_geometric_health;
    out.observability_healthy = out.observability >= config.min_observability;
    return out;
}

// ---------------------------------------------------------------------------
// P1.4 Per-track lifecycle
// ---------------------------------------------------------------------------

enum class TrackState
{
    Trusted = 0,
    Suspect,
    DownWeighted,
    Rejected,
    Recovering,
};

inline const char *toString(TrackState state)
{
    switch (state)
    {
        case TrackState::Trusted:      return "TRUSTED";
        case TrackState::Suspect:      return "SUSPECT";
        case TrackState::DownWeighted: return "DOWNWEIGHTED";
        case TrackState::Rejected:     return "REJECTED";
        case TrackState::Recovering:   return "RECOVERING";
    }
    return "UNKNOWN";
}

// What the policy is allowed to do to a track this frame.
enum class Action
{
    Accept = 0,
    DownWeight,
    HardReject,
};

inline const char *toString(Action action)
{
    switch (action)
    {
        case Action::Accept:     return "accept";
        case Action::DownWeight: return "down_weight";
        case Action::HardReject: return "hard_reject";
    }
    return "unknown";
}

struct LifecycleConfig
{
    // Consecutive suspicious frames before a track leaves TRUSTED, and before a
    // SUSPECT track is actually down-weighted.
    int suspect_frames = 1;
    int downweight_frames = 2;
    // Hard rejection needs all three: high risk, agreement, and enough redundancy.
    double hard_reject_risk = 0.60;
    bool hard_reject_requires_agreement = true;
    // Time a track must stay clean before it is trusted again.
    double recover_dwell_s = 0.5;
};

struct TrackEvidence
{
    double fused_risk = 0.0;
    bool semantic_hit = false;
    bool geo_hit = false;
    bool two_expert_agreement = false;
};

struct TrackRecord
{
    TrackState state = TrackState::Trusted;
    int evidence_streak = 0;
    int clean_streak = 0;
    double state_entered_s = 0.0;
};

struct LifecycleDecision
{
    TrackState state = TrackState::Trusted;
    Action action = Action::Accept;
    // Set when hard rejection was warranted by evidence but blocked by a guard, so
    // the reason is auditable rather than invisible.
    bool hard_reject_blocked_by_observability = false;
    bool hard_reject_blocked_by_health = false;
    bool hard_reject_blocked_by_redundancy = false;
};

class LifecycleManager
{
  public:
    void configure(const LifecycleConfig &config) { config_ = config; }
    const LifecycleConfig &config() const { return config_; }
    void clear()
    {
        tracks_.clear();
        frame_started_ = false;
        frame_deletion_allowance_ = 0;
    }
    size_t size() const { return tracks_.size(); }

    const TrackRecord *find(int id) const
    {
        const auto it = tracks_.find(id);
        return it == tracks_.end() ? nullptr : &it->second;
    }

    // Drop bookkeeping for ids that no longer exist, so the map cannot grow without
    // bound over a long sequence.
    template <typename IdContainer>
    void retainOnly(const IdContainer &live_ids)
    {
        std::map<int, TrackRecord> kept;
        for (int id : live_ids)
        {
            const auto it = tracks_.find(id);
            if (it != tracks_.end())
                kept.emplace(id, it->second);
        }
        tracks_.swap(kept);
    }

    // Deletions still permitted this frame. Exposed for telemetry.
    int deletionAllowance() const { return frame_deletion_allowance_; }

    LifecycleDecision update(int id, double timestamp_s, const TrackEvidence &evidence,
                             const Health &health, int tracked_features,
                             const HealthConfig &health_config)
    {
        // A per-frame deletion budget. Checking `tracked_features >= min_tracks` once
        // per track is not enough: with N tracks flagged in the same frame, each check
        // sees the pre-deletion count and the batch can overshoot the minimum. The
        // allowance is what actually keeps the feature set above the floor.
        if (timestamp_s != frame_timestamp_s_ || !frame_started_)
        {
            frame_timestamp_s_ = timestamp_s;
            frame_started_ = true;
            frame_deletion_allowance_ =
                std::max(0, tracked_features - health_config.min_tracks_for_hard_reject);
        }

        TrackRecord &record = tracks_.emplace(id, TrackRecord{}).first->second;
        if (record.state_entered_s == 0.0)
            record.state_entered_s = timestamp_s;

        const bool suspicious = evidence.semantic_hit || evidence.geo_hit;
        if (suspicious)
        {
            record.evidence_streak++;
            record.clean_streak = 0;
        }
        else
        {
            record.clean_streak++;
            record.evidence_streak = 0;
        }

        const TrackState previous = record.state;

        if (!suspicious)
        {
            // Evidence disappeared: go through RECOVERING, and only return to TRUSTED
            // after staying clean for the dwell time.
            if (previous == TrackState::Suspect || previous == TrackState::DownWeighted ||
                previous == TrackState::Rejected)
                setState(record, TrackState::Recovering, timestamp_s);
            else if (previous == TrackState::Recovering &&
                     timestamp_s - record.state_entered_s >= config_.recover_dwell_s)
                setState(record, TrackState::Trusted, timestamp_s);
        }
        else if (record.evidence_streak >= config_.downweight_frames)
        {
            setState(record, TrackState::DownWeighted, timestamp_s);
        }
        else if (record.evidence_streak >= config_.suspect_frames)
        {
            setState(record, TrackState::Suspect, timestamp_s);
        }

        LifecycleDecision decision;
        decision.state = record.state;
        decision.action = record.state == TrackState::DownWeighted ? Action::DownWeight
                                                                   : Action::Accept;

        // Hard rejection is the only irreversible action, so it needs every guard.
        const bool risk_warrants = evidence.fused_risk >= config_.hard_reject_risk;
        const bool agreement_ok =
            !config_.hard_reject_requires_agreement || evidence.two_expert_agreement;
        if (record.state == TrackState::DownWeighted && risk_warrants && agreement_ok)
        {
            const bool experts_healthy =
                (!evidence.semantic_hit || health.semantic_healthy) &&
                (!evidence.geo_hit || health.geometric_healthy);
            // Both the standing floor and the remaining budget for THIS frame.
            const bool redundant =
                tracked_features >= health_config.min_tracks_for_hard_reject &&
                frame_deletion_allowance_ > 0;

            if (!experts_healthy)
                decision.hard_reject_blocked_by_health = true;
            else if (!health.observability_healthy)
                decision.hard_reject_blocked_by_observability = true;
            else if (!redundant)
                decision.hard_reject_blocked_by_redundancy = true;
            else
            {
                frame_deletion_allowance_--;
                setState(record, TrackState::Rejected, timestamp_s);
                decision.state = record.state;
                decision.action = Action::HardReject;
            }
        }
        return decision;
    }

  private:
    static void setState(TrackRecord &record, TrackState state, double timestamp_s)
    {
        if (record.state == state)
            return;
        record.state = state;
        record.state_entered_s = timestamp_s;
    }

    LifecycleConfig config_;
    std::map<int, TrackRecord> tracks_;
    // Per-frame deletion budget (see update()).
    double frame_timestamp_s_ = 0.0;
    bool frame_started_ = false;
    int frame_deletion_allowance_ = 0;
};

}  // namespace sem_policy
