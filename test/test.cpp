#include <gtest/gtest.h>
#include <memory>
#include "server.hpp"
#include "comon.h"

// Demonstrate some basic assertions.
class testServer : public testing::Test
{
  protected:
    void SetUp() override
    {
      serv =  std::make_unique<ServerConditionVar>();
    }
    void TearDown() override
    {

    }
  std::unique_ptr<ServerConditionVar> serv;
};

TEST_F(testServer, CheckThreads) 
{
  EXPECT_NE(serv->getMaxThread(), 0);
}


TEST_F(testServer, CheckAddEvent) 
{

  using namespace global::DatabaseConntetion;
  //EXPECT_EQ(serv->addEvent(), status::succes);
  //EXPECT_EQ(serv->getReqNum(0),1);
}

