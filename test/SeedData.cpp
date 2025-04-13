#include "../test/SeedData.h"
#include <random>
#include <vector>
#include <string>
#include "nlohmann/json.hpp"
#include "singular/types/include/Side.h"

namespace singular::gateway::bitfinex{
    // Random number generators
    static std::mt19937 gen(std::random_device{}());

    double getRandomPrice(double min, double max) {
        std::uniform_real_distribution<> dist(min, max);
        return std::round(dist(gen) * 100) / 100;  // Round to 2 decimal places
    }

    double getRandomQuantity(double min, double max) {
        std::uniform_real_distribution<> dist(min, max);
        return std::round(dist(gen) * 1000000) / 1000000;  // Round to 6 decimal places
    }

    std::string getRandomSymbol() {
        static const std::vector<std::string> symbols = {
            "BTCUSD", "ETHUSD", "SOLUSD", "XRPUSD"
        };
        std::uniform_int_distribution<> dist(0, symbols.size() - 1);
        return symbols[dist(gen)];
    }

    singular::types::Side getRandomSide() {
        return (gen() % 2 == 0) ? singular::types::Side::BUY : singular::types::Side::SELL;
    }

    singular::types::OrderId getRandomOrderId(bool valid) {
        static const singular::types::OrderId valid_ids[] = {12345, 23456, 34567, 45678};
        static const singular::types::OrderId invalid_ids[] = {99999, 88888, 77777};
        
        if (valid) {
            std::uniform_int_distribution<size_t> dist(0, std::size(valid_ids) - 1);
            return valid_ids[dist(gen)];
        } else {
            std::uniform_int_distribution<size_t> dist(0, std::size(invalid_ids) - 1);
            return invalid_ids[dist(gen)];
        }
    }

    std::string getRandomDate() {
        static const std::vector<std::string> dates = {
            "2024-01-01", "2024-02-01", "2024-03-01", "2024-04-01"
        };
        std::uniform_int_distribution<> dist(0, dates.size() - 1);
        return dates[dist(gen)];
    }

    std::vector<std::string> getRandomSymbols(size_t count) {
        static const std::vector<std::string> available_symbols = {
            "BTCUSD", "ETHUSD", "SOLUSD", "AVAXUSD", "DOTUSD"
        };
        
        std::vector<std::string> selected;
        std::sample(
            available_symbols.begin(),
            available_symbols.end(),
            std::back_inserter(selected),
            count,
            std::mt19937{std::random_device{}()}
        );
        return selected;
    }
}

namespace singular
{
    namespace gateway
    {
        namespace bitfinex
        {
            // Function to create place order related seed data
            std::unordered_map<std::string, SeedData> CreateSeedData() {
                std::unordered_map<std::string, SeedData> seed_data;

                // Create multiple random test cases
                for (int i = 0; i < 5; i++) {
                    std::string symbol = getRandomSymbol();
                    singular::types::Side side = getRandomSide();
                    double price = getRandomPrice();
                    double quantity = getRandomQuantity();
                    std::string test_name = (side == singular::types::Side::BUY ? "LIMIT_BUY_" : "LIMIT_SELL_") + std::to_string(i);

                    seed_data[test_name] = SeedData(
                        symbol,           // symbol
                        "LIMIT",         // order_type
                        side == singular::types::Side::BUY ? "BUY" : "SELL",  // side
                        price,           // price
                        quantity,        // quantity
                        "12345",         // client_id
                        nlohmann::json::array({  // expected_output
                            0,                   // Channel ID
                            "on",               // Event type
                            "",                 // Empty string
                            {                   // Order details
                                {"amount", (side == singular::types::Side::SELL ? "-" : "") + 
                                          std::to_string(quantity)},
                                {"price", std::to_string(price)},
                                {"symbol", "t" + symbol},
                                {"type", "EXCHANGE LIMIT"}
                            }
                        }),
                        "Sent a place " + std::to_string(quantity) + " order for t" + 
                        symbol + "@LIMIT price:" + std::to_string(price)
                    );
                }

                return seed_data;
            }

            // Function to create cancel order seed data
            std::unordered_map<std::string, CancelOrderSeedData> CreateCancelOrderSeedData() {
                std::unordered_map<std::string, CancelOrderSeedData> seed_data;

                // Valid order cases
                for (int i = 0; i < 3; i++) {
                    auto order_id = getRandomOrderId(true);
                    auto client_id = order_id + 1000;  // Ensure unique client ID
                    auto date = getRandomDate();
                    
                    std::string test_name = "VALID_ORDER_" + std::to_string(i);
                    seed_data[test_name] = CancelOrderSeedData(
                        order_id,
                        singular::types::RequestSource::RS_LIMIT_ORDER,
                        client_id,
                        date,
                        {
                            0,
                            "oc",
                            nullptr,
                            {
                                {"cid", client_id},
                                {"cid_date", date}
                            }
                        },
                        "Order cancelled successfully"
                    );
                }

                // Keep the original invalid case for testing error conditions
                seed_data["INVALID_ORDER"] = CancelOrderSeedData(
                    getRandomOrderId(false),
                    singular::types::RequestSource::RS_LIMIT_ORDER,
                    -1,
                    "",
                    {},
                    "Order ID not found in internal_to_client_id_map."
                );

                return seed_data;
            }

