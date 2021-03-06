// Copyright (c) 2019 - 2021 by Robert Bosch GmbH. All rights reserved.
// Copyright (c) 2021 by Apex.AI Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "test.hpp"

#include "iceoryx_posh/internal/capro/capro_message.hpp"
#include "iceoryx_posh/internal/popo/ports/publisher_port_user.hpp"

#include "iceoryx_posh/iceoryx_posh_types.hpp"
#include "iceoryx_posh/internal/popo/ports/publisher_port_user.hpp"
#include "iceoryx_posh/internal/roudi/port_manager.hpp"
#include "iceoryx_posh/roudi/memory/iceoryx_roudi_memory_manager.hpp"
#include "iceoryx_utils/cxx/generic_raii.hpp"
#include "iceoryx_utils/internal/relocatable_pointer/relative_ptr.hpp"
#include "iceoryx_utils/posix_wrapper/posix_access_rights.hpp"

#include <cstdint>
#include <limits> // std::numeric_limits

using namespace ::testing;
using ::testing::Return;

using iox::popo::PublisherOptions;
using iox::popo::PublisherPortUser;
using iox::popo::SubscriberOptions;
using iox::popo::SubscriberPortUser;
using iox::roudi::IceOryxRouDiMemoryManager;
using iox::roudi::PortManager;
using iox::roudi::PortPoolError;
using iox::runtime::PortConfigInfo;

class PortManagerTester : public PortManager
{
  public:
    PortManagerTester(IceOryxRouDiMemoryManager* roudiMemoryManager)
        : PortManager(roudiMemoryManager)
    {
    }

  private:
    FRIEND_TEST(PortManager_test, CheckDeleteOfPortsFromProcess1);
    FRIEND_TEST(PortManager_test, CheckDeleteOfPortsFromProcess2);
};

class PortManager_test : public Test
{
  public:
    iox::mepoo::MemoryManager* m_payloadMemoryManager{nullptr};
    IceOryxRouDiMemoryManager* m_roudiMemoryManager{nullptr};
    PortManagerTester* m_portManager{nullptr};

    uint16_t m_instIdCounter, m_eventIdCounter, m_sIdCounter;

    iox::ProcessName_t m_ProcessName{"TestProcess"};

    void SetUp() override
    {
        testing::internal::CaptureStderr();
        m_instIdCounter = m_sIdCounter = 1U;
        m_eventIdCounter = 0;
        // starting at {1,1,1}

        auto config = iox::RouDiConfig_t().setDefaults();
        m_roudiMemoryManager = new IceOryxRouDiMemoryManager(config);
        m_roudiMemoryManager->createAndAnnounceMemory();
        m_portManager = new PortManagerTester(m_roudiMemoryManager);

        auto user = iox::posix::PosixUser::getUserOfCurrentProcess().getName();
        m_payloadMemoryManager =
            m_roudiMemoryManager->segmentManager().value()->getSegmentInformationForUser(user).m_memoryManager;

        // clearing the introspection, is not in d'tor -> SEGFAULT in delete sporadically
        m_portManager->stopPortIntrospection();
        m_portManager->deletePortsOfProcess(iox::roudi::IPC_CHANNEL_ROUDI_NAME);
    }

    void TearDown() override
    {
        delete m_portManager;
        delete m_roudiMemoryManager;
        iox::RelativePointer::unregisterAll();

        if (Test::HasFailure())
        {
            std::cout << testing::internal::GetCapturedStderr() << std::endl;
        }
        else
        {
            (void)testing::internal::GetCapturedStderr();
        }
    }
    iox::capro::ServiceDescription getUniqueSD()
    {
        m_eventIdCounter++;
        if (m_eventIdCounter == std::numeric_limits<uint16_t>::max())
        {
            m_eventIdCounter = 1U;
            m_instIdCounter++; // not using max (wildcard)
            if (m_instIdCounter == std::numeric_limits<uint16_t>::max())
            {
                m_instIdCounter = 1U;
                m_sIdCounter++;
                if (m_sIdCounter == std::numeric_limits<uint16_t>::max())
                {
                    // ASSERT_TRUE(false); // limits of test reached no more unique ids possible
                }
            }
        }
        return {m_sIdCounter, m_eventIdCounter, m_instIdCounter};
    }

    iox::cxx::GenericRAII m_uniqueRouDiId{[] { iox::popo::internal::setUniqueRouDiId(0); },
                                          [] { iox::popo::internal::unsetUniqueRouDiId(); }};

    void acquireMaxNumberOfEventVariables(
        const std::string& process,
        std::function<void(iox::popo::EventVariableData*)> f = std::function<void(iox::popo::EventVariableData*)>())
    {
        for (unsigned int i = 0U; i < iox::MAX_NUMBER_OF_EVENT_VARIABLES; i++)
        {
            iox::ProcessName_t newProcessName(iox::cxx::TruncateToCapacity, process + std::to_string(i));
            auto eventVariableDataResult = m_portManager->acquireEventVariableData(newProcessName);
            ASSERT_THAT(eventVariableDataResult.has_error(), Eq(false));
            if (f)
            {
                f(eventVariableDataResult.value());
            }
        }
    }

