#include "RankRefreshScheduler.hpp"
#include "RankManager.hpp"
#include <algorithm>

using namespace geode::prelude;

RankRefreshScheduler* RankRefreshScheduler::get() {
    static RankRefreshScheduler* instance = nullptr;
    if (!instance) {
        auto ret = new RankRefreshScheduler();
        if (ret && ret->init()) {
            ret->autorelease();
            OverlayManager::get()->addChild(ret);
            instance = ret;
        }
        else {
            CC_SAFE_DELETE(ret);
        }
    }
    return instance;
}

bool RankRefreshScheduler::init() {
    if (!CCNode::init())
        return false;

    m_interval = static_cast<float>(
        std::clamp(Mod::get()->getSettingValue<int>("refresh-seconds"), 120, 600)
    );

    this->scheduleUpdate();
    return true;
}

void RankRefreshScheduler::setInterval(float interval) {
    m_interval = std::clamp(interval, 120.f, 600.f);
}

void RankRefreshScheduler::queueLeaderboardFetch(float delay) {
    delay = std::max(0.f, delay);

    // Keep the earliest already queued fetch instead of pushing it farther
    // away if two callers happen to request one at nearly the same time.
    if (!m_oneShotPending || delay < m_oneShotTimer)
        m_oneShotTimer = delay;

    m_oneShotPending = true;
}

void RankRefreshScheduler::cancelQueuedLeaderboardFetch() {
    m_oneShotPending = false;
    m_oneShotTimer = 0.f;
}

void RankRefreshScheduler::update(float dt) {
    if (m_oneShotPending) {
        m_oneShotTimer -= dt;

        if (m_oneShotTimer <= 0.f) {
            m_oneShotPending = false;
            m_oneShotTimer = 0.f;

            log::info("Running delayed leaderboard fetch");
            RankManager::get().requestLeaderboardOnly();

            // Avoid an immediate second request if the regular timer happened
            // to be near its interval at the same moment.
            m_timer = 0.f;
            return;
        }
    }

    m_timer += dt;
    if (m_timer < m_interval)
        return;

    if (!Mod::get()->getSettingValue<bool>("background-refresh")) {
        m_timer = 0.f;
        return;
    }

    // Do not throw this refresh opportunity away. Keep the timer ready so the
    // refresh happens as soon as the short completion flow releases the flag.
    if (RankManager::get().shouldSkipBackgroundRefresh())
        return;

    m_timer = 0.f;
    log::info("Background rank refresh");

    auto accountID = GJAccountManager::sharedState()->m_accountID;
    if (accountID == 0) {
        log::warn("Background rank refresh: User not logged in.");
        return;
    }

    RankManager::get().requestRankUpdate();
}
