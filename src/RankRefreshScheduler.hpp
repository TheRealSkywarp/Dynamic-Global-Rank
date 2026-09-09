#pragma once

#include <Geode/Geode.hpp>

class RankRefreshScheduler : public cocos2d::CCNode {
public:
    static RankRefreshScheduler* get();
    void setInterval(float interval);
    void queueLeaderboardFetch(float delay);

private:
    bool init() override;
    void update(float dt) override;

    float m_timer = 0.f;
    float m_interval = 240.f;

    bool m_oneShotPending = false;
    float m_oneShotTimer = 0.f;
};