    void acquireMaxNumberOfInterfaces(
        std::string processName,
        std::function<void(iox::popo::InterfacePortData*)> f = std::function<void(iox::popo::InterfacePortData*)>())
    {
        for (unsigned int i = 0; i < iox::MAX_INTERFACE_NUMBER; i++)
        {
            auto newProcessName = processName + std::to_string(i);
            auto interfacePort = m_portManager->acquireInterfacePortData(
                iox::capro::Interfaces::INTERNAL, iox::ProcessName_t(iox::cxx::TruncateToCapacity, newProcessName));
            ASSERT_NE(interfacePort, nullptr);
            if (f)
            {
                f(interfacePort);
            }
        }
    }

    void acquireMaxNumberOfApplications(
        std::string processName,
        std::function<void(iox::popo::ApplicationPortData*)> f = std::function<void(iox::popo::ApplicationPortData*)>())
    {
        for (unsigned int i = 0; i < iox::MAX_PROCESS_NUMBER; i++)
        {
            auto newProcessName = processName + std::to_string(i);
            auto applicationPort = m_portManager->acquireApplicationPortData(
                iox::ProcessName_t(iox::cxx::TruncateToCapacity, newProcessName));
            ASSERT_NE(applicationPort, nullptr);
            if (f)
            {
                f(applicationPort);
            }
        }
    }

    void acquireMaxNumberOfConditionVariables(std::string processName,
                                              std::function<void(iox::popo::ConditionVariableData*)> f =
                                                  std::function<void(iox::popo::ConditionVariableData*)>())
    {
        for (unsigned int i = 0; i < iox::MAX_NUMBER_OF_CONDITION_VARIABLES; i++)
        {
            auto newProcessName = processName + std::to_string(i);
            auto condVar = m_portManager->acquireConditionVariableData(
                iox::ProcessName_t(iox::cxx::TruncateToCapacity, newProcessName));
            ASSERT_FALSE(condVar.has_error());
            if (f)
            {
                f(condVar.value());
            }
        }
    }

    void
    acquireMaxNumberOfNodes(std::string nodeName,
                            std::string processName,
                            std::function<void(iox::runtime::NodeData*, iox::NodeName_t, iox::ProcessName_t)> f =
                                std::function<void(iox::runtime::NodeData*, iox::NodeName_t, iox::ProcessName_t)>())
    {
        for (unsigned int i = 0U; i < iox::MAX_NODE_NUMBER; i++)
        {
            iox::ProcessName_t newProcessName(iox::cxx::TruncateToCapacity, processName + std::to_string(i));
            iox::NodeName_t newNodeName(iox::cxx::TruncateToCapacity, nodeName + std::to_string(i));
            auto node = m_portManager->acquireNodeData(newProcessName, newNodeName);
            ASSERT_FALSE(node.has_error());
            if (f)
            {
                f(node.value(), newNodeName, newProcessName);
            }
        }
    }
};

template <typename vector>
void setDestroyFlagAndClearContainer(vector& container)
{
    for (auto& item : container)
    {
        item->m_toBeDestroyed.store(true, std::memory_order_relaxed);
    }
    container.clear();
}

TEST_F(PortManager_test, DoDiscoveryWithSingleShotPublisherFirst)
{
    PublisherOptions publisherOptions{1U, iox::NodeName_t("node"), false};
    SubscriberOptions subscriberOptions{1U, 1U, iox::NodeName_t("node"), false};

    PublisherPortUser publisher(
        m_portManager
            ->acquirePublisherPortData(
                {1U, 1U, 1U}, publisherOptions, "guiseppe", m_payloadMemoryManager, PortConfigInfo())
            .value());
    ASSERT_TRUE(publisher);
    publisher.offer();
    // no doDiscovery() at this position is intentional

    SubscriberPortUser subscriber(
        m_portManager->acquireSubscriberPortData({1U, 1U, 1U}, subscriberOptions, "schlomo", PortConfigInfo()).value());
    ASSERT_TRUE(subscriber);
    subscriber.subscribe();

    m_portManager->doDiscovery();

    ASSERT_TRUE(publisher.hasSubscribers());
    EXPECT_THAT(subscriber.getSubscriptionState(), Eq(iox::SubscribeState::SUBSCRIBED));
}

