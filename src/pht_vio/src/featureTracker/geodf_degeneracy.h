#pragma once

// GeoDF fundamental-matrix degeneracy guard (plan P1.7).
//
// GeoDF only checked F.empty(). A fundamental matrix estimated during pure rotation,
// at low parallax, from features clustered in one image region, or from a frame the
// movers dominate, is not empty -- it is confidently wrong. Hard-rejecting static
// structure on that basis is exactly the failure mode the paper claims to avoid.
//
// When the geometry is degenerate:
//   - GeoDF must not hard-reject;
//   - GeoDF must not count as strong agreement evidence;
//   - only a diagnostic residual or a mild down-weight is justified.

#include <algorithm>
#include <cmath>
#include <vector>

namespace geodf_degeneracy
{

enum class Health
{
    Healthy = 0,   // hard rejection permitted
    Weak,          // down-weight only, not strong agreement evidence
    Degenerate,    // no GeoDF decision at all
};

inline const char *toString(Health health)
{
    switch (health)
    {
        case Health::Healthy:    return "healthy";
        case Health::Weak:       return "weak";
        case Health::Degenerate: return "degenerate";
    }
    return "unknown";
}

enum class Cause
{
    NONE = 0,
    NO_FUNDAMENTAL,
    LOW_GRID_OCCUPANCY,
    LOW_PARALLAX,
    ILL_CONDITIONED,
    FEW_INLIERS,
    LOW_INLIER_RATIO,
    MOVER_DOMINANT,
};

inline const char *toString(Cause cause)
{
    switch (cause)
    {
        case Cause::NONE:               return "NONE";
        case Cause::NO_FUNDAMENTAL:     return "NO_FUNDAMENTAL";
        case Cause::LOW_GRID_OCCUPANCY: return "LOW_GRID_OCCUPANCY";
        case Cause::LOW_PARALLAX:       return "LOW_PARALLAX";
        case Cause::ILL_CONDITIONED:    return "ILL_CONDITIONED";
        case Cause::FEW_INLIERS:        return "FEW_INLIERS";
        case Cause::LOW_INLIER_RATIO:   return "LOW_INLIER_RATIO";
        case Cause::MOVER_DOMINANT:     return "MOVER_DOMINANT";
    }
    return "UNKNOWN";
}

struct Config
{
    double min_grid_occupancy = 0.35;
    double min_median_parallax_px = 1.0;
    double max_design_condition_number = 1.0e6;
    int min_ransac_inliers = 20;
    double min_ransac_inlier_ratio = 0.35;
    // A frame where the flagged set dominates is more likely a bad F than a scene in
    // which most of the image really is moving.
    double max_mover_share = 0.60;
};

struct Observation
{
    bool fundamental_valid = false;
    // Fraction of a coarse image grid occupied by the correspondences used for F.
    double grid_occupancy = 0.0;
    double median_parallax_px = 0.0;
    // Condition number of the 8-point design matrix.
    double design_condition_number = 0.0;
    int ransac_inliers = 0;
    int ransac_total = 0;
    // Share of scored tracks flagged as epipolar outliers.
    double mover_share = 0.0;
};

struct Result
{
    Health health = Health::Healthy;
    Cause cause = Cause::NONE;
    // In [0, 1]: how well conditioned the geometry is. Feeds semantic/geometric
    // health in sem_policy.h rather than being re-derived there.
    double conditioning = 0.0;
    double inlier_ratio = 0.0;

    bool mayHardReject() const { return health == Health::Healthy; }
    bool mayCountAsStrongAgreement() const { return health == Health::Healthy; }
};

// Median without mutating the caller's data.
inline double median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    const double hi = values[mid];
    if (values.size() % 2 == 1)
        return hi;
    std::nth_element(values.begin(), values.begin() + mid - 1, values.end());
    return 0.5 * (hi + values[mid - 1]);
}

// Median absolute deviation, a robust spread estimate for the residual diagnostics.
inline double medianAbsoluteDeviation(const std::vector<double> &values)
{
    if (values.empty())
        return 0.0;
    const double m = median(values);
    std::vector<double> deviations;
    deviations.reserve(values.size());
    for (double v : values)
        deviations.push_back(std::abs(v - m));
    return median(std::move(deviations));
}

