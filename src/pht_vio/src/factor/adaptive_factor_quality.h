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

inline double adaptiveHuberDelta(const std::vector<double> &whitened_norms,
                                 double previous_delta,
                                 double fallback_delta,
                                 const AdaptiveHuberConfig &config)
{
    const double fallback =
        clamp(fallback_delta, config.min_delta, config.max_delta);
    if (!config.enabled ||
        static_cast<int>(whitened_norms.size()) < std::max(1, config.min_samples))
        return config.enabled ? clamp(previous_delta, config.min_delta, config.max_delta)
                              : fallback;

    std::vector<double> finite;
    finite.reserve(whitened_norms.size());
    for (double value : whitened_norms)
    {
        if (std::isfinite(value) && value >= 0.0)
            finite.push_back(value);
    }
    if (static_cast<int>(finite.size()) < std::max(1, config.min_samples))
        return clamp(previous_delta, config.min_delta, config.max_delta);

    const double med = median(finite);
    std::vector<double> deviations;
    deviations.reserve(finite.size());
    for (double value : finite)
        deviations.push_back(std::abs(value - med));

    // A 2-D whitened Gaussian has a Rayleigh-distributed norm whose median is
    // sqrt(2 log 2) times its component standard deviation. MAD is retained as
    // a safeguard for mixed/heavy-tailed residual sets.
    constexpr double rayleigh_median = 1.1774100225154747;
    const double sigma_from_median = med / rayleigh_median;
    const double sigma_from_mad = 1.4826 * median(deviations);
    const double robust_sigma =
        std::max(0.1, std::max(sigma_from_median, sigma_from_mad));
    const double target =
        clamp(config.k * robust_sigma, config.min_delta, config.max_delta);
    const double alpha = clamp(config.ema, 0.0, 1.0);
    const double previous = std::isfinite(previous_delta) && previous_delta > 0.0
                                ? previous_delta
                                : fallback;
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

inline double imuNoiseInflation(double dt,
                                const Eigen::Vector3d &acc0,
                                const Eigen::Vector3d &gyr0,
                                const Eigen::Vector3d &acc1,
                                const Eigen::Vector3d &gyr1,
                                const ImuQualityConfig &config)
{
    if (!config.enabled)
        return 1.0;

    const double gap_threshold = std::max(1e-6, config.gap_threshold_s);
    const double acc_limit = std::max(1e-6, config.acc_saturation);
    const double gyr_limit = std::max(1e-6, config.gyr_saturation);
    const double gap_excess = std::max(0.0, dt / gap_threshold - 1.0);
    const double acc_excess =
        std::max(0.0, std::max(acc0.norm(), acc1.norm()) / acc_limit - 1.0);
    const double gyr_excess =
        std::max(0.0, std::max(gyr0.norm(), gyr1.norm()) / gyr_limit - 1.0);

    double inflation =
        1.0 + std::max(0.0, config.gap_gain) * gap_excess * gap_excess +
        std::max(0.0, config.saturation_gain) *
            (acc_excess * acc_excess + gyr_excess * gyr_excess);
    if (!std::isfinite(inflation))
        inflation = config.max_inflation;
    return clamp(inflation, 1.0, std::max(1.0, config.max_inflation));
}

}  // namespace adaptive_factor