TEST_F(PortManager_test, DoDiscoveryWithSingleShotSubscriberFirst)
{
    PublisherOptions publisherOptions{1U, iox::NodeName_t("node"), false};
    SubscriberOptions subscriberOptions{1U, 1U, iox::NodeName_t("node"), false};

    SubscriberPortUser subscriber(
        m_portManager->acquireSubscriberPortData({1U, 1U, 1U}, subscriberOptions, "schlomo", PortConfigInfo()).value());
    ASSERT_TRUE(subscriber);
    subscriber.subscribe();
    // no doDiscovery() at this position is intentional

    PublisherPortUser publisher(
        m_portManager
            ->acquirePublisherPortData(
                {1U, 1U, 1U}, publisherOptions, "guiseppe", m_payloadMemoryManager, PortConfigInfo())
            .value());
    ASSERT_TRUE(publisher);
    publisher.offer();

    m_portManager->doDiscovery();

    ASSERT_TRUE(publisher.hasSubscribers());
    EXPECT_THAT(subscriber.getSubscriptionState(), Eq(iox::SubscribeState::SUBSCRIBED));
}

TEST_F(PortManager_test, DoDiscoveryWithDiscoveryLoopInBetweenCreationOfSubscriberAndPublisher)
{
    PublisherOptions publisherOptions{1U, iox::NodeName_t("node"), false};
    SubscriberOptions subscriberOptions{1U, 1U, iox::NodeName_t("node"), false};

    SubscriberPortUser subscriber(
        m_portManager->acquireSubscriberPortData({1U, 1U, 1U}, subscriberOptions, "schlomo", PortConfigInfo()).value());
    ASSERT_TRUE(subscriber);
    subscriber.subscribe();
    m_portManager->doDiscovery();

    PublisherPortUser publisher(
        m_portManager
            ->acquirePublisherPortData(
                {1U, 1U, 1U}, publisherOptions, "guiseppe", m_payloadMemoryManager, PortConfigInfo())
            .value());
    ASSERT_TRUE(publisher);
    publisher.offer();

    m_portManager->doDiscovery();

    ASSERT_TRUE(publisher.hasSubscribers());
    EXPECT_THAT(subscriber.getSubscriptionState(), Eq(iox::SubscribeState::SUBSCRIBED));
}

TEST_F(PortManager_test, DoDiscoveryWithSubscribersCreatedBeforeAndAfterCreationOfPublisher)
{
    PublisherOptions publisherOptions{1U, iox::NodeName_t("node"), false};
    SubscriberOptions subscriberOptions{1U, 1U, iox::NodeName_t("node"), false};

    SubscriberPortUser subscriber1(
        m_portManager->acquireSubscriberPortData({1U, 1U, 1U}, subscriberOptions, "schlomo", PortConfigInfo()).value());
    ASSERT_TRUE(subscriber1);
    subscriber1.subscribe();

    m_portManager->doDiscovery();

    PublisherPortUser publisher(
        m_portManager
            ->acquirePublisherPortData(
                {1U, 1U, 1U}, publisherOptions, "guiseppe", m_payloadMemoryManager, PortConfigInfo())
            .value());
    ASSERT_TRUE(publisher);
    publisher.offer();

    SubscriberPortUser subscriber2(
        m_portManager->acquireSubscriberPortData({1U, 1U, 1U}, subscriberOptions, "ingnatz", PortConfigInfo()).value());
    ASSERT_TRUE(subscriber2);
    subscriber2.subscribe();

    m_portManager->doDiscovery();

    ASSERT_TRUE(publisher.hasSubscribers());
    EXPECT_THAT(subscriber1.getSubscriptionState(), Eq(iox::SubscribeState::SUBSCRIBED));
    EXPECT_THAT(subscriber2.getSubscriptionState(), Eq(iox::SubscribeState::SUBSCRIBED));
}

TEST_F(PortManager_test, SubscribeOnCreateSubscribesWithoutDiscoveryLoopWhenPublisherAvailable)
{
    PublisherOptions publisherOptions{1U, iox::NodeName_t("node"), false};
    SubscriberOptions subscriberOptions{1U, 1U, iox::NodeName_t("node"), true};
    PublisherPortUser publisher(
        m_portManager
            ->acquirePublisherPortData(
                {1U, 1U, 1U}, publisherOptions, "guiseppe", m_payloadMemoryManager, PortConfigInfo())
            .value());
    publisher.offer();
    m_portManager->doDiscovery();

    SubscriberPortUser subscriber(
        m_portManager->acquireSubscriberPortData({1U, 1U, 1U}, subscriberOptions, "schlomo", PortConfigInfo()).value());

    ASSERT_TRUE(publisher.hasSubscribers());
    EXPECT_THAT(subscriber.getSubscriptionState(), Eq(iox::SubscribeState::SUBSCRIBED));
}

