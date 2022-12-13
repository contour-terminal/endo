// SPDX-License-Identifier: Apache-2.0

#include <cstdio>

#include <xzero/Buffer.h>
#include <xzero/net/Cidr.h>
#include <xzero/testing.h>

using namespace xzero;

TEST(Cidr, contains)
{
    Cidr cidr(IPAddress("192.168.0.0"), 24);
    IPAddress ip0("192.168.0.1");
    IPAddress ip1("192.168.1.1");

    ASSERT_TRUE(cidr.contains(ip0));
    ASSERT_FALSE(cidr.contains(ip1));
}

TEST(Cidr, equals)
{
    ASSERT_EQ(Cidr("0.0.0.0", 0), Cidr("0.0.0.0", 0));
    ASSERT_EQ(Cidr("1.2.3.4", 24), Cidr("1.2.3.4", 24));

    ASSERT_NE(Cidr("1.2.3.4", 24), Cidr("1.2.1.4", 24));
    ASSERT_NE(Cidr("1.2.3.4", 24), Cidr("1.2.3.4", 23));
}
