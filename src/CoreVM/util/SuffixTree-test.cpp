// SPDX-License-Identifier: Apache-2.0

import CoreVM;
#include <xzero/testing.h>

TEST(SuffixTree, exactMatch)
{
    CoreVM::util::SuffixTree<std::string, int> t;
    t.insert("www.example.com.", 1);
    t.insert("example.com.", 2);
    t.insert("com.", 3);

    int out { 0 };
    EXPECT_TRUE(t.lookup("www.example.com.", &out));
    EXPECT_EQ(1, out);

    EXPECT_TRUE(t.lookup("example.com.", &out));
    EXPECT_EQ(2, out);

    EXPECT_TRUE(t.lookup("com.", &out));
    EXPECT_EQ(3, out);
}

TEST(SuffixTree, subMatch)
{
    CoreVM::util::SuffixTree<std::string, int> t;
    t.insert("www.example.com.", 1);
    t.insert("example.com.", 2);
    t.insert("com.", 3);

    int out { 0 };
    EXPECT_TRUE(t.lookup("mirror.www.example.com.", &out));
    EXPECT_EQ(1, out);

    EXPECT_TRUE(t.lookup("www2.example.com.", &out));
    EXPECT_EQ(2, out);

    EXPECT_TRUE(t.lookup("foo.com.", &out));
    EXPECT_EQ(3, out);
}