TEST_F(PortManager_test, OfferOnCreateSubscribesWithoutDiscoveryLoopWhenSubscriberAvailable)
{
    PublisherOptions publisherOptions{1U, iox::NodeName_t("node"), true};
    SubscriberOptions subscriberOptions{1U, 1U, iox::NodeName_t("node"), false};
    SubscriberPortUser subscriber(
        m_portManager->acquireSubscriberPortData({1U, 1U, 1U}, subscriberOptions, "schlomo", PortConfigInfo()).value());
    subscriber.subscribe();
    m_portManager->doDiscovery();

    PublisherPortUser publisher(
        m_portManager
            ->acquirePublisherPortData(
                {1U, 1U, 1U}, publisherOptions, "guiseppe", m_payloadMemoryManager, PortConfigInfo())
            .value());

    ASSERT_TRUE(publisher.hasSubscribers());
    EXPECT_THAT(subscriber.getSubscriptionState(), Eq(iox::SubscribeState::SUBSCRIBED));
}

TEST_F(PortManager_test, OfferOnCreateAndSubscribeOnCreateNeedsNoMoreDiscoveryLoopSubscriberFirst)
{
    PublisherOptions publisherOptions{1U, iox::NodeName_t("node"), true};
    SubscriberOptions subscriberOptions{1U, 1U, iox::NodeName_t("node"), true};
    SubscriberPortUser subscriber(
        m_portManager->acquireSubscriberPortData({1U, 1U, 1U}, subscriberOptions, "schlomo", PortConfigInfo()).value());

    PublisherPortUser publisher(
        m_portManager
            ->acquirePublisherPortData(
                {1U, 1U, 1U}, publisherOptions, "guiseppe", m_payloadMemoryManager, PortConfigInfo())
            .value());

    ASSERT_TRUE(publisher.hasSubscribers());
    EXPECT_THAT(subscriber.getSubscriptionState(), Eq(iox::SubscribeState::SUBSCRIBED));
}

TEST_F(PortManager_test, OfferOnCreateAndSubscribeOnCreateNeedsNoMoreDiscoveryLoopPublisherFirst)
{
    PublisherOptions publisherOptions{1U, iox::NodeName_t("node"), true};
    SubscriberOptions subscriberOptions{1U, 1U, iox::NodeName_t("node"), true};
    PublisherPortUser publisher(
        m_portManager
            ->acquirePublisherPortData(
                {1U, 1U, 1U}, publisherOptions, "guiseppe", m_payloadMemoryManager, PortConfigInfo())
            .value());

    SubscriberPortUser subscriber(
        m_portManager->acquireSubscriberPortData({1U, 1U, 1U}, subscriberOptions, "schlomo", PortConfigInfo()).value());


    ASSERT_TRUE(publisher.hasSubscribers());
    EXPECT_THAT(subscriber.getSubscriptionState(), Eq(iox::SubscribeState::SUBSCRIBED));
}


TEST_F(PortManager_test, AcquiringOneMoreThanMaximumNumberOfPublishersFails)
{
    iox::ProcessName_t processName = "test1";
    PublisherOptions publisherOptions{1U, iox::NodeName_t("run1")};

    for (unsigned int i = 0; i < iox::MAX_PUBLISHERS; i++)
    {
        auto publisherPortDataResult = m_portManager->acquirePublisherPortData(
            getUniqueSD(), publisherOptions, processName, m_payloadMemoryManager, PortConfigInfo());

        ASSERT_FALSE(publisherPortDataResult.has_error());
    }

    { // test if overflow errors get hit

        bool errorHandlerCalled = false;
        auto errorHandlerGuard = iox::ErrorHandler::SetTemporaryErrorHandler(
            [&errorHandlerCalled](const iox::Error error [[gnu::unused]],
                                  const std::function<void()>,
                                  const iox::ErrorLevel) { errorHandlerCalled = true; });

        auto publisherPortDataResult = m_portManager->acquirePublisherPortData(
            getUniqueSD(), publisherOptions, processName, m_payloadMemoryManager, PortConfigInfo());
        EXPECT_TRUE(errorHandlerCalled);
        ASSERT_TRUE(publisherPortDataResult.has_error());
        EXPECT_THAT(publisherPortDataResult.get_error(), Eq(PortPoolError::PUBLISHER_PORT_LIST_FULL));
    }
}

TEST_F(PortManager_test, AcquiringOneMoreThanMaximumNumberOfSubscribersFails)
{
    iox::ProcessName_t processName1 = "test1";
    SubscriberOptions subscriberOptions{1U, 1U, iox::NodeName_t("run1")};

    for (unsigned int i = 0; i < iox::MAX_SUBSCRIBERS; i++)
    {
        auto subscriberPortDataResult =
            m_portManager->acquireSubscriberPortData(getUniqueSD(), subscriberOptions, processName1, PortConfigInfo());
        ASSERT_FALSE(subscriberPortDataResult.has_error());
    }

    { // test if overflow errors get hit

        bool errorHandlerCalled = false;
        auto errorHandlerGuard = iox::ErrorHandler::SetTemporaryErrorHandler(
            [&errorHandlerCalled](const iox::Error error [[gnu::unused]],
                                  const std::function<void()>,
                                  const iox::ErrorLevel) { errorHandlerCalled = true; });
        auto subscriberPortDataResult =
            m_portManager->acquireSubscriberPortData(getUniqueSD(), subscriberOptions, processName1, PortConfigInfo());
        EXPECT_TRUE(errorHandlerCalled);
        EXPECT_THAT(subscriberPortDataResult.get_error(), Eq(PortPoolError::SUBSCRIBER_PORT_LIST_FULL));
    }
}

