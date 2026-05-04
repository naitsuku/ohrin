// tests/wx_tests/test_shared_data.cpp

#include <gtest/gtest.h>

#include "ipc/SakuraSharedData.h"

using sakura_wx::ipc::SakuraSharedData;

TEST(SakuraSharedData, OpenAndClose) {
    SakuraSharedData a;
    ASSERT_TRUE(a.Open());
    EXPECT_GE(a.GetInstanceCount(), 1u);
    a.Close();
}

TEST(SakuraSharedData, TwoInstancesShareCount) {
    SakuraSharedData a;
    ASSERT_TRUE(a.Open());
    uint32_t before = a.GetInstanceCount();

    SakuraSharedData b;
    ASSERT_TRUE(b.Open());
    EXPECT_EQ(b.GetInstanceCount(), before + 1u);
    EXPECT_FALSE(b.IsPrimaryInstance());

    b.Close();
    EXPECT_EQ(a.GetInstanceCount(), before);

    a.Close();
}

TEST(SakuraSharedData, GeometryRoundTrip) {
    SakuraSharedData a;
    ASSERT_TRUE(a.Open());

    a.SetLastWindowGeometry(10, 20, 800, 600);
    int x, y, w, h;
    ASSERT_TRUE(a.GetLastWindowGeometry(x, y, w, h));
    EXPECT_EQ(x, 10);
    EXPECT_EQ(y, 20);
    EXPECT_EQ(w, 800);
    EXPECT_EQ(h, 600);

    a.Close();
}
