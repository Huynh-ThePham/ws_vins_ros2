#include "featureTracker/temporal_smoothing.h"
#include "test_support.h"

#include <cmath>
#include <vector>

namespace
{

double analyticalStep(double t, double step_at, double tau)
{
    if (t < step_at)
        return 0.0;
    return 1.0 - std::exp(-(t - step_at) / tau);
}

// Drive a continuous-time EMA with a step risk at a fixed frame rate.
std::vector<double> sampleEma(double hz, double tau_a, double tau_r,
                              double t_end, double step_at)
{
    std::vector<double> out;
    const double dt = 1.0 / hz;
    double ema = 0.0;
    for (double t = 0.0; t <= t_end + 1e-12; t += dt)
    {
        const double sample = (t + 1e-12 >= step_at) ? 1.0 : 0.0;
        ema = temporal_smooth::emaUpdate(ema, sample, dt, tau_a, tau_r);
        out.push_back(ema);
    }
    return out;
}

double valueAt(const std::vector<double> &series, double hz, double t)
{
    const int idx = static_cast<int>(std::llround(t * hz));
    CHECK(idx >= 0);
    CHECK(idx < static_cast<int>(series.size()));
    return series[static_cast<size_t>(idx)];
}

}  // namespace

int main()
{
    TEST_CASE("TemporalSmooth.AlphaFromDtMatchesContinuousFormula");
    {
        const double dt = 0.05;
        const double tau = 0.3;
        const double a = temporal_smooth::alphaFromDt(dt, tau);
        CHECK_NEAR(a, 1.0 - std::exp(-dt / tau), 1e-12);
    }

    TEST_CASE("TemporalSmooth.RatesTrackAnalyticalSolution");
    {
        // Discrete EMAs at 10/20/30 Hz must stay near the continuous solution of
        // the same wall-clock step. Early samples have larger discretization
        // error; later samples must converge across rates.
        const double tau = 0.25;  // attack == release for a clean comparison
        const double t_end = 2.0;
        const double step_at = 0.4;
        for (double hz : {10.0, 20.0, 30.0})
        {
            const auto series = sampleEma(hz, tau, tau, t_end, step_at);
            for (double t : {0.8, 1.2, 1.8})
            {
                const double got = valueAt(series, hz, t);
                const double expect = analyticalStep(t, step_at, tau);
                CHECK_NEAR(got, expect, 0.08);
            }
        }
        // Cross-rate agreement once the step has aged.
        const auto s10 = sampleEma(10.0, tau, tau, t_end, step_at);
        const auto s20 = sampleEma(20.0, tau, tau, t_end, step_at);
        const auto s30 = sampleEma(30.0, tau, tau, t_end, step_at);
        for (double t : {1.0, 1.5, 2.0})
        {
            CHECK_NEAR(valueAt(s10, 10.0, t), valueAt(s20, 20.0, t), 0.05);
            CHECK_NEAR(valueAt(s20, 20.0, t), valueAt(s30, 30.0, t), 0.05);
        }
    }

    TEST_CASE("TemporalSmooth.DroppedFramesStillTrackWallClock");
    {
        const double tau_a = 0.25;
        const double tau_r = 0.25;
        double ema = 0.0;
        double t = 0.0;
        const auto advance = [&](double dt, double sample) {
            ema = temporal_smooth::emaUpdate(ema, sample, dt, tau_a, tau_r);
            t += dt;
        };
        while (t + 1e-12 < 0.4)
            advance(0.05, 0.0);
        advance(0.15, 1.0);  // dropped frames → larger Δt
        while (t + 1e-12 < 1.0)
            advance(0.05, 1.0);

        const double expect = analyticalStep(1.0, 0.4, tau_a);
        CHECK_NEAR(ema, expect, 0.10);
    }

    TEST_CASE("TemporalSmooth.AttackFasterThanRelease");
    {
        const double dt = 0.05;
        const double up = temporal_smooth::emaUpdate(0.0, 1.0, dt, 0.1, 0.5);
        const double down = temporal_smooth::emaUpdate(1.0, 0.0, dt, 0.1, 0.5);
        CHECK(up > (1.0 - down));  // rises more in one step than it falls
    }

    TEST_MAIN_RETURN();
}
