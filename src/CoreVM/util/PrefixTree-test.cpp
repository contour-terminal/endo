// SPDX-License-Identifier: Apache-2.0

import CoreVM;
#include <xzero/testing.h>

TEST(PrefixTree, exactMatch)
{
    CoreVM::util::PrefixTree<std::string, int> t;
    t.insert("/foo", 1);
    t.insert("/foo/bar", 2);
    t.insert("/foo/fnord", 3);

    int out { 0 };
    EXPECT_TRUE(t.lookup("/foo", &out));
    EXPECT_EQ(1, out);

    EXPECT_TRUE(t.lookup("/foo/bar", &out));
    EXPECT_EQ(2, out);

    EXPECT_TRUE(t.lookup("/foo/fnord", &out));
    EXPECT_EQ(3, out);
}

TEST(PrefixTree, subMatch)
{
    CoreVM::util::PrefixTree<std::string, int> t;
    t.insert("/foo", 1);
    t.insert("/foo/bar", 2);
    t.insert("/foo/fnord", 3);

    int out { 0 };
    EXPECT_TRUE(t.lookup("/foo/index.html", &out));
    EXPECT_EQ(1, out);

    EXPECT_TRUE(t.lookup("/foo/bar/", &out));
    EXPECT_EQ(2, out);

    EXPECT_TRUE(t.lookup("/foo/fnord/HACKING.md", &out));
    EXPECT_EQ(3, out);
}
