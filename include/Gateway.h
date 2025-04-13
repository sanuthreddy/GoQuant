#pragma once

#include <string_view>
#include <thread>
#include <nlohmann/json.hpp>
#include <regex>
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

#include <gateway/include/AbstractGateway_V2.h>
#include <singular/utility/include/RedisHelper.h>
#include "ErrorCodes.h"

using namespace hv;

namespace singular
{
    namespace gateway
    {
        namespace bitfinex
        {
            class BitfinexGatewayTest;

            class Gateway : public AbstractGateway_V2
            {
                friend class BitfinexGatewayTest;

            public:
                Gateway(hv::EventLoopPtr &executor,
                        bool authenticate,
                        const std::string &name,
                        const std::string &key,
                        const std::string &secret,
                        const std::string &passphrase,
                        const std::string &mode);

                void initialize_callback_funcs() override;

                std::string convert_timestamp_to_string(long long timestamp_ms);

                void login_ws_public_coinm_client();

                bool create_listen_key();

                void ping_listen_key();

                void send_position_information_request();

                void send_account_information_request();

                void send_account_extended_information_request();

                void parse_public_usdm_websocket(const std::string &buffer);

                void parse_user_data_coinm_websocket(const std::string &buffer);

                void login_private();

                void run();

                void parse_websocket(const std::string &buffer);

                void run_ws_public_client();

                void run_ws_user_data_client();

                unsigned long long get_client_id(singular::types::OrderId order_id);

                void do_place(singular::types::Symbol symbol, singular::types::InstrumentType type,
                              singular::types::OrderId order_id, singular::types::OrderType order_type,
                              singular::types::Side side, double price, double quantity, singular::types::RequestSource source, std::string credential_id = "", std::string td_mode = "");

                void do_cancel(singular::types::OrderId order_id, singular::types::RequestSource source);

                void do_cancel(std::string exchange_order_id, singular::types::Instrument *instrument, singular::types::RequestSource source);

                void do_modify(singular::types::Order *order, double quantity, double price, singular::types::RequestSource source);

                void parse_http_response(const HttpResponse &response, const std::string op);

                void parse_depth_snapshot(const nlohmann::json &json_data);

                void parse_public_snap_websocket(const std::string &buffer);

                void parse_public_coinm_websocket(const std::string &buffer);

                void do_subscribe_orderbooks(std::vector<singular::types::Symbol> &symbols) override;

                void do_unsubscribe_orderbooks(std::vector<singular::types::Symbol> &symbols) override;

                void do_subscribe_tickers(std::vector<singular::types::Symbol> &symbols) override;

                void do_unsubscribe_tickers(std::vector<singular::types::Symbol> &symbols) override;

                void get_symbol_depth_snapshot(const singular::types::Symbol &symbol);

                void get_depth_snapshots(std::vector<singular::types::Symbol> &symbols);

                void do_subscribe_trades(std::vector<singular::types::Symbol> &symbols) override;

                void do_unsubscribe_trades(std::vector<singular::types::Symbol> &symbols) override;

                void do_subscribe_top_of_book(std::vector<singular::types::Symbol> &symbols);

                void do_subscribe_funding(std::vector<singular::types::Symbol> &symbols);

                singular::types::GatewayStatus status();

                void send_login_response(const std::string msg, const int code);

                nlohmann::json get_open_orders();

                void set_order_channel_status(std::string session_id, std::string credential_id = "");

                void unset_order_channel_status(std::string session_id);

                void logout();

                nlohmann::json get_account_data();

                nlohmann::json get_position_data();

                nlohmann::json get_order_data();

                nlohmann::json get_orderbook_data();

                nlohmann::json get_last_trades_data();

                void stream_order_data(nlohmann::json message, const std::string order_state);

                void send_reject_response(const nlohmann::json &message) override;

                std::string calc_hmac_sha384_hex(const std::string &key, const std::string &data);

                void close_private_socket();
                void close_public_socket();
                void purge();
                bool is_purged() const;
                void send_algo_execution_status(const nlohmann::json &message);
                void send_final_latency_info(singular::types::AlgorithmId algo_id, char *credential_id);
                void unset_order_execution_quality_channel_status(std::string session_id);
                void set_order_execution_quality_channel_status(std::string session_id, std::string credential_id);
                // void do_websocket_task_latency(singular::utility::TimePoint latency_info, long long internal_order_id, std::string credential_id);

                void do_websocket_task_latency(singular::utility::TimePoint latency_info, std::string credential_id);

                void do_send_native_order_latency(long long internal_order_id);

                #ifdef TEST_BUILD
                std::vector<std::string> mock_generated_messages;
                void add_test_session(const std::string& session_id) {
                    session_map.push_back(session_id);
                }