            // Function to create subscription-related seed data
            std::vector<SubscribeSeedData> CreateSubscribeSeedData() {
                std::vector<SubscribeSeedData> seed_data_list;

                // Generate 3 random valid subscription cases with different symbol counts
                for (size_t symbol_count : {2, 3, 1}) {
                    auto symbols = getRandomSymbols(symbol_count);
                    std::vector<nlohmann::json> expected_messages;
                    std::vector<std::string> expected_logs;

                    for (const auto& symbol : symbols) {
                        nlohmann::json message = {
                            {"event", "subscribe"},
                            {"channel", "book"},
                            {"symbol", "t" + symbol}
                        };
                        expected_messages.push_back(message);
                        
                        std::string log = "Subscribing order book channel: " + message.dump();
                        expected_logs.push_back(log);
                    }

                    seed_data_list.emplace_back(symbols, expected_messages, expected_logs);
                }

                // Keep the edge case for empty symbols
                seed_data_list.emplace_back(
                    std::vector<std::string>{},
                    std::vector<nlohmann::json>{},
                    std::vector<std::string>{}
                );

                return seed_data_list;
            }

            std::vector<ModifyOrderSeedData> CreateModifyOrderSeedData() {
                std::vector<ModifyOrderSeedData> seed_data;

                // Create a few test cases with random data
                for (int i = 0; i < 3; i++) {
                    std::string symbol = getRandomSymbol();
                    double new_price = getRandomPrice();
                    double new_quantity = getRandomQuantity();
                    singular::types::OrderId order_id = getRandomOrderId(true);

                    nlohmann::json expected_message = nlohmann::json::array({
                        0,
                        "ou",  // Order update
                        nullptr,
                        {
                            {"id", std::to_string(order_id)},
                            {"amount", std::to_string(new_quantity)},
                            {"price", std::to_string(new_price)}
                        }
                    });

                    seed_data.emplace_back(symbol, new_quantity, new_price, order_id, expected_message);
                }

                return seed_data;
            }

            std::vector<UnsubscribeSeedData> CreateUnsubscribeSeedData() {
                std::vector<UnsubscribeSeedData> seed_data_list;

                // Generate test cases with different symbol counts
                for (size_t symbol_count : {2, 3, 1}) {
                    auto symbols = getRandomSymbols(symbol_count);
                    std::vector<nlohmann::json> expected_messages;
                    std::vector<std::string> expected_logs;

                    for (const auto& symbol : symbols) {
                        nlohmann::json message = {
                            {"event", "unsubscribe"},
                            {"channel", "book"},
                            {"symbol", "t" + symbol}
                        };
                        expected_messages.push_back(message);
                        
                        std::string log = "Unsubscribing from order book channel: " + message.dump();
                        expected_logs.push_back(log);
                    }

                    seed_data_list.emplace_back(symbols, expected_messages, expected_logs);
                }

                // Add edge case for empty symbols
                seed_data_list.emplace_back(
                    std::vector<std::string>{},
                    std::vector<nlohmann::json>{},
                    std::vector<std::string>{}
                );

                return seed_data_list;
            }
            std::unordered_map<std::string, SeedData> CreateSubscribeTickersSeedData() {
                std::unordered_map<std::string, SeedData> seed_data;

                // Generate test cases with different symbols
                auto symbols = getRandomSymbols(2); // Get 2 random symbols
                
                for (const auto& symbol : symbols) {
                    std::string key = "SUBSCRIBE_" + symbol;
                    nlohmann::json message = {
                        {"event", "subscribe"},
                        {"channel", "ticker"},
                        {"symbol", "t" + symbol}
                    };
                    
                    seed_data[key] = SeedData(
                        symbol, "SUBSCRIBE", "", 0.0, 0.0, "",
                        message,
                        "Subscribed to ticker for " + symbol
                    );
                }

                return seed_data;
            }

            std::unordered_map<std::string, SeedData> CreateUnsubscribeTickersSeedData() {
                std::unordered_map<std::string, SeedData> seed_data;
                
                // Generate test cases with different symbols
                auto symbols = getRandomSymbols(2);
                
                for (const auto& symbol : symbols) {
                    std::string key = "UNSUBSCRIBE_" + symbol;
                    nlohmann::json message = {
                        {"event", "unsubscribe"},
                        {"chanId", 97798324989674}  // Using the same channel ID as the implementation
                    };
                    
                    seed_data[key] = SeedData(
                        symbol, "UNSUBSCRIBE", "", 0.0, 0.0, "",
                        message,
                        "Unsubscribed from ticker for " + symbol
                    );
                }

                return seed_data;
            }

            std::unordered_map<std::string, SeedData> CreateSubscribeTradeSeedData() {
                std::unordered_map<std::string, SeedData> seed_data;

                // Generate test cases with different symbols
                auto symbols = getRandomSymbols(2); // Get 2 random symbols
                
                for (const auto& symbol : symbols) {
                    std::string key = "SUBSCRIBE_" + symbol;
                    nlohmann::json message = {
                        {"event", "subscribe"},
                        {"channel", "trades"},
                        {"symbol", "t" + symbol}
                    };
                    
                    seed_data[key] = SeedData(
                        symbol, "SUBSCRIBE", "", 0.0, 0.0, "",
                        message,
                        "Subscribed to trades for " + symbol
                    );
                }

                return seed_data;
            }

            std::unordered_map<std::string, SeedData> CreateUnsubscribeTradeSeedData() {
                std::unordered_map<std::string, SeedData> seed_data;
                
                // Generate test cases with different symbols
                auto symbols = getRandomSymbols(2);
                
                for (const auto& symbol : symbols) {
                    std::string key = "UNSUBSCRIBE_" + symbol;
                    nlohmann::json message = {
                        {"event", "unsubscribe"},
                        {"chanId", 97798324989674}  // Using the same channel ID as the implementation
                    };
                    
                    seed_data[key] = SeedData(
                        symbol, "UNSUBSCRIBE", "", 0.0, 0.0, "",
                        message,
                        "Unsubscribed from trades for " + symbol
                    );
                }

                return seed_data;
            }
        } // namespace bitfinex
    } // namespace gateway
} // namespace singular