#include "factor/adaptive_factor_quality.h"
#include "test_support.h"

#include <cmath>
#include <random>
#include <vector>

namespace
{

// Monte Carlo: whitened 2-D Gaussian components → MAD scale ≈ 1.
// Semantic √w must not change the scale estimate when the raw residual is fixed.
constexpr double kChi2_2_0p95 = 5.99146454710798;  // χ²_{2, 0.95}
constexpr double kSqrtChi2 = 2.447746849;           // √χ²_{2, 0.95}

}  // namespace

int main()
{
    TEST_CASE("RobustScale.GaussianComponentsNearUnbiased");
    {
        std::mt19937 rng(7);
        std::normal_distribution<double> N(0.0, 1.0);
        std::vector<double> components;
        components.reserve(4000);
        for (int i = 0; i < 2000; ++i)
        {
            components.push_back(std::abs(N(rng)));
            components.push_back(std::abs(N(rng)));
        }

        adaptive_factor::AdaptiveHuberConfig cfg;
        cfg.enabled = true;
        cfg.ema = 1.0;
        cfg.min_samples = 100;
        cfg.min_delta = 0.1;
        cfg.max_delta = 10.0;
        cfg.k = 1.0;  // report σ directly
        const double sigma =
            adaptive_factor::adaptiveHuberDelta(components, 1.0, 1.0, cfg);
        CHECK(sigma > 0.85);
        CHECK(sigma < 1.15);
    }

    TEST_CASE("RobustScale.SemanticWeightDoesNotBiasScale");
    {
        // Same raw residual components; changing an external weight must not
        // alter the scale when the estimator only sees unweighted components.
        const std::vector<double> raw{0.4, 0.5, 0.55, 0.6, 0.7, 0.45, 0.52,
                                      0.48, 0.58, 0.62, 0.51, 0.49,
                                      0.53, 0.57, 0.54, 0.56, 0.5, 0.5,
                                      0.5, 0.5, 0.5, 0.5, 0.5, 0.5,
                                      0.5, 0.5, 0.5, 0.5, 0.5, 0.5,
                                      0.5, 0.5};
        adaptive_factor::AdaptiveHuberConfig cfg;
        cfg.enabled = true;
        cfg.ema = 1.0;
        cfg.min_samples = 20;
        cfg.min_delta = 0.1;
        cfg.max_delta = 10.0;
        cfg.k = 1.345;
        const double s1 =
            adaptive_factor::adaptiveHuberDelta(raw, 1.0, 1.0, cfg);
        // "Weighted" components would be √w * r; production must NOT feed those.
        std::vector<double> wrongly_weighted = raw;
        for (double &v : wrongly_weighted)
            v *= std::sqrt(0.25);
        const double s_wrong =
            adaptive_factor::adaptiveHuberDelta(wrongly_weighted, 1.0, 1.0, cfg);
        CHECK(s1 > s_wrong * 1.5);  // if weights leaked in, scale would shrink
        const double s2 =
            adaptive_factor::adaptiveHuberDelta(raw, 1.0, 1.0, cfg);
        CHECK_NEAR(s1, s2, 1e-12);
    }

    TEST_CASE("RobustScale.HuberThresholdMatchesChiSquare2DCoverage");
    {
        // For whitened 2-D residuals, a radial threshold √χ²_{2,p} has coverage p
        // under the Gaussian null. We only check that our default k·σ mapping is
        // in the same ballpark as √χ²_{2,0.95} when σ≈1.
        adaptive_factor::AdaptiveHuberConfig cfg;
        cfg.enabled = true;
        cfg.ema = 1.0;
        cfg.min_samples = 100;
        cfg.min_delta = 0.1;
        cfg.max_delta = 10.0;
        cfg.k = kSqrtChi2;  // set k so delta ≈ √χ² when σ=1

        std::mt19937 rng(11);
        std::normal_distribution<double> N(0.0, 1.0);
        std::vector<double> components;
        int inside = 0;
        const int n_pairs = 5000;
        for (int i = 0; i < n_pairs; ++i)
        {
            const double x = N(rng);
            const double y = N(rng);
            components.push_back(std::abs(x));
            components.push_back(std::abs(y));
            if (x * x + y * y <= kChi2_2_0p95)
                inside++;
        }
        const double coverage = static_cast<double>(inside) / n_pairs;
        CHECK(coverage > 0.93);
        CHECK(coverage < 0.97);

        const double delta =
            adaptive_factor::adaptiveHuberDelta(components, 1.0, 1.0, cfg);
        CHECK_NEAR(delta, kSqrtChi2, 0.25);
    }

    TEST_MAIN_RETURN();
}
