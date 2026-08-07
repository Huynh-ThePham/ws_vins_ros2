#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <Eigen/Core>

namespace adaptive_factor
{

inline double clamp(double value, double lo, double hi)
{
    return std::min(hi, std::max(lo, value));
}

struct VisualQualityConfig
{
    bool enabled = false;
    double min_weight = 0.35;
    double lk_error_scale = 20.0;
    double fb_error_scale = 0.5;
    int full_age = 4;
};

inline double visualObservationWeight(double lk_error,
                                      double fb_error,
                                      int track_age,
                                      const VisualQualityConfig &config)
{
    if (!config.enabled)
        return 1.0;

    const double lk_scale = std::max(1e-6, config.lk_error_scale);
    const double fb_scale = std::max(1e-6, config.fb_error_scale);
    const double lk_ratio = std::max(0.0, lk_error) / lk_scale;
    const double fb_ratio = std::max(0.0, fb_error) / fb_scale;
    const double q_lk = 1.0 / (1.0 + lk_ratio * lk_ratio);
    const double q_fb = 1.0 / (1.0 + fb_ratio * fb_ratio);
    const double age_ratio =
        clamp(static_cast<double>(std::max(1, track_age)) /
                  static_cast<double>(std::max(1, config.full_age)),
              0.0, 1.0);
    // New tracks are useful for coverage but should not immediately carry the
    // same information as a temporally verified track.
    const double q_age = 0.85 + 0.15 * age_ratio;
    return clamp(q_lk * q_fb * q_age,
                 clamp(config.min_weight, 0.01, 1.0), 1.0);
}

inline double median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    const std::size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    const double upper = values[mid];
    if (values.size() % 2 != 0)
        return upper;
    std::nth_element(values.begin(), values.begin() + mid - 1, values.end());
    return 0.5 * (values[mid - 1] + upper);
}

struct AdaptiveHuberConfig
{
    bool enabled = false;
    double min_delta = 0.75;
    double max_delta = 3.0;
    double k = 1.345;
    double ema = 0.10;
    int min_samples = 30;
};

// Robust scale from SIGNED whitened residual components.
//
// Callers must pass rx * sqrt_info, ry * sqrt_info (signed), never |r|, never
// Rayleigh norms, and never residuals already multiplied by semantic/GeoDF √w.
//
// For a 1-D Gaussian, MAD ≈ 0.6745 σ so σ̂ = 1.4826 * median(|r - median(r)|).
// Do NOT also use median(|r|)/0.6745 on folded |r| samples — that double-counts.
//
// The delta EMA deliberately stays per-optimization rather than continuous-time.
// A time-based version was benchmarked (docs/ATE_STUDY_P0.md): it left ten of
// twelve cells bit-identical and cost 10.8% median ATE on one training cell, so
// its only measurable effect was to make one scene worse.
inline double adaptiveHuberDelta(const std::vector<double> &whitened_components,
                                 double previous_delta,
                                 double fallback_delta,
                                 const AdaptiveHuberConfig &config)
{
    const double fallback =
        clamp(fallback_delta, config.min_delta, config.max_delta);
    if (!config.enabled ||
        static_cast<int>(whitened_components.size()) < std::max(1, config.min_samples))
        return config.enabled ? clamp(previous_delta, config.min_delta, config.max_delta)
                              : fallback;

    std::vector<double> finite;
    finite.reserve(whitened_components.size());
    for (double value : whitened_components)
    {
        if (std::isfinite(value))
            finite.push_back(value);
    }
    if (static_cast<int>(finite.size()) < std::max(1, config.min_samples))
        return clamp(previous_delta, config.min_delta, config.max_delta);

    const double med = median(finite);
    std::vector<double> deviations;
    deviations.reserve(finite.size());
    for (double value : finite)
        deviations.push_back(std::abs(value - med));

    constexpr double gaussian_mad_to_sigma = 1.4826;
    const double robust_sigma =
        std::max(0.1, gaussian_mad_to_sigma * median(deviations));
    const double target =
        clamp(config.k * robust_sigma, config.min_delta, config.max_delta);
    const double previous = std::isfinite(previous_delta) && previous_delta > 0.0
                                ? previous_delta
                                : fallback;

    const double alpha = clamp(config.ema, 0.0, 1.0);
    return clamp((1.0 - alpha) * previous + alpha * target,
                 config.min_delta, config.max_delta);
}

