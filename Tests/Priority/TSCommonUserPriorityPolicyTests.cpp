#include "EventPipeline/Priority/TSCommonUserPriorityPolicy.h"
#include "TSTestHarness.h"
#include "TSTestSuites.h"

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    using namespace TikStudio::Tests;

    void RequireBreakdownEqual(
        const FTSCommonUserPriorityBreakdown& Actual,
        const FTSCommonUserPriorityBreakdown& Expected,
        const std::string& Context
    )
    {
        Require(
            Actual.FollowRoleContribution == Expected.FollowRoleContribution,
            Context + ": FollowRoleContribution"
        );
        Require(
            Actual.ModeratorContribution == Expected.ModeratorContribution,
            Context + ": ModeratorContribution"
        );
        Require(
            Actual.SubscriberContribution == Expected.SubscriberContribution,
            Context + ": SubscriberContribution"
        );
        Require(
            Actual.NewGifterContribution == Expected.NewGifterContribution,
            Context + ": NewGifterContribution"
        );
        Require(
            Actual.TopGifterRankContribution ==
                Expected.TopGifterRankContribution,
            Context + ": TopGifterRankContribution"
        );
        Require(
            Actual.GifterLevelContribution == Expected.GifterLevelContribution,
            Context + ": GifterLevelContribution"
        );
        Require(
            Actual.TeamMemberLevelContribution ==
                Expected.TeamMemberLevelContribution,
            Context + ": TeamMemberLevelContribution"
        );
        Require(
            Actual.TotalAdjustment == Expected.TotalAdjustment,
            Context + ": TotalAdjustment"
        );
    }

    void RequireUserEqual(
        const FTSUserSnapshot& Actual,
        const FTSUserSnapshot& Expected,
        const std::string& Context
    )
    {
        Require(
            Actual.UniqueId == Expected.UniqueId
                && Actual.Nickname == Expected.Nickname
                && Actual.ProfilePictureUrl == Expected.ProfilePictureUrl
                && Actual.FollowRole == Expected.FollowRole
                && Actual.bIsModerator == Expected.bIsModerator
                && Actual.bIsSubscriber == Expected.bIsSubscriber
                && Actual.bIsNewGifter == Expected.bIsNewGifter
                && Actual.TopGifterRank == Expected.TopGifterRank
                && Actual.GifterLevel == Expected.GifterLevel
                && Actual.TeamMemberLevel == Expected.TeamMemberLevel,
            Context
        );
    }

    void RequireSettingsEqual(
        const FTSCommonUserPrioritySettings& Actual,
        const FTSCommonUserPrioritySettings& Expected,
        const std::string& Context
    )
    {
        Require(
            Actual.FollowRolePointsPerLevel ==
                    Expected.FollowRolePointsPerLevel
                && Actual.MaxFollowRole == Expected.MaxFollowRole
                && Actual.ModeratorBonus == Expected.ModeratorBonus
                && Actual.SubscriberBonus == Expected.SubscriberBonus
                && Actual.NewGifterBonus == Expected.NewGifterBonus
                && Actual.TopGifterRankPointsPerPosition ==
                    Expected.TopGifterRankPointsPerPosition
                && Actual.MaxRewardedTopGifterRank ==
                    Expected.MaxRewardedTopGifterRank
                && Actual.GifterLevelPointsPerLevel ==
                    Expected.GifterLevelPointsPerLevel
                && Actual.MaxGifterLevel == Expected.MaxGifterLevel
                && Actual.TeamMemberLevelPointsPerLevel ==
                    Expected.TeamMemberLevelPointsPerLevel
                && Actual.MaxTeamMemberLevel == Expected.MaxTeamMemberLevel,
            Context
        );
    }

    void RequireInvalidSettings(
        const FTSCommonUserPrioritySettings& Settings,
        const std::string& Context
    )
    {
        Require(
            !FTSCommonUserPriorityPolicy::AreSettingsValid(Settings),
            Context + ": AreSettingsValid"
        );

        bool bThrewInvalidArgument = false;
        try
        {
            (void)FTSCommonUserPriorityPolicy::Evaluate(
                FTSUserSnapshot{},
                Settings
            );
        }
        catch (const std::invalid_argument&)
        {
            bThrewInvalidArgument = true;
        }

        Require(
            bThrewInvalidArgument,
            Context + ": Evaluate must throw invalid_argument"
        );
    }

    void TestDefaultSettings()
    {
        const FTSCommonUserPrioritySettings Settings;

        Require(
            Settings.FollowRolePointsPerLevel == 5
                && Settings.MaxFollowRole == 2
                && Settings.ModeratorBonus == 15
                && Settings.SubscriberBonus == 10
                && Settings.NewGifterBonus == 15
                && Settings.TopGifterRankPointsPerPosition == 4
                && Settings.MaxRewardedTopGifterRank == 5
                && Settings.GifterLevelPointsPerLevel == 1
                && Settings.MaxGifterLevel == 50
                && Settings.TeamMemberLevelPointsPerLevel == 1
                && Settings.MaxTeamMemberLevel == 50,
            "Common priority default settings"
        );
        Require(
            FTSCommonUserPriorityPolicy::AreSettingsValid(Settings),
            "Common priority defaults must be valid"
        );
    }

    void TestUserWithoutAttributes()
    {
        const FTSCommonUserPriorityBreakdown Actual =
            FTSCommonUserPriorityPolicy::Evaluate(
                FTSUserSnapshot{},
                FTSCommonUserPrioritySettings{}
            );

        RequireBreakdownEqual(
            Actual,
            FTSCommonUserPriorityBreakdown{},
            "User without priority attributes"
        );
    }

    void TestValidFollowRole()
    {
        constexpr std::array<std::pair<std::int32_t, std::int64_t>, 3> Cases{{
            {0, 0},
            {1, 5},
            {2, 10}
        }};

        for (const auto& [Value, Expected] : Cases)
        {
            FTSUserSnapshot User;
            User.FollowRole = Value;
            const FTSCommonUserPriorityBreakdown Result =
                FTSCommonUserPriorityPolicy::Evaluate(
                    User,
                    FTSCommonUserPrioritySettings{}
                );
            Require(
                Result.FollowRoleContribution == Expected
                    && Result.TotalAdjustment == Expected,
                "Valid FollowRole contribution"
            );
        }
    }

    void TestFollowRoleLimits()
    {
        FTSUserSnapshot User;
        User.FollowRole = -1;
        FTSCommonUserPriorityBreakdown Result =
            FTSCommonUserPriorityPolicy::Evaluate(
                User,
                FTSCommonUserPrioritySettings{}
            );
        Require(
            Result.FollowRoleContribution == 0
                && Result.TotalAdjustment == 0,
            "Negative FollowRole must clamp to zero"
        );

        User.FollowRole = std::numeric_limits<std::int32_t>::max();
        Result = FTSCommonUserPriorityPolicy::Evaluate(
            User,
            FTSCommonUserPrioritySettings{}
        );
        Require(
            Result.FollowRoleContribution == 10
                && Result.TotalAdjustment == 10,
            "High FollowRole must clamp to maximum"
        );
    }

    void TestModeratorBonus()
    {
        FTSUserSnapshot User;
        const FTSCommonUserPriorityBreakdown WithoutModerator =
            FTSCommonUserPriorityPolicy::Evaluate(
                User,
                FTSCommonUserPrioritySettings{}
            );
        User.bIsModerator = true;
        const FTSCommonUserPriorityBreakdown WithModerator =
            FTSCommonUserPriorityPolicy::Evaluate(
                User,
                FTSCommonUserPrioritySettings{}
            );

        Require(
            WithoutModerator.ModeratorContribution == 0
                && WithModerator.ModeratorContribution == 15
                && WithModerator.TotalAdjustment == 15,
            "Moderator bonus"
        );
    }

    void TestSubscriberBonus()
    {
        FTSUserSnapshot User;
        const FTSCommonUserPriorityBreakdown WithoutSubscriber =
            FTSCommonUserPriorityPolicy::Evaluate(
                User,
                FTSCommonUserPrioritySettings{}
            );
        User.bIsSubscriber = true;
        const FTSCommonUserPriorityBreakdown WithSubscriber =
            FTSCommonUserPriorityPolicy::Evaluate(
                User,
                FTSCommonUserPrioritySettings{}
            );

        Require(
            WithoutSubscriber.SubscriberContribution == 0
                && WithSubscriber.SubscriberContribution == 10
                && WithSubscriber.TotalAdjustment == 10,
            "Subscriber bonus"
        );
    }

    void TestNewGifterBonus()
    {
        FTSUserSnapshot User;
        const FTSCommonUserPriorityBreakdown WithoutNewGifter =
            FTSCommonUserPriorityPolicy::Evaluate(
                User,
                FTSCommonUserPrioritySettings{}
            );
        User.bIsNewGifter = true;
        const FTSCommonUserPriorityBreakdown WithNewGifter =
            FTSCommonUserPriorityPolicy::Evaluate(
                User,
                FTSCommonUserPrioritySettings{}
            );

        Require(
            WithoutNewGifter.NewGifterContribution == 0
                && WithNewGifter.NewGifterContribution == 15
                && WithNewGifter.TotalAdjustment == 15,
            "New gifter bonus"
        );
    }

    void TestRewardedTopGifterRanks()
    {
        constexpr std::array<std::pair<std::int32_t, std::int64_t>, 5> Cases{{
            {1, 20},
            {2, 16},
            {3, 12},
            {4, 8},
            {5, 4}
        }};

        for (const auto& [Rank, Expected] : Cases)
        {
            FTSUserSnapshot User;
            User.TopGifterRank = Rank;
            const FTSCommonUserPriorityBreakdown Result =
                FTSCommonUserPriorityPolicy::Evaluate(
                    User,
                    FTSCommonUserPrioritySettings{}
                );
            Require(
                Result.TopGifterRankContribution == Expected
                    && Result.TotalAdjustment == Expected,
                "Rewarded TopGifterRank contribution"
            );
        }
    }

    void TestUnrewardedTopGifterRanks()
    {
        constexpr std::array<std::int32_t, 4> Cases{
            -1,
            0,
            6,
            std::numeric_limits<std::int32_t>::max()
        };

        for (const std::int32_t Rank : Cases)
        {
            FTSUserSnapshot User;
            User.TopGifterRank = Rank;
            const FTSCommonUserPriorityBreakdown Result =
                FTSCommonUserPriorityPolicy::Evaluate(
                    User,
                    FTSCommonUserPrioritySettings{}
                );
            Require(
                Result.TopGifterRankContribution == 0
                    && Result.TotalAdjustment == 0,
                "Unrewarded TopGifterRank contribution"
            );
        }
    }

    void TestValidGifterLevel()
    {
        constexpr std::array<std::int32_t, 3> Cases{0, 10, 50};

        for (const std::int32_t Level : Cases)
        {
            FTSUserSnapshot User;
            User.GifterLevel = Level;
            const FTSCommonUserPriorityBreakdown Result =
                FTSCommonUserPriorityPolicy::Evaluate(
                    User,
                    FTSCommonUserPrioritySettings{}
                );
            Require(
                Result.GifterLevelContribution == Level
                    && Result.TotalAdjustment == Level,
                "Valid GifterLevel contribution"
            );
        }
    }

    void TestGifterLevelLimits()
    {
        FTSUserSnapshot User;
        User.GifterLevel = -1;
        FTSCommonUserPriorityBreakdown Result =
            FTSCommonUserPriorityPolicy::Evaluate(
                User,
                FTSCommonUserPrioritySettings{}
            );
        Require(
            Result.GifterLevelContribution == 0
                && Result.TotalAdjustment == 0,
            "Negative GifterLevel must clamp to zero"
        );

        User.GifterLevel = 51;
        Result = FTSCommonUserPriorityPolicy::Evaluate(
            User,
            FTSCommonUserPrioritySettings{}
        );
        Require(
            Result.GifterLevelContribution == 50
                && Result.TotalAdjustment == 50,
            "High GifterLevel must clamp to maximum"
        );
    }

    void TestValidTeamMemberLevel()
    {
        constexpr std::array<std::int32_t, 3> Cases{0, 10, 50};

        for (const std::int32_t Level : Cases)
        {
            FTSUserSnapshot User;
            User.TeamMemberLevel = Level;
            const FTSCommonUserPriorityBreakdown Result =
                FTSCommonUserPriorityPolicy::Evaluate(
                    User,
                    FTSCommonUserPrioritySettings{}
                );
            Require(
                Result.TeamMemberLevelContribution == Level
                    && Result.TotalAdjustment == Level,
                "Valid TeamMemberLevel contribution"
            );
        }
    }

    void TestTeamMemberLevelLimits()
    {
        FTSUserSnapshot User;
        User.TeamMemberLevel = -1;
        FTSCommonUserPriorityBreakdown Result =
            FTSCommonUserPriorityPolicy::Evaluate(
                User,
                FTSCommonUserPrioritySettings{}
            );
        Require(
            Result.TeamMemberLevelContribution == 0
                && Result.TotalAdjustment == 0,
            "Negative TeamMemberLevel must clamp to zero"
        );

        User.TeamMemberLevel = 51;
        Result = FTSCommonUserPriorityPolicy::Evaluate(
            User,
            FTSCommonUserPrioritySettings{}
        );
        Require(
            Result.TeamMemberLevelContribution == 50
                && Result.TotalAdjustment == 50,
            "High TeamMemberLevel must clamp to maximum"
        );
    }

    void TestCombinedDefaultMaximum()
    {
        FTSUserSnapshot User;
        User.FollowRole = 2;
        User.bIsModerator = true;
        User.bIsSubscriber = true;
        User.bIsNewGifter = true;
        User.TopGifterRank = 1;
        User.GifterLevel = 50;
        User.TeamMemberLevel = 50;

        FTSCommonUserPriorityBreakdown Expected;
        Expected.FollowRoleContribution = 10;
        Expected.ModeratorContribution = 15;
        Expected.SubscriberContribution = 10;
        Expected.NewGifterContribution = 15;
        Expected.TopGifterRankContribution = 20;
        Expected.GifterLevelContribution = 50;
        Expected.TeamMemberLevelContribution = 50;
        Expected.TotalAdjustment = 170;

        RequireBreakdownEqual(
            FTSCommonUserPriorityPolicy::Evaluate(
                User,
                FTSCommonUserPrioritySettings{}
            ),
            Expected,
            "Combined default maximum"
        );
    }

    void TestOrdinaryCombination()
    {
        FTSUserSnapshot User;
        User.FollowRole = 2;
        User.bIsModerator = true;
        User.bIsSubscriber = true;
        User.GifterLevel = 10;

        FTSCommonUserPriorityBreakdown Expected;
        Expected.FollowRoleContribution = 10;
        Expected.ModeratorContribution = 15;
        Expected.SubscriberContribution = 10;
        Expected.GifterLevelContribution = 10;
        Expected.TotalAdjustment = 45;

        RequireBreakdownEqual(
            FTSCommonUserPriorityPolicy::Evaluate(
                User,
                FTSCommonUserPrioritySettings{}
            ),
            Expected,
            "Ordinary common priority combination"
        );
    }

    void TestCustomSettings()
    {
        FTSCommonUserPrioritySettings Settings;
        Settings.FollowRolePointsPerLevel = 7;
        Settings.MaxFollowRole = 3;
        Settings.ModeratorBonus = 2;
        Settings.SubscriberBonus = 3;
        Settings.NewGifterBonus = 4;
        Settings.TopGifterRankPointsPerPosition = 6;
        Settings.MaxRewardedTopGifterRank = 4;
        Settings.GifterLevelPointsPerLevel = 3;
        Settings.MaxGifterLevel = 8;
        Settings.TeamMemberLevelPointsPerLevel = 4;
        Settings.MaxTeamMemberLevel = 9;

        FTSUserSnapshot User;
        User.FollowRole = 10;
        User.bIsModerator = true;
        User.bIsSubscriber = true;
        User.bIsNewGifter = true;
        User.TopGifterRank = 2;
        User.GifterLevel = 20;
        User.TeamMemberLevel = 20;

        FTSCommonUserPriorityBreakdown Expected;
        Expected.FollowRoleContribution = 21;
        Expected.ModeratorContribution = 2;
        Expected.SubscriberContribution = 3;
        Expected.NewGifterContribution = 4;
        Expected.TopGifterRankContribution = 18;
        Expected.GifterLevelContribution = 24;
        Expected.TeamMemberLevelContribution = 36;
        Expected.TotalAdjustment = 108;

        RequireBreakdownEqual(
            FTSCommonUserPriorityPolicy::Evaluate(User, Settings),
            Expected,
            "Custom common priority settings"
        );
    }

    void TestInvalidSettings()
    {
        FTSCommonUserPrioritySettings Settings;

        Settings.FollowRolePointsPerLevel = -1;
        RequireInvalidSettings(Settings, "Negative FollowRole points");
        Settings = {};
        Settings.MaxFollowRole = -1;
        RequireInvalidSettings(Settings, "Negative MaxFollowRole");
        Settings = {};
        Settings.ModeratorBonus = -1;
        RequireInvalidSettings(Settings, "Negative ModeratorBonus");
        Settings = {};
        Settings.SubscriberBonus = -1;
        RequireInvalidSettings(Settings, "Negative SubscriberBonus");
        Settings = {};
        Settings.NewGifterBonus = -1;
        RequireInvalidSettings(Settings, "Negative NewGifterBonus");
        Settings = {};
        Settings.TopGifterRankPointsPerPosition = -1;
        RequireInvalidSettings(Settings, "Negative TopGifterRank points");
        Settings = {};
        Settings.MaxRewardedTopGifterRank = -1;
        RequireInvalidSettings(Settings, "Negative TopGifterRank maximum");
        Settings = {};
        Settings.GifterLevelPointsPerLevel = -1;
        RequireInvalidSettings(Settings, "Negative GifterLevel points");
        Settings = {};
        Settings.MaxGifterLevel = -1;
        RequireInvalidSettings(Settings, "Negative MaxGifterLevel");
        Settings = {};
        Settings.TeamMemberLevelPointsPerLevel = -1;
        RequireInvalidSettings(Settings, "Negative TeamMemberLevel points");
        Settings = {};
        Settings.MaxTeamMemberLevel = -1;
        RequireInvalidSettings(Settings, "Negative MaxTeamMemberLevel");

        Settings = {};
        Settings.FollowRolePointsPerLevel = 0;
        Settings.MaxFollowRole = 0;
        Settings.ModeratorBonus = 0;
        Settings.SubscriberBonus = 0;
        Settings.NewGifterBonus = 0;
        Settings.TopGifterRankPointsPerPosition = 0;
        Settings.MaxRewardedTopGifterRank = 0;
        Settings.GifterLevelPointsPerLevel = 0;
        Settings.MaxGifterLevel = 0;
        Settings.TeamMemberLevelPointsPerLevel = 0;
        Settings.MaxTeamMemberLevel = 0;
        Require(
            FTSCommonUserPriorityPolicy::AreSettingsValid(Settings),
            "Zero common priority settings must be valid"
        );
        Require(
            FTSCommonUserPriorityPolicy::Evaluate(
                FTSUserSnapshot{},
                Settings
            ).TotalAdjustment == 0,
            "Zero common priority settings must disable all rules"
        );
    }

    void TestSaturationAndImmutability()
    {
        constexpr std::int64_t Maximum =
            std::numeric_limits<std::int64_t>::max();

        FTSCommonUserPrioritySettings Settings;
        Settings.FollowRolePointsPerLevel = Maximum;
        Settings.ModeratorBonus = Maximum;
        Settings.SubscriberBonus = Maximum;
        Settings.NewGifterBonus = Maximum;
        Settings.TopGifterRankPointsPerPosition = Maximum;
        Settings.GifterLevelPointsPerLevel = Maximum;
        Settings.TeamMemberLevelPointsPerLevel = Maximum;
        const FTSCommonUserPrioritySettings OriginalSettings = Settings;

        FTSUserSnapshot User;
        User.UniqueId = "priority-user";
        User.Nickname = "Priority User";
        User.ProfilePictureUrl = "https://example.invalid/priority.png";
        User.FollowRole = 2;
        User.bIsModerator = true;
        User.bIsSubscriber = true;
        User.bIsNewGifter = true;
        User.TopGifterRank = 1;
        User.GifterLevel = 50;
        User.TeamMemberLevel = 50;
        const FTSUserSnapshot OriginalUser = User;

        FTSCommonUserPriorityBreakdown Expected;
        Expected.FollowRoleContribution = Maximum;
        Expected.ModeratorContribution = Maximum;
        Expected.SubscriberContribution = Maximum;
        Expected.NewGifterContribution = Maximum;
        Expected.TopGifterRankContribution = Maximum;
        Expected.GifterLevelContribution = Maximum;
        Expected.TeamMemberLevelContribution = Maximum;
        Expected.TotalAdjustment = Maximum;

        const FTSCommonUserPriorityBreakdown Result =
            FTSCommonUserPriorityPolicy::Evaluate(User, Settings);
        RequireBreakdownEqual(Result, Expected, "Saturated common priority");
        RequireUserEqual(User, OriginalUser, "Evaluate must not mutate user");
        RequireSettingsEqual(
            Settings,
            OriginalSettings,
            "Evaluate must not mutate settings"
        );

        FTSUserSnapshot DifferentIdentifiers = User;
        DifferentIdentifiers.UniqueId.clear();
        DifferentIdentifiers.Nickname = "Unrelated nickname";
        DifferentIdentifiers.ProfilePictureUrl.clear();
        RequireBreakdownEqual(
            FTSCommonUserPriorityPolicy::Evaluate(
                DifferentIdentifiers,
                Settings
            ),
            Result,
            "Identity strings must not affect common priority"
        );
    }
}