TEST_F(PortManager_test, AcquiringOneMoreThanMaximumNumberOfInterfacesFails)
{
    std::string processName = "itf";

    // first aquire all possible Interfaces
    acquireMaxNumberOfInterfaces(processName);

    // test if overflow errors get hit
    {
        auto errorHandlerCalled{false};
        auto errorHandlerGuard = iox::ErrorHandler::SetTemporaryErrorHandler(
            [&errorHandlerCalled](const iox::Error, const std::function<void()>, const iox::ErrorLevel) {
                errorHandlerCalled = true;
            });

        auto interfacePort = m_portManager->acquireInterfacePortData(iox::capro::Interfaces::INTERNAL, "itfPenguin");
        EXPECT_EQ(interfacePort, nullptr);
        EXPECT_TRUE(errorHandlerCalled);
    }
}

TEST_F(PortManager_test, DeleteInterfacePortfromMaximumNumberAndAddOneIsSuccessful)
{
    std::string processName = "itf";

    // first aquire all possible Interfaces
    acquireMaxNumberOfInterfaces(processName);

    // delete one and add one should be possible now
    {
        unsigned int testi = 0;
        auto newProcessName = processName + std::to_string(testi);
        // this is done because there is no removeInterfaceData method in the PortManager class
        m_portManager->deletePortsOfProcess(iox::ProcessName_t(iox::cxx::TruncateToCapacity, newProcessName));

        auto interfacePort = m_portManager->acquireInterfacePortData(
            iox::capro::Interfaces::INTERNAL, iox::ProcessName_t(iox::cxx::TruncateToCapacity, newProcessName));
        EXPECT_NE(interfacePort, nullptr);
    }
}

TEST_F(PortManager_test, AcquireInterfacePortDataAfterDestroyingPreviouslyAcquiredOnesIsSuccessful)
{
    std::vector<iox::popo::InterfacePortData*> interfaceContainer;
    std::string processName = "itf";

    // first aquire all possible interfaces
    acquireMaxNumberOfInterfaces(processName, [&](auto interafcePort) { interfaceContainer.push_back(interafcePort); });

    // set the destroy flag and let the discovery loop take care
    setDestroyFlagAndClearContainer(interfaceContainer);
    m_portManager->doDiscovery();

    // so we should able to get some more now
    acquireMaxNumberOfInterfaces(processName);
}

TEST_F(PortManager_test, AcquiringOneMoreThanMaximumNumberOfApplicationsFails)
{
    std::string processName = "app";

    // first aquire all possible applications
    acquireMaxNumberOfApplications(processName);

    // test if overflow errors get hit
    {
        auto errorHandlerCalled{false};
        auto errorHandlerGuard = iox::ErrorHandler::SetTemporaryErrorHandler(
            [&errorHandlerCalled](const iox::Error, const std::function<void()>, const iox::ErrorLevel) {
                errorHandlerCalled = true;
            });

        auto appPort = m_portManager->acquireApplicationPortData("appPenguin");
        EXPECT_EQ(appPort, nullptr);
        EXPECT_TRUE(errorHandlerCalled);
    }
}

TEST_F(PortManager_test, DeleteApplicationPortfromMaximumNumberAndAddOneIsSuccessful)
{
    std::string processName = "app";

    // first aquire all possible applications
    acquireMaxNumberOfApplications(processName);

    // delete one and add one should be possible now
    {
        unsigned int testi = 0;
        auto newprocessName = processName + std::to_string(testi);
        // this is done because there is no removeApplicationData method in the PortManager class
        m_portManager->deletePortsOfProcess(iox::ProcessName_t(iox::cxx::TruncateToCapacity, newprocessName));

        auto appPort =
            m_portManager->acquireApplicationPortData(iox::ProcessName_t(iox::cxx::TruncateToCapacity, newprocessName));
        EXPECT_NE(appPort, nullptr);
    }
}

TEST_F(PortManager_test, AcquireApplicationPortAfterDestroyingPreviouslyAcquiredOnesIsSuccessful)
{
    std::vector<iox::popo::ApplicationPortData*> appContainer;

    std::string processName = "app";

    // first aquire all possible applications
    acquireMaxNumberOfApplications(processName, [&](auto appPort) { appContainer.push_back(appPort); });

    // set the destroy flag and let the discovery loop take care
    setDestroyFlagAndClearContainer(appContainer);
    m_portManager->doDiscovery();

    // so we should able to get some more now
    acquireMaxNumberOfApplications(processName);
}

