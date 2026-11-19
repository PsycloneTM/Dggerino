#include "providers/dgg/DggAccount.hpp"

#include "controllers/accounts/Account.hpp"
#include "Test.hpp"

using namespace chatterino;

TEST(DggAccount, DefaultIsAnonymousGuest)
{
    DggAccount account{DggAccountData{}};

    EXPECT_TRUE(account.isAnonymous());
    EXPECT_EQ(account.getProviderId(), ProviderId::Dgg);
    EXPECT_EQ(account.getCategory(), QString("Dgg"));
    EXPECT_TRUE(account.loginKey().isEmpty());
}

TEST(DggAccount, ConstructedFromData)
{
    DggAccount account{DggAccountData{
        .username = "Cyclone",
        .userID = 42,
        .loginKey = "k",
    }};

    EXPECT_FALSE(account.isAnonymous());
    // usernames are normalized to lower case
    EXPECT_EQ(account.username(), QString("cyclone"));
    EXPECT_EQ(account.userID(), 42U);
    EXPECT_EQ(account.toString(), QString("cyclone"));
    EXPECT_EQ(account.loginKey(), QString("k"));
}
