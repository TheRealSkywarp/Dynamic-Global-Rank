#include "RankManager.hpp"
#include "RankPopup.hpp"
#include "RankRefreshScheduler.hpp"
#include <Geode/modify/GameLevelManager.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace {
    constexpr float COMPLETION_RETRY_DELAYS[] = {
        0.60f,
        0.90f,
        1.40f,
        2.20f,
        3.50f,
        5.00f,
    };

    constexpr int COMPLETION_RETRY_COUNT =
        static_cast<int>(sizeof(COMPLETION_RETRY_DELAYS) / sizeof(COMPLETION_RETRY_DELAYS[0]));

    constexpr int MAX_INITIAL_RETRIES = 2;
    constexpr float INITIAL_RETRY_DELAY = 1.00f;
}

RankManager& RankManager::get() {
    static RankManager instance;
    return instance;
}

bool RankManager::shouldSkipBackgroundRefresh() const {
    return m_skipBackgroundRefresh;
}

int RankManager::getCurrentRank() const {
    return m_currentRank;
}

void RankManager::load() {
    m_currentRank = Mod::get()->getSavedValue<int>("last-rank", -1);

    // This value intentionally starts fresh each game session. It is only used
    // to reject an older network response that arrives after a newer one.
    m_lastAcceptedServerStars = -1;

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

    // Never trust GD's cached global-stars leaderboard for an explicit refresh.
    glm->m_storedLevels->removeObjectForKey(key.c_str());
    glm->getLeaderboardScores(LeaderboardType::Global, LeaderboardStat::Stars);

    if (m_pendingLevelComplete) {
        // A queued completion fetch has now actually been dispatched.
        m_completionRetryScheduled = false;

        // The regular scheduler is minutes apart, so once the first request is
        // on the wire there is no reason to keep it blocked indefinitely.
        m_skipBackgroundRefresh = false;
    }
}

void RankManager::requestInitialRank() {
    if (m_initialRankRequestIssued)
        return;

    auto accountID = GJAccountManager::sharedState()->m_accountID;
    if (accountID == 0) {
        log::warn("Initial rank fetch skipped for now: user is not logged in");
        return;
    }

    m_initialRankRequestIssued = true;
    m_initialRankRequestPending = true;
    m_initialRetryCount = 0;

    log::info("Requesting silent initial rank baseline");

    // Refresh the uploaded score first, then read the leaderboard shortly
    // afterwards. This gives us a current baseline before the first rated level.
    GameLevelManager::sharedState()->updateUserScore();
    RankRefreshScheduler::get()->queueLeaderboardFetch(0.25f);
}

void RankManager::setRankSilently(int newRank) {
    if (newRank <= 0)
        return;

    m_currentRank = newRank;
    Mod::get()->setSavedValue("last-rank", newRank);
    log::info("Rank baseline set silently to {}", newRank);
}

void RankManager::updateRank(int newRank, bool suppressPopup) {
    if (newRank <= 0)
        return;

    if (m_currentRank == -1) {
        setRankSilently(newRank);
        return;
    }

    if (m_currentRank != newRank) {
        int difference = newRank - m_currentRank;

        if (difference < 0)
            log::info("Rank improved by {} places", -difference);
        else
            log::info("Rank dropped by {} places", difference);

        if (!suppressPopup)
            RankPopup::get()->showRankChange(m_currentRank, newRank);
    }

    m_currentRank = newRank;
    Mod::get()->setSavedValue("last-rank", m_currentRank);
}

void RankManager::clearCompletionState() {
    // If a manual leaderboard open resolved the completion before our queued
    // retry fired, do not leave an unnecessary extra request behind.
    RankRefreshScheduler::get()->cancelQueuedLeaderboardFetch();

    m_pendingLevelComplete = false;
    m_skipBackgroundRefresh = false;
    m_completionRetryScheduled = false;

    m_preCompletionStars = -1;
    m_expectedStars = -1;
    m_completionRetryCount = 0;
}

void RankManager::scheduleCompletionRetry(char const* reason) {
    if (!m_pendingLevelComplete)
        return;

    if (m_completionRetryScheduled)
        return;

    if (m_completionRetryCount >= COMPLETION_RETRY_COUNT) {
        // Keep the completion pending so a later manual/background leaderboard
        // response can still finish it, but stop hammering the server.
        m_skipBackgroundRefresh = false;
        log::warn(
            "Completion rank is still not fresh after {} retries ({}). Waiting for the next leaderboard refresh.",
            m_completionRetryCount,
            reason
        );
        return;
    }

    float delay = COMPLETION_RETRY_DELAYS[m_completionRetryCount];
    ++m_completionRetryCount;
    m_completionRetryScheduled = true;

    log::info(
        "Completion leaderboard data is stale/missing ({}); retry {}/{} in {:.2f}s",
        reason,
        m_completionRetryCount,
        COMPLETION_RETRY_COUNT,
        delay
    );

    RankRefreshScheduler::get()->queueLeaderboardFetch(delay);
}