namespace TikStudio::Tests
{
    void RegisterCommonUserPriorityPolicyTests(FTSTestCases& Tests)
    {
        Tests.push_back({"Common priority default settings", &TestDefaultSettings});
        Tests.push_back({"Common priority empty user", &TestUserWithoutAttributes});
        Tests.push_back({"Common priority valid FollowRole", &TestValidFollowRole});
        Tests.push_back({"Common priority FollowRole limits", &TestFollowRoleLimits});
        Tests.push_back({"Common priority moderator bonus", &TestModeratorBonus});
        Tests.push_back({"Common priority subscriber bonus", &TestSubscriberBonus});
        Tests.push_back({"Common priority new gifter bonus", &TestNewGifterBonus});
        Tests.push_back({"Common priority rewarded Top Gifter ranks", &TestRewardedTopGifterRanks});
        Tests.push_back({"Common priority unrewarded Top Gifter ranks", &TestUnrewardedTopGifterRanks});
        Tests.push_back({"Common priority valid GifterLevel", &TestValidGifterLevel});
        Tests.push_back({"Common priority GifterLevel limits", &TestGifterLevelLimits});
        Tests.push_back({"Common priority valid TeamMemberLevel", &TestValidTeamMemberLevel});
        Tests.push_back({"Common priority TeamMemberLevel limits", &TestTeamMemberLevelLimits});
        Tests.push_back({"Common priority combined default maximum", &TestCombinedDefaultMaximum});
        Tests.push_back({"Common priority ordinary combination", &TestOrdinaryCombination});
        Tests.push_back({"Common priority custom settings", &TestCustomSettings});
        Tests.push_back({"Common priority invalid settings", &TestInvalidSettings});
        Tests.push_back({"Common priority saturation and immutability", &TestSaturationAndImmutability});
    }
}