// Fraction of grid cells occupied, for a rows x cols grid over an image.
inline double gridOccupancy(const std::vector<std::pair<double, double>> &points,
                            double width, double height, int cols = 6, int rows = 4)
{
    if (points.empty() || width <= 0.0 || height <= 0.0 || cols <= 0 || rows <= 0)
        return 0.0;
    std::vector<char> occupied(static_cast<size_t>(cols) * rows, 0);
    for (const auto &p : points)
    {
        if (!std::isfinite(p.first) || !std::isfinite(p.second))
            continue;
        int cx = static_cast<int>(p.first / width * cols);
        int cy = static_cast<int>(p.second / height * rows);
        cx = std::min(cols - 1, std::max(0, cx));
        cy = std::min(rows - 1, std::max(0, cy));
        occupied[static_cast<size_t>(cy) * cols + cx] = 1;
    }
    int count = 0;
    for (char c : occupied)
        count += c ? 1 : 0;
    return static_cast<double>(count) / static_cast<double>(cols * rows);
}

inline Result evaluate(const Observation &obs, const Config &config)
{
    Result out;
    out.inlier_ratio = obs.ransac_total > 0
                           ? static_cast<double>(obs.ransac_inliers) / obs.ransac_total
                           : 0.0;

    if (!obs.fundamental_valid)
    {
        out.health = Health::Degenerate;
        out.cause = Cause::NO_FUNDAMENTAL;
        return out;
    }

    // Conditioning score, used by the health model. Each factor is 1 when comfortably
    // past its threshold and falls toward 0 as the geometry weakens.
    const double occupancy_score =
        config.min_grid_occupancy > 0.0
            ? std::min(1.0, obs.grid_occupancy / config.min_grid_occupancy)
            : 1.0;
    const double parallax_score =
        config.min_median_parallax_px > 0.0
            ? std::min(1.0, obs.median_parallax_px / config.min_median_parallax_px)
            : 1.0;
    const double condition_score =
        (config.max_design_condition_number > 0.0 && obs.design_condition_number > 0.0)
            ? std::min(1.0, config.max_design_condition_number / obs.design_condition_number)
            : 1.0;
    const double inlier_score =
        config.min_ransac_inlier_ratio > 0.0
            ? std::min(1.0, out.inlier_ratio / config.min_ransac_inlier_ratio)
            : 1.0;
    out.conditioning = std::min(std::min(occupancy_score, parallax_score),
                                std::min(condition_score, inlier_score));
    out.conditioning = std::min(1.0, std::max(0.0, out.conditioning));

    // Ordered by how badly each cause invalidates a GeoDF decision. Pure
    // rotation / low parallax comes first: it is the failure mode that most reliably
    // turns static structure into apparent motion.
    if (obs.median_parallax_px < config.min_median_parallax_px)
    {
        out.health = Health::Degenerate;
        out.cause = Cause::LOW_PARALLAX;
        return out;
    }
    if (obs.design_condition_number > config.max_design_condition_number &&
        config.max_design_condition_number > 0.0)
    {
        out.health = Health::Degenerate;
        out.cause = Cause::ILL_CONDITIONED;
        return out;
    }
    if (obs.grid_occupancy < config.min_grid_occupancy)
    {
        out.health = Health::Weak;
        out.cause = Cause::LOW_GRID_OCCUPANCY;
        return out;
    }
    if (obs.ransac_inliers < config.min_ransac_inliers)
    {
        out.health = Health::Weak;
        out.cause = Cause::FEW_INLIERS;
        return out;
    }
    if (out.inlier_ratio < config.min_ransac_inlier_ratio)
    {
        out.health = Health::Weak;
        out.cause = Cause::LOW_INLIER_RATIO;
        return out;
    }
    if (obs.mover_share > config.max_mover_share)
    {
        out.health = Health::Weak;
        out.cause = Cause::MOVER_DOMINANT;
        return out;
    }

    out.health = Health::Healthy;
    out.cause = Cause::NONE;
    return out;
}

}  // namespace geodf_degeneracy
