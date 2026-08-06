#pragma once

// Continuous-time exponential smoothing for policy signals.
//
// Frame-indexed EMA with a fixed alpha is frame-rate dependent: the same physical
// risk sequence sampled at 10 Hz vs 30 Hz reaches different steady states. Use
//
//   α(Δt) = 1 - exp(-Δt / τ)
//
// with separate attack (rising) and release (falling) time constants so that
// risk can rise quickly and decay slowly — independent of the camera rate.

#include <cmath>

namespace temporal_smooth
{

inline double clamp01(double v)
{
    if (v < 0.0)
        return 0.0;
    if (v > 1.0)
        return 1.0;
    return v;
}

// Convert a legacy per-frame alpha defined at a reference rate into a time
// constant τ such that α_ref ≈ 1 - exp(-dt_ref / τ).
inline double tauFromFrameAlpha(double alpha_per_frame, double reference_hz = 20.0)
{
    const double a = clamp01(alpha_per_frame);
    if (a <= 0.0)
        return 1e9;
    if (a >= 1.0)
        return 1e-9;
    const double dt_ref = 1.0 / std::max(1e-6, reference_hz);
    return -dt_ref / std::log(1.0 - a);
}

inline double alphaFromDt(double dt_s, double tau_s)
{
    if (!(dt_s > 0.0) || !std::isfinite(dt_s))
        return 1.0;  // unknown timing: take the new sample (fail-open to fresh data)
    if (!(tau_s > 0.0) || !std::isfinite(tau_s))
        return 1.0;
    const double a = 1.0 - std::exp(-dt_s / tau_s);
    if (!std::isfinite(a))
        return 1.0;
    return clamp01(a);
}

// Asymmetric continuous-time EMA. Attack is used when the sample rises;
// release when it falls.
inline double emaUpdate(double previous, double sample, double dt_s,
                        double tau_attack_s, double tau_release_s)
{
    if (!std::isfinite(sample))
        return previous;
    if (!std::isfinite(previous) || previous < 0.0)
        return sample;
    const double tau = (sample >= previous) ? tau_attack_s : tau_release_s;
    const double a = alphaFromDt(dt_s, tau);
    return a * sample + (1.0 - a) * previous;
}

}  // namespace temporal_smooth