                // Test-specific setup method
                void setup_test_order_mappings(
                    singular::types::OrderId order_id,
                    const std::string& symbol,
                    singular::types::RequestSource source,
                    const std::string& date
                ) {
                    internal_to_client_id_map_[order_id] = order_id;
                    client_to_internal_id_map_[order_id] = order_id;
                    client_id_to_source_map_[order_id] = source;
                    internal_id_symbol_map_[order_id] = symbol;
                    client_id_to_symbol_map_[order_id] = symbol;
                    client_id_to_date_map_[order_id] = date;
                }
                #endif

            private:
                std::string log_service_name = "BITFINEX";
                int default_recv_window = 6000;
                std::string broker_tag = "QrCwfu5A";
                const char *public_url;
                const char *private_url;
                const char *env_mode;
                const std::string rest_host_ = "https://api-pub.bitfinex.com/v2";
                bool sim_trading;

                std::unique_ptr<singular::network::WebsocketClient> public_client_;
                std::unique_ptr<singular::network::WebsocketClient> private_client_;

                static constexpr size_t INITIAL_MAP_SIZE = 10000; // Adjust based on expected load
                static constexpr float LOAD_FACTOR = 0.7f;        // Optimal load factor for performance
                static constexpr size_t ESTIMATED_MESSAGE_SIZE = 256;

                void initializeMaps();

                // Websockets URL
                std::string private_coinm_url;
                std::string user_data_url;
                std::string listen_key_url;

                // Websocket Clients
                hv::HttpClient rest_private_client;

                // Stream Data
                nlohmann::json account_info_;
                nlohmann::json order_data_;
                nlohmann::json position_data_;
                nlohmann::json open_orders_array = nlohmann::json::array();

                // Authentication
                bool authenticate_;
                bool authenticated_ = false;
                bool is_purged_ = false;

                std::string name_;
                std::string key_;
                std::string secret_;
                std::string passphrase_;
                std::string listen_key_;
                bool listen_key_result_;
                std::string mode_;
                unsigned long long last_client_id;

                singular::types::GatewayStatus private_status_ = singular::types::GatewayStatus::OFFLINE;
                singular::types::GatewayStatus public_status_ = singular::types::GatewayStatus::OFFLINE;

                singular::types::GatewayStatus public_coinm_status_ = singular::types::GatewayStatus::ONLINE;
                singular::types::GatewayStatus user_data_coinm_status_ = singular::types::GatewayStatus::OFFLINE;
                singular::types::GatewayStatus public_coinm_status_snap_ = singular::types::GatewayStatus::OFFLINE;

                // Maps
                std::map<unsigned long int, singular::types::OrderId> client_to_internal_id_map_;
                std::unordered_map<unsigned long int, std::string> client_id_to_date_map_;

                std::unordered_map<uint16_t, std::string> channel_id_to_symbol_map;
                std::unordered_map<std::string, unsigned long int> orderbook_symbol_to_channel_id_map_;
                std::unordered_map<unsigned long int, std::string> orderbook_channel_id_to_symbol_map_;
                std::unordered_map<std::string, unsigned long int> ticker_symbol_to_channel_id_map_;
                std::unordered_map<unsigned long int, std::string> ticker_channel_id_to_symbol_map_;
                std::unordered_map<std::string, unsigned long int> trades_symbol_to_channel_id_map_;
                std::unordered_map<unsigned long int, std::string> trades_channel_id_to_symbol_map_;

                std::unordered_map<std::string, size_t> positions_symbol_to_position_idx_map_;

                std::unordered_map<singular::types::OrderId, unsigned long int> internal_to_client_id_map_;

                std::map<unsigned long int, singular::types::Symbol> client_id_to_symbol_map_;
                std::unordered_map<singular::types::OrderId, std::string> internal_to_credential_id_map_;
                std::unordered_map<singular::types::OrderId, singular::types::Symbol> internal_id_symbol_map_;

                std::map<unsigned long int, std::string> client_id_to_order_type_map_;
                std::map<unsigned long int, singular::types::RequestSource> client_id_to_source_map_;
                nlohmann::json order_execution_quality_session_map = nlohmann::json::array();
                nlohmann::json session_map = nlohmann::json::array();
                std::map<unsigned long int, double> client_id_to_price_map_;
                std::map<unsigned long int, double> client_id_to_qty_map_;
                std::map<unsigned long int, singular::types::Side> client_id_to_side_map_;

                // State
                std::map<singular::types::Symbol, unsigned long int> symbol_to_snapshot_id_map_;
                std::map<singular::types::Symbol, unsigned long int> symbol_to_pu_id_map_;
                std::queue<singular::types::Symbol> snapshot_request_queue_;

                std::vector<unsigned long int> modify_order_vect;

                // Mutex
                std::unordered_set<std::string> unsubscribed_symbols_;
                std::mutex symbols_mutex_;
                singular::utility::LatencyMeasure *latency_measure = nullptr;
                singular::utility::RedisHelper redis_helper_;

                bool login_status;
                // std::string initializeMessage() const
                // {
                //     std::string message;
                //     message.reserve(ESTIMATED_MESSAGE_SIZE);
                //     return message;
                // }
            };

        } // namespace binance_coinm
    } // namespace gateway
} // namespace singular
