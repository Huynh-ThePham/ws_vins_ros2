// Plan P1.7: GeoDF only checked F.empty(). A fundamental matrix estimated during
// pure rotation, at low parallax, from clustered features, or from a mover-dominated
// frame is not empty -- it is confidently wrong. Hard-rejecting static structure on
// that basis is the failure mode the paper claims to avoid.

#include "featureTracker/geodf_degeneracy.h"
#include "test_support.h"

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
    c.max_design_condition_number = 1.0e6;
    c.min_ransac_inliers = 20;
    c.min_ransac_inlier_ratio = 0.35;
    c.max_mover_share = 0.60;
    return c;
}

gd::Observation healthy()
{
    gd::Observation obs;
    obs.fundamental_valid = true;
    obs.grid_occupancy = 0.75;
    obs.median_parallax_px = 4.5;
    obs.design_condition_number = 1.0e3;
    obs.ransac_inliers = 90;
    obs.ransac_total = 110;
    obs.mover_share = 0.15;
    return obs;
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
        // The most important case: with no translation the epipolar geometry is
        // unidentifiable, so every static feature can look like a mover.
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
        obs.design_condition_number = 5.0e8;
        const gd::Result r = gd::evaluate(obs, cfg);
        CHECK(r.health == gd::Health::Degenerate);
        CHECK(r.cause == gd::Cause::ILL_CONDITIONED);
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
        ratio.ransac_total = 200;   // 0.15 inlier ratio
        const gd::Result r2 = gd::evaluate(ratio, cfg);
        CHECK(r2.health == gd::Health::Weak);
        CHECK(r2.cause == gd::Cause::LOW_INLIER_RATIO);
        CHECK_NEAR(r2.inlier_ratio, 0.15, 1e-12);
    }

    TEST_CASE("Degeneracy.MoverDominantFrameIsWeak");
    {
        // If most of the frame is flagged, a wrong F is a likelier explanation than a
        // scene in which almost everything genuinely moves.
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
        // All points in one cell.
        std::vector<std::pair<double, double>> clustered;
        for (int i = 0; i < 50; i++)
            clustered.emplace_back(10.0 + i * 0.1, 10.0 + i * 0.1);
        CHECK_NEAR(gd::gridOccupancy(clustered, 752, 480, 6, 4), 1.0 / 24.0, 1e-12);

        // One point per cell of a 6x4 grid.
        std::vector<std::pair<double, double>> spread;
        for (int cy = 0; cy < 4; cy++)
            for (int cx = 0; cx < 6; cx++)
                spread.emplace_back((cx + 0.5) * 752.0 / 6.0, (cy + 0.5) * 480.0 / 4.0);
        CHECK_NEAR(gd::gridOccupancy(spread, 752, 480, 6, 4), 1.0, 1e-12);

        // Out-of-range and non-finite input must not index out of bounds.
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
        // MAD of a symmetric set around 3 with deviations {2,1,0,1,2} -> 1.
        CHECK_NEAR(gd::medianAbsoluteDeviation({1.0, 2.0, 3.0, 4.0, 5.0}), 1.0, 1e-12);
        CHECK(gd::medianAbsoluteDeviation({}) == 0.0);
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
        };
        for (const auto &entry : causes)
            CHECK(std::string(gd::toString(entry.first)) == entry.second);
    }

    TEST_MAIN_RETURN();
}