TEST_F(PortManager_test, AcquiringOneMoreThanMaximumNumberOfConditionVariablesFails)
{
    std::string processName = "HypnoToadForEver";

    // first aquire all possible condition variables
    acquireMaxNumberOfConditionVariables(processName);

    // test if overflow errors get hit
    {
        auto errorHandlerCalled{false};
        auto errorHandlerGuard = iox::ErrorHandler::SetTemporaryErrorHandler(
            [&errorHandlerCalled](const iox::Error, const std::function<void()>, const iox::ErrorLevel) {
                errorHandlerCalled = true;
            });

        auto conditionVariableResult = m_portManager->acquireConditionVariableData("AnotherToad");
        EXPECT_TRUE(conditionVariableResult.has_error());
        EXPECT_TRUE(errorHandlerCalled);
        EXPECT_THAT(conditionVariableResult.get_error(), Eq(PortPoolError::CONDITION_VARIABLE_LIST_FULL));
    }
}

TEST_F(PortManager_test, DeleteConditionVariablePortfromMaximumNumberAndAddOneIsSuccessful)
{
    std::string processName = "HypnoToadForEver";

    // first aquire all possible condition variables
    acquireMaxNumberOfConditionVariables(processName);

    // delete one and add one should be possible now
    {
        unsigned int testi = 0;
        auto newProcessName = processName + std::to_string(testi);
        // this is done because there is no removeConditionVariableData method in the PortManager class
        m_portManager->deletePortsOfProcess(iox::ProcessName_t(iox::cxx::TruncateToCapacity, newProcessName));

        auto conditionVariableResult = m_portManager->acquireConditionVariableData(
            iox::ProcessName_t(iox::cxx::TruncateToCapacity, newProcessName));
        EXPECT_FALSE(conditionVariableResult.has_error());
    }
}

TEST_F(PortManager_test, AcquireConditionVariablesDataAfterDestroyingPreviouslyAcquiredOnesIsSuccessful)
{
    std::vector<iox::popo::ConditionVariableData*> condVarContainer;

    std::string processName = "HypnoToadForEver";

    // first aquire all possible condition variables
    acquireMaxNumberOfConditionVariables(processName, [&](auto condVar) { condVarContainer.push_back(condVar); });

    // set the destroy flag and let the discovery loop take care
    setDestroyFlagAndClearContainer(condVarContainer);
    m_portManager->doDiscovery();

    // so we should able to get some more now
    acquireMaxNumberOfConditionVariables(processName);
}

TEST_F(PortManager_test, AcquiringMaximumNumberOfEventVariablesWorks)
{
    std::string process = "BuddyHolly";

    acquireMaxNumberOfEventVariables(process);
}

TEST_F(PortManager_test, AcquiringOneMoreThanMaximumNumberOfEventVariableFails)
{
    std::string process = "BuddyHollysBrille";

    // first acquire all possible event variables
    acquireMaxNumberOfEventVariables(process);

    // test if overflow errors get hit
    auto errorHandlerCalled{false};
    auto errorHandlerGuard = iox::ErrorHandler::SetTemporaryErrorHandler(
        [&errorHandlerCalled](const iox::Error, const std::function<void()>, const iox::ErrorLevel) {
            errorHandlerCalled = true;
        });

    auto eventVariableDataResult = m_portManager->acquireEventVariableData("AnotherBrille");
    EXPECT_THAT(eventVariableDataResult.has_error(), Eq(true));
    EXPECT_THAT(errorHandlerCalled, Eq(true));
    EXPECT_THAT(eventVariableDataResult.get_error(), Eq(PortPoolError::EVENT_VARIABLE_LIST_FULL));
}

TEST_F(PortManager_test, DeletingEventVariableWorks)
{
    std::string process = "BudSpencer";

    // first acquire all possible event variables
    acquireMaxNumberOfEventVariables(process);

    // delete one and add one eventVariableDataResult should be possible now
    unsigned int i = 0U;
    iox::ProcessName_t newProcessName(iox::cxx::TruncateToCapacity, process + std::to_string(i));
    m_portManager->deletePortsOfProcess(newProcessName);

    auto eventVariableDataResult = m_portManager->acquireEventVariableData(newProcessName);
    EXPECT_THAT(eventVariableDataResult.has_error(), Eq(false));
}

TEST_F(PortManager_test, DestroyEventVariableAndAddNewOneSucceeds)
{
    iox::ProcessName_t process = "Terence Hill";
    std::vector<iox::popo::EventVariableData*> eventVariableContainer;

    // first acquire all possible event variables
    acquireMaxNumberOfEventVariables(
        process, [&](auto eventVariableData) { eventVariableContainer.push_back(eventVariableData); });

    setDestroyFlagAndClearContainer(eventVariableContainer);
    m_portManager->doDiscovery();

    // we should be able to get some more now
    acquireMaxNumberOfEventVariables(process);
}

