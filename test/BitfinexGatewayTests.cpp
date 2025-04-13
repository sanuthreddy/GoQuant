#include <gtest/gtest.h>
#include "../include/Gateway.h"
#include "../test/SeedData.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <memory>
#include <random>

namespace singular::gateway::bitfinex {

// Test fixture for the Gateway class
class BitfinexGatewayTest : public ::testing::Test {
protected:
    std::unique_ptr<Gateway> gateway_;

    void SetUp() override {
        std::shared_ptr<hv::EventLoop> executor = std::make_shared<hv::EventLoop>();
        gateway_ = std::make_unique<Gateway>(
            executor,
            true,
            "test_account",
            "test_key",
            "test_secret",
            "test_passphrase",
            "DEV"
        );
    }

    void TearDown() override {
    #ifdef TEST_BUILD
        gateway_->mock_generated_messages.clear(); // Clear mock storage after each test
    #endif
    }

    // Protected helper methods to access private members
    void setupOrderMappings(unsigned long client_id, const std::string& symbol) {
        gateway_->internal_to_client_id_map_[client_id] = client_id;
        gateway_->client_to_internal_id_map_[client_id] = client_id;
        gateway_->client_id_to_source_map_[client_id] = singular::types::RequestSource::RS_LIMIT_ORDER;
        gateway_->internal_id_symbol_map_[client_id] = symbol;
        gateway_->client_id_to_symbol_map_[client_id] = symbol;
    }

