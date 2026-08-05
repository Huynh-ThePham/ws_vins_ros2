// Plan P1.2: the hold must be a duration in seconds driven by sensor timestamps,
// not a frame count. sem_policy_hold_frames: 180 meant 18 s at 10 Hz, 6 s at 30 Hz,
// and something else again whenever frames were dropped.

#include "featureTracker/sem_policy.h"
#include "test_support.h"

#include <string>
#include <vector>

namespace sp = sem_policy;

namespace
{

sp::PolicyConfig config()
{
    sp::PolicyConfig c;
    c.assist_hold_s = 1.5;
    c.strong_hold_s = 1.0;
    c.min_state_dwell_s = 0.3;
    return c;
}

sp::PolicyInput quiet(double t)
{
    sp::PolicyInput in;
    in.timestamp_s = t;
    return in;
}

sp::PolicyInput burst(double t)
{
    sp::PolicyInput in = quiet(t);
    in.semantic_burst = true;
    in.semantic_scene_active = true;
    return in;
}

sp::PolicyInput strong(double t)
{
    sp::PolicyInput in = quiet(t);
    in.semantic_strong = true;
    in.semantic_scene_active = true;
    return in;
}

// How long the assist hold survives after a single trigger, at a given frame rate.
double holdDurationAtRate(double rate_hz)
{
    sp::PolicyFsm fsm(config());
    const double dt = 1.0 / rate_hz;
    double t = 0.0;
    fsm.update(burst(t));
    double last_active_t = t;
    for (int i = 0; i < 2000; i++)
    {
        t += dt;
        const sp::PolicyOutput out = fsm.update(quiet(t));
        if (!out.assist_hold_active)
            break;
        last_active_t = t;
    }
    return last_active_t;
}

}  // namespace

