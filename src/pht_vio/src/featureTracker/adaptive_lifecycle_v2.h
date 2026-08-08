#pragma once

#include <algorithm>
#include <iterator>
#include <map>
#include <set>

#include "adaptive_arbitration_v2.h"

namespace adaptive_lifecycle_v2
{

enum class State
{
    Trusted = 0,
    Suspect,
    Disagreement,
    DownWeighted,
    Quarantined,
    Rejected,
    Recovering,
};

inline const char *toString(State state)
{
    switch (state)
    {
        case State::Trusted: return "TRUSTED";
        case State::Suspect: return "SUSPECT";
        case State::Disagreement: return "DISAGREEMENT";
        case State::DownWeighted: return "DOWNWEIGHTED";
        case State::Quarantined: return "QUARANTINED";
        case State::Rejected: return "REJECTED";
        case State::Recovering: return "RECOVERING";
    }
    return "UNKNOWN";
}

struct Config
{
    int suspect_frames = 1;
    int downweight_frames = 2;
    int hard_reject_dwell_frames = 3;
    double recover_dwell_s = 0.5;
    int minimum_surviving_tracks = 40;
};

struct Record
{
    State state = State::Trusted;
    int suspicious_streak = 0;
    int hard_action_streak = 0;
    double state_entered_s = 0.0;
};

struct Decision
{
    State state = State::Trusted;
    adaptive_arbitration_v2::Action action = adaptive_arbitration_v2::Action::Keep;
    bool hard_reject_blocked_by_dwell = false;
    bool hard_reject_blocked_by_budget = false;
};

class Manager
{
  public:
    void configure(const Config &config) { config_ = config; }
    void clear()
    {
        records_.clear();
        frame_started_ = false;
        deletion_allowance_ = 0;
    }

    template <typename IdContainer>
    void retainOnly(const IdContainer &ids)
    {
        std::set<int> live(ids.begin(), ids.end());
        for (auto it = records_.begin(); it != records_.end();)
            it = live.count(it->first) ? std::next(it) : records_.erase(it);
    }

    const Record *find(int id) const
    {
        const auto it = records_.find(id);
        return it == records_.end() ? nullptr : &it->second;
    }

    Decision update(int id, double timestamp_s,
                    const adaptive_arbitration_v2::ArbitrationResult &arbitration,
                    int tracked_features)
    {
        using adaptive_arbitration_v2::Action;
        if (!frame_started_ || timestamp_s != frame_timestamp_s_)
        {
            frame_started_ = true;
            frame_timestamp_s_ = timestamp_s;
            deletion_allowance_ = std::max(
                0, tracked_features - config_.minimum_surviving_tracks);
        }

        Record &record = records_[id];
        if (record.state_entered_s == 0.0)
            record.state_entered_s = timestamp_s;

        const bool suspicious = arbitration.action != Action::Keep;
        record.suspicious_streak = suspicious ? record.suspicious_streak + 1 : 0;
        record.hard_action_streak = arbitration.action == Action::HardReject
                                        ? record.hard_action_streak + 1
                                        : 0;

        if (arbitration.action == Action::Quarantine)
        {
            if (record.suspicious_streak >= config_.downweight_frames)
                setState(record, State::Quarantined, timestamp_s);
            else
                setState(record, State::Disagreement, timestamp_s);
        }
        else if (!suspicious)
        {
            if (record.state != State::Trusted && record.state != State::Recovering)
                setState(record, State::Recovering, timestamp_s);
            else if (record.state == State::Recovering &&
                     timestamp_s - record.state_entered_s >= config_.recover_dwell_s)
                setState(record, State::Trusted, timestamp_s);
        }
        else if (record.suspicious_streak >= config_.downweight_frames)
            setState(record, State::DownWeighted, timestamp_s);
        else if (record.suspicious_streak >= config_.suspect_frames)
            setState(record, State::Suspect, timestamp_s);

        Decision decision;
        decision.state = record.state;
        decision.action = arbitration.action;
        if (arbitration.action == Action::HardReject)
        {
            if (record.hard_action_streak < config_.hard_reject_dwell_frames)
            {
                decision.action = Action::DownWeight;
                decision.hard_reject_blocked_by_dwell = true;
            }
            else if (deletion_allowance_ <= 0)
            {
                decision.action = Action::DownWeight;
                decision.hard_reject_blocked_by_budget = true;
            }
            else
            {
                --deletion_allowance_;
                setState(record, State::Rejected, timestamp_s);
                decision.state = State::Rejected;
            }
        }
        return decision;
    }

  private:
    static void setState(Record &record, State state, double timestamp_s)
    {
        if (record.state == state)
            return;
        record.state = state;
        record.state_entered_s = timestamp_s;
    }

    Config config_;
    std::map<int, Record> records_;
    bool frame_started_ = false;
    double frame_timestamp_s_ = 0.0;
    int deletion_allowance_ = 0;
};

}  // namespace adaptive_lifecycle_v2
