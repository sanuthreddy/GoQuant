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

#include <gateway/include/AbstractGateway_DM.h>
#include "ErrorCodes.h"

namespace singular {
namespace gateway {
namespace bitfinex {

class Gateway_DM : public AbstractGateway_DM {
public:
    Gateway_DM(hv::EventLoopPtr& executor,
            bool authenticate,
            const std::string& name,
            const std::string& key,
            const std::string& secret,
            const std::string& passphrase,
            const std::string& mode);

    void do_subscribe_orderbooks(std::vector<singular::types::Symbol>& symbols);
    void do_unsubscribe_orderbooks(std::vector<singular::types::Symbol>& symbols);
    void do_subscribe_tickers(std::vector<singular::types::Symbol>& symbols);
    void do_unsubscribe_tickers(std::vector<singular::types::Symbol>& symbols);
    void do_subscribe_top_of_book(std::vector<singular::types::Symbol>& symbols);
    void do_subscribe_trades(std::vector<singular::types::Symbol>& symbols);
    void do_unsubscribe_trades(std::vector<singular::types::Symbol>& symbols);
    void do_subscribe_funding(std::vector<singular::types::Symbol>& symbols);
    
    singular::types::GatewayStatus status();
    void logout();
    
    nlohmann::json get_account_data();
    nlohmann::json get_position_data();

    nlohmann::json get_orderbook_data();
    nlohmann::json get_last_trades_data();
    void initialize_callback_funcs();

    void purge();
    bool is_purged() const { return is_purged_; }
    void close_public_socket();

private:
    std::string calc_hmac_sha384_hex(const std::string &key, const std::string &data);
    std::string signature(uint64_t timestamp, std::string& request_path, std::string_view secret);
    void start_public_client();
    void parse_websocket(const std::string& buffer);

    std::string log_service_name_ = "BITFINEX_DM";
    const char* public_url_;
    const char* env_mode_;
    bool sim_trading_;

    std::unique_ptr<singular::network::WebsocketClient> public_client_;
    
    bool authenticate_;
    bool authenticated_ = {false};
    bool is_purged_ = { false };

    std::string name_;
    std::string key_;
    std::string secret_;
    std::string passphrase_;
    std::string mode_;

    nlohmann::json account_info_;
    nlohmann::json session_map_ = nlohmann::json::array();

    singular::types::GatewayStatus public_status_ = {singular::types::GatewayStatus::OFFLINE};
    void initializeMaps();

    // Class constant for buffer size
    static constexpr size_t ESTIMATED_MESSAGE_SIZE = 256;
    
    // Utility method for message initialization
    std::string initializeMessage() const {
        std::string message;
        message.reserve(ESTIMATED_MESSAGE_SIZE);
        return message;
    }

    bool login_status_;

    
    // Local cache for account and position data
    std::mutex account_cache_mutex;
    std::mutex position_cache_mutex;
    nlohmann::json account_data_cache = nlohmann::json::array();
    nlohmann::json position_data_cache = nlohmann::json::array();
    std::atomic<bool> account_data_updated = false;
    std::atomic<bool> position_data_updated = false;

    //Maps
    std::unordered_map<unsigned long int, std::string> client_id_to_date_map_;
    std::unordered_map<uint16_t, std::string> channel_id_to_symbol_map;
    std::unordered_map<std::string, unsigned long int> orderbook_symbol_to_channel_id_map_;
    std::unordered_map<unsigned long int, std::string> orderbook_channel_id_to_symbol_map_;
    std::unordered_map<std::string, unsigned long int> ticker_symbol_to_channel_id_map_;
    std::unordered_map<unsigned long int, std::string> ticker_channel_id_to_symbol_map_;
    std::unordered_map<std::string, unsigned long int> trades_symbol_to_channel_id_map_;
    std::unordered_map<unsigned long int, std::string> trades_channel_id_to_symbol_map_;

    const std::string exchange_name = "BITFINEX";
};

} // namespace bitfinex
} // namespace gateway
} // namespace singular