TEST_F(PortManager_test, AcquiringMaximumNumberOfNodesWorks)
{
    std::string processName = "Process";
    std::string nodeName = iox::NodeName_t("node");

    acquireMaxNumberOfNodes(nodeName, processName, [&](auto node, auto newNodeName, auto newProcessName) {
        EXPECT_THAT(node->m_node, StrEq(newNodeName));
        EXPECT_THAT(node->m_process, StrEq(newProcessName));
    });
}

TEST_F(PortManager_test, AcquiringOneMoreThanMaximumNumberOfNodesFails)
{
    std::string processName = "Process";
    std::string nodeName = iox::NodeName_t("node");

    // first acquire all possible NodeData
    acquireMaxNumberOfNodes(nodeName, processName);

    // test if overflow errors get hit
    auto errorHandlerCalled{false};
    auto errorHandlerGuard = iox::ErrorHandler::SetTemporaryErrorHandler(
        [&errorHandlerCalled](const iox::Error, const std::function<void()>, const iox::ErrorLevel) {
            errorHandlerCalled = true;
        });

    auto nodeResult = m_portManager->acquireNodeData("AnotherProcess", "AnotherNode");
    EXPECT_THAT(nodeResult.has_error(), Eq(true));
    EXPECT_THAT(errorHandlerCalled, Eq(true));
    EXPECT_THAT(nodeResult.get_error(), Eq(PortPoolError::NODE_DATA_LIST_FULL));
}

TEST_F(PortManager_test, DeleteNodePortfromMaximumNumberandAddOneIsSuccessful)
{
    std::string processName = "Process";
    std::string nodeName = iox::NodeName_t("node");

    // first acquire all possible NodeData
    acquireMaxNumberOfNodes(nodeName, processName);

    // delete one and add one NodeData should be possible now
    unsigned int i = 0U;
    iox::ProcessName_t newProcessName(iox::cxx::TruncateToCapacity, processName + std::to_string(i));
    iox::NodeName_t newNodeName(iox::cxx::TruncateToCapacity, nodeName + std::to_string(i));
    // this is done because there is no removeNodeData method in the PortManager class
    m_portManager->deletePortsOfProcess(newProcessName);

    auto nodeResult = m_portManager->acquireNodeData(newProcessName, newNodeName);
    ASSERT_THAT(nodeResult.has_error(), Eq(false));
    EXPECT_THAT(nodeResult.value()->m_node, StrEq(newNodeName));
    EXPECT_THAT(nodeResult.value()->m_process, StrEq(newProcessName));
}


TEST_F(PortManager_test, AcquireNodeDataAfterDestroyingPreviouslyAcquiredOnesIsSuccessful)
{
    iox::ProcessName_t processName = "Humuhumunukunukuapua'a";
    iox::NodeName_t nodeName = "Taumatawhakatangihangakoauauotamateaturipukakapikimaungahoronukupokaiwhenuakitanatahu";
    std::vector<iox::runtime::NodeData*> nodeContainer;

    // first acquire all possible NodeData
    acquireMaxNumberOfNodes(
        nodeName, processName, [&](auto node, auto newNodeName [[gnu::unused]], auto newProcessName [[gnu::unused]]) {
            nodeContainer.push_back(node);
        });

    // set the destroy flag and let the discovery loop take care
    setDestroyFlagAndClearContainer(nodeContainer);
    m_portManager->doDiscovery();

    // so we should be able to get some more now
    acquireMaxNumberOfNodes(nodeName, processName);
}

