#pragma once
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <string>
#include <singular/types/include/OrderId.h>
#include <singular/types/include/RequestSource.h>
#include <singular/types/include/Instrument.h>
#include <singular/types/include/Order.h>

namespace singular
{
    namespace gateway
    {
        namespace bitfinex
        {
            // Struct to represent a single seed data entry
            struct SeedData {
                std::string symbol;
                std::string order_type;
                std::string side;
                double price;
                double quantity;
                std::string client_id;
                nlohmann::json expected_output;
                std::string expected_log; // Correct field name

                // Add default constructor
                SeedData() = default;

                SeedData(const std::string& sym,
                            const std::string& orderType,
                            const std::string& side,
                            double price,
                            double quantity,
                            const std::string& clientId,
                            const nlohmann::json& expectedOutput,
                            const std::string& expectedLog)
                        : symbol(sym),
                        order_type(orderType),
                        side(side),
                        price(price),
                        quantity(quantity),
                        client_id(clientId),
                        expected_output(expectedOutput),
                        expected_log(expectedLog) {}
            };

            // Struct to represent subscription-related seed data
            struct SubscribeSeedData {
                std::vector<std::string> symbols;
                std::vector<nlohmann::json> expected_messages;
                std::vector<std::string> expected_logs; 

            SubscribeSeedData() = default;
            
            SubscribeSeedData(const std::vector<std::string>& sym,
                                  const std::vector<nlohmann::json>& expMsgs,
                                  const std::vector<std::string>& expLogs)
                    : symbols(sym),
                      expected_messages(expMsgs),
                      expected_logs(expLogs) {} 
            };

            // Struct to represent cancel order seed data
            struct CancelOrderSeedData {
                singular::types::OrderId order_id;
                singular::types::RequestSource source;
                int client_id;
                std::string client_date;
                nlohmann::json expected_message;
                std::string expected_log_message;

            CancelOrderSeedData() = default;

            CancelOrderSeedData(singular::types::OrderId orderId,
                                    singular::types::RequestSource src,
                                    int clientId,
                                    const std::string& clientDate,
                                    const nlohmann::json& expMsg,
                                    const std::string& expLogMsg)
                    : order_id(orderId),
                      source(src),
                      client_id(clientId),
                      client_date(clientDate),
                      expected_message(expMsg),
                      expected_log_message(expLogMsg) {}
            };

            // Struct to represent modify order seed data
            struct ModifyOrderSeedData {
                std::string symbol;
                double new_quantity;
                double new_price;
                singular::types::OrderId order_id;
                nlohmann::json expected_message;

                ModifyOrderSeedData() = default;

                ModifyOrderSeedData(
                    const std::string& sym,
                    double qty,
                    double prc,
                    singular::types::OrderId id,
                    const nlohmann::json& exp_msg
                ) : symbol(sym), new_quantity(qty), new_price(prc),
                    order_id(id), expected_message(exp_msg) {}
            };

            // Struct to represent unsubscribe seed data
            struct UnsubscribeSeedData {
                std::vector<std::string> symbols;
                std::vector<nlohmann::json> expected_messages;
                std::vector<std::string> expected_logs;

                UnsubscribeSeedData() = default;

                UnsubscribeSeedData(
                    const std::vector<std::string>& sym,
                    const std::vector<nlohmann::json>& exp_msgs,
                    const std::vector<std::string>& exp_logs
                ) : symbols(sym),
                    expected_messages(exp_msgs),
                    expected_logs(exp_logs) {}
            };

            // Function to create place order related seed data
            std::unordered_map<std::string, CancelOrderSeedData> CreateCancelOrderSeedData();

            // Function to create seed data
            std::unordered_map<std::string, SeedData> CreateSeedData();

            // Function to create subscription-related seed data
            std::vector<SubscribeSeedData> CreateSubscribeSeedData();

            // Function to create modify order seed data
            std::vector<ModifyOrderSeedData> CreateModifyOrderSeedData();

            // Function to create unsubscribe seed data
            std::vector<UnsubscribeSeedData> CreateUnsubscribeSeedData();

            // Function to create subscribe ticker seed data
            std::unordered_map<std::string, SeedData> CreateSubscribeTickersSeedData();

            // Function to create unsubscribe ticker seed data
            std::unordered_map<std::string, SeedData> CreateUnsubscribeTickersSeedData();

            // Function to create subscribe trade seed data
            std::unordered_map<std::string, SeedData> CreateSubscribeTradeSeedData();

            // Function to create unsubscribe trade seed data
            std::unordered_map<std::string, SeedData> CreateUnsubscribeTradeSeedData();

            // Add function declarations
            std::string getRandomSymbol();
            singular::types::OrderId getRandomOrderId(bool valid = true);
            singular::types::Side getRandomSide();
            double getRandomPrice(double min = 10000.0, double max = 50000.0);
            double getRandomQuantity(double min = 0.1, double max = 10.0);
            std::vector<std::string> getRandomSymbols(size_t count);
            

        }
    }
}