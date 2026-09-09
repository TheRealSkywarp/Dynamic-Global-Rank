#include "RankManager.hpp"
#include "RankPopup.hpp"
#include "RankRefreshScheduler.hpp"
#include <Geode/modify/GameLevelManager.hpp>
#include <cstdlib>

using namespace geode::prelude;

namespace {
    constexpr float COMPLETION_RETRY_DELAY = 1.25f;
    constexpr int SUSPICIOUS_SMALL_DELTA = 2;
    constexpr int MAX_SMALL_DELTA_RETRIES = 1;
}

RankManager& RankManager::get() {
    static RankManager instance;
    return instance;
}

bool RankManager::shouldSkipBackgroundRefresh() const {
    return m_skipBackgroundRefresh;
}

void RankManager::load() {
    m_currentRank = Mod::get()->getSavedValue<int>("last-rank", -1);
    log::info("Loaded saved rank: {}", m_currentRank);
}

void RankManager::requestRankUpdate() {
    auto glm = GameLevelManager::sharedState();
    glm->updateUserScore();
    requestLeaderboardOnly();
}

void RankManager::requestLeaderboardOnly() {
    auto glm = GameLevelManager::sharedState();
    auto key = fmt::format(
        "lb_{}_{}",
        static_cast<int>(LeaderboardType::Global),
        static_cast<int>(LeaderboardStat::Stars)
    );

    glm->m_storedLevels->removeObjectForKey(key.c_str());
    glm->getLeaderboardScores(LeaderboardType::Global, LeaderboardStat::Stars);

    if (m_pendingLevelComplete)
        m_skipBackgroundRefresh = false;
}

int RankManager::getCurrentRank() const {
    return m_currentRank;
}

void RankManager::updateRank(int newRank) {
    if (m_currentRank == -1) {
        log::info("Found rank: {}", newRank);
        m_currentRank = newRank;
        Mod::get()->setSavedValue("last-rank", newRank);
        return;
    }

    if (m_currentRank != newRank) {
        int difference = newRank - m_currentRank;

        if (difference < 0)
            log::info("Rank improved by {} places", -difference);
        else
            log::info("Rank dropped by {} places", difference);

        RankPopup::get()->showRankChange(m_currentRank, newRank);
    }

    m_currentRank = newRank;
    Mod::get()->setSavedValue("last-rank", m_currentRank);
}

void RankManager::clearCompletionState() {
    m_pendingLevelComplete = false;
    m_skipBackgroundRefresh = false;
    m_completionRetryCount = 0;
}

void RankManager::updateRankFromScore(GJUserScore* score) {
    if (!score)
        return;

    int newRank = score->m_playerRank;

    if (newRank <= 0) {
        if (m_pendingLevelComplete && m_completionRetryCount < MAX_SMALL_DELTA_RETRIES) {
            ++m_completionRetryCount;
            log::warn("Completion leaderboard returned an invalid rank; retrying");
            RankRefreshScheduler::get()->queueLeaderboardFetch(COMPLETION_RETRY_DELAY);
        }
        else if (m_pendingLevelComplete) {
            clearCompletionState();
        }
        return;
    }

    if (
        m_pendingLevelComplete &&
        m_currentRank > 0 &&
        std::abs(newRank - m_currentRank) <= SUSPICIOUS_SMALL_DELTA &&
        m_completionRetryCount < MAX_SMALL_DELTA_RETRIES
    ) {
        ++m_completionRetryCount;
        log::info(
            "Completion rank result {} -> {} looks cached; retrying once",
            m_currentRank,
            newRank
        );
        RankRefreshScheduler::get()->queueLeaderboardFetch(COMPLETION_RETRY_DELAY);
        return;
    }

    updateRank(newRank);

    if (m_pendingLevelComplete)
        clearCompletionState();
}

void RankManager::markLevelCompleted() {
    m_pendingLevelComplete = true;
    m_skipBackgroundRefresh = true;
    m_completionRetryCount = 0;

    log::info("Level completion pending rank refresh");
}

void RankManager::onLevelInfoOpened() {
    if (!m_pendingLevelComplete)
        return;

    if (!Mod::get()->getSettingValue<bool>("show-after-level-complete")) {
        clearCompletionState();
        return;
    }

    log::info("Uploading score and requesting completion rank refresh");

    GameLevelManager::sharedState()->updateUserScore();
    RankRefreshScheduler::get()->queueLeaderboardFetch(0.f);
}