void RankManager::scheduleInitialRetry() {
    if (!m_initialRankRequestPending)
        return;

    if (m_initialRetryCount >= MAX_INITIAL_RETRIES) {
        log::warn("Initial rank baseline could not be obtained after {} retries", m_initialRetryCount);
        m_initialRankRequestPending = false;
        return;
    }

    ++m_initialRetryCount;
    log::info("Retrying silent initial rank baseline ({}/{})", m_initialRetryCount, MAX_INITIAL_RETRIES);
    RankRefreshScheduler::get()->queueLeaderboardFetch(INITIAL_RETRY_DELAY);
}

void RankManager::updateRankFromScore(GJUserScore* score) {
    if (!score) {
        onLeaderboardResultMissing();
        return;
    }

    int newRank = score->m_playerRank;
    int serverStars = score->m_stars;

    if (newRank <= 0) {
        if (m_pendingLevelComplete)
            scheduleCompletionRetry("invalid rank");
        else if (m_initialRankRequestPending)
            scheduleInitialRetry();
        return;
    }

    // Reject an older response that arrives after a newer response in the same
    // game session. This prevents a late request from creating a bogus rank drop.
    if (m_lastAcceptedServerStars >= 0 && serverStars < m_lastAcceptedServerStars) {
        log::warn(
            "Ignoring out-of-order leaderboard response: {} server stars < {} already accepted",
            serverStars,
            m_lastAcceptedServerStars
        );

        if (m_pendingLevelComplete)
            scheduleCompletionRetry("out-of-order response");
        return;
    }

    if (m_pendingLevelComplete) {
        // This is the important freshness check: do not infer freshness from a
        // tiny rank delta. The server must actually know about the stars earned
        // by the completed rated level before its rank is allowed to be shown.
        if (m_expectedStars > 0 && serverStars < m_expectedStars) {
            // If there was no baseline yet, a response containing exactly the
            // pre-completion star total is useful as the old rank. Seed it
            // silently, then keep waiting for the fresh post-completion result.
            if (
                m_preCompletionStars >= 0 &&
                serverStars == m_preCompletionStars &&
                (m_currentRank == -1 || m_initialRankRequestPending)
            ) {
                setRankSilently(newRank);
                m_lastAcceptedServerStars = serverStars;
                log::info(
                    "Captured pre-completion rank baseline {} at {} stars",
                    newRank,
                    serverStars
                );
            }

            log::info(
                "Leaderboard still has {} stars, but completion requires at least {}; not displaying rank {}",
                serverStars,
                m_expectedStars,
                newRank
            );

            scheduleCompletionRetry("server stars have not caught up");
            return;
        }

        // The server now reflects the completed level. This rank is eligible
        // for display regardless of whether the delta is +150, -3, +1, etc.
        m_lastAcceptedServerStars = std::max(m_lastAcceptedServerStars, serverStars);
        m_initialRankRequestPending = false;

        updateRank(newRank, false);
        clearCompletionState();
        return;
    }

    // A normal response can also be stale relative to the user's current local
    // star total. Ignore it instead of letting a delayed response move the rank
    // backwards; the next normal refresh will reconcile it once the server does.
    int localStars = GameStatsManager::sharedState()->getStat("6");
    if (localStars > 0 && serverStars < localStars) {
        log::info(
            "Ignoring leaderboard response with {} stars because local save has {}",
            serverStars,
            localStars
        );

        if (m_initialRankRequestPending)
            scheduleInitialRetry();
        return;
    }

    m_lastAcceptedServerStars = std::max(m_lastAcceptedServerStars, serverStars);

    if (m_initialRankRequestPending) {
        // Startup fetch is only a baseline. Do not throw a popup merely because
        // the user's rank drifted while the game was closed.
        setRankSilently(newRank);
        m_initialRankRequestPending = false;
        RankRefreshScheduler::get()->cancelQueuedLeaderboardFetch();
        return;
    }

    updateRank(newRank, false);
}

void RankManager::onLeaderboardResultMissing() {
    if (m_pendingLevelComplete) {
        scheduleCompletionRetry("player missing from leaderboard response");
        return;
    }

    if (m_initialRankRequestPending)
        scheduleInitialRetry();
}

void RankManager::markLevelCompleted(int oldStars, int newStars) {
    if (newStars <= oldStars)
        return;

    m_pendingLevelComplete = true;
    m_skipBackgroundRefresh = true;
    m_completionRetryScheduled = false;
    m_completionRetryCount = 0;

    m_preCompletionStars = oldStars;
    m_expectedStars = newStars;

    log::info(
        "Level completion pending rank refresh: {} -> {} stars",
        oldStars,
        newStars
    );
}

void RankManager::onLevelInfoOpened() {
    if (!m_pendingLevelComplete)
        return;

    if (!Mod::get()->getSettingValue<bool>("show-after-level-complete")) {
        clearCompletionState();
        return;
    }

    log::info(
        "Uploading score and requesting completion rank refresh (expecting at least {} server stars)",
        m_expectedStars
    );

    GameLevelManager::sharedState()->updateUserScore();

    // First read immediately. If Boomlings still reports fewer stars than the
    // local post-completion total, retries use a short progressive backoff.
    RankRefreshScheduler::get()->queueLeaderboardFetch(0.f);
}
