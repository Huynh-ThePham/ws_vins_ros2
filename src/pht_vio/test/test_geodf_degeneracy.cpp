// Plan P1.7 / P0: GeoDF design-matrix conditioning is the normalized eight-point
// κ_eff = σ₁/σ₈, not Sampson median/MAD.

#include "featureTracker/geodf_degeneracy.h"
#include "test_support.h"

#include <Eigen/Core>

#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace gd = geodf_degeneracy;

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

    TEST_CASE("Degeneracy.CollinearCorrespondencesProduceIllConditioningOrInvalid");
    {
        std::vector<Eigen::Vector2d> cur, prev;
        std::vector<double> sampson;
        makeCollinearCorrespondences(30, cur, prev, sampson);
        const gd::DesignMatrixMetrics m =
            gd::computeDesignMatrixMetrics(cur, prev, sampson);
        // Either metrics refuse validity, or κ_eff is large relative to a good set.
        std::vector<Eigen::Vector2d> good_c, good_p;
        std::vector<double> good_s;
        makeTranslatingCorrespondences(30, 0.15, good_c, good_p, good_s);
        const gd::DesignMatrixMetrics good =
            gd::computeDesignMatrixMetrics(good_c, good_p, good_s);
        CHECK(good.valid);
        if (m.valid)
            CHECK(m.effective_design_condition > good.effective_design_condition);
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

    TEST_MAIN_RETURN();
}
