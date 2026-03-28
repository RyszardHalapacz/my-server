// =============================================================================
// Legacy server tests (ServerBase, ServerConditionVar, etc.)
// Kept for educational purposes — commented out intentionally.
// Active server classes: server::SingleThreadServer (CRTP).
// =============================================================================

#include <gtest/gtest.h>
#include <memory>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "server.hpp"
#pragma GCC diagnostic pop

#include "common.h"

TEST(Test, test)
{
  ASSERT_TRUE(true);
}

// class testServer : public testing::Test
// {
//   protected:
//     void SetUp() override
//     {
//       serv =  std::make_unique<ServerConditionVar>();
//     }
//     void TearDown() override
//     {

//     }
//   std::unique_ptr<ServerConditionVar> serv;
// };

// TEST_F(testServer, CheckThreads) 
// {
//   EXPECT_NE(serv->getMaxThread(), 0);
// }


// TEST_F(testServer, CheckAddEvent) 
// {

//   using namespace global::DatabaseConntetion;
//   //EXPECT_EQ(serv->addEvent(), status::succes);
//   //EXPECT_EQ(serv->getReqNum(0),1);
// }

