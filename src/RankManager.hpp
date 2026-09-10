#pragma once
#include <Geode/Geode.hpp>

using namespace geode::prelude;

class RankManager {
public:
    static RankManager& get();

    void load();
    void requestRankUpdate();
    void requestLeaderboardOnly();
    void requestInitialRank();

    void updateRankFromScore(GJUserScore* score);
    void onLeaderboardResultMissing();

    void markLevelCompleted(int oldStars, int newStars);
    void onLevelInfoOpened();

    bool shouldSkipBackgroundRefresh() const;
    int getCurrentRank() const;

private:
    int m_currentRank = -1;
    int m_lastAcceptedServerStars = -1;

    bool m_pendingLevelComplete = false;
    bool m_skipBackgroundRefresh = false;
    bool m_completionRetryScheduled = false;

    int m_preCompletionStars = -1;
    int m_expectedStars = -1;
    int m_completionRetryCount = 0;

    bool m_initialRankRequestIssued = false;
    bool m_initialRankRequestPending = false;
    int m_initialRetryCount = 0;

    void updateRank(int newRank, bool suppressPopup = false);
    void setRankSilently(int newRank);
    void clearCompletionState();
    void scheduleCompletionRetry(char const* reason);
    void scheduleInitialRetry();
};