    void setupCancelMappings(singular::types::OrderId order_id, int client_id, 
                           const std::string& client_date, singular::types::RequestSource source) {
        gateway_->internal_to_client_id_map_[order_id] = client_id;
        gateway_->client_to_internal_id_map_[client_id] = order_id;
        gateway_->client_id_to_date_map_[client_id] = client_date;
        gateway_->client_id_to_source_map_[client_id] = source;
        gateway_->internal_id_symbol_map_[order_id] = "BTCUSD";
    }
};

// Test case for the do_place function
TEST_F(BitfinexGatewayTest, DoPlace_Validation) {
    // Use seed data to get randomized test cases
    auto seed_data = CreateSeedData();

    for (const auto& [test_name, data] : seed_data) {
        SCOPED_TRACE("Testing case: " + test_name);
        try {
            gateway_->mock_generated_messages.clear();  // Clear before test
            
            // Get random client ID from seed data
            unsigned long client_id = std::stoul(data.client_id);
            setupOrderMappings(client_id, data.symbol);

            // Place the order using seed data values
            gateway_->do_place(
                data.symbol,
                singular::types::InstrumentType::SPOT,
                client_id,
                data.order_type == "LIMIT" ? singular::types::OrderType::LIMIT : singular::types::OrderType::MARKET,
                data.side == "BUY" ? singular::types::Side::BUY : singular::types::Side::SELL,
                data.price,
                data.quantity,
                singular::types::RequestSource::RS_LIMIT_ORDER,
                "",
                ""
            );

            // Verify message was generated
            ASSERT_FALSE(gateway_->mock_generated_messages.empty()) 
                << "No message generated for test case: " << test_name;

            // Parse and validate the generated message
            std::string msg = gateway_->mock_generated_messages.back();
            auto message = nlohmann::json::parse(msg);
            auto expected = data.expected_output;
            
            // Verify message structure matches expected output
            EXPECT_EQ(expected.size(), message.size()) 
                << "Message size mismatch for test case: " << test_name;
            EXPECT_EQ(expected[1], message[1]) 
                << "Event type mismatch for test case: " << test_name;
            
            // Verify order details
            EXPECT_TRUE(message[3].contains("amount")) 
                << "Missing amount field for test case: " << test_name;
            EXPECT_TRUE(message[3].contains("symbol")) 
                << "Missing symbol field for test case: " << test_name;
            EXPECT_TRUE(message[3].contains("type")) 
                << "Missing type field for test case: " << test_name;

            // Compare actual vs expected values
            EXPECT_EQ(expected[3]["amount"], message[3]["amount"]);
            EXPECT_EQ(expected[3]["symbol"], message[3]["symbol"]);
            EXPECT_EQ(expected[3]["type"], message[3]["type"]);

        } catch (const std::exception& e) {
            FAIL() << "Test case " << test_name << " failed with exception: " << e.what();
        }
    }
}

// Test case for the do_cancel function
TEST_F(BitfinexGatewayTest, DoCancel_Validation) {
    auto seed_data = CreateCancelOrderSeedData();

    for (const auto& [name, data] : seed_data) {
        SCOPED_TRACE("Testing case: " + name);
        gateway_->mock_generated_messages.clear(); // Clear previous messages

        try {
            if (name == "VALID_ORDER") {
                // Setup all required mappings for valid order
                setupCancelMappings(data.order_id, data.client_id, data.client_date, data.source);
            }

            gateway_->do_cancel(data.order_id, data.source);

            if (name == "VALID_ORDER") {
                ASSERT_FALSE(gateway_->mock_generated_messages.empty())
                    << "Expected WebSocket message was not generated for valid order_id.";
                
                nlohmann::json actual_message = nlohmann::json::parse(gateway_->mock_generated_messages.back());
                ASSERT_EQ(data.expected_message.dump(), actual_message.dump())
                    << "Generated WebSocket message does not match the expected message."
                    << "\nExpected: " << data.expected_message.dump()
                    << "\nActual: " << actual_message.dump();
            } else {
                ASSERT_TRUE(gateway_->mock_generated_messages.empty())
                    << "Unexpected WebSocket message generated for invalid order_id.";
            }

        } catch (const std::exception& e) {
            FAIL() << "Test case " << name << " failed with exception: " << e.what();
        }
    }
}

TEST_F(BitfinexGatewayTest, DoCancel_InvalidOrderId) {
    auto seed_data = CreateCancelOrderSeedData();

    // Extract the invalid order_id test case
    const auto& data = seed_data["INVALID_ORDER"];
    SCOPED_TRACE("Testing invalid order_id");

    // Call the function under test
    EXPECT_NO_THROW(gateway_->do_cancel(data.order_id, data.source));

    // Validate no messages were generated
    ASSERT_TRUE(gateway_->mock_generated_messages.empty());

    // Validate the log message (if mock logging is captured)
    ASSERT_EQ(data.expected_log_message, "Order ID not found in internal_to_client_id_map.");
}

TEST_F(BitfinexGatewayTest, DoSubscribeOrderbooks_Validation) {
    auto seed_data_list = CreateSubscribeSeedData(); // Get the seed data with valid and edge cases

    for (const auto& seed_data : seed_data_list) {
        SCOPED_TRACE(seed_data.symbols.empty() ? "Testing empty symbols" : "Testing valid symbols");

        // Call the function under test
        std::vector<std::string> symbols = seed_data.symbols;
        EXPECT_NO_THROW(gateway_->do_subscribe_orderbooks(symbols));
        //EXPECT_NO_THROW(gateway_->do_subscribe_orderbooks(seed_data.symbols));

        if (!seed_data.symbols.empty()) {
            // Validate the generated messages
            ASSERT_EQ(gateway_->mock_generated_messages.size(), seed_data.expected_messages.size())
                << "Mismatch in the number of generated WebSocket messages";

            for (size_t i = 0; i < seed_data.expected_messages.size(); ++i) {
                nlohmann::json actual_message = nlohmann::json::parse(gateway_->mock_generated_messages[i]);
                ASSERT_EQ(seed_data.expected_messages[i].dump(), actual_message.dump())
                    << "Mismatch in WebSocket message for symbol index " << i;
            }

            // Validate logs directly from expected_logs
            ASSERT_EQ(seed_data.expected_logs.size(), seed_data.expected_messages.size())
                << "Mismatch between expected logs and messages";

            for (size_t i = 0; i < seed_data.expected_logs.size(); ++i) {
                SCOPED_TRACE("Validating log for symbol: " + seed_data.symbols[i]);
                std::string expected_log = seed_data.expected_logs[i];
                ASSERT_FALSE(expected_log.empty()) << "Expected log is empty for symbol index " << i;
            }
        } else {
            // Edge case: Empty symbols
            ASSERT_TRUE(gateway_->mock_generated_messages.empty())
                << "WebSocket messages were generated for empty symbols";
            ASSERT_TRUE(seed_data.expected_logs.empty())
                << "Logs were generated for empty symbols";
        }

        // Clear state for the next iteration
        gateway_->mock_generated_messages.clear();
    }
}

// Test randomized limit orders
TEST_F(BitfinexGatewayTest, DoPlace_RandomizedLimitOrders) {
    auto seed_data = CreateSeedData();  // Uses randomization from SeedData.cpp
    
    for (const auto& [name, data] : seed_data) {
        SCOPED_TRACE("Testing case: " + name);
        gateway_->mock_generated_messages.clear();

        try {
            setupOrderMappings(12345, data.symbol);
            
            gateway_->do_place(
                data.symbol,
                singular::types::InstrumentType::SPOT,
                12345,
                singular::types::OrderType::LIMIT,
                data.side == "BUY" ? singular::types::Side::BUY : singular::types::Side::SELL,
                data.price,
                data.quantity,
                singular::types::RequestSource::RS_LIMIT_ORDER,
                "",
                ""
            );

            ASSERT_FALSE(gateway_->mock_generated_messages.empty());
            auto message = nlohmann::json::parse(gateway_->mock_generated_messages.back());
            
            // Verify the message structure
            ASSERT_EQ(message.size(), 4);
            ASSERT_TRUE(message[3].is_object());
            ASSERT_EQ(message[1], "on");
            
            // Verify the order details
            EXPECT_EQ(message[3]["symbol"], "t" + data.symbol);
            EXPECT_EQ(message[3]["type"], "EXCHANGE LIMIT");
            
            // Convert amount to string with proper sign
            std::string expected_amount = (data.side == "SELL" ? "-" : "") + std::to_string(data.quantity);
            EXPECT_EQ(message[3]["amount"], expected_amount);
            EXPECT_EQ(message[3]["price"], std::to_string(data.price));

        } catch (const std::exception& e) {
            FAIL() << "Test case " << name << " failed with exception: " << e.what();
        }
    }
}

// Test market orders with random quantities
TEST_F(BitfinexGatewayTest, DoPlace_RandomizedMarketOrders) {
    std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<> qty_dist(0.1, 5.0);  // Random quantities between 0.1 and 5.0
    
    static const std::vector<std::string> test_symbols = {"BTCUSD", "ETHUSD", "SOLUSD"};
    
    for (const auto& symbol : test_symbols) {
        SCOPED_TRACE("Testing market order for " + symbol);
        gateway_->mock_generated_messages.clear();
        
        try {
            double quantity = std::round(qty_dist(gen) * 1000000) / 1000000;  // Round to 6 decimal places
            unsigned long client_id = 12345 + (&symbol - &test_symbols[0]);  // Unique client ID for each symbol
            
            setupOrderMappings(client_id, symbol);
            
            gateway_->do_place(
                symbol,
                singular::types::InstrumentType::SPOT,
                client_id,
                singular::types::OrderType::MARKET,
                singular::types::Side::BUY,  // Testing BUY side
                0.0,  // Price is 0 for market orders
                quantity,
                singular::types::RequestSource::RS_MARKET_ORDER,
                "",
                ""
            );

            ASSERT_FALSE(gateway_->mock_generated_messages.empty());
            auto message = nlohmann::json::parse(gateway_->mock_generated_messages.back());
            
            // Verify market order structure
            ASSERT_EQ(message.size(), 4);
            ASSERT_TRUE(message[3].is_object());
            ASSERT_EQ(message[1], "on");
            
            // Verify market order details
            EXPECT_EQ(message[3]["symbol"], "t" + symbol);
            EXPECT_EQ(message[3]["type"], "EXCHANGE MARKET");
            EXPECT_EQ(message[3]["amount"], std::to_string(quantity));
            EXPECT_FALSE(message[3].contains("price"));  // Market orders shouldn't include price

        } catch (const std::exception& e) {
            FAIL() << "Market order test for " << symbol << " failed with exception: " << e.what();
        }
    }
}

TEST_F(BitfinexGatewayTest, DoCancel_ByExchangeId_EdgeCases) {
    // Test with empty exchange order ID
    EXPECT_THROW({
        try {
            gateway_->do_cancel("", nullptr, singular::types::RequestSource::RS_LIMIT_ORDER);
        } catch (const std::invalid_argument& e) {
            // Verify the error message
            EXPECT_STREQ(e.what(), "stoll");
            throw;
        }
    }, std::invalid_argument);
}

TEST_F(BitfinexGatewayTest, DoCancel_NullInstrument) {
    // Test with invalid order ID format
    EXPECT_THROW({
        try {
            gateway_->do_cancel("tBTCUSD-1234-ABCD", nullptr, singular::types::RequestSource::RS_LIMIT_ORDER);
        } catch (const std::invalid_argument& e) {
            // Verify the error message
            EXPECT_STREQ(e.what(), "stoll");
            throw;
        }
    }, std::invalid_argument);
}

TEST_F(BitfinexGatewayTest, DoModify_Test) {
    // Create a simple test case with fixed values
    auto seed_data = singular::gateway::bitfinex::CreateModifyOrderSeedData();
    ASSERT_FALSE(seed_data.empty());
    auto test_case = seed_data[0];
    std::string symbol = test_case.symbol;
    singular::types::OrderId order_id = test_case.order_id;
    double new_quantity = test_case.new_quantity;
    double new_price = test_case.new_price;
    
    // Create instrument
    auto instrument = std::make_unique<singular::types::Instrument>(
        singular::types::Exchange::BITFINEX,
        symbol,
        singular::types::InstrumentType::SPOT,
        0.0, 0.0, 1.0, 0.0, 0.0, 0.0,
        "SPOT"
    );

    // Create order
    auto order = std::make_unique<singular::types::Order>(
        order_id,
        nullptr,
        instrument.get(),
        singular::types::OrderType::LIMIT,
        singular::types::Side::BUY,
        10000.0,  // Original price
        1.0,      // Original quantity
        symbol,
        ""
    );

    // Setup all required mappings
    setupOrderMappings(order_id, symbol);

    // Clear any existing messages
    gateway_->mock_generated_messages.clear();

    // Perform the modification
    EXPECT_NO_THROW(gateway_->do_modify(
        order.get(),
        new_quantity,
        new_price,
        singular::types::RequestSource::RS_LIMIT_ORDER
    ));

    // Verify the results
    ASSERT_FALSE(gateway_->mock_generated_messages.empty());
    auto message = nlohmann::json::parse(gateway_->mock_generated_messages.back());
    
    // Basic message structure verification
    ASSERT_TRUE(message.is_array());
    EXPECT_EQ(message[1], "on");  // New order command
    
    // Verify order details
    ASSERT_TRUE(message[3].is_object());
    EXPECT_EQ(message[3]["symbol"], "t" + symbol);
    EXPECT_EQ(message[3]["type"], "EXCHANGE LIMIT");
    EXPECT_EQ(message[3]["amount"], std::to_string(new_quantity));
    EXPECT_EQ(message[3]["price"], std::to_string(new_price));
}

TEST_F(BitfinexGatewayTest, DoUnsubscribeOrderbooks_Test) {
    auto seed_data = CreateUnsubscribeSeedData();
    ASSERT_FALSE(seed_data.empty());
    
    // Use first test case
    const auto& test_case = seed_data[0];
    
    // Clear any existing messages
    gateway_->mock_generated_messages.clear();

    // Convert string symbols to Symbol type
    std::vector<singular::types::Symbol> symbols;
    std::transform(test_case.symbols.begin(), test_case.symbols.end(),
                  std::back_inserter(symbols),
                  [](const std::string& s) { return s; });

    // Perform unsubscribe
    EXPECT_NO_THROW(gateway_->do_unsubscribe_orderbooks(symbols));

    // Verify results
    ASSERT_FALSE(gateway_->mock_generated_messages.empty());
    auto message = nlohmann::json::parse(gateway_->mock_generated_messages.back());
    
    // Verify message structure matches expected format from seed data
    EXPECT_EQ(message["event"], test_case.expected_messages[0]["event"]);
    EXPECT_TRUE(message.contains("chanId"));

    // Test empty symbols case using the last test case (which is empty by design)
    gateway_->mock_generated_messages.clear();
    std::vector<singular::types::Symbol> empty_symbols;
    EXPECT_NO_THROW(gateway_->do_unsubscribe_orderbooks(empty_symbols));
    EXPECT_TRUE(gateway_->mock_generated_messages.empty());
}

// Test for login_private function
TEST_F(BitfinexGatewayTest, LoginPrivate_Test) {
    gateway_->mock_generated_messages.clear();

    // Call login_private
    EXPECT_NO_THROW(gateway_->login_private());

    // Verify message was generated
    ASSERT_FALSE(gateway_->mock_generated_messages.empty());
    auto message = nlohmann::json::parse(gateway_->mock_generated_messages.back());

    // Verify message structure
    EXPECT_EQ(message["event"], "auth");
    EXPECT_EQ(message["apiKey"], "test_key");
    EXPECT_TRUE(message.contains("authNonce"));
    EXPECT_TRUE(message.contains("authPayload"));
    EXPECT_TRUE(message.contains("authSig"));
}

// Test for parse_websocket function with different message types
TEST_F(BitfinexGatewayTest, ParseWebsocket_Test) {
    // Test empty buffer
    EXPECT_NO_THROW(gateway_->parse_websocket(""));

    // Test info message
    std::string info_message = R"({"event":"info","version":2})";
    EXPECT_NO_THROW(gateway_->parse_websocket(info_message));

    // Test auth message
    std::string auth_message = R"({"event":"auth","status":"OK"})";
    EXPECT_NO_THROW(gateway_->parse_websocket(auth_message));

    // Test invalid JSON
    EXPECT_NO_THROW(gateway_->parse_websocket("invalid json"));
}

// Test for status function
TEST_F(BitfinexGatewayTest, Status_Test) {
    // Test initial status
    EXPECT_EQ(gateway_->status(), singular::types::GatewayStatus::OFFLINE);

    // We can't directly set private members, so test status through behavior
    // Login and verify status changes
    gateway_->login_private();
    auto message = nlohmann::json::parse(R"({"event":"auth","status":"OK"})");
    gateway_->parse_websocket(message.dump());
    
    // Status should now be ONLINE after successful auth
    EXPECT_EQ(gateway_->status(), singular::types::GatewayStatus::ONLINE);
}

// Test for purge function
TEST_F(BitfinexGatewayTest, Purge_Test) {
    // First login to set up an active state
    gateway_->login_private();
    auto auth_message = nlohmann::json::parse(R"({"event":"auth","status":"OK"})");
    gateway_->parse_websocket(auth_message.dump());
    
    // Verify we're in ONLINE state
    EXPECT_EQ(gateway_->status(), singular::types::GatewayStatus::ONLINE);

    // Clear messages before purge
    gateway_->mock_generated_messages.clear();

    // Call purge
    EXPECT_NO_THROW(gateway_->purge());

    // Verify state after purge
    EXPECT_EQ(gateway_->status(), singular::types::GatewayStatus::OFFLINE);

    // Verify no new messages were generated after purge
    EXPECT_TRUE(gateway_->mock_generated_messages.empty());
}

// Test for get_client_id function
TEST_F(BitfinexGatewayTest, GetClientId_Test) {
    singular::types::OrderId test_order_id = 12345;
    auto client_id = gateway_->get_client_id(test_order_id);
    
    // Verify client_id format
    EXPECT_GT(client_id, 0);
    EXPECT_EQ(client_id % 1000, (test_order_id % 1000) + 1);
}

// Test for convert_timestamp_to_string function
TEST_F(BitfinexGatewayTest, ConvertTimestampToString_Test) {
    long long test_timestamp = 1609459200000; // 2021-01-01 00:00:00 UTC
    std::string timestamp_str = gateway_->convert_timestamp_to_string(test_timestamp);
    
    // Verify timestamp format
    EXPECT_EQ(timestamp_str, "2021-01-01T00:00:00Z");
}

// Test for SubscribeTickersTest
TEST_F(BitfinexGatewayTest, SubscribeTickersTest) {
    auto seed_data = CreateSubscribeTickersSeedData();
    gateway_->mock_generated_messages.clear();

    // Get test symbols
    std::vector<singular::types::Symbol> test_symbols;
    for (const auto& [key, data] : seed_data) {
        test_symbols.push_back(data.symbol);
    }

    // Subscribe to tickers
    EXPECT_NO_THROW(gateway_->do_subscribe_tickers(test_symbols));

    // Verify messages were generated
    ASSERT_EQ(gateway_->mock_generated_messages.size(), test_symbols.size());

    // Verify each generated message matches expected format
    for (size_t i = 0; i < test_symbols.size(); i++) {
        const auto& symbol = test_symbols[i];
        const auto& expected_data = seed_data["SUBSCRIBE_" + symbol];
        
        auto generated_message = nlohmann::json::parse(gateway_->mock_generated_messages[i]);
        EXPECT_EQ(generated_message["event"], "subscribe");
        EXPECT_EQ(generated_message["channel"], "ticker");
        EXPECT_EQ(generated_message["symbol"], "t" + symbol);
    }
}

TEST_F(BitfinexGatewayTest, UnsubscribeTickersTest) {
    auto seed_data = CreateUnsubscribeTickersSeedData();
    gateway_->mock_generated_messages.clear();

    // Get test symbols
    std::vector<singular::types::Symbol> test_symbols;
    for (const auto& [key, data] : seed_data) {
        test_symbols.push_back(data.symbol);
    }

    // Unsubscribe from tickers
    EXPECT_NO_THROW(gateway_->do_unsubscribe_tickers(test_symbols));

    // Verify messages were generated
    ASSERT_EQ(gateway_->mock_generated_messages.size(), test_symbols.size());

    // Verify each generated message matches expected format
    for (size_t i = 0; i < test_symbols.size(); i++) {
        auto generated_message = nlohmann::json::parse(gateway_->mock_generated_messages[i]);
        EXPECT_EQ(generated_message["event"], "unsubscribe");
    }
}

TEST_F(BitfinexGatewayTest, SubscribeTradesTest) {
    auto seed_data = CreateSubscribeTradeSeedData();
    gateway_->mock_generated_messages.clear();

    // Get test symbols
    std::vector<singular::types::Symbol> test_symbols;
    for (const auto& [key, data] : seed_data) {
        test_symbols.push_back(data.symbol);
    }

    // Subscribe to trades
    EXPECT_NO_THROW(gateway_->do_subscribe_trades(test_symbols));

    // Verify messages were generated
    ASSERT_EQ(gateway_->mock_generated_messages.size(), test_symbols.size());

    // Verify each generated message matches expected format
    for (size_t i = 0; i < test_symbols.size(); i++) {
        const auto& symbol = test_symbols[i];
        const auto& expected_data = seed_data["SUBSCRIBE_" + symbol];
        
        auto generated_message = nlohmann::json::parse(gateway_->mock_generated_messages[i]);
        EXPECT_EQ(generated_message["event"], "subscribe");
        EXPECT_EQ(generated_message["channel"], "trades");
        EXPECT_EQ(generated_message["symbol"], "t" + symbol);
    }
}

TEST_F(BitfinexGatewayTest, UnsubscribeTradesTest) {
    auto seed_data = CreateUnsubscribeTradeSeedData();
    gateway_->mock_generated_messages.clear();

    // Get test symbols
    std::vector<singular::types::Symbol> test_symbols;
    for (const auto& [key, data] : seed_data) {
        test_symbols.push_back(data.symbol);
    }

    // Unsubscribe from trades
    EXPECT_NO_THROW(gateway_->do_unsubscribe_trades(test_symbols));

    // Verify messages were generated
    ASSERT_EQ(gateway_->mock_generated_messages.size(), test_symbols.size());

    // Verify each generated message matches expected format
    for (size_t i = 0; i < test_symbols.size(); i++) {
        auto generated_message = nlohmann::json::parse(gateway_->mock_generated_messages[i]);
        EXPECT_EQ(generated_message["event"], "unsubscribe");
    }
}

TEST_F(BitfinexGatewayTest, OrderExecutionQualityChannelStatusTest) {
    std::string test_session_id = "test_session_123";
    std::string test_credential_id = "test_cred_123";

    // Test setting status
    EXPECT_NO_THROW(gateway_->set_order_execution_quality_channel_status(test_session_id, test_credential_id));
    
    // Test unsetting status
    EXPECT_NO_THROW(gateway_->unset_order_execution_quality_channel_status(test_session_id));
}

TEST_F(BitfinexGatewayTest, SendAlgoExecutionStatusTest) {
    // Clear any existing messages
    gateway_->mock_generated_messages.clear();

    // Add test session
    gateway_->add_test_session("test_session");

    // Create test message
    nlohmann::json test_message = {
        {"message", "Test execution message"},
        {"is_initialized", true},
        {"request_source", "test_source"},
        {"algorithm_id", 12345},
        {"symbol", "BTCUSD"},
        {"is_completed", false}
    };

    // Test sending algo execution status directly
    EXPECT_NO_THROW(gateway_->send_algo_execution_status(test_message));
}

TEST_F(BitfinexGatewayTest, OrderChannelStatusTest) {
    std::string test_session_id = "test_session_123";
    std::string test_credential_id = "test_cred_123";

    // Test setting status
    EXPECT_NO_THROW(gateway_->set_order_channel_status(test_session_id, test_credential_id));
    
    // Test unsetting status
    EXPECT_NO_THROW(gateway_->unset_order_channel_status(test_session_id));
}

TEST_F(BitfinexGatewayTest, StreamOrderDataTest) {
    // Clear any existing messages
    gateway_->mock_generated_messages.clear();

    // Add test session
    gateway_->add_test_session("test_session");

    // Setup order mappings
    unsigned long client_id = 12345;
    setupOrderMappings(client_id, "BTCUSD");

    // Create a test order update message
    nlohmann::json order_update = {
        {"id", client_id},
        {"symbol", "tBTCUSD"},
        {"price", "50000.0"},
        {"quantity", "1.0"},
        {"type", "EXCHANGE MARKET"},
        {"status", "ACTIVE"}
    };

    // Test streaming order data
    EXPECT_NO_THROW(gateway_->stream_order_data(order_update, "received"));
}

TEST_F(BitfinexGatewayTest, SendRejectResponseTest) {
    // Clear any existing messages
    gateway_->mock_generated_messages.clear();

    // Add test session
    gateway_->add_test_session("test_session");

    // Setup order mappings before placing the order
    unsigned long client_id = 67890;
    setupOrderMappings(client_id, "BTCUSD");

    // First place an order to set up the internal mappings properly
    gateway_->do_place(
        "BTCUSD",
        singular::types::InstrumentType::SPOT,
        client_id,
        singular::types::OrderType::LIMIT,
        singular::types::Side::BUY,
        50000.0,
        1.0,
        singular::types::RequestSource::RS_LIMIT_ORDER,
        "",
        ""
    );

    // Create test reject message with proper structure
    nlohmann::json test_message = nlohmann::json::array();
    test_message.push_back("n");           // [0] notification type
    test_message.push_back("on-req");      // [1] order new request
    test_message.push_back("ERROR");       // [2] status
    test_message.push_back(nullptr);       // [3] placeholder

    // Create notify info array with all required fields
    nlohmann::json notify_info = nlohmann::json::array();
    notify_info.push_back(12345);          // [0] id
    notify_info.push_back(0);              // [1] gid
    notify_info.push_back(client_id);      // [2] cid (client id)
    notify_info.push_back("tBTCUSD");      // [3] symbol
    notify_info.push_back(0);              // [4] mts_create
    notify_info.push_back(0);              // [5] mts_update
    notify_info.push_back(1.0);            // [6] amount
    notify_info.push_back(1.0);            // [7] amount_orig
    notify_info.push_back("EXCHANGE LIMIT"); // [8] type
    notify_info.push_back(nullptr);        // [9] type_prev
    notify_info.push_back(nullptr);        // [10] mts_tif
    notify_info.push_back(nullptr);        // [11] placeholder
    notify_info.push_back(0);              // [12] flags
    notify_info.push_back("ACTIVE");       // [13] status
    notify_info.push_back(nullptr);        // [14] placeholder
    notify_info.push_back(nullptr);        // [15] placeholder
    notify_info.push_back(50000.0);        // [16] price
    notify_info.push_back(0);              // [17] price_avg
    notify_info.push_back(0);              // [18] price_trailing
    notify_info.push_back(0);              // [19] price_aux_limit
    notify_info.push_back(nullptr);        // [20] placeholder
    notify_info.push_back(nullptr);        // [21] placeholder
    notify_info.push_back(nullptr);        // [22] placeholder
    notify_info.push_back(0);              // [23] notify
    notify_info.push_back(0);              // [24] hidden
    notify_info.push_back(nullptr);        // [25] placed_id

    // Add notify info array to the message
    test_message.push_back(notify_info);   // [4] notify info array
    test_message.push_back(nullptr);       // [5] placeholder
    test_message.push_back(nullptr);       // [6] placeholder
    test_message.push_back("Test error");  // [7] error message

    // Clear messages before testing reject response
    gateway_->mock_generated_messages.clear();

    // Test sending reject response
    EXPECT_NO_THROW(gateway_->send_reject_response(test_message));

}

TEST_F(BitfinexGatewayTest, TestDoPlace) {
    for (int i = 0; i < 100; ++i) {
        // Clear previous messages
        gateway_->mock_generated_messages.clear();

        std::string symbol = getRandomSymbol();
        singular::types::OrderId order_id = getRandomOrderId();
        
        // Use the existing helper method from the test fixture
        setupOrderMappings(order_id, symbol);

        singular::types::InstrumentType type = singular::types::InstrumentType::SPOT;
        singular::types::OrderType order_type = singular::types::OrderType::MARKET;
        singular::types::Side side = getRandomSide();
        double price = getRandomPrice();
        double quantity = getRandomQuantity();
        singular::types::RequestSource source = singular::types::RequestSource::RS_MARKET_ORDER;
        std::string credential_id = "test_cred";
        std::string td_mode = "test_mode";

        EXPECT_NO_THROW(gateway_->do_place(
            symbol, type, order_id, order_type, side, 
            price, quantity, source, credential_id, td_mode
        ));

        // Verify a message was generated
        ASSERT_FALSE(gateway_->mock_generated_messages.empty());
        
        // Verify the message structure
        auto message = nlohmann::json::parse(gateway_->mock_generated_messages.back());
        ASSERT_TRUE(message.is_array());
        EXPECT_EQ(message[1], "on");
        ASSERT_TRUE(message[3].is_object());
        EXPECT_EQ(message[3]["symbol"], "t" + symbol);
        EXPECT_EQ(message[3]["type"], "EXCHANGE MARKET");
        
        // Verify amount has correct sign based on side
        double expected_amount = (side == singular::types::Side::SELL) ? -quantity : quantity;
        EXPECT_EQ(std::stod(message[3]["amount"].get<std::string>()), expected_amount);
    }
}

TEST_F(BitfinexGatewayTest, TestDoCancel) {
    for (int i = 0; i < 100; ++i) {
        // Clear previous messages
        gateway_->mock_generated_messages.clear();

        singular::types::OrderId order_id = getRandomOrderId();
        std::string symbol = "BTCUSD"; // Using a fixed symbol for simplicity
        std::string client_date = "2024-01-01T00:00:00Z";
        singular::types::RequestSource source = singular::types::RequestSource::RS_MARKET_ORDER;

        // Setup the required mappings using the fixture's helper method
        setupCancelMappings(order_id, order_id, client_date, source);

        EXPECT_NO_THROW(gateway_->do_cancel(order_id, source));

        // Verify a message was generated
        ASSERT_FALSE(gateway_->mock_generated_messages.empty());
        
        // Verify the message structure
        auto message = nlohmann::json::parse(gateway_->mock_generated_messages.back());
        ASSERT_TRUE(message.is_array());
        EXPECT_EQ(message[1], "oc"); // Order cancel command
        ASSERT_TRUE(message[3].is_object());
        EXPECT_EQ(message[3]["cid"], order_id);
    }
}

TEST_F(BitfinexGatewayTest, TestDoModify) {
    for (int i = 0; i < 100; ++i) {
        // Clear previous messages
        gateway_->mock_generated_messages.clear();

        // Create test instrument
        std::string symbol = getRandomSymbol();
        auto instrument = std::make_unique<singular::types::Instrument>(
            singular::types::Exchange::BITFINEX,
            symbol,
            singular::types::InstrumentType::SPOT,
            0.0, 0.0, 1.0, 0.0, 0.0, 0.0,
            "SPOT"
        );

        // Create test order
        singular::types::OrderId order_id = getRandomOrderId();
        int client_id = order_id;  // Use same ID for simplicity
        auto order = std::make_unique<singular::types::Order>(
            order_id,
            nullptr,
            instrument.get(),
            singular::types::OrderType::LIMIT,
            getRandomSide(),
            getRandomPrice(10000.0, 50000.0),
            getRandomQuantity(0.1, 10.0),
            symbol,
            ""
        );

        // Setup both order and cancel mappings
        setupOrderMappings(client_id, symbol);
        setupCancelMappings(
            order_id,
            client_id,
            "2024-01-01T00:00:00Z",
            singular::types::RequestSource::RS_LIMIT_ORDER
        );

        // Generate random modification values
        double new_quantity = getRandomQuantity(0.1, 10.0);
        double new_price = getRandomPrice(10000.0, 50000.0);
        singular::types::RequestSource source = singular::types::RequestSource::RS_LIMIT_ORDER;

        EXPECT_NO_THROW(gateway_->do_modify(
            order.get(),
            new_quantity,
            new_price,
            source
        ));

        // Verify messages were generated
        ASSERT_EQ(gateway_->mock_generated_messages.size(), 2) 
            << "Expected exactly 2 messages for modify operation";

        // First message should be the cancel
        auto cancel_message = nlohmann::json::parse(gateway_->mock_generated_messages[0]);
        ASSERT_TRUE(cancel_message.is_array());
        EXPECT_EQ(cancel_message[1], "oc");
        ASSERT_TRUE(cancel_message[3].is_object());
        EXPECT_EQ(std::to_string(cancel_message[3]["cid"].get<int>()), std::to_string(client_id));

        // Second message should be the new order
        auto new_order_message = nlohmann::json::parse(gateway_->mock_generated_messages[1]);
        ASSERT_TRUE(new_order_message.is_array());
        EXPECT_EQ(new_order_message[1], "on");
        ASSERT_TRUE(new_order_message[3].is_object());
        EXPECT_EQ(new_order_message[3]["symbol"], "t" + symbol);
        EXPECT_EQ(new_order_message[3]["type"], "EXCHANGE LIMIT");
        EXPECT_EQ(std::stod(new_order_message[3]["price"].get<std::string>()), new_price);
        EXPECT_EQ(std::abs(std::stod(new_order_message[3]["amount"].get<std::string>())), new_quantity);
    }
}

TEST_F(BitfinexGatewayTest, TestDoSubscribeOrderbooks) {
    for (int i = 0; i < 100; ++i) {
        // Clear previous messages
        gateway_->mock_generated_messages.clear();

        // Get random symbols
        std::vector<singular::types::Symbol> symbols = getRandomSymbols(10);
        
        // Subscribe to orderbooks
        EXPECT_NO_THROW(gateway_->do_subscribe_orderbooks(symbols));

        // Verify messages were generated
        ASSERT_EQ(gateway_->mock_generated_messages.size(), symbols.size())
            << "Expected one message per symbol";

        // Verify each subscription message
        for (size_t j = 0; j < symbols.size(); ++j) {
            auto message = nlohmann::json::parse(gateway_->mock_generated_messages[j]);
            
            // Verify basic message structure
            EXPECT_EQ(message["event"], "subscribe");
            EXPECT_EQ(message["channel"], "book");
            EXPECT_EQ(message["symbol"], "t" + std::string(symbols[j]));
        }
    }
}

TEST_F(BitfinexGatewayTest, TestDoUnsubscribeOrderbooks) {
    for (int i = 0; i < 100; ++i) {
        // Clear previous messages
        gateway_->mock_generated_messages.clear();

        // Get random symbols
        std::vector<singular::types::Symbol> symbols = getRandomSymbols(10);
        
        // Unsubscribe from orderbooks
        EXPECT_NO_THROW(gateway_->do_unsubscribe_orderbooks(symbols));

        // Verify messages were generated
        ASSERT_EQ(gateway_->mock_generated_messages.size(), symbols.size())
            << "Expected one message per symbol";

        // Verify each unsubscription message
        for (size_t j = 0; j < symbols.size(); ++j) {
            auto message = nlohmann::json::parse(gateway_->mock_generated_messages[j]);
            
            // Verify message structure
            ASSERT_TRUE(message.is_object()) << "Message should be an object";
            EXPECT_EQ(message["event"], "unsubscribe");
            EXPECT_TRUE(message.contains("chanId"));
        }
    }
}

// Create a test parameter structure
struct OrderTestParams {
    std::string symbol;
    singular::types::OrderType order_type;
    singular::types::Side side;
    double price;
    double quantity;
    singular::types::RequestSource source;
};

// Create a test fixture that uses parameters
class BitfinexOrderTest : public testing::TestWithParam<OrderTestParams> {
protected:
    std::shared_ptr<Gateway> gateway_;
    
