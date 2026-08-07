// Plan P1.7 / P0: GeoDF design-matrix conditioning is the normalized eight-point
// κ_eff = σ₁/σ₈, not Sampson median/MAD. Also covers Sampson fail-closed (P0.2).

#include "featureTracker/geodf_degeneracy.h"
#include "featureTracker/geodf_sampson.h"
#include "test_support.h"

#include <Eigen/Core>

#include <cmath>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace gd = geodf_degeneracy;
namespace gs = geodf_sampson;

namespace
{

gd::Config config()
{
    gd::Config c;
    c.min_grid_occupancy = 0.35;
    c.min_median_parallax_px = 1.0;
    c.max_effective_design_condition = 1.0e6;
    c.max_design_condition_number = 1.0e6;
    c.min_ransac_inliers = 20;
    c.min_ransac_inlier_ratio = 0.35;
    c.max_mover_share = 0.60;
    c.min_nullspace_gap = 0.0;  // telemetry only by default
    // Unit tests inject kappa directly; production sets require_known_conditioning.
    c.require_known_conditioning = false;
    return c;
}

gd::Observation healthy()
{
    gd::Observation obs;
    obs.fundamental_valid = true;
    obs.grid_occupancy = 0.75;
    obs.median_parallax_px = 4.5;
    obs.effective_design_condition = 1.0e3;
    obs.nullspace_gap = 50.0;
    obs.nullspace_gap_available = true;
    obs.design_metrics_valid = true;
    obs.design_condition_number = 1.0e3;
    obs.sampson_median = 0.4;
    obs.sampson_mad = 0.2;
    obs.ransac_inliers = 90;
    obs.ransac_total = 110;
    obs.mover_share = 0.15;
    return obs;
}

// Synthetic translating camera: X' = X + t in normalized image coords (z=1 plane
// of a fronto-parallel cloud). Non-degenerate for the eight-point algorithm.
void makeTranslatingCorrespondences(int n, double tx,
                                    std::vector<Eigen::Vector2d> &cur,
                                    std::vector<Eigen::Vector2d> &prev,
                                    std::vector<double> &sampson)
{
    cur.clear();
    prev.clear();
    sampson.clear();
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> u(-0.4, 0.4);
    std::uniform_real_distribution<double> depth(2.0, 8.0);
    for (int i = 0; i < n; i++)
    {
        const double X = u(rng);
        const double Y = u(rng);
        const double Z = depth(rng);
        // cam0 observation
        cur.emplace_back(X / Z, Y / Z);
        // cam1 = cam0 translated by (tx, 0, 0) in world of the plane
        prev.emplace_back((X - tx) / Z, Y / Z);
        sampson.push_back(0.05);  // small equal residuals: must NOT become κ
    }
}

void makeCollinearCorrespondences(int n,
                                  std::vector<Eigen::Vector2d> &cur,
                                  std::vector<Eigen::Vector2d> &prev,
                                  std::vector<double> &sampson)
{
    cur.clear();
    prev.clear();
    sampson.clear();
    for (int i = 0; i < n; i++)
    {
        const double t = -0.4 + 0.8 * i / std::max(1, n - 1);
        cur.emplace_back(t, 0.01 * t);
        prev.emplace_back(t + 0.02, 0.01 * t);
        sampson.push_back(0.05);
    }
}

// Near-collinear with tiny transverse noise — still expected to be worse than
// a well-spread translating set (high κ_eff or invalid).
void makeNearCollinearCorrespondences(int n,
                                      std::vector<Eigen::Vector2d> &cur,
                                      std::vector<Eigen::Vector2d> &prev,
                                      std::vector<double> &sampson)
{
    cur.clear();
    prev.clear();
    sampson.clear();
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> noise(-1e-4, 1e-4);
    for (int i = 0; i < n; i++)
    {
        const double t = -0.4 + 0.8 * i / std::max(1, n - 1);
        cur.emplace_back(t + noise(rng), 0.005 * t + noise(rng));
        prev.emplace_back(t + 0.02 + noise(rng), 0.005 * t + noise(rng));
        sampson.push_back(0.05);
    }
}

// All points clustered in a tiny image patch — design matrix poorly conditioned.
void makeClusteredCorrespondences(int n,
                                  std::vector<Eigen::Vector2d> &cur,
                                  std::vector<Eigen::Vector2d> &prev,
                                  std::vector<double> &sampson)
{
    cur.clear();
    prev.clear();
    sampson.clear();
    std::mt19937 rng(13);
    std::uniform_real_distribution<double> u(-0.005, 0.005);
    for (int i = 0; i < n; i++)
    {
        cur.emplace_back(0.10 + u(rng), 0.05 + u(rng));
        prev.emplace_back(0.11 + u(rng), 0.05 + u(rng));
        sampson.push_back(0.05);
    }
}

}  // namespace