int main()
{
    TEST_CASE("PolicyFsm.HoldDurationIsIndependentOfFrameRate");
    {
        // This is the whole point of P1.2. The hold must last ~assist_hold_s at every
        // rate; a frame counter would scale inversely with the rate instead.
        const double at_10hz = holdDurationAtRate(10.0);
        const double at_30hz = holdDurationAtRate(30.0);
        const double at_5hz = holdDurationAtRate(5.0);
        for (double d : {at_5hz, at_10hz, at_30hz})
        {
            CHECK(d > 1.5 - 1.0 / 5.0 - 1e-9);  // within one frame period of the target
            CHECK(d <= 1.5 + 1e-9);
        }
        // And they must agree with each other to within a frame period, not by a
        // factor of 3 the way a frame count would.
        CHECK(std::abs(at_10hz - at_30hz) < 0.2);
        CHECK(std::abs(at_5hz - at_30hz) < 0.25);
    }

    TEST_CASE("PolicyFsm.DroppedFramesDoNotShortenTheHold");
    {
        // A frame-count hold expires early when frames are dropped. A duration does
        // not: only elapsed sensor time matters.
        sp::PolicyFsm fsm(config());
        fsm.update(burst(0.0));
        // Two frames covering 1.4 s, as if almost everything in between was dropped.
        CHECK(fsm.update(quiet(0.7)).assist_hold_active);
        CHECK(fsm.update(quiet(1.4)).assist_hold_active);
        CHECK(!fsm.update(quiet(1.6)).assist_hold_active);
    }

    TEST_CASE("PolicyFsm.EscalatesImmediatelyAndDecaysThroughAssist");
    {
        sp::PolicyFsm fsm(config());
        CHECK(fsm.update(quiet(0.0)).state == sp::PolicyState::StaticSafe);

        // Escalation is never delayed by the dwell timer.
        CHECK(fsm.update(strong(0.1)).state == sp::PolicyState::StrongDynamic);

        // strong_hold_s = 1.0 keeps strong armed, then assist_hold_s = 1.5 (armed at
        // the same trigger) keeps assist alive a little longer, then static-safe.
        CHECK(fsm.update(quiet(0.9)).state == sp::PolicyState::StrongDynamic);
        const sp::PolicyOutput assist = fsm.update(quiet(1.3));
        CHECK(assist.state == sp::PolicyState::DynamicAssist);
        CHECK(assist.assist_hold_active);
        CHECK(!assist.strong_hold_active);
        CHECK(fsm.update(quiet(2.0)).state == sp::PolicyState::StaticSafe);
    }

    TEST_CASE("PolicyFsm.MinimumDwellBlocksOnlyDowngrades");
    {
        sp::PolicyConfig c = config();
        c.assist_hold_s = 0.05;
        c.strong_hold_s = 0.05;
        c.min_state_dwell_s = 0.5;
        sp::PolicyFsm fsm(c);

        fsm.update(strong(0.0));
        CHECK(fsm.state() == sp::PolicyState::StrongDynamic);

        // Holds have expired, but the dwell time has not: the state is held.
        const sp::PolicyOutput held = fsm.update(quiet(0.2));
        CHECK(held.state == sp::PolicyState::StrongDynamic);
        CHECK(held.dwell_blocked_downgrade);

        // Past the dwell time it may fall back.
        const sp::PolicyOutput released = fsm.update(quiet(0.6));
        CHECK(released.state == sp::PolicyState::StaticSafe);
        CHECK(!released.dwell_blocked_downgrade);
    }

    TEST_CASE("PolicyFsm.OverlapAgreementArmsStrongDirectly");
    {
        sp::PolicyFsm fsm(config());
        sp::PolicyInput in = quiet(0.0);
        in.overlap_agreement = true;
        in.semantic_scene_active = true;
        const sp::PolicyOutput out = fsm.update(in);
        CHECK(out.state == sp::PolicyState::StrongDynamic);
        CHECK(out.hard_reject_armed);
    }

    TEST_CASE("PolicyFsm.AssistPlusGeoEvidenceReachesStrong");
    {
        sp::PolicyFsm fsm(config());
        fsm.update(burst(0.0));  // burst alone -> assist
        sp::PolicyInput in = quiet(0.5);
        in.geo_evidence = true;
        in.semantic_scene_active = true;
        CHECK(fsm.update(in).state == sp::PolicyState::StrongDynamic);
    }

    TEST_CASE("PolicyFsm.StaticSafeSuppressesHardRejectButAllowsSoftMask");
    {
        sp::PolicyFsm fsm(config());
        sp::PolicyInput in = quiet(0.0);
        in.semantic_scene_active = true;   // scene gate open, no dynamic trigger
        const sp::PolicyOutput out = fsm.update(in);
        CHECK(out.state == sp::PolicyState::StaticSafe);
        CHECK(out.soft_mask_active);       // preserves the fully-static fix
        CHECK(!out.hard_reject_armed);     // but never deletes on a static scene
    }

    TEST_CASE("PolicyFsm.HardRejectRequiresTheSceneGate");
    {
        sp::PolicyFsm fsm(config());
        sp::PolicyInput in = quiet(0.0);
        in.semantic_strong = true;
        in.semantic_scene_active = false;  // gate closed
        const sp::PolicyOutput out = fsm.update(in);
        CHECK(out.state == sp::PolicyState::StrongDynamic);
        CHECK(!out.hard_reject_armed);
    }

    TEST_CASE("PolicyFsm.RepeatedTriggersExtendRatherThanStack");
    {
        sp::PolicyFsm fsm(config());
        for (double t = 0.0; t < 5.0; t += 0.1)
            CHECK(fsm.update(burst(t)).assist_hold_active);
        // After the last trigger at ~4.9 s the hold must expire on schedule, not have
        // accumulated 50 triggers' worth of time.
        CHECK(fsm.update(quiet(6.0)).assist_hold_active);
        CHECK(!fsm.update(quiet(6.6)).assist_hold_active);
    }

    TEST_CASE("PolicyFsm.BackwardsTimestampDoesNotLatchTheHoldForever");
    {
        // A bag restart or a clock glitch previously left hold_until far in the
        // future, arming the policy for the rest of the run.
        sp::PolicyFsm fsm(config());
        fsm.update(burst(1000.0));
        CHECK(fsm.update(quiet(1000.2)).assist_hold_active);
        const sp::PolicyOutput restarted = fsm.update(quiet(0.0));
        CHECK(!restarted.assist_hold_active);
        CHECK(restarted.state == sp::PolicyState::StaticSafe);
    }

    TEST_CASE("PolicyFsm.ResetReturnsToStaticSafe");
    {
        sp::PolicyFsm fsm(config());
        fsm.update(strong(0.0));
        CHECK(fsm.state() == sp::PolicyState::StrongDynamic);
        fsm.reset();
        CHECK(fsm.state() == sp::PolicyState::StaticSafe);
        CHECK(!fsm.update(quiet(0.05)).assist_hold_active);
    }

    TEST_CASE("PolicyFsm.StateNamesAreStable");
    {
        CHECK(std::string(sp::toString(sp::PolicyState::StaticSafe)) == "static_safe");
        CHECK(std::string(sp::toString(sp::PolicyState::DynamicAssist)) == "dynamic_assist");
        CHECK(std::string(sp::toString(sp::PolicyState::StrongDynamic)) == "strong_dynamic");
        // The integer encoding is logged into sem_geodf_stats.csv and consumed by the
        // tuning scripts, so it is part of the contract.
        CHECK(static_cast<int>(sp::PolicyState::StaticSafe) == 0);
        CHECK(static_cast<int>(sp::PolicyState::DynamicAssist) == 1);
        CHECK(static_cast<int>(sp::PolicyState::StrongDynamic) == 2);
    }

    TEST_MAIN_RETURN();
}
