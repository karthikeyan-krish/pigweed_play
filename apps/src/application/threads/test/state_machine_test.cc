#include "state_machine.h"

#include <gtest/gtest.h>

#include <vector>

namespace play::thread {
namespace {

struct TransitionRecord {
  const State* previous;
  const State* current;
};

class StateMachineTest : public ::testing::Test {
 protected:
  StateMachineTest()
      : smc([this](const State* prev, const State* curr) {
          transitions.push_back({prev, curr});
        }) {}

  StateMachineContext smc;
  std::vector<TransitionRecord> transitions;
};

TEST_F(StateMachineTest, InitialStateIsNullBeforeStart) {
  EXPECT_EQ(smc.GetCurrentState(), nullptr);
  EXPECT_EQ(smc.GetPreviousState(), nullptr);
  EXPECT_FALSE(smc.GetButtonPressed());
  EXPECT_TRUE(transitions.empty());
}

TEST_F(StateMachineTest, StartSetsIdleState) {
  EXPECT_EQ(smc.Start(), 0);

  EXPECT_EQ(smc.GetCurrentState(), &StateIdle::instance());
  EXPECT_EQ(smc.GetPreviousState(), nullptr);
  EXPECT_FALSE(smc.GetButtonPressed());

  ASSERT_EQ(transitions.size(), 1u);
  EXPECT_EQ(transitions[0].previous, nullptr);
  EXPECT_EQ(transitions[0].current, &StateIdle::instance());
}

TEST_F(StateMachineTest, ButtonPressFromIdleTransitionsToButtonPressed) {
  ASSERT_EQ(smc.Start(), 0);
  transitions.clear();

  smc.HandleButtonPress();

  EXPECT_EQ(smc.GetPreviousState(), &StateIdle::instance());
  EXPECT_EQ(smc.GetCurrentState(), &StateButtonPressed::instance());
  EXPECT_TRUE(smc.GetButtonPressed());

  ASSERT_EQ(transitions.size(), 1u);
  EXPECT_EQ(transitions[0].previous, &StateIdle::instance());
  EXPECT_EQ(transitions[0].current, &StateButtonPressed::instance());
}

TEST_F(StateMachineTest, ButtonReleaseFromButtonPressedTransitionsToIdle) {
  ASSERT_EQ(smc.Start(), 0);
  smc.HandleButtonPress();
  transitions.clear();

  smc.HandleButtonRelease();

  EXPECT_EQ(smc.GetPreviousState(), &StateButtonPressed::instance());
  EXPECT_EQ(smc.GetCurrentState(), &StateIdle::instance());
  EXPECT_FALSE(smc.GetButtonPressed());

  ASSERT_EQ(transitions.size(), 1u);
  EXPECT_EQ(transitions[0].previous, &StateButtonPressed::instance());
  EXPECT_EQ(transitions[0].current, &StateIdle::instance());
}

TEST_F(StateMachineTest, ButtonReleaseInIdleDoesNothing) {
  ASSERT_EQ(smc.Start(), 0);
  transitions.clear();

  smc.HandleButtonRelease();

  EXPECT_EQ(smc.GetCurrentState(), &StateIdle::instance());
  EXPECT_EQ(smc.GetPreviousState(), nullptr);
  EXPECT_FALSE(smc.GetButtonPressed());
  EXPECT_TRUE(transitions.empty());
}

TEST_F(StateMachineTest, ButtonPressInButtonPressedDoesNothing) {
  ASSERT_EQ(smc.Start(), 0);
  smc.HandleButtonPress();
  transitions.clear();

  smc.HandleButtonPress();

  EXPECT_EQ(smc.GetCurrentState(), &StateButtonPressed::instance());
  EXPECT_EQ(smc.GetPreviousState(), &StateIdle::instance());
  EXPECT_TRUE(smc.GetButtonPressed());
  EXPECT_TRUE(transitions.empty());
}

TEST_F(StateMachineTest, FullPressReleaseCycleProducesExpectedTransitions) {
  ASSERT_EQ(smc.Start(), 0);
  transitions.clear();

  smc.HandleButtonPress();
  smc.HandleButtonRelease();

  EXPECT_EQ(smc.GetCurrentState(), &StateIdle::instance());
  EXPECT_EQ(smc.GetPreviousState(), &StateButtonPressed::instance());
  EXPECT_FALSE(smc.GetButtonPressed());

  ASSERT_EQ(transitions.size(), 2u);

  EXPECT_EQ(transitions[0].previous, &StateIdle::instance());
  EXPECT_EQ(transitions[0].current, &StateButtonPressed::instance());

  EXPECT_EQ(transitions[1].previous, &StateButtonPressed::instance());
  EXPECT_EQ(transitions[1].current, &StateIdle::instance());
}

TEST_F(StateMachineTest, MultipleCyclesRemainDeterministic) {
  ASSERT_EQ(smc.Start(), 0);
  transitions.clear();

  for (int i = 0; i < 3; ++i) {
    smc.HandleButtonPress();
    EXPECT_EQ(smc.GetCurrentState(), &StateButtonPressed::instance());
    EXPECT_TRUE(smc.GetButtonPressed());

    smc.HandleButtonRelease();
    EXPECT_EQ(smc.GetCurrentState(), &StateIdle::instance());
    EXPECT_FALSE(smc.GetButtonPressed());
  }

  ASSERT_EQ(transitions.size(), 6u);
  EXPECT_EQ(smc.GetCurrentState(), &StateIdle::instance());
  EXPECT_EQ(smc.GetPreviousState(), &StateButtonPressed::instance());
}

TEST_F(StateMachineTest, SetStateCanBeCalledDirectlyToForceTransition) {
  ASSERT_EQ(smc.Start(), 0);
  transitions.clear();

  smc.SetState(&StateButtonPressed::instance());

  EXPECT_EQ(smc.GetPreviousState(), &StateIdle::instance());
  EXPECT_EQ(smc.GetCurrentState(), &StateButtonPressed::instance());
  EXPECT_TRUE(smc.GetButtonPressed());

  ASSERT_EQ(transitions.size(), 1u);
  EXPECT_EQ(transitions[0].previous, &StateIdle::instance());
  EXPECT_EQ(transitions[0].current, &StateButtonPressed::instance());
}

TEST_F(StateMachineTest, SetStateToNullClearsCurrentStateAndCallsCallback) {
  ASSERT_EQ(smc.Start(), 0);
  transitions.clear();

  smc.SetState(nullptr);

  EXPECT_EQ(smc.GetPreviousState(), &StateIdle::instance());
  EXPECT_EQ(smc.GetCurrentState(), nullptr);
  EXPECT_FALSE(smc.GetButtonPressed());

  ASSERT_EQ(transitions.size(), 1u);
  EXPECT_EQ(transitions[0].previous, &StateIdle::instance());
  EXPECT_EQ(transitions[0].current, nullptr);
}

}  // namespace
}  // namespace play::thread
