// Plan P0.4 acceptance criteria: synthetic divergence must be flagged with the
// correct structured reason, not silently produce a plausible trajectory.
//
// The four cases the plan names explicitly -- NaN, bias explosion, empty feature
// set, pose jump -- each get a test, plus solver failure, severity ordering, and
// the recovery behaviour of the low-feature streak.

#include "estimator/failure_detection.h"
#include "test_support.h"

#include <limits>
#include <string>

namespace fd = failure_detection;

namespace
{

fd::Config strictConfig()
{
    fd::Config c;
    c.enable = true;
    c.max_acc_bias = 2.5;
    c.max_gyro_bias = 1.0;
    c.max_translation_step_m = 5.0;
    c.max_rotation_step_deg = 50.0;
    c.min_tracked_features = 10;
    c.max_consecutive_low_feature_frames = 5;
    return c;
}

// A frame that passes every check.
fd::Observation healthy()
{
    fd::Observation obs;
    obs.tracked_features = 120;
    obs.acc_bias_norm = 0.08;
    obs.gyro_bias_norm = 0.004;
    obs.translation_step_m = 0.03;
    obs.rotation_step_deg = 1.2;
    obs.solver_failed = false;
    obs.state_has_nan = false;
    return obs;
}

bool is(fd::FailureReason actual, fd::FailureReason expected)
{
    if (actual == expected)
        return true;
    std::printf("            got %s, expected %s\n", fd::toString(actual),
                fd::toString(expected));
    return false;
}

}  // namespace