struct ImuQualityConfig
{
    bool enabled = false;
    double gap_threshold_s = 0.015;
    double acc_saturation = 80.0;
    double gyr_saturation = 8.0;
    double gap_gain = 3.0;
    double saturation_gain = 10.0;
    double max_inflation = 20.0;
};

struct ImuNoiseInflation
{
    double acc_measurement = 1.0;
    double gyr_measurement = 1.0;
    double acc_bias_walk = 1.0;
    double gyr_bias_walk = 1.0;

    double maxFactor() const
    {
        return std::max({acc_measurement, gyr_measurement, acc_bias_walk, gyr_bias_walk});
    }
};

// Inflate IMU noise blocks independently from failure evidence.
// Accel saturation must NOT automatically inflate gyro noise (and vice versa).
// Packet gaps inflate measurement noise more than bias random walk.
inline ImuNoiseInflation imuNoiseInflationSplit(double dt,
                                                const Eigen::Vector3d &acc0,
                                                const Eigen::Vector3d &gyr0,
                                                const Eigen::Vector3d &acc1,
                                                const Eigen::Vector3d &gyr1,
                                                const ImuQualityConfig &config)
{
    ImuNoiseInflation out;
    if (!config.enabled)
        return out;

    const double gap_threshold = std::max(1e-6, config.gap_threshold_s);
    const double acc_limit = std::max(1e-6, config.acc_saturation);
    const double gyr_limit = std::max(1e-6, config.gyr_saturation);
    const double gap_excess = std::max(0.0, dt / gap_threshold - 1.0);
    const double acc_excess =
        std::max(0.0, std::max(acc0.norm(), acc1.norm()) / acc_limit - 1.0);
    const double gyr_excess =
        std::max(0.0, std::max(gyr0.norm(), gyr1.norm()) / gyr_limit - 1.0);

    const double gap_term =
        std::max(0.0, config.gap_gain) * gap_excess * gap_excess;
    const double acc_term =
        std::max(0.0, config.saturation_gain) * acc_excess * acc_excess;
    const double gyr_term =
        std::max(0.0, config.saturation_gain) * gyr_excess * gyr_excess;
    const double max_inf = std::max(1.0, config.max_inflation);

    out.acc_measurement = clamp(1.0 + gap_term + acc_term, 1.0, max_inf);
    out.gyr_measurement = clamp(1.0 + gap_term + gyr_term, 1.0, max_inf);
    // Bias random walk: milder inflation; gap evidence only (not saturation of
    // a single sample, which is a measurement event).
    out.acc_bias_walk = clamp(1.0 + 0.25 * gap_term, 1.0, max_inf);
    out.gyr_bias_walk = clamp(1.0 + 0.25 * gap_term, 1.0, max_inf);

    if (!std::isfinite(out.acc_measurement) || !std::isfinite(out.gyr_measurement) ||
        !std::isfinite(out.acc_bias_walk) || !std::isfinite(out.gyr_bias_walk))
    {
        out.acc_measurement = out.gyr_measurement = out.acc_bias_walk =
            out.gyr_bias_walk = max_inf;
    }
    return out;
}

// Backward-compatible scalar: max block inflation (for logging / mean stats).
inline double imuNoiseInflation(double dt,
                                const Eigen::Vector3d &acc0,
                                const Eigen::Vector3d &gyr0,
                                const Eigen::Vector3d &acc1,
                                const Eigen::Vector3d &gyr1,
                                const ImuQualityConfig &config)
{
    return imuNoiseInflationSplit(dt, acc0, gyr0, acc1, gyr1, config).maxFactor();
}

}  // namespace adaptive_factor
