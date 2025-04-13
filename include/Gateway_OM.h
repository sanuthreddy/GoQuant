#pragma once

#include <string_view>
#include <thread>
#include <nlohmann/json.hpp>
#include <singular/network/network/include/WebsocketClient.h>
#include <singular/types/include/Order.h>
#include <singular/event/include/OrderbookUpdate.h>
#include <singular/event/include/Trade.h>
#include <singular/event/include/Event.h>
#include <singular/event/include/PlaceAck.h>
#include <singular/event/include/PlaceReject.h>
#include <singular/event/include/CancelAck.h>
#include <singular/event/include/CancelReject.h>
#include <singular/event/include/Fill.h>
#include <singular/event/include/GatewayDisconnect.h>
#include <singular/utility/include/Authentication.h>
#include <singular/utility/include/Time.h>
#include <singular/utility/include/LatencyLogger.h>
#include <singular/utility/include/EnvLoader.h>
#include <singular/network/include/GlobalWebsocket.h>
#include <singular/types/include/GlobalAlgorithmId.h>
#include <singular/utility/include/LatencyManager.h>
#include <singular/utility/include/SharedMemoryManager.h>

#include <gateway/include/AbstractGateway_OM.h>
#include <singular/utility/include/RedisHelper.h>
#include "ErrorCodes.h"

namespace singular
{
    namespace gateway
    {
        namespace bitfinex
        {

            class Gateway_OM : public AbstractGateway_OM
            {
            public:
                Gateway_OM(hv::EventLoopPtr &executor,
                           bool authenticate,
                           const std::string &name,
                           const std::string &key,
                           const std::string &secret,
                           const std::string &passphrase,
                           const std::string &mode);

                void initialize_callback_funcs();

                void do_place(singular::types::Symbol symbol, singular::types::InstrumentType type,
                              singular::types::OrderId order_id, singular::types::OrderType order_type,
                              singular::types::Side side, double price, double quantity, singular::types::RequestSource source,
                              std::string credential_id = "", std::string td_mode = "");

                void do_websocket_task_latency(singular::utility::TimePoint latency_info, long long internal_order_id, std::string credential_id = "");
                void do_send_native_order_latency(long long internal_order_id);

                void do_cancel(singular::types::OrderId order_id, singular::types::RequestSource source);
                void do_cancel(std::string exchange_order_id, singular::types::Instrument *instrument, singular::types::RequestSource source);
                void do_modify(singular::types::Order *order, double quantity, double price, singular::types::RequestSource source);
                void do_subscribe_positions();
                void do_subscribe_account();
                void do_unsubscribe_positions();

                singular::types::GatewayStatus status();
                std::string getCurrentTimestamp();
                std::string iso_timestamp();
                nlohmann::json get_open_orders();
                void logout();

                nlohmann::json get_order_data();
                nlohmann::json get_account_data();
                nlohmann::json get_position_data();

                void set_order_channel_status(std::string session_id, std::string credential_id = "");
                void unset_order_channel_status(std::string session_id);
                void set_order_execution_quality_channel_status(std::string session_id, std::string credential_id = "");
                void unset_order_execution_quality_channel_status(std::string session_id);

                void send_final_latency_info(singular::types::AlgorithmId algo_id, char *credential_id);
                void close_private_socket();

                void purge();
                bool is_purged() const { return is_purged_; }

            private:
                void subscribe_fills();
                void unsubscribe_fills();
                std::string signature(uint64_t timestamp, std::string &request_path, std::string_view secret);
                std::string calc_hmac_sha384_hex(const std::string &key, const std::string &data);
                void login_private();
                void start_private_client();

                unsigned long long get_client_id(singular::types::OrderId order_id);
                bool isFutureInstrument(const std::string &symbol);
                void parse_websocket(const std::string &buffer);
                void stream_order_data(nlohmann::json message, const std::string order_state);
                void send_reject_response(const nlohmann::json &message) override;
                void send_algo_execution_status(const nlohmann::json &message) override;
                void send_account_info_to_shared_memory(const std::string &exchange_name);
                void send_position_to_shared_memory(const std::string &exchange_name);

                std::string log_service_name_ = "BITFINEX_OM";

                const char *private_url_;
                const char *env_mode_;
                bool sim_trading_;

                std::unique_ptr<singular::network::WebsocketClient> private_client_;

                bool authenticate_;
                bool authenticated_ = {false};
                bool is_purged_ = {false};

                std::string name_;
                std::string key_;
                std::string secret_;
                std::string passphrase_;
                std::string mode_;

                nlohmann::json account_info_;
                nlohmann::json session_map_ = nlohmann::json::array();

                singular::types::GatewayStatus private_status_ = {singular::types::GatewayStatus::OFFLINE};

                std::map<unsigned long int, singular::types::Symbol> client_id_to_symbol_map_;
                std::map<unsigned long int, singular::types::OrderId> client_to_internal_id_map_;
                std::map<unsigned long int, singular::types::RequestSource> client_id_to_source_map_;

                nlohmann::json order_execution_quality_session_map_ = nlohmann::json::array();
                std::map<unsigned long int, double> client_id_to_price_map_;
                std::map<unsigned long int, double> client_id_to_qty_map_;
                std::map<unsigned long int, singular::types::Side> client_id_to_side_map_;
                std::map<unsigned long int, std::string> client_id_to_date_map_;

                // Maps a symbol to the index at which the position
                // (for the symbol) is present in an array containing
                // positions for various symbols.
                std::unordered_map<std::string, size_t> positions_symbol_to_position_idx_map_;

                // Maps a wallet identifier (composed of both wallet_type and
                // currency) to the index at which the wallet (for the identifier)
                // is present in an array containing wallets for various identifiers.
                std::unordered_map<std::string, size_t> wallets_identifier_to_wallet_idx_map_;

                // Maps the client id to the index where an open order is
                // present in the open orders array
                std::unordered_map<unsigned long int, size_t> open_orders_client_id_to_order_idx_map_;

                nlohmann::json position_data_;
                nlohmann::json order_data_;
                std::unordered_map<unsigned long int, nlohmann::json> open_orders_map_;
                nlohmann::json open_orders_array_ = nlohmann::json::array();

                std::vector<unsigned long int> modify_order_vect_;

                // These maps are used by do_cancel and do_modify (HOT PATH)
                // So changing them to unordered_map with LOAD_FACTOR and INITIAL_MAP_SIZE

                static constexpr size_t INITIAL_MAP_SIZE = 10000; // Adjust based on expected load
                static constexpr float LOAD_FACTOR = 0.7f;        // Optimal load factor for performance

                std::unordered_map<singular::types::OrderId, unsigned long int> internal_to_client_id_map_;
                std::unordered_map<singular::types::OrderId, singular::types::Symbol> internal_id_symbol_map_;
                std::unordered_map<singular::types::OrderId, std::string> internal_to_credential_id_map_;

                void initializeMaps();

                // Class constant for buffer size
                static constexpr size_t ESTIMATED_MESSAGE_SIZE = 256;

                // Utility method for message initialization
                std::string initializeMessage() const
                {
                    std::string message;
                    message.reserve(ESTIMATED_MESSAGE_SIZE);
                    return message;
                }

                bool login_status_;

                singular::utility::LatencyMeasure *latency_measure_ = nullptr;
                singular::utility::RedisHelper redis_helper_;

                const std::string exchange_name = "BITFINEX";
                mutable std::shared_mutex position_mutex_;
                mutable std::shared_mutex account_mutex_;
                nlohmann::json position_data_json;  
                nlohmann::json account_data_json; 
            };

        } // namespace bitfinex
    } // namespace gateway
} // namespace singular