TEST_F(PortManager_test, PortsDestroyInProcess2ChangeStatesOfPortsInProcess1)
{
    iox::ProcessName_t processName1 = "myProcess1";
    iox::ProcessName_t processName2 = "myProcess2";
    iox::capro::ServiceDescription cap1(1, 1, 1);
    iox::capro::ServiceDescription cap2(2, 2, 2);
    PublisherOptions publisherOptions{1U, iox::NodeName_t("node"), false};
    SubscriberOptions subscriberOptions{1U, 1U, iox::NodeName_t("node"), false};

    // two processes process1 and process2 each with a publisher and subscriber that match to the other process
    auto publisherData1 =
        m_portManager
            ->acquirePublisherPortData(cap1, publisherOptions, processName1, m_payloadMemoryManager, PortConfigInfo())
            .value();
    auto subscriberData1 =
        m_portManager->acquireSubscriberPortData(cap2, subscriberOptions, processName1, PortConfigInfo()).value();

    auto publisherData2 =
        m_portManager
            ->acquirePublisherPortData(cap2, publisherOptions, processName2, m_payloadMemoryManager, PortConfigInfo())
            .value();
    auto subscriberData2 =
        m_portManager->acquireSubscriberPortData(cap1, subscriberOptions, processName2, PortConfigInfo()).value();

    // let them connect
    {
        PublisherPortUser publisher1(publisherData1);
        ASSERT_TRUE(publisher1);
        publisher1.offer();
        SubscriberPortUser subscriber1(subscriberData1);
        ASSERT_TRUE(subscriber1);
        subscriber1.subscribe();

        PublisherPortUser publisher2(publisherData2);
        ASSERT_TRUE(publisher2);
        publisher2.offer();
        SubscriberPortUser subscriber2(subscriberData2);
        ASSERT_TRUE(subscriber2);
        subscriber2.subscribe();

        m_portManager->doDiscovery();

        ASSERT_TRUE(publisher1.hasSubscribers());
        ASSERT_TRUE(publisher2.hasSubscribers());
        EXPECT_THAT(subscriber1.getSubscriptionState(), Eq(iox::SubscribeState::SUBSCRIBED));
        EXPECT_THAT(subscriber2.getSubscriptionState(), Eq(iox::SubscribeState::SUBSCRIBED));
    }

    // destroy the ports of process2 and check if states of ports in process1 changed as expected
    {
        PublisherPortUser publisher1(publisherData1);
        ASSERT_TRUE(publisher1);
        SubscriberPortUser subscriber1(subscriberData1);
        ASSERT_TRUE(subscriber1);

        PublisherPortUser publisher2(publisherData2);
        ASSERT_TRUE(publisher2);
        publisher2.destroy();
        SubscriberPortUser subscriber2(subscriberData2);
        ASSERT_TRUE(subscriber2);
        subscriber2.destroy();

        m_portManager->doDiscovery();

        ASSERT_FALSE(publisher1.hasSubscribers());
        if (std::is_same<iox::build::CommunicationPolicy, iox::build::OneToManyPolicy>::value)
        {
            EXPECT_THAT(subscriber1.getSubscriptionState(), Eq(iox::SubscribeState::WAIT_FOR_OFFER));
        }
    }

    // re-create the ports of process processName2
    publisherData2 =
        m_portManager
            ->acquirePublisherPortData(cap2, publisherOptions, processName2, m_payloadMemoryManager, PortConfigInfo())
            .value();
    subscriberData2 =
        m_portManager->acquireSubscriberPortData(cap1, subscriberOptions, processName2, PortConfigInfo()).value();

    // let them connect
    {
        PublisherPortUser publisher1(publisherData1);
        ASSERT_TRUE(publisher1);
        SubscriberPortUser subscriber1(subscriberData1);
        ASSERT_TRUE(subscriber1);

        PublisherPortUser publisher2(publisherData2);
        ASSERT_TRUE(publisher2);
        publisher2.offer();
        SubscriberPortUser subscriber2(subscriberData2);
        ASSERT_TRUE(subscriber2);
        subscriber2.subscribe();

        m_portManager->doDiscovery();

        ASSERT_TRUE(publisher1.hasSubscribers());
        ASSERT_TRUE(publisher2.hasSubscribers());
        EXPECT_THAT(subscriber1.getSubscriptionState(), Eq(iox::SubscribeState::SUBSCRIBED));
        EXPECT_THAT(subscriber2.getSubscriptionState(), Eq(iox::SubscribeState::SUBSCRIBED));
    }

    // cleanup process process2 and check if states of ports in process1 changed  as expected
    {
        m_portManager->deletePortsOfProcess(processName2);
        PublisherPortUser publisher1(publisherData1);
        ASSERT_TRUE(publisher1);
        SubscriberPortUser subscriber1(subscriberData1);
        ASSERT_TRUE(subscriber1);

        ASSERT_FALSE(publisher1.hasSubscribers());
        if (std::is_same<iox::build::CommunicationPolicy, iox::build::OneToManyPolicy>::value)
        {
            EXPECT_THAT(subscriber1.getSubscriptionState(), Eq(iox::SubscribeState::WAIT_FOR_OFFER));
        }
    }
}

TEST_F(PortManager_test, OfferPublisherServiceUpdatesServiceRegistryChangeCounter)
{
    auto serviceCounter = m_portManager->serviceRegistryChangeCounter();
    ASSERT_NE(serviceCounter, nullptr);

    auto initialCount = serviceCounter->load();
    PublisherOptions publisherOptions{1};

    auto publisherPortData = m_portManager->acquirePublisherPortData(
        {1U, 1U, 1U}, publisherOptions, m_ProcessName, m_payloadMemoryManager, PortConfigInfo());
    ASSERT_FALSE(publisherPortData.has_error());

    PublisherPortUser publisher(publisherPortData.value());

    publisher.offer();
    m_portManager->doDiscovery();

    EXPECT_EQ(serviceCounter->load(), initialCount + 1);
}