    void SetUp() override {
        std::shared_ptr<hv::EventLoop> executor = std::make_shared<hv::EventLoop>();
        gateway_ = std::make_shared<Gateway>(
            executor, true, "test_account", "test_key", 
            "test_secret", "test_passphrase", "DEV"
        );
    }

    void setupOrderMappings(singular::types::OrderId order_id, const std::string& symbol) {
        gateway_->add_test_session("test_session");
        gateway_->setup_test_order_mappings(
            order_id,
            symbol,
            singular::types::RequestSource::RS_LIMIT_ORDER,
            "2024-01-01T00:00:00Z"
        );
    }
};

// Generate test parameters
std::vector<OrderTestParams> GenerateTestCases() {
    std::vector<OrderTestParams> params;
    std::vector<std::string> symbols = {"BTCUSD", "ETHUSD", "LTCUSD", "XRPUSD"};
    std::vector<singular::types::OrderType> types = {
        singular::types::OrderType::MARKET,
        singular::types::OrderType::LIMIT
    };
    std::vector<singular::types::Side> sides = {
        singular::types::Side::BUY,
        singular::types::Side::SELL
    };
    std::vector<double> prices = {10000.0, 20000.0, 30000.0};
    std::vector<double> quantities = {0.1, 0.5, 1.0};
    std::vector<singular::types::RequestSource> sources = {
        singular::types::RequestSource::RS_MARKET_ORDER,
        singular::types::RequestSource::RS_LIMIT_ORDER
    };

    // Generate combinations
    for (const auto& symbol : symbols)
    for (const auto& type : types)
    for (const auto& side : sides)
    for (const auto& price : prices)
    for (const auto& qty : quantities)
    for (const auto& src : sources) {
        params.push_back({symbol, type, side, price, qty, src});
    }
    
    return params;
}

// Create the parameterized test
TEST_P(BitfinexOrderTest, PlaceOrderVariations) {
    const auto& params = GetParam();
    
    singular::types::OrderId order_id = getRandomOrderId();
    setupOrderMappings(order_id, params.symbol);

    EXPECT_NO_THROW(gateway_->do_place(
        params.symbol,
        singular::types::InstrumentType::SPOT,
        order_id,
        params.order_type,
        params.side,
        params.price,
        params.quantity,
        params.source,
        "test_cred",
        "test_mode"
    ));

    // Verify message
    ASSERT_FALSE(gateway_->mock_generated_messages.empty());
    auto message = nlohmann::json::parse(gateway_->mock_generated_messages.back());
    ASSERT_TRUE(message.is_array());
    EXPECT_EQ(message[1], "on");
    ASSERT_TRUE(message[3].is_object());
    EXPECT_EQ(message[3]["symbol"], "t" + params.symbol);

    // Verify order type
    std::string expected_type = (params.order_type == singular::types::OrderType::MARKET) 
        ? "EXCHANGE MARKET" 
        : "EXCHANGE LIMIT";
    EXPECT_EQ(message[3]["type"], expected_type);

    // Verify amount (quantity with sign based on side)
    double expected_amount = (params.side == singular::types::Side::SELL) 
        ? -params.quantity 
        : params.quantity;
    EXPECT_EQ(std::stod(message[3]["amount"].get<std::string>()), expected_amount);

    // Verify price only for limit orders
    if (params.order_type == singular::types::OrderType::LIMIT) {
        EXPECT_EQ(std::stod(message[3]["price"].get<std::string>()), params.price);
    }
}

} // namespace singular::gateway::bitfinex

// Fix the test suite instantiation
using namespace singular::gateway::bitfinex;

INSTANTIATE_TEST_SUITE_P(
    BitfinexOrder,  // Simplified prefix
    BitfinexOrderTest,  // Test fixture name
    testing::ValuesIn(GenerateTestCases()),
    [](const testing::TestParamInfo<OrderTestParams>& info) {
        std::stringstream ss;
        ss << "Order_" 
           << info.param.symbol << "_"
           << static_cast<int>(info.param.order_type) << "_"
           << static_cast<int>(info.param.side) << "_"
           << info.index;
        return ss.str();
    }
);

// Main function
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}