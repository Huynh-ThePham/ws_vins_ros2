#include "factor/adaptive_factor_quality.h"
#include "featureTracker/temporal_smoothing.h"
#include "test_support.h"

#include <cmath>
#include <random>
#include <vector>

namespace
{

// Monte Carlo: whitened 2-D Gaussian signed components → MAD scale ≈ 1.
// Semantic √w must not change the scale estimate when the raw residual is fixed.
constexpr double kChi2_2_0p95 = 5.99146454710798;  // χ²_{2, 0.95}
constexpr double kSqrtChi2 = 2.447746849;           // √χ²_{2, 0.95}

adaptive_factor::AdaptiveHuberConfig makeCfg()
{
    adaptive_factor::AdaptiveHuberConfig cfg;
    cfg.enabled = true;
    cfg.ema = 1.0;  // no temporal smoothing in single-shot estimates
    cfg.min_samples = 100;
    cfg.min_delta = 0.1;
    cfg.max_delta = 10.0;
    cfg.k = 1.0;  // report σ directly
    return cfg;
}

std::vector<double> sampleSignedGaussian(std::mt19937 &rng, int n_pairs,
                                         double outlier_frac, double outlier_scale)
{
    std::normal_distribution<double> N(0.0, 1.0);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    std::vector<double> components;
    components.reserve(static_cast<size_t>(2 * n_pairs));
    for (int i = 0; i < n_pairs; ++i)
    {
        double x = N(rng);
        double y = N(rng);
        if (U(rng) < outlier_frac)
        {
            x *= outlier_scale;
            y *= outlier_scale;
        }
        components.push_back(x);
        components.push_back(y);
    }
    return components;
}

}  // namespace

int main()
{
    TEST_CASE("RobustScale.SignedGaussianComponentsNearUnbiased");
    {
        std::mt19937 rng(7);
        const auto components = sampleSignedGaussian(rng, 2000, 0.0, 1.0);
        const double sigma =
            adaptive_factor::adaptiveHuberDelta(components, 1.0, 1.0, makeCfg());
        CHECK(sigma > 0.85);
        CHECK(sigma < 1.15);
    }

    TEST_CASE("RobustScale.OutlierFractions5_10_20");
    {
        // MAD must stay near 1 under heavy-tailed contamination; |r|-folded
        // estimators with the wrong constant would be systematically off.
        for (double frac : {0.05, 0.10, 0.20})
        {
            std::mt19937 rng(static_cast<unsigned>(1000 + static_cast<int>(frac * 100)));
            const auto components = sampleSignedGaussian(rng, 4000, frac, 20.0);
            const double sigma =
                adaptive_factor::adaptiveHuberDelta(components, 1.0, 1.0, makeCfg());
            if (!(sigma > 0.7 && sigma < 1.4))
                std::printf("            outlier_frac=%.2f sigma=%.3f\n", frac, sigma);
            CHECK(sigma > 0.7);
            CHECK(sigma < 1.4);
        }
    }

    TEST_CASE("RobustScale.SemanticWeightDoesNotBiasScale");
    {
        // Same raw residual components; changing an external weight must not
        // alter the scale when the estimator only sees unweighted components.
        const std::vector<double> raw{
            -0.4, 0.5, -0.55, 0.6, 0.7, -0.45, 0.52, -0.48, 0.58, -0.62,
            0.51, -0.49, 0.53, -0.57, 0.54, -0.56, 0.5, -0.5, 0.5, -0.5,
            0.5, -0.5, 0.5, -0.5, 0.5, -0.5, 0.5, -0.5, 0.5, -0.5, 0.5, -0.5};
        adaptive_factor::AdaptiveHuberConfig cfg = makeCfg();
        cfg.min_samples = 20;
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

    TEST_CASE("RobustScale.FrameRateIndependentEma");
    {
        // Same physical time series at 10/20/30 Hz must converge to the same
        // continuous-time EMA when τ is derived from config.ema @ 20 Hz.
        adaptive_factor::AdaptiveHuberConfig cfg = makeCfg();
        cfg.ema = 0.10;
        cfg.k = 1.0;
        cfg.min_delta = 0.1;
        cfg.max_delta = 10.0;

        // Constant target σ≈2 from identical signed samples each update.
        std::mt19937 rng(42);
        std::normal_distribution<double> N(0.0, 2.0);
        std::vector<double> components;
        components.reserve(400);
        for (int i = 0; i < 200; ++i)
        {
            components.push_back(N(rng));
            components.push_back(N(rng));
        }

        auto run_hz = [&](double hz) {
            double delta = 1.0;
            const double dt = 1.0 / hz;
            // Simulate 2.0 seconds of updates with the same residual snapshot.
            const int steps = static_cast<int>(std::lround(2.0 * hz));
            for (int i = 0; i < steps; ++i)
                delta = adaptive_factor::adaptiveHuberDelta(components, delta, 1.0, cfg, dt);
            return delta;
        };

        const double d10 = run_hz(10.0);
        const double d20 = run_hz(20.0);
        const double d30 = run_hz(30.0);
        CHECK_NEAR(d10, d20, 0.05);
        CHECK_NEAR(d20, d30, 0.05);
        // Sanity: τ from frame alpha matches temporal_smoothing helper.
        const double tau = temporal_smooth::tauFromFrameAlpha(0.10, 20.0);
        CHECK(tau > 0.4);
        CHECK(tau < 0.6);
    }

    TEST_CASE("RobustScale.HuberThresholdMatchesChiSquare2DCoverage");
    {
        // For whitened 2-D residuals, a radial threshold √χ²_{2,p} has coverage p
        // under the Gaussian null. We only check that our default k·σ mapping is
        // in the same ballpark as √χ²_{2,0.95} when σ≈1.
        adaptive_factor::AdaptiveHuberConfig cfg = makeCfg();
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
            components.push_back(x);
            components.push_back(y);
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

    TEST_CASE("RobustScale.AbsFoldedSamplesAreNotRequired");
    {
        // Feeding |N(0,1)| with MAD-of-signed constant would bias high; the API
        // contract is signed samples. This guards against regressing to abs().
        std::mt19937 rng(99);
        std::normal_distribution<double> N(0.0, 1.0);
        std::vector<double> signed_c, folded;
        for (int i = 0; i < 3000; ++i)
        {
            const double v = N(rng);
            signed_c.push_back(v);
            folded.push_back(std::abs(v));
        }
        const double s_signed =
            adaptive_factor::adaptiveHuberDelta(signed_c, 1.0, 1.0, makeCfg());
        const double s_folded =
            adaptive_factor::adaptiveHuberDelta(folded, 1.0, 1.0, makeCfg());
        CHECK(s_signed > 0.85);
        CHECK(s_signed < 1.15);
        // Folded samples under the signed MAD formula underestimate σ.
        CHECK(s_folded < s_signed);
        CHECK(s_folded < 0.85);
    }

    TEST_MAIN_RETURN();
}