int main()
{
    const gd::Config cfg = config();

    TEST_CASE("Degeneracy.HealthyGeometryMayHardReject");
    {
        const gd::Result r = gd::evaluate(healthy(), cfg);
        CHECK(r.health == gd::Health::Healthy);
        CHECK(r.cause == gd::Cause::NONE);
        CHECK(r.mayHardReject());
        CHECK(r.mayCountAsStrongAgreement());
        CHECK_NEAR(r.conditioning, 1.0, 1e-12);
    }

    TEST_CASE("Degeneracy.MissingFundamentalIsDegenerate");
    {
        gd::Observation obs = healthy();
        obs.fundamental_valid = false;
        const gd::Result r = gd::evaluate(obs, cfg);
        CHECK(r.health == gd::Health::Degenerate);
        CHECK(r.cause == gd::Cause::NO_FUNDAMENTAL);
        CHECK(!r.mayHardReject());
        CHECK(r.conditioning == 0.0);
    }

    TEST_CASE("Degeneracy.PureRotationAndLowParallaxAreDegenerate");
    {
        gd::Observation obs = healthy();
        obs.median_parallax_px = 0.2;
        const gd::Result r = gd::evaluate(obs, cfg);
        CHECK(r.health == gd::Health::Degenerate);
        CHECK(r.cause == gd::Cause::LOW_PARALLAX);
        CHECK(!r.mayHardReject());
        CHECK(!r.mayCountAsStrongAgreement());
        CHECK(r.conditioning < 0.35);
    }

    TEST_CASE("Degeneracy.IllConditionedDesignMatrixIsDegenerate");
    {
        gd::Observation obs = healthy();
        obs.effective_design_condition = 5.0e8;
        obs.design_condition_number = 5.0e8;
        const gd::Result r = gd::evaluate(obs, cfg);
        CHECK(r.health == gd::Health::Degenerate);
        CHECK(r.cause == gd::Cause::ILL_CONDITIONED);
        CHECK(!r.mayHardReject());
    }

    TEST_CASE("Degeneracy.UnknownConditioningFailsClosedInPublication");
    {
        gd::Config pub = cfg;
        pub.require_known_conditioning = true;
        gd::Observation obs = healthy();
        obs.design_metrics_valid = false;
        const gd::Result r = gd::evaluate(obs, pub);
        CHECK(r.health == gd::Health::Degenerate);
        CHECK(r.cause == gd::Cause::UNKNOWN_CONDITIONING);
        CHECK(!r.mayHardReject());
    }

    TEST_CASE("Degeneracy.ClusteredFeaturesAreWeakNotTrusted");
    {
        gd::Observation obs = healthy();
        obs.grid_occupancy = 0.10;
        const gd::Result r = gd::evaluate(obs, cfg);
        CHECK(r.health == gd::Health::Weak);
        CHECK(r.cause == gd::Cause::LOW_GRID_OCCUPANCY);
        CHECK(!r.mayHardReject());
        CHECK(!r.mayCountAsStrongAgreement());
    }

    TEST_CASE("Degeneracy.WeakRansacSupportIsWeak");
    {
        gd::Observation few = healthy();
        few.ransac_inliers = 8;
        few.ransac_total = 20;
        const gd::Result r1 = gd::evaluate(few, cfg);
        CHECK(r1.health == gd::Health::Weak);
        CHECK(r1.cause == gd::Cause::FEW_INLIERS);

        gd::Observation ratio = healthy();
        ratio.ransac_inliers = 30;
        ratio.ransac_total = 200;
        const gd::Result r2 = gd::evaluate(ratio, cfg);
        CHECK(r2.health == gd::Health::Weak);
        CHECK(r2.cause == gd::Cause::LOW_INLIER_RATIO);
        CHECK_NEAR(r2.inlier_ratio, 0.15, 1e-12);
    }

    TEST_CASE("Degeneracy.MoverDominantFrameIsWeak");
    {
        gd::Observation obs = healthy();
        obs.mover_share = 0.85;
        const gd::Result r = gd::evaluate(obs, cfg);
        CHECK(r.health == gd::Health::Weak);
        CHECK(r.cause == gd::Cause::MOVER_DOMINANT);
        CHECK(!r.mayHardReject());
    }

    TEST_CASE("Degeneracy.NullspaceGapTelemetryByDefault");
    {
        // min_nullspace_gap=0: even missing/low gap must not gate Healthy.
        gd::Observation obs = healthy();
        obs.nullspace_gap_available = false;
        obs.nullspace_gap = 0.0;
        const gd::Result r = gd::evaluate(obs, cfg);
        CHECK(r.health == gd::Health::Healthy);
        CHECK(r.mayHardReject());
    }

    TEST_CASE("Degeneracy.NullspaceGapGateWhenConfigured");
    {
        gd::Config gated = cfg;
        gated.min_nullspace_gap = 10.0;
        gd::Observation low = healthy();
        low.nullspace_gap = 2.0;
        low.nullspace_gap_available = true;
        const gd::Result r_low = gd::evaluate(low, gated);
        CHECK(r_low.health == gd::Health::Degenerate);
        CHECK(r_low.cause == gd::Cause::ILL_CONDITIONED);

        gd::Observation missing = healthy();
        missing.nullspace_gap_available = false;
        const gd::Result r_miss = gd::evaluate(missing, gated);
        CHECK(r_miss.health == gd::Health::Degenerate);
        CHECK(r_miss.cause == gd::Cause::ILL_CONDITIONED);

        gd::Observation ok = healthy();
        ok.nullspace_gap = 50.0;
        ok.nullspace_gap_available = true;
        const gd::Result r_ok = gd::evaluate(ok, gated);
        CHECK(r_ok.health == gd::Health::Healthy);
    }

    TEST_CASE("Degeneracy.ConditioningIsMonotoneAndBounded");
    {
        double previous = -1.0;
        for (int i = 0; i <= 20; i++)
        {
            gd::Observation obs = healthy();
            obs.median_parallax_px = i * 0.1;
            const double c = gd::evaluate(obs, cfg).conditioning;
            CHECK(c >= previous - 1e-12);
            CHECK(c >= 0.0);
            CHECK(c <= 1.0);
            previous = c;
        }
    }

    TEST_CASE("Degeneracy.GridOccupancy");
    {
        std::vector<std::pair<double, double>> clustered;
        for (int i = 0; i < 50; i++)
            clustered.emplace_back(10.0 + i * 0.1, 10.0 + i * 0.1);
        CHECK_NEAR(gd::gridOccupancy(clustered, 752, 480, 6, 4), 1.0 / 24.0, 1e-12);

        std::vector<std::pair<double, double>> spread;
        for (int cy = 0; cy < 4; cy++)
            for (int cx = 0; cx < 6; cx++)
                spread.emplace_back((cx + 0.5) * 752.0 / 6.0, (cy + 0.5) * 480.0 / 4.0);
        CHECK_NEAR(gd::gridOccupancy(spread, 752, 480, 6, 4), 1.0, 1e-12);

        std::vector<std::pair<double, double>> nasty{
            {-100.0, -100.0}, {10000.0, 10000.0},
            {std::nan(""), 5.0}, {5.0, std::nan("")}};
        const double occ = gd::gridOccupancy(nasty, 752, 480, 6, 4);
        CHECK(occ >= 0.0);
        CHECK(occ <= 1.0);

        CHECK(gd::gridOccupancy({}, 752, 480) == 0.0);
        CHECK(gd::gridOccupancy(spread, 0.0, 480) == 0.0);
    }

    TEST_CASE("Degeneracy.MedianAndMad");
    {
        CHECK_NEAR(gd::median({3.0, 1.0, 2.0}), 2.0, 1e-12);
        CHECK_NEAR(gd::median({4.0, 1.0, 3.0, 2.0}), 2.5, 1e-12);
        CHECK(gd::median({}) == 0.0);
        CHECK_NEAR(gd::medianAbsoluteDeviation({1.0, 2.0, 3.0, 4.0, 5.0}), 1.0, 1e-12);
        CHECK(gd::medianAbsoluteDeviation({}) == 0.0);
    }

    TEST_CASE("Degeneracy.DesignMetricsFromGoodCorrespondences");
    {
        std::vector<Eigen::Vector2d> cur, prev;
        std::vector<double> sampson;
        makeTranslatingCorrespondences(40, 0.15, cur, prev, sampson);
        const gd::DesignMatrixMetrics m =
            gd::computeDesignMatrixMetrics(cur, prev, sampson);
        CHECK(m.valid);
        CHECK(m.correspondence_count == 40);
        CHECK(m.nullspace_gap_available);  // N>=9 => σ₉ exists
        CHECK(m.effective_design_condition >= 1.0);
        CHECK(std::isfinite(m.effective_design_condition));
        CHECK(std::isfinite(m.nullspace_gap));
        CHECK(m.sigma1 >= m.sigma8);
        CHECK(m.sigma8 >= m.sigma9 - 1e-12);
        // Equal Sampson residuals must NOT be reported as κ.
        CHECK_NEAR(m.sampson_median, 0.05, 1e-12);
        CHECK(m.sampson_mad < 1e-12);
        CHECK(m.effective_design_condition !=
              std::max(1.0, m.sampson_median / std::max(m.sampson_mad, 1e-9)));
    }

    // Exactly 8 finite pairs: thin SVD of 8×9 yields 8 singular values.
    // Expected: κ_eff = σ1/max(σ8,eps) finite and valid; nullspace_gap_available=false.
    TEST_CASE("Degeneracy.ExactlyEightPointsValidWithoutNullspaceGap");
    {
        std::vector<Eigen::Vector2d> cur, prev;
        std::vector<double> sampson;
        makeTranslatingCorrespondences(8, 0.15, cur, prev, sampson);
        const gd::DesignMatrixMetrics m =
            gd::computeDesignMatrixMetrics(cur, prev, sampson);
        CHECK(m.valid);
        CHECK(m.correspondence_count == 8);
        CHECK(!m.nullspace_gap_available);
        CHECK(std::isfinite(m.effective_design_condition));
        CHECK(m.effective_design_condition >= 1.0);
        CHECK(m.sigma9 == 0.0);
    }

    // N>=9: nullspace gap must be available when σ₉ is finite.
    TEST_CASE("Degeneracy.NinePlusPointsExposeNullspaceGap");
    {
        std::vector<Eigen::Vector2d> cur, prev;
        std::vector<double> sampson;
        makeTranslatingCorrespondences(9, 0.15, cur, prev, sampson);
        const gd::DesignMatrixMetrics m9 =
            gd::computeDesignMatrixMetrics(cur, prev, sampson);
        CHECK(m9.valid);
        CHECK(m9.correspondence_count == 9);
        CHECK(m9.nullspace_gap_available);
        CHECK(std::isfinite(m9.nullspace_gap));

        makeTranslatingCorrespondences(25, 0.12, cur, prev, sampson);
        const gd::DesignMatrixMetrics m25 =
            gd::computeDesignMatrixMetrics(cur, prev, sampson);
        CHECK(m25.valid);
        CHECK(m25.nullspace_gap_available);
    }

    // NaN at different indices in the two views must not misalign pairs.
    // Expected: drop only indices where either view is non-finite; remaining
    // pairs stay matched; metrics stay valid if >=8 finite pairs remain.
    TEST_CASE("Degeneracy.NaNFilteredAsPairsNotIndependently");
    {
        std::vector<Eigen::Vector2d> cur, prev;
        std::vector<double> sampson;
        makeTranslatingCorrespondences(20, 0.15, cur, prev, sampson);
        cur[2].x() = std::numeric_limits<double>::quiet_NaN();   // drop pair 2
        prev[5].y() = std::numeric_limits<double>::quiet_NaN();  // drop pair 5
        cur[7] = Eigen::Vector2d(std::numeric_limits<double>::infinity(), 0.0);
        // Independent per-view filtering would leave unequal sizes / wrong matches.
        const gd::DesignMatrixMetrics m =
            gd::computeDesignMatrixMetrics(cur, prev, sampson);
        CHECK(m.correspondence_count == 17);  // 20 - 3 dropped pairs
        CHECK(m.valid);
        CHECK(std::isfinite(m.effective_design_condition));

        // Fewer than 8 finite pairs => invalid.
        std::vector<Eigen::Vector2d> few_c, few_p;
        std::vector<double> few_s;
        makeTranslatingCorrespondences(10, 0.1, few_c, few_p, few_s);
        for (int i = 0; i < 3; i++)
            few_c[i].x() = std::numeric_limits<double>::quiet_NaN();
        // After pair filter: 7 pairs < 8
        const gd::DesignMatrixMetrics few =
            gd::computeDesignMatrixMetrics(few_c, few_p, few_s);
        CHECK(few.correspondence_count == 7);
        CHECK(!few.valid);
    }

    TEST_CASE("Degeneracy.EqualLargeSampsonDoesNotBecomeConditionNumber");
    {
        // The old bug: median/MAD of equal residuals collapses to a tiny ratio
        // floor, which was then treated as κ. Design metrics must come from A.
        std::vector<Eigen::Vector2d> cur, prev;
        std::vector<double> sampson(30, 8.0);  // large but identical
        makeTranslatingCorrespondences(30, 0.12, cur, prev, sampson);
        for (double &s : sampson)
            s = 8.0;
        const gd::DesignMatrixMetrics m =
            gd::computeDesignMatrixMetrics(cur, prev, sampson);
        CHECK(m.valid);
        CHECK_NEAR(m.sampson_median, 8.0, 1e-12);
        CHECK(m.sampson_mad < 1e-12);
        // κ_eff is a property of A, not of residual spread.
        CHECK(m.effective_design_condition > 1.0);
        CHECK(m.effective_design_condition < 1.0e12);
    }

    // Collinear / near-collinear: expect invalid metrics OR κ_eff worse than a
    // good translating set (ill-conditioned design matrix).
    TEST_CASE("Degeneracy.CollinearCorrespondencesProduceIllConditioningOrInvalid");
    {
        std::vector<Eigen::Vector2d> cur, prev;
        std::vector<double> sampson;
        makeCollinearCorrespondences(30, cur, prev, sampson);
        const gd::DesignMatrixMetrics m =
            gd::computeDesignMatrixMetrics(cur, prev, sampson);
        std::vector<Eigen::Vector2d> good_c, good_p;
        std::vector<double> good_s;
        makeTranslatingCorrespondences(30, 0.15, good_c, good_p, good_s);
        const gd::DesignMatrixMetrics good =
            gd::computeDesignMatrixMetrics(good_c, good_p, good_s);
        CHECK(good.valid);
        if (m.valid)
            CHECK(m.effective_design_condition > good.effective_design_condition);
    }

    TEST_CASE("Degeneracy.NearCollinearWorseThanTranslating");
    {
        std::vector<Eigen::Vector2d> cur, prev, good_c, good_p;
        std::vector<double> sampson, good_s;
        makeNearCollinearCorrespondences(30, cur, prev, sampson);
        makeTranslatingCorrespondences(30, 0.15, good_c, good_p, good_s);
        const gd::DesignMatrixMetrics m =
            gd::computeDesignMatrixMetrics(cur, prev, sampson);
        const gd::DesignMatrixMetrics good =
            gd::computeDesignMatrixMetrics(good_c, good_p, good_s);
        CHECK(good.valid);
        // Expected: near-collinear is invalid or has larger κ_eff.
        if (m.valid)
            CHECK(m.effective_design_condition > good.effective_design_condition);
    }

    // Clustered points: Hartley normalization removes absolute scale, so κ_eff
    // need not exceed a well-spread set. Expected failure mode is a collapsed
    // nullspace gap (σ8≈σ9 ⇒ ambiguous F), which is telemetry unless
    // Config.min_nullspace_gap > 0 gates it.
    TEST_CASE("Degeneracy.ClusteredCorrespondencesCollapseNullspaceGap");
    {
        std::vector<Eigen::Vector2d> cur, prev, good_c, good_p;
        std::vector<double> sampson, good_s;
        makeClusteredCorrespondences(30, cur, prev, sampson);
        makeTranslatingCorrespondences(30, 0.15, good_c, good_p, good_s);
        const gd::DesignMatrixMetrics m =
            gd::computeDesignMatrixMetrics(cur, prev, sampson);
        const gd::DesignMatrixMetrics good =
            gd::computeDesignMatrixMetrics(good_c, good_p, good_s);
        CHECK(good.valid);
        CHECK(good.nullspace_gap_available);
        CHECK(m.valid);
        CHECK(m.nullspace_gap_available);
        CHECK(m.nullspace_gap < good.nullspace_gap);
        // Clustered patch also fails the image-grid occupancy check used in evaluate().
        std::vector<std::pair<double, double>> pts;
        for (const auto &p : cur)
            pts.emplace_back(p.x() * 100.0 + 376.0, p.y() * 100.0 + 240.0);
        CHECK(gd::gridOccupancy(pts, 752, 480, 6, 4) < 0.35);
    }

    TEST_CASE("Degeneracy.InsufficientCorrespondencesAreInvalid");
    {
        std::vector<Eigen::Vector2d> cur = {Eigen::Vector2d(0, 0), Eigen::Vector2d(1, 0)};
        std::vector<Eigen::Vector2d> prev = {Eigen::Vector2d(0.1, 0), Eigen::Vector2d(1.1, 0)};
        std::vector<double> sampson = {0.1, 0.1};
        const gd::DesignMatrixMetrics m =
            gd::computeDesignMatrixMetrics(cur, prev, sampson);
        CHECK(!m.valid);
    }

    TEST_CASE("Degeneracy.NamesAreStable");
    {
        CHECK(std::string(gd::toString(gd::Health::Healthy)) == "healthy");
        CHECK(std::string(gd::toString(gd::Health::Weak)) == "weak");
        CHECK(std::string(gd::toString(gd::Health::Degenerate)) == "degenerate");
        const std::pair<gd::Cause, const char *> causes[] = {
            {gd::Cause::NONE, "NONE"},
            {gd::Cause::NO_FUNDAMENTAL, "NO_FUNDAMENTAL"},
            {gd::Cause::LOW_GRID_OCCUPANCY, "LOW_GRID_OCCUPANCY"},
            {gd::Cause::LOW_PARALLAX, "LOW_PARALLAX"},
            {gd::Cause::ILL_CONDITIONED, "ILL_CONDITIONED"},
            {gd::Cause::FEW_INLIERS, "FEW_INLIERS"},
            {gd::Cause::LOW_INLIER_RATIO, "LOW_INLIER_RATIO"},
            {gd::Cause::MOVER_DOMINANT, "MOVER_DOMINANT"},
            {gd::Cause::UNKNOWN_CONDITIONING, "UNKNOWN_CONDITIONING"},
        };
        for (const auto &entry : causes)
            CHECK(std::string(gd::toString(entry.first)) == entry.second);
    }

    // P0.2: near-zero Sampson denominator must be invalid (not S^2=0).
    TEST_CASE("Sampson.NearZeroDenomIsInvalidNotPerfectInlier");
    {
        // F = 0 => Fx = 0, F^T x' = 0 => denom = 0, num = 0.
        const gs::SampsonResult zero_F =
            gs::sampsonSquaredDistance(0, 0, 0, 0, 0, 0, 0, 0, 0, 1.0, 2.0, 3.0, 4.0);
        CHECK(!zero_F.valid);
        CHECK(!std::isfinite(zero_F.squared_distance));
        CHECK(zero_F.squared_distance == std::numeric_limits<double>::infinity());

        // Tiny F entries that still produce a near-zero denom.
        const gs::SampsonResult tiny =
            gs::sampsonSquaredDistance(1e-20, 0, 0, 0, 1e-20, 0, 0, 0, 0,
                                       0.5, 0.5, 0.5, 0.5);
        CHECK(!tiny.valid);
        CHECK(tiny.squared_distance == std::numeric_limits<double>::infinity());
    }

    TEST_CASE("Sampson.ValidInlierHasFiniteSquaredDistance");
    {
        // A simple nonzero F with points that yield a finite residual.
        // F ≈ [[0,0,0],[0,0,-1],[0,1,0]] (skew-symmetric translation-like).
        const gs::SampsonResult ok =
            gs::sampsonSquaredDistance(0, 0, 0, 0, 0, -1, 0, 1, 0,
                                       0.1, 0.2, 0.15, 0.25);
        CHECK(ok.valid);
        CHECK(std::isfinite(ok.squared_distance));
        CHECK(ok.squared_distance >= 0.0);
    }

    TEST_CASE("Sampson.NonFinitePointsAreInvalid");
    {
        const gs::SampsonResult bad =
            gs::sampsonSquaredDistance(0, 0, 0, 0, 0, -1, 0, 1, 0,
                                       std::numeric_limits<double>::quiet_NaN(),
                                       0.0, 0.1, 0.2);
        CHECK(!bad.valid);
        CHECK(bad.squared_distance == std::numeric_limits<double>::infinity());
    }

    TEST_MAIN_RETURN();
}
