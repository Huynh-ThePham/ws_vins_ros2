// Plan P1.1: overlap must not fire on a coincidence between two tiny candidate sets.

#include "featureTracker/sem_policy.h"
#include "test_support.h"

#include <string>

namespace sp = sem_policy;

int main()
{
    TEST_CASE("Overlap.LegacyMaxFiresOnASingleSharedFeature");
    {
        // The defect this whole section exists for: two 2-element sets sharing one
        // feature scored 0.5 with directional max, which cleared a 0.35 threshold.
        sp::OverlapConfig legacy;
        legacy.metric = sp::OverlapMetric::DirectionalMax;
        legacy.min_sem_candidates = 0;
        legacy.min_geo_candidates = 0;
        legacy.min_intersection = 0;
        const sp::OverlapResult loose = sp::computeOverlap({2, 2, 1}, legacy);
        CHECK(loose.has_support);
        CHECK_NEAR(loose.raw, 0.5, 1e-12);
        CHECK(loose.value >= 0.35);

        // With minimum support required, the same coincidence carries no weight.
        sp::OverlapConfig guarded = legacy;
        guarded.min_sem_candidates = 4;
        guarded.min_geo_candidates = 4;
        guarded.min_intersection = 2;
        const sp::OverlapResult guarded_result = sp::computeOverlap({2, 2, 1}, guarded);
        CHECK(!guarded_result.has_support);
        CHECK_NEAR(guarded_result.value, 0.0, 1e-12);
    }

    TEST_CASE("Overlap.MetricFormulas");
    {
        sp::OverlapConfig config;
        config.min_sem_candidates = 0;
        config.min_geo_candidates = 0;
        config.min_intersection = 0;

        // |S| = 10, |G| = 4, |I| = 4: G is a subset of S.
        const sp::OverlapCounts counts{10, 4, 4};

        config.metric = sp::OverlapMetric::DirectionalMax;
        CHECK_NEAR(sp::computeOverlap(counts, config).raw, 1.0, 1e-12);       // 4/4

        config.metric = sp::OverlapMetric::DirectionalMin;
        CHECK_NEAR(sp::computeOverlap(counts, config).raw, 0.4, 1e-12);       // 4/10

        config.metric = sp::OverlapMetric::Dice;
        CHECK_NEAR(sp::computeOverlap(counts, config).raw, 2.0 * 4 / 14, 1e-12);

        config.metric = sp::OverlapMetric::Jaccard;
        CHECK_NEAR(sp::computeOverlap(counts, config).raw, 4.0 / 10.0, 1e-12);
    }

    TEST_CASE("Overlap.DiceIsSymmetricAndPenalizesSizeMismatch");
    {
        sp::OverlapConfig config;
        config.metric = sp::OverlapMetric::Dice;
        config.min_sem_candidates = 0;
        config.min_geo_candidates = 0;
        config.min_intersection = 0;

        // Symmetric: swapping the two sets must not change the score. Directional max
        // is not symmetric, which is why it could be gamed by the smaller set.
        CHECK_NEAR(sp::computeOverlap({10, 4, 4}, config).raw,
                   sp::computeOverlap({4, 10, 4}, config).raw, 1e-12);

        // A subset relation scores lower than a genuine mutual match.
        CHECK(sp::computeOverlap({10, 4, 4}, config).raw <
              sp::computeOverlap({6, 6, 5}, config).raw);

        // Identical sets score exactly 1.
        CHECK_NEAR(sp::computeOverlap({8, 8, 8}, config).raw, 1.0, 1e-12);
        // Disjoint sets score exactly 0.
        CHECK_NEAR(sp::computeOverlap({8, 8, 0}, config).raw, 0.0, 1e-12);
    }

    TEST_CASE("Overlap.SupportConfidenceScalesSmallIntersections");
    {
        sp::OverlapConfig config;
        config.metric = sp::OverlapMetric::DiceSupport;
        config.min_sem_candidates = 4;
        config.min_geo_candidates = 4;
        config.min_intersection = 2;
        config.support_saturation = 8;

        // Small but qualifying intersection: raw Dice is high, confidence is not.
        const sp::OverlapResult small = sp::computeOverlap({4, 4, 2}, config);
        CHECK(small.has_support);
        CHECK_NEAR(small.raw, 0.5, 1e-12);
        CHECK_NEAR(small.support, 0.25, 1e-12);
        CHECK_NEAR(small.value, 0.125, 1e-12);

        // Large intersection: confidence saturates and the value equals raw Dice.
        const sp::OverlapResult large = sp::computeOverlap({10, 10, 8}, config);
        CHECK_NEAR(large.support, 1.0, 1e-12);
        CHECK_NEAR(large.value, large.raw, 1e-12);
        CHECK(large.value > small.value);
    }

    TEST_CASE("Overlap.MonotoneInIntersection");
    {
        for (sp::OverlapMetric metric : {sp::OverlapMetric::DirectionalMax,
                                         sp::OverlapMetric::DirectionalMin,
                                         sp::OverlapMetric::Dice,
                                         sp::OverlapMetric::Jaccard,
                                         sp::OverlapMetric::DiceSupport})
        {
            sp::OverlapConfig config;
            config.metric = metric;
            config.min_sem_candidates = 0;
            config.min_geo_candidates = 0;
            config.min_intersection = 0;
            double previous = -1.0;
            for (int i = 0; i <= 10; i++)
            {
                const double value = sp::computeOverlap({10, 10, i}, config).value;
                CHECK(value >= previous - 1e-12);
                CHECK(value >= 0.0);
                CHECK(value <= 1.0);
                previous = value;
            }
        }
    }

    TEST_CASE("Overlap.EmptySetsAndDegenerateInput");
    {
        sp::OverlapConfig config;
        config.min_sem_candidates = 0;
        config.min_geo_candidates = 0;
        config.min_intersection = 0;
        // No division by zero, no NaN reaching a threshold comparison.
        for (const sp::OverlapCounts &counts :
             {sp::OverlapCounts{0, 0, 0}, sp::OverlapCounts{0, 5, 0},
              sp::OverlapCounts{5, 0, 0}, sp::OverlapCounts{-1, -1, -1}})
        {
            const sp::OverlapResult r = sp::computeOverlap(counts, config);
            CHECK(r.raw == 0.0);
            CHECK(r.value == 0.0);
        }
    }

    TEST_CASE("Overlap.MetricNamesRoundTrip");
    {
        for (sp::OverlapMetric metric : {sp::OverlapMetric::DirectionalMax,
                                         sp::OverlapMetric::DirectionalMin,
                                         sp::OverlapMetric::Dice,
                                         sp::OverlapMetric::Jaccard,
                                         sp::OverlapMetric::DiceSupport})
        {
            bool ok = false;
            CHECK(sp::parseOverlapMetric(sp::toString(metric), &ok) == metric);
            CHECK(ok);
        }
        // An unknown name must be reported, not silently accepted as the default.
        bool ok = true;
        CHECK(sp::parseOverlapMetric("not_a_metric", &ok) == sp::OverlapMetric::Dice);
        CHECK(!ok);
        // Aliases used in configs.
        CHECK(sp::parseOverlapMetric("max") == sp::OverlapMetric::DirectionalMax);
        CHECK(sp::parseOverlapMetric("min") == sp::OverlapMetric::DirectionalMin);
    }

    TEST_MAIN_RETURN();
}