int main()
{
    TEST_CASE("FailureDetection.HealthyStateIsNotAFailure");
    {
        fd::Detector detector(strictConfig());
        for (int i = 0; i < 100; i++)
            CHECK(is(detector.evaluate(healthy()), fd::FailureReason::NONE));
    }

    TEST_CASE("FailureDetection.NanState");
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();

        fd::Detector detector(strictConfig());
        fd::Observation obs = healthy();
        obs.state_has_nan = true;
        CHECK(is(detector.evaluate(obs), fd::FailureReason::NAN_STATE));

        // A NaN reaching the detector through a scalar rather than the flag must
        // still be caught: a NaN comparison is false, so a naive threshold test
        // would report the state as healthy.
        for (double *field : {&obs.acc_bias_norm, &obs.gyro_bias_norm,
                              &obs.translation_step_m, &obs.rotation_step_deg})
        {
            fd::Detector d(strictConfig());
            fd::Observation poisoned = healthy();
            double *target = field == &obs.acc_bias_norm      ? &poisoned.acc_bias_norm
                             : field == &obs.gyro_bias_norm   ? &poisoned.gyro_bias_norm
                             : field == &obs.translation_step_m ? &poisoned.translation_step_m
                                                                : &poisoned.rotation_step_deg;
            *target = nan;
            CHECK(is(d.evaluate(poisoned), fd::FailureReason::NAN_STATE));
            *target = inf;
            fd::Detector d2(strictConfig());
            CHECK(is(d2.evaluate(poisoned), fd::FailureReason::NAN_STATE));
        }
    }

    TEST_CASE("FailureDetection.BiasExplosion");
    {
        fd::Detector detector(strictConfig());
        fd::Observation acc = healthy();
        acc.acc_bias_norm = 3.1;
        CHECK(is(detector.evaluate(acc), fd::FailureReason::ACC_BIAS_DIVERGENCE));

        fd::Detector gyro_detector(strictConfig());
        fd::Observation gyro = healthy();
        gyro.gyro_bias_norm = 1.4;
        CHECK(is(gyro_detector.evaluate(gyro), fd::FailureReason::GYRO_BIAS_DIVERGENCE));

        // Exactly at the threshold is still healthy; the check is strictly greater.
        fd::Detector edge(strictConfig());
        fd::Observation at_limit = healthy();
        at_limit.acc_bias_norm = 2.5;
        at_limit.gyro_bias_norm = 1.0;
        CHECK(is(edge.evaluate(at_limit), fd::FailureReason::NONE));
    }

    TEST_CASE("FailureDetection.EmptyFeatureSet");
    {
        fd::Config config = strictConfig();
        fd::Detector detector(config);
        fd::Observation empty = healthy();
        empty.tracked_features = 0;

        // A short burst of sparse frames is not yet a failure.
        for (int i = 1; i < config.max_consecutive_low_feature_frames; i++)
        {
            CHECK(is(detector.evaluate(empty), fd::FailureReason::NONE));
            CHECK(detector.lowFeatureStreak() == i);
        }
        // The configured run length is.
        CHECK(is(detector.evaluate(empty), fd::FailureReason::LOW_FEATURE_COUNT));

        // One healthy frame clears the streak, so an intermittent dropout does not
        // accumulate into a false failure.
        fd::Detector intermittent(config);
        for (int cycle = 0; cycle < 20; cycle++)
        {
            for (int i = 0; i < config.max_consecutive_low_feature_frames - 1; i++)
                CHECK(is(intermittent.evaluate(empty), fd::FailureReason::NONE));
            CHECK(is(intermittent.evaluate(healthy()), fd::FailureReason::NONE));
            CHECK(intermittent.lowFeatureStreak() == 0);
        }
    }

    TEST_CASE("FailureDetection.PoseJump");
    {
        fd::Detector translation(strictConfig());
        fd::Observation jump = healthy();
        jump.translation_step_m = 7.5;
        CHECK(is(translation.evaluate(jump), fd::FailureReason::TRANSLATION_JUMP));

        fd::Detector rotation(strictConfig());
        fd::Observation spin = healthy();
        spin.rotation_step_deg = 91.0;
        CHECK(is(rotation.evaluate(spin), fd::FailureReason::ROTATION_JUMP));
    }

    TEST_CASE("FailureDetection.SolverFailure");
    {
        fd::Detector detector(strictConfig());
        fd::Observation obs = healthy();
        obs.solver_failed = true;
        CHECK(is(detector.evaluate(obs), fd::FailureReason::SOLVER_FAILURE));
    }

    TEST_CASE("FailureDetection.MostSevereReasonWins");
    {
        // With everything wrong at once, the reported reason must be the root
        // cause, not whichever check happens to run first.
        fd::Observation all_bad;
        all_bad.tracked_features = 0;
        all_bad.acc_bias_norm = 99.0;
        all_bad.gyro_bias_norm = 99.0;
        all_bad.translation_step_m = 99.0;
        all_bad.rotation_step_deg = 179.0;
        all_bad.solver_failed = true;
        all_bad.state_has_nan = true;

        fd::Detector d1(strictConfig());
        CHECK(is(d1.evaluate(all_bad), fd::FailureReason::NAN_STATE));

        all_bad.state_has_nan = false;
        fd::Detector d2(strictConfig());
        CHECK(is(d2.evaluate(all_bad), fd::FailureReason::SOLVER_FAILURE));

        all_bad.solver_failed = false;
        fd::Detector d3(strictConfig());
        CHECK(is(d3.evaluate(all_bad), fd::FailureReason::ACC_BIAS_DIVERGENCE));

        all_bad.acc_bias_norm = 0.1;
        fd::Detector d4(strictConfig());
        CHECK(is(d4.evaluate(all_bad), fd::FailureReason::GYRO_BIAS_DIVERGENCE));

        all_bad.gyro_bias_norm = 0.01;
        fd::Detector d5(strictConfig());
        CHECK(is(d5.evaluate(all_bad), fd::FailureReason::TRANSLATION_JUMP));

        all_bad.translation_step_m = 0.05;
        fd::Detector d6(strictConfig());
        CHECK(is(d6.evaluate(all_bad), fd::FailureReason::ROTATION_JUMP));
    }

    TEST_CASE("FailureDetection.DisabledDetectorReportsNothing");
    {
        // The escape hatch must be explicit, and it must not accumulate state.
        fd::Config off = strictConfig();
        off.enable = false;
        fd::Detector detector(off);
        fd::Observation all_bad = healthy();
        all_bad.state_has_nan = true;
        all_bad.tracked_features = 0;
        all_bad.acc_bias_norm = 99.0;
        for (int i = 0; i < 50; i++)
            CHECK(is(detector.evaluate(all_bad), fd::FailureReason::NONE));
        CHECK(detector.lowFeatureStreak() == 0);
    }

    TEST_CASE("FailureDetection.EveryReasonHasAStableName");
    {
        // The names go into failure_status.json and into published failure tables,
        // so they are part of the artifact contract.
        const std::pair<fd::FailureReason, const char *> expected[] = {
            {fd::FailureReason::NONE, "NONE"},
            {fd::FailureReason::LOW_FEATURE_COUNT, "LOW_FEATURE_COUNT"},
            {fd::FailureReason::ACC_BIAS_DIVERGENCE, "ACC_BIAS_DIVERGENCE"},
            {fd::FailureReason::GYRO_BIAS_DIVERGENCE, "GYRO_BIAS_DIVERGENCE"},
            {fd::FailureReason::TRANSLATION_JUMP, "TRANSLATION_JUMP"},
            {fd::FailureReason::ROTATION_JUMP, "ROTATION_JUMP"},
            {fd::FailureReason::SOLVER_FAILURE, "SOLVER_FAILURE"},
            {fd::FailureReason::NAN_STATE, "NAN_STATE"},
        };
        for (const auto &entry : expected)
            CHECK(std::string(fd::toString(entry.first)) == entry.second);
    }

    TEST_MAIN_RETURN();
}
