#pragma once

// Structured VIO failure detection (plan P0.4).
//
// Estimator::failureDetection() used to begin with `return false;`, so every check
// below it was dead code and a diverged run produced a plausible-looking
// trajectory instead of being reported as failed. That silently removed the worst
// cases from every table.
//
// The decision logic lives here as a pure function so it can be unit-tested
// against synthetic divergence (NaN, bias explosion, empty feature set, pose jump)
// without a dataset or a ROS node.

#include <cmath>

namespace failure_detection
{

enum class FailureReason
{
    NONE = 0,
    LOW_FEATURE_COUNT,
    ACC_BIAS_DIVERGENCE,
    GYRO_BIAS_DIVERGENCE,
    TRANSLATION_JUMP,
    ROTATION_JUMP,
    SOLVER_FAILURE,
    NAN_STATE,
};

inline const char *toString(FailureReason reason)
{
    switch (reason)
    {
        case FailureReason::NONE:                return "NONE";
        case FailureReason::LOW_FEATURE_COUNT:   return "LOW_FEATURE_COUNT";
        case FailureReason::ACC_BIAS_DIVERGENCE: return "ACC_BIAS_DIVERGENCE";
        case FailureReason::GYRO_BIAS_DIVERGENCE:return "GYRO_BIAS_DIVERGENCE";
        case FailureReason::TRANSLATION_JUMP:    return "TRANSLATION_JUMP";
        case FailureReason::ROTATION_JUMP:       return "ROTATION_JUMP";
        case FailureReason::SOLVER_FAILURE:      return "SOLVER_FAILURE";
        case FailureReason::NAN_STATE:           return "NAN_STATE";
    }
    return "UNKNOWN";
}

struct Config
{
    bool enable = true;
    double max_acc_bias = 2.5;
    double max_gyro_bias = 1.0;
    double max_translation_step_m = 5.0;
    double max_rotation_step_deg = 50.0;
    int min_tracked_features = 10;
    // A single sparse frame is normal (a turn, a textureless wall). Only a
    // sustained run of them is a failure.
    int max_consecutive_low_feature_frames = 5;
};

// Per-frame observables. Everything is already reduced to scalars by the caller so
// this header stays free of Eigen and of the estimator's state layout.
struct Observation
{
    int tracked_features = 0;
    double acc_bias_norm = 0.0;
    double gyro_bias_norm = 0.0;
    double translation_step_m = 0.0;
    double rotation_step_deg = 0.0;
    bool solver_failed = false;
    bool state_has_nan = false;
};

class Detector
{
  public:
    Detector() = default;
    explicit Detector(const Config &config) : config_(config) {}

    void configure(const Config &config)
    {
        config_ = config;
        reset();
    }

    void reset() { low_feature_streak_ = 0; }

    const Config &config() const { return config_; }
    int lowFeatureStreak() const { return low_feature_streak_; }

    // Checks run most-severe first, so the reported reason is the root cause
    // rather than whatever happened to be tested first.
    FailureReason evaluate(const Observation &obs)
    {
        if (!config_.enable)
        {
            low_feature_streak_ = 0;
            return FailureReason::NONE;
        }

        // A non-finite state is unrecoverable and makes every other test
        // meaningless, so it is checked before anything else.
        if (obs.state_has_nan || !std::isfinite(obs.acc_bias_norm) ||
            !std::isfinite(obs.gyro_bias_norm) || !std::isfinite(obs.translation_step_m) ||
            !std::isfinite(obs.rotation_step_deg))
        {
            low_feature_streak_ = 0;
            return FailureReason::NAN_STATE;
        }

        if (obs.solver_failed)
        {
            low_feature_streak_ = 0;
            return FailureReason::SOLVER_FAILURE;
        }

        if (obs.acc_bias_norm > config_.max_acc_bias)
        {
            low_feature_streak_ = 0;
            return FailureReason::ACC_BIAS_DIVERGENCE;
        }

        if (obs.gyro_bias_norm > config_.max_gyro_bias)
        {
            low_feature_streak_ = 0;
            return FailureReason::GYRO_BIAS_DIVERGENCE;
        }

        if (obs.translation_step_m > config_.max_translation_step_m)
        {
            low_feature_streak_ = 0;
            return FailureReason::TRANSLATION_JUMP;
        }

        if (obs.rotation_step_deg > config_.max_rotation_step_deg)
        {
            low_feature_streak_ = 0;
            return FailureReason::ROTATION_JUMP;
        }

        if (obs.tracked_features < config_.min_tracked_features)
        {
            low_feature_streak_++;
            if (low_feature_streak_ >= config_.max_consecutive_low_feature_frames)
                return FailureReason::LOW_FEATURE_COUNT;
            return FailureReason::NONE;
        }

        low_feature_streak_ = 0;
        return FailureReason::NONE;
    }

  private:
    Config config_;
    int low_feature_streak_ = 0;
};

}  // namespace failure_detection
