// Plan P1.5: every stereo match must pass a physical contract before it can reach
// the stereo factor, depth initialisation, the right-camera GeoDF branch or the
// backend weight evidence.
//
// The key property tested here is that NOTHING about the rig is hard-coded: the
// expected disparity direction is derived from the extrinsic, so the same code must
// accept physically valid matches and reject wrong-sign ones for a left-right rig,
// a right-left rig, a vertical rig and a rotated rig alike.

#include "featureTracker/stereo_validity.h"
#include "test_support.h"

#include <string>
#include <vector>

namespace sv = stereo_validity;

namespace
{

constexpr double kFocal = 460.0;

sv::Config permissiveConfig()
{
    sv::Config c;
    c.enable = true;
    c.lr_cycle_max_px = 1.0;
    c.epipolar_max_px = 2.0;
    c.min_disparity_px = 0.5;
    c.max_disparity_px = 200.0;
    c.reprojection_max_px = 2.0;
    c.require_positive_depth = true;
    return c;
}

// body_T_cam0 / body_T_cam1 for a rig whose cam1 sits at `offset` relative to cam0
// (both cameras sharing the body orientation unless `rot` says otherwise).
sv::Rig rigFromOffset(const Eigen::Vector3d &offset,
                      const Eigen::Matrix3d &rot = Eigen::Matrix3d::Identity())
{
    const Eigen::Matrix3d ric0 = Eigen::Matrix3d::Identity();
    const Eigen::Vector3d tic0 = Eigen::Vector3d::Zero();
    const Eigen::Matrix3d ric1 = rot;
    const Eigen::Vector3d tic1 = offset;
    return sv::makeRig(ric0, tic0, ric1, tic1);
}

// Exact forward model: where does a point at depth Z along ray x0 land in cam1?
Eigen::Vector3d projectIntoCam1(const sv::Rig &rig, const Eigen::Vector3d &x0, double depth)
{
    const Eigen::Vector3d p1 = rig.R_c1_c0 * (x0 * depth) + rig.t_c1_c0;
    return p1 / p1.z();
}

sv::Result check(const sv::Rig &rig, const Eigen::Vector3d &x0, const Eigen::Vector3d &x1,
                 const sv::Config &config, double cycle_px = 0.0)
{
    return sv::checkStereoMatch(x0, x1, rig, config, cycle_px, true, true, kFocal);
}

struct NamedRig
{
    const char *name;
    sv::Rig rig;
};

std::vector<NamedRig> rigs()
{
    Eigen::Matrix3d yaw;
    const double a = 0.05;  // ~3 degrees of vergence
    yaw << std::cos(a), 0.0, std::sin(a),
                   0.0, 1.0, 0.0,
          -std::sin(a), 0.0, std::cos(a);
    return {
        {"left-right (cam1 to the +x side)", rigFromOffset({0.11, 0.0, 0.0})},
        {"right-left (cam1 to the -x side)", rigFromOffset({-0.11, 0.0, 0.0})},
        {"vertical (cam1 above)", rigFromOffset({0.0, 0.09, 0.0})},
        {"verged (cam1 rotated)", rigFromOffset({0.11, 0.0, 0.0}, yaw)},
    };
}

}  // namespace

