#include <Geode/Geode.hpp>
#include <Geode/modify/GameLevelManager.hpp>
#include <Geode/modify/LoadingLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/loader/Event.hpp>
#include <Geode/loader/SettingV3.hpp>
#include "RankManager.hpp"
#include "RankPopup.hpp"
#include "RankRefreshScheduler.hpp"

using namespace geode::prelude;

class $modify(MyGameLevelManager, GameLevelManager) {
    void onGetLeaderboardScoresCompleted(gd::string response, gd::string tag) {
        GameLevelManager::onGetLeaderboardScoresCompleted(response, tag);

        if (tag != "lb_2_0")
            return;

        auto glm = GameLevelManager::sharedState();
        auto accountID = GJAccountManager::sharedState()->m_accountID;
        auto scores = typeinfo_cast<CCArray*>(glm->m_storedLevels->objectForKey(tag.c_str()));

        if (!scores) {
            RankManager::get().onLeaderboardResultMissing();
            return;
        }

        bool foundSelf = false;

        for (auto score : CCArrayExt<GJUserScore*>(scores)) {
            if (score->m_accountID == accountID) {
                foundSelf = true;
                RankManager::get().updateRankFromScore(score);
                break;
            }
        }

        if (!foundSelf)
            RankManager::get().onLeaderboardResultMissing();
    }
};

class $modify(MyPlayLayer, PlayLayer) {
    void levelComplete() {
        auto gsm = GameStatsManager::sharedState();
        int oldStars = gsm->getStat("6");

        PlayLayer::levelComplete();

        int newStars = gsm->getStat("6");
        if (newStars > oldStars) {
            log::info("Earned {} stars", newStars - oldStars);
            RankManager::get().markLevelCompleted(oldStars, newStars);
        }
    }
};

class $modify(MyLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge))
            return false;

        RankManager::get().onLevelInfoOpened();
        return true;
    }
};

class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init())
            return false;

        // Obtain a silent, fresh baseline as soon as the main menu is available.
        // This fixes the first rated completion after launch having no old rank
        // to compare against (especially after changing the mod ID / fresh install).
        RankManager::get().requestInitialRank();
        return true;
    }
};

$on_mod(Loaded) {
    log::info("Dynamic Global Rank loaded");

    RankManager::get().load();
    RankPopup::get();
    RankRefreshScheduler::get();
}

$execute {
    listenForSettingChanges<int>("refresh-seconds", [](int value) {
        RankRefreshScheduler::get()->setInterval(value);
    });

    listenForSettingChanges<ccColor3B>("popup-background-color", [](ccColor3B) {
        if (RankPopup::get())
            RankPopup::get()->updateColor();
    });

    listenForSettingChanges<int>("popup-background-opacity", [](int) {
        if (RankPopup::get())
            RankPopup::get()->updateColor();
    });
}