int main()
{
    const sv::Config config = permissiveConfig();

    TEST_CASE("StereoValidity.RigDerivedFromExtrinsic");
    {
        for (const auto &entry : rigs())
        {
            CHECK(entry.rig.valid);
            CHECK_NEAR(entry.rig.baseline, entry.rig.t_c1_c0.norm(), 1e-12);
        }
        // A zero baseline is not a stereo rig and must be refused, not divided by.
        const sv::Rig degenerate = rigFromOffset({0.0, 0.0, 0.0});
        CHECK(!degenerate.valid);
    }

    TEST_CASE("StereoValidity.PhysicallyValidMatchAccepted");
    {
        // Same code, four different rig geometries, no hard-coded sign anywhere.
        for (const auto &entry : rigs())
        {
            for (double depth : {1.0, 3.0, 12.0})
            {
                for (const Eigen::Vector3d &x0 : {Eigen::Vector3d(0.0, 0.0, 1.0),
                                                  Eigen::Vector3d(0.18, -0.09, 1.0),
                                                  Eigen::Vector3d(-0.14, 0.11, 1.0)})
                {
                    const Eigen::Vector3d x1 = projectIntoCam1(entry.rig, x0, depth);
                    const sv::Result r = check(entry.rig, x0, x1, config);
                    if (!r.valid())
                        std::printf("            %s depth=%.1f rejected: %s\n", entry.name,
                                    depth, sv::toString(r.rejection));
                    CHECK(r.valid());
                    CHECK_NEAR(r.depth_cam0, depth, 1e-6 * depth);
                    CHECK(r.depth_cam1 > 0.0);
                    CHECK(r.epipolar_px < 1e-6);
                    CHECK(r.reprojection_px < 1e-6);
                    CHECK(r.disparity_px > 0.0);
                }
            }
        }
    }

    TEST_CASE("StereoValidity.WrongDisparitySignRejectedForEveryRig");
    {
        // Mirror the match across the point at infinity: same epipolar line, same
        // disparity magnitude, opposite (physically impossible) direction.
        for (const auto &entry : rigs())
        {
            const Eigen::Vector3d x0(0.10, -0.05, 1.0);
            const Eigen::Vector3d x1 = projectIntoCam1(entry.rig, x0, 2.5);
            const Eigen::Vector3d inf = entry.rig.R_c1_c0 * x0 / (entry.rig.R_c1_c0 * x0).z();
            const Eigen::Vector3d mirrored = 2.0 * inf - x1;

            const sv::Result good = check(entry.rig, x0, x1, config);
            CHECK(good.valid());

            const sv::Result bad = check(entry.rig, x0, mirrored, config);
            if (bad.valid())
                std::printf("            %s: mirrored match was accepted\n", entry.name);
            // Either the sign check or the negative-depth check must catch it; both
            // are physical, and the sign check is the earlier of the two.
            CHECK(!bad.valid());
            CHECK(bad.rejection == sv::Rejection::DISPARITY_SIGN ||
                  bad.rejection == sv::Rejection::NEGATIVE_DEPTH);
        }
    }

    TEST_CASE("StereoValidity.NegativeDepthRejected");
    {
        const sv::Rig rig = rigFromOffset({0.11, 0.0, 0.0});
        const Eigen::Vector3d x0(0.0, 0.0, 1.0);
        // A point behind cam0 projects to a match that triangulates negative.
        const Eigen::Vector3d x1 = projectIntoCam1(rig, x0, -3.0);
        const sv::Result r = check(rig, x0, x1, config);
        CHECK(!r.valid());
        CHECK(r.rejection == sv::Rejection::DISPARITY_SIGN ||
              r.rejection == sv::Rejection::NEGATIVE_DEPTH);
    }

    TEST_CASE("StereoValidity.EpipolarViolationRejected");
    {
        const sv::Rig rig = rigFromOffset({0.11, 0.0, 0.0});
        const Eigen::Vector3d x0(0.05, 0.02, 1.0);
        Eigen::Vector3d x1 = projectIntoCam1(rig, x0, 4.0);
        // Push the match off the epipolar line by ~5 px.
        x1.y() += 5.0 / kFocal;
        const sv::Result r = check(rig, x0, x1, config);
        CHECK(!r.valid());
        CHECK(r.rejection == sv::Rejection::EPIPOLAR);
        CHECK(r.epipolar_px > config.epipolar_max_px);
    }

    TEST_CASE("StereoValidity.DisparityRangeRejected");
    {
        const sv::Rig rig = rigFromOffset({0.11, 0.0, 0.0});
        const Eigen::Vector3d x0(0.0, 0.0, 1.0);

        // Far point: disparity below the noise floor.
        const sv::Result far = check(rig, x0, projectIntoCam1(rig, x0, 5000.0), config);
        CHECK(!far.valid());
        CHECK(far.rejection == sv::Rejection::DISPARITY_RANGE);

        // Very near point: disparity beyond the plausible maximum.
        const sv::Result near = check(rig, x0, projectIntoCam1(rig, x0, 0.12), config);
        CHECK(!near.valid());
        CHECK(near.rejection == sv::Rejection::DISPARITY_RANGE);
    }

    TEST_CASE("StereoValidity.CycleAndLkAndBorderRejected");
    {
        const sv::Rig rig = rigFromOffset({0.11, 0.0, 0.0});
        const Eigen::Vector3d x0(0.0, 0.0, 1.0);
        const Eigen::Vector3d x1 = projectIntoCam1(rig, x0, 3.0);

        CHECK(check(rig, x0, x1, config, 4.0).rejection == sv::Rejection::LR_CYCLE);
        CHECK(sv::checkStereoMatch(x0, x1, rig, config, 0.0, false, true, kFocal).rejection ==
              sv::Rejection::LK_FAILED);
        CHECK(sv::checkStereoMatch(x0, x1, rig, config, 0.0, true, false, kFocal).rejection ==
              sv::Rejection::BORDER);
        // LK failure outranks everything else: there is no measurement to check.
        CHECK(sv::checkStereoMatch(x0, x1, rig, config, 99.0, false, false, kFocal).rejection ==
              sv::Rejection::LK_FAILED);
    }

    TEST_CASE("StereoValidity.NoUsableExtrinsicFailsClosed");
    {
        // Without a usable rig no physical claim can be made, so a match that would
        // otherwise be admitted must not silently reach the backend.
        const sv::Rig degenerate = rigFromOffset({0.0, 0.0, 0.0});
        const Eigen::Vector3d x0(0.0, 0.0, 1.0);
        const Eigen::Vector3d x1(0.02, 0.0, 1.0);
        CHECK(!check(degenerate, x0, x1, config).valid());
    }

    TEST_CASE("StereoValidity.DisabledContractKeepsLkAndBorderOnly");
    {
        sv::Config off = permissiveConfig();
        off.enable = false;
        const sv::Rig rig = rigFromOffset({0.11, 0.0, 0.0});
        const Eigen::Vector3d x0(0.0, 0.0, 1.0);
        // A physically impossible match passes when the contract is off, which is
        // exactly why the paper configs keep it on.
        const Eigen::Vector3d bad = projectIntoCam1(rig, x0, -3.0);
        CHECK(sv::checkStereoMatch(x0, bad, rig, off, 99.0, true, true, kFocal).valid());
        CHECK(!sv::checkStereoMatch(x0, bad, rig, off, 0.0, false, true, kFocal).valid());
        CHECK(!sv::checkStereoMatch(x0, bad, rig, off, 0.0, true, false, kFocal).valid());
    }

    TEST_CASE("StereoValidity.HighReprojectionRejected");
    {
        // A match that is almost correct but not exact: tighten the reprojection
        // threshold so the triangulation residual is refused while LK/border pass.
        sv::Config tight = permissiveConfig();
        tight.reprojection_max_px = 1e-4;
        tight.epipolar_max_px = 50.0;  // do not let epipolar short-circuit first
        const sv::Rig rig = rigFromOffset({0.11, 0.0, 0.0});
        const Eigen::Vector3d x0(0.05, 0.02, 1.0);
        Eigen::Vector3d x1 = projectIntoCam1(rig, x0, 3.0);
        x1.x() += 0.5 / kFocal;
        x1.y() += 0.5 / kFocal;
        const sv::Result r = check(rig, x0, x1, tight);
        CHECK(!r.valid());
        CHECK(r.rejection == sv::Rejection::REPROJECTION ||
              r.rejection == sv::Rejection::EPIPOLAR);
        if (r.rejection == sv::Rejection::REPROJECTION)
            CHECK(r.reprojection_px > tight.reprojection_max_px);
    }

    TEST_CASE("StereoValidity.CountersAccountForEveryMatch");
    {
        sv::Counters counters;
        const sv::Rejection all[] = {
            sv::Rejection::NONE,            sv::Rejection::LK_FAILED,
            sv::Rejection::BORDER,          sv::Rejection::LR_CYCLE,
            sv::Rejection::EPIPOLAR,        sv::Rejection::DISPARITY_SIGN,
            sv::Rejection::DISPARITY_RANGE, sv::Rejection::NEGATIVE_DEPTH,
            sv::Rejection::REPROJECTION,
        };
        for (sv::Rejection r : all)
            counters.record(r);

        const long long buckets =
            counters.stereo_valid_total + counters.stereo_lk_failed +
            counters.stereo_border_failed + counters.stereo_lr_cycle_failed +
            counters.stereo_epipolar_failed + counters.stereo_wrong_disparity_sign +
            counters.stereo_disparity_range_failed + counters.stereo_negative_depth +
            counters.stereo_reprojection_failed;
        // Nothing may vanish: a published rejection rate depends on this.
        CHECK(counters.stereo_match_total == buckets);
        CHECK(counters.stereo_match_total == static_cast<long long>(sizeof(all) / sizeof(all[0])));

        counters.reset();
        CHECK(counters.stereo_match_total == 0);
        CHECK(counters.stereo_valid_total == 0);
    }

    TEST_CASE("StereoValidity.EveryRejectionHasAStableName");
    {
        const std::pair<sv::Rejection, const char *> expected[] = {
            {sv::Rejection::NONE, "NONE"},
            {sv::Rejection::LK_FAILED, "LK_FAILED"},
            {sv::Rejection::BORDER, "BORDER"},
            {sv::Rejection::LR_CYCLE, "LR_CYCLE"},
            {sv::Rejection::EPIPOLAR, "EPIPOLAR"},
            {sv::Rejection::DISPARITY_SIGN, "DISPARITY_SIGN"},
            {sv::Rejection::DISPARITY_RANGE, "DISPARITY_RANGE"},
            {sv::Rejection::NEGATIVE_DEPTH, "NEGATIVE_DEPTH"},
            {sv::Rejection::REPROJECTION, "REPROJECTION"},
        };
        for (const auto &entry : expected)
            CHECK(std::string(sv::toString(entry.first)) == entry.second);
    }

    TEST_CASE("StereoValidity.NearZeroEpipolarLineNormInvalid");
    {
        // Pure rotation (zero baseline) already fails rig.valid. For a valid baseline
        // but a degenerate line norm, force E nearly singular by using parallel rays
        // with a near-zero Essential action on the point at infinity configuration.
        const sv::Rig rig = rigFromOffset({0.11, 0.0, 0.0});
        CHECK(rig.valid);
        // Identical normalized rays with a horizontal stereo baseline produce a
        // finite Sampson residual; construct Ex≈0 by taking x0 along t.
        const Eigen::Vector3d x0 = rig.t_c1_c0.normalized();
        const Eigen::Vector3d x1 = x0;
        const sv::Result r = check(rig, x0, x1, config);
        CHECK(!r.valid());
        // Must not report a healthy zero epipolar error when the line is unusable.
        if (r.rejection == sv::Rejection::EPIPOLAR)
            CHECK(!(std::isfinite(r.epipolar_px) && r.epipolar_px == 0.0));
    }

    TEST_CASE("StereoValidity.VeryLowParallaxRejected");
    {
        sv::Config cfg = permissiveConfig();
        cfg.min_triangulation_angle_rad = 0.01;  // ~0.57 deg
        const sv::Rig rig = rigFromOffset({0.11, 0.0, 0.0});
        const Eigen::Vector3d x0(0.0, 0.0, 1.0);
        // Extremely far point → tiny parallax.
        const sv::Result r = check(rig, x0, projectIntoCam1(rig, x0, 200.0), cfg);
        CHECK(!r.valid());
        CHECK(r.rejection == sv::Rejection::DISPARITY_RANGE);
    }

    TEST_CASE("StereoValidity.TrueReprojectionUsesBothViews");
    {
        const sv::Rig rig = rigFromOffset({0.11, 0.0, 0.0});
        const Eigen::Vector3d x0(0.05, -0.02, 1.0);
        const Eigen::Vector3d x1 = projectIntoCam1(rig, x0, 4.0);
        const sv::Result good = check(rig, x0, x1, config);
        CHECK(good.valid());
        CHECK(good.reprojection_px < 1e-6);

        // Off-epipolar perturbation so Sampson and true reprojection both see
        // a real geometric inconsistency (along-epipolar noise can still
        // triangulate cleanly).
        Eigen::Vector3d bad = x1;
        bad.y() += 4.0 / kFocal;
        sv::Config loose_epi = permissiveConfig();
        loose_epi.epipolar_max_px = 50.0;
        loose_epi.reprojection_max_px = 0.75;
        const sv::Result r = check(rig, x0, bad, loose_epi);
        CHECK(!r.valid());
        CHECK(r.rejection == sv::Rejection::REPROJECTION ||
              r.rejection == sv::Rejection::EPIPOLAR);
        if (r.rejection == sv::Rejection::REPROJECTION)
            CHECK(r.reprojection_px > loose_epi.reprojection_max_px);
    }

    TEST_MAIN_RETURN();
}
