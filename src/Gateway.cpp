#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdio>

#ifdef TEST_BUILD
  #include "../include/Gateway.h"
  #include <vector>
  #include <string>
#else
    #include "bitfinex/include/Gateway.h"
#endif

#include <gateway/include/GatewayFactoryManager.h>

namespace singular
{
    namespace gateway
    {
        namespace bitfinex
        {

            std::unique_ptr<singular::gateway::AbstractGateway_V2> create_bitfinex_gateway(
                hv::EventLoopPtr &executor, bool authenticate,
                const std::string &account_name, const std::string &key,
                const std::string &secret, const std::string &passphrase,
                const std::string &mode)
            {
                return std::make_unique<Gateway>(
                    executor, authenticate, account_name, key, secret, passphrase, mode);
            }

            namespace
            {
                struct Registrar
                {
                    Registrar()
                    {
                        singular::gateway::GatewayFactoryManager::register_factory(
                            singular::types::Exchange::BITFINEX, create_bitfinex_gateway);
                        std::cout << "Registered BITFINEX Gateway factory." << std::endl;
                    }
                };
                Registrar registrar;
            }

            Gateway::Gateway(hv::EventLoopPtr &executor,
                             bool authenticate,
                             const std::string &name,
                             const std::string &key,
                             const std::string &secret,
                             const std::string &passphrase,
                             const std::string &mode)
                : AbstractGateway_V2(executor, 120, 20),
                  authenticate_(authenticate),
                  name_(name),
                  key_(key),
                  secret_(secret),
                  passphrase_(passphrase),
                  mode_(mode)
            {
                // Constructor implementation
                #ifdef TEST_BUILD
                    // Mock values for tests
                    public_url = "mock_public_url";
                    private_url = "mock_private_url";
                #else
                    loadEnvFile(".env");
                    public_url = getExchangeUrl("BITFINEX_ENV_MODE", "DEV_BITFINEX_PUBLIC_WS_URL", "PROD_BITFINEX_PUBLIC_WS_URL");
                    private_url = getExchangeUrl("BITFINEX_ENV_MODE", "DEV_BITFINEX_PRIVATE_WS_URL", "PROD_BITFINEX_PRIVATE_WS_URL");
                #endif
                env_mode = std::getenv("BITFINEX_ENV_MODE");
                if (env_mode && strcmp(env_mode, "DEV") == 0)
                {
                    sim_trading = true;
                }
                else
                {
                    sim_trading = false;
                }

                if (!public_url)
                {
                    std::cerr << "Public URL not set. Please check your .env file." << std::endl;
                }

                if (!private_url && authenticate)
                {
                    std::cerr << "Private URL not set. Please check your .env file." << std::endl;
                }

                public_client_ = std::make_unique<singular::network::WebsocketClient>(executor, public_url);

                if (authenticate)
                {
                    private_client_ = std::make_unique<singular::network::WebsocketClient>(executor, private_url);
                }

                // Function to initialize maps with LOAD FACTOR and INITIALIZE MAP SIZE
                initializeMaps();

                login_status = true;
                latency_measure = singular::utility::LatencyManager::get();
            }

            void Gateway::close_private_socket()
            {
                private_client_->close();
                private_status_ = singular::types::GatewayStatus::OFFLINE;
            }

            void Gateway::close_public_socket()
            {
                public_client_->close();
                public_status_ = singular::types::GatewayStatus::OFFLINE;
            }

            bool Gateway::is_purged() const
            {
                return is_purged_;
            }

            void Gateway::purge()
            {
                close_public_socket();
                close_private_socket();
                authenticated_ = false;
                login_status = false;
                is_purged_ = true;
                #ifdef TEST_BUILD
                    mock_generated_messages.clear();
                #endif
            }

            void Gateway::initializeMaps()
            {
                internal_to_client_id_map_.max_load_factor(LOAD_FACTOR);
                internal_id_symbol_map_.max_load_factor(LOAD_FACTOR);
                internal_to_credential_id_map_.max_load_factor(LOAD_FACTOR);

                internal_to_client_id_map_.reserve(INITIAL_MAP_SIZE);
                internal_id_symbol_map_.reserve(INITIAL_MAP_SIZE);
                internal_to_credential_id_map_.reserve(INITIAL_MAP_SIZE);
            }

            void Gateway::run()
            {
                public_client_->close();
                private_client_->close();
                // std::this_thread::sleep_for(std::chrono::seconds(10));
                public_client_->run(
                    [this](const HttpResponsePtr &response)
                    {
                        if (authenticate_)
                        {
                            // login_public();
                        }
                    },
                    [this]()
                    {
                        public_status_ = singular::types::GatewayStatus::OFFLINE;
                        // send_gateway_disconnect(singular::types::Exchange::BITFINEX, name_);
                    },
                    [this](const std::string &message)
                    {
                        parse_websocket(message);
                    });

                if (authenticate_)
                {
                    private_client_->run(
                        [this](const HttpResponsePtr &response)
                        {
                            std::cout << "Recived http response...\n";
                        },
                        [this]()
                        {
                            private_status_ = singular::types::GatewayStatus::OFFLINE;
                            authenticated_ = false;
                            // send_gateway_disconnect(singular::types::Exchange::BITFINEX, name_);
                        },
                        [this](const std::string &message)
                        {
                            // FMTLOG(fmtlog::INF, "{}", message);
                            parse_websocket(message);
                        });
                    while (!private_client_->is_open())
                        ;
                    login_private();
                }
            }

            std::string Gateway::calc_hmac_sha384_hex(const std::string &key, const std::string &data)
            {
                unsigned char hash[EVP_MAX_MD_SIZE];
                unsigned int length = 0;

                // Use OpenSSL's HMAC function
                HMAC_CTX *ctx = HMAC_CTX_new();
                HMAC_Init_ex(ctx, key.c_str(), key.length(), EVP_sha384(), nullptr);
                HMAC_Update(ctx, reinterpret_cast<const unsigned char *>(data.c_str()), data.length());
                HMAC_Final(ctx, hash, &length);
                HMAC_CTX_free(ctx);

                // Convert the result to a hexadecimal string
                std::ostringstream oss;
                for (unsigned int i = 0; i < length; ++i)
                {
                    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
                }
                return oss.str();
            }

            void Gateway::login_private()
            {
                #ifdef TEST_BUILD
                    auto timestamp = 1609459200000000; // Fixed timestamp for testing
                #else
                    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                #endif

                std::string auth_payload = "AUTH" + std::to_string(timestamp);
                std::string auth_sig = calc_hmac_sha384_hex(secret_, auth_payload);

                nlohmann::json payload = {
                    {"event", "auth"},
                    {"apiKey", key_},
                    {"authNonce", timestamp},
                    {"authPayload", auth_payload},
                    {"authSig", auth_sig}
                };

                #ifdef TEST_BUILD
                    mock_generated_messages.push_back(payload.dump());
                #else
                    private_client_->send(payload.dump());
                #endif
            }

            void Gateway::parse_websocket(const std::string &buffer)
            {
                #ifdef TEST_BUILD
                    if (buffer.empty()) {
                        singular::utility::log_event(log_service_name, 
                            singular::utility::OEMSEvent::WS_CONNECTION_ERROR, 
                            "Empty WebSocket message received.");
                        return;
                    }
                #endif
                auto end_time = std::chrono::high_resolution_clock::now();
                nlohmann::json message;

                if (buffer.empty())
                {
                    singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_ERROR, "Empty WebSocket message received.");
                    return;
                }

                try
                {
                    message = nlohmann::json::parse(buffer);
                    unsigned long int channel_id;

                    // A bitfinex response message can be an:
                    // - an event (json)
                    // - a array of things
                    if (message.is_object() && message.contains("event"))
                    {
                        if (message["event"] == "info")
                        {
                            // Information about state of connection
                            // https://docs.bitfinex.com/docs/ws-general
                            if (message.contains("version"))
                            {
                                // Version of websocket
                                singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Bitfinex Server uses Websocket protocol version " + std::to_string(static_cast<unsigned int>(message["version"])));
                            }
                            else if (message.contains("code"))
                            {
                                singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Bitfinex Server event, code " + std::to_string(static_cast<unsigned long long>(message["code"])) + message["msg"].get<std::string>());
                            }
                            else
                            {
                                // Unknown info message
                                singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Unknown info message " + buffer);
                            }
                        } // if (message["event"] == "info") ends

                        else if (message["event"] == "auth")
                        {
                            if (message.contains("status") &&
                                message["status"] == "OK")
                            {
                                // Successful authentication
                                authenticated_ = true;
                                public_status_ = singular::types::GatewayStatus::ONLINE;
                                private_status_ = singular::types::GatewayStatus::ONLINE;

                                // Extract success message if available, default to "SUCCESS"
                                std::string success_message = message.contains("msg") ? message["msg"].get<std::string>() : "SUCCESS";
                                singular::types::EventDetail detail(
                                    success_message,
                                    200, // Hardcoded for success
                                    success_message,
                                    singular::event::EventType::LOGIN_ACCEPT,
                                    std::nullopt);

                                send_operation_response("SUCCESS", detail);
                                singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::LOGIN_EXCHANGE_SUCCESS, "Login successful.");
                                // Successful authentication automatically leads
                                // to the subscription of account info channel which
                                // automatically leads to subscription of the following:
                            }
                            else
                            {
                                // Extract error code and message, provide defaults if not present
                                int error_code = message.contains("code") ? message["code"].get<int>() : 500; // Default to 500
                                std::string error_message = message.contains("msg") ? message["msg"].get<std::string>() : "UNKNOWN ERROR";

                                singular::types::EventDetail detail(
                                    error_message,
                                    error_code, // Use the dynamic error code
                                    error_message,
                                    singular::event::EventType::LOGIN_FAIL,
                                    std::nullopt);
                                send_operation_response("FAILED", detail);
                                send_gateway_disconnect(singular::types::Exchange::BITFINEX, name_);
                                singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::LOGIN_EXCHANGE_ERROR, "Login failed [ERROR] " + error_message);
                            }
                        } // if (message["event"] == "auth") ends
                        else if (message["event"] == "subscribed")
                        {
                            if (!message.contains("channel"))
                            {
                                singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Subscribed event without channel name " + buffer);
                                return;
                            }

                            std::string log_message;
                            const std::string &channel = message["channel"];

                            log_message = "Successfully subscribed to the " + channel + " channel";

                            if (channel == "book")
                            {
                                // order book
                                std::string symbol = message["symbol"];

                                // Trim the symbol to remove 't' from the start if it exists
                                if (symbol[0] == 't')
                                {
                                    symbol = symbol.substr(1); // Remove the 't' from the start of the symbol
                                }

                                // Save the channel ID and symbol in the map
                                channel_id = message["chanId"];
                                orderbook_symbol_to_channel_id_map_[symbol] = channel_id;
                                orderbook_channel_id_to_symbol_map_[channel_id] = symbol;
                                singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::ORDERBOOK_CHANNEL, log_message + " with symbol " + symbol);
                            }
                            else if (channel == "trades")
                            {
                                // trades channel
                                std::string symbol = message["symbol"];
                                // Trim the symbol to remove 't' from the start if it exists
                                if (symbol[0] == 't')
                                {
                                    symbol = symbol.substr(1); // Remove the 't' from the start of the symbol
                                }
                                // add channel id and symbol to the trades maps
                                channel_id = message["chanId"];
                                trades_symbol_to_channel_id_map_[symbol] = channel_id;
                                trades_channel_id_to_symbol_map_[channel_id] = symbol;
                                singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::LASTTRADES_CHANNEL, log_message + " with symbol " + symbol);
                            } // else if (channel == "trades") ends
                            else if (channel == "ticker")
                            {
                                // trades channel
                                std::string symbol = message["symbol"];
                                // Trim the symbol to remove 't' from the start if it exists
                                if (symbol[0] == 't')
                                {
                                    symbol = symbol.substr(1); // Remove the 't' from the start of the symbol
                                }
                                // add channel id and symbol to the trades maps
                                channel_id = message["chanId"];
                                ticker_symbol_to_channel_id_map_[symbol] = channel_id;
                                ticker_channel_id_to_symbol_map_[channel_id] = symbol;
                                singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::TICKER_CHANNEL, log_message + " with symbol " + symbol);
                            } // else if (channel == "ticker") ends

                        } // else if (message[event] == "subscribed") ends
                        else if (message["event"] == "unsubscribed")
                        {
                            std::string log_message;

                        } // else if (message["event"] == "unsubscribed")
                        else
                        {
                            singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Received event: " + buffer);
                        }
                    } // if (message.contains("event")) ends

                    else if (message.is_array())
                    {
                        if (message.is_array() && message.size() > 0 && message[0].is_number())
                        {

                            channel_id = message[0];
                        }
                        if (message[0] == 0)
                        {
                            // 0 : Channel ID
                            if (message[1] == "os" || message[1] == "on" || message[1] == "ou" || message[1] == "oc")
                            {
                                const auto &event_type = message[1];

                                // Helper function to extract common order data

                                auto extract_order_data = [](const nlohmann::json &order)
                                {
                                    uint64_t id = order.size() > 0 && order[0].is_number() ? order[0].get<uint64_t>() : 0;
                                    uint64_t gid = order.size() > 1 && order[1].is_number() ? order[1].get<uint64_t>() : 0;
                                    uint64_t client_id = order.size() > 2 && order[2].is_number() ? order[2].get<uint64_t>() : 0;

                                    std::string symbol = order.size() > 3 && order[3].is_string() ? order[3].get<std::string>() : "";
                                    if (!symbol.empty() && symbol[0] == 't')
                                    {
                                        symbol = symbol.substr(1); // Remove the 't' prefix
                                    }

                                    uint64_t mts_create = order.size() > 4 && order[4].is_number() ? order[4].get<uint64_t>() : 0;
                                    uint64_t mts_update = order.size() > 5 && order[5].is_number() ? order[5].get<uint64_t>() : 0;

                                    double amount = order.size() > 6 && order[6].is_number() ? order[6].get<double>() : 0.0;
                                    double amount_orig = order.size() > 7 && order[7].is_number() ? order[7].get<double>() : 0.0;

                                    std::string order_type = order.size() > 8 && order[8].is_string() ? order[8].get<std::string>() : "";
                                    std::string type_prev = order.size() > 9 && order[9].is_string() ? order[9].get<std::string>() : "";

                                    uint64_t mts_tif = order.size() > 10 && order[10].is_number() ? order[10].get<uint64_t>() : 0;

                                    int flags = order.size() > 12 && order[12].is_number() ? order[12].get<int>() : 0;

                                    std::string status = order.size() > 13 && order[13].is_string() ? order[13].get<std::string>() : "";
                                    // Trim leading and trailing whitespace
                                    status.erase(0, status.find_first_not_of(" \t")); // Trim leading whitespace
                                    status.erase(status.find_last_not_of(" \t") + 1); // Trim trailing whitespace

                                    // Extract the part before '@', and trim the space just before '@' if exists
                                    size_t at_pos = status.find('@');
                                    if (at_pos != std::string::npos)
                                    {
                                        status = status.substr(0, at_pos); // Take the substring before '@'
                                        // Trim any trailing space that might be left before '@'
                                        status.erase(status.find_last_not_of(" \t") + 1); // Trim trailing space before '@'
                                    }

                                    // Placeholder fields
                                    // double placeholder1 = order.size() > 14 && order[14].is_number() ? order[14].get<double>() : 0.0;
                                    // double placeholder2 = order.size() > 15 && order[15].is_number() ? order[15].get<double>() : 0.0;
                                    // double placeholder3 = order.size() > 20 && order[20].is_number() ? order[20].get<double>() : 0.0;

                                    double price = order.size() > 16 && order[16].is_number() ? order[16].get<double>() : 0.0;
                                    double price_avg = order.size() > 17 && order[17].is_number() ? order[17].get<double>() : 0.0;
                                    double price_trailing = order.size() > 18 && order[18].is_number() ? order[18].get<double>() : 0.0;
                                    double price_aux_limit = order.size() > 19 && order[19].is_number() ? order[19].get<double>() : 0.0;

                                    int notify = order.size() > 23 && order[23].is_number() ? order[23].get<int>() : 0;
                                    int hidden = order.size() > 24 && order[24].is_number() ? order[24].get<int>() : 0;

                                    uint64_t placed_id = order.size() > 25 && order[25].is_number() ? order[25].get<uint64_t>() : 0;

                                    std::string routing = order.size() > 28 && order[28].is_string() ? order[28].get<std::string>() : "";

                                    nlohmann::json meta = order.size() > 31 && order[31].is_object() ? order[31] : nlohmann::json::object();

                                    return std::make_tuple(
                                        id, gid, client_id, symbol, mts_create, mts_update, amount, amount_orig,
                                        order_type, type_prev, mts_tif, flags, status, price, price_avg, price_trailing, price_aux_limit, notify,
                                        hidden, placed_id, routing, meta);
                                };

                                if (event_type == "os" || event_type == "ou")
                                {
                                    if (message.size() > 2)
                                    {
                                        for (const auto &order : message[2])
                                        {
                                            const std::string ord_status = order[13].get<std::string>();
                                            auto [id, gid, client_id, symbol, mts_create, mts_update, amount, amount_orig, order_type, type_prev, mts_tif, flags, status, price, price_avg, price_trailing, price_aux_limit, notify,
                                                  hidden, placed_id, routing, meta] = extract_order_data(order);
                                            long long internal_order_id = client_to_internal_id_map_[client_id];
                                            auto req_source = client_id_to_source_map_[client_id];
                                            std::string request_source = singular::types::get_request_source_string[req_source];
                                            auto end_time_rtsc = singular::utility::LatencyMeasure::captureTimestamp();

                                            // Use chrono for end time in nanoseconds
                                            auto end_time = std::chrono::steady_clock::now();
                                            latency_measure->setEndTime(
                                                internal_order_id,
                                                std::chrono::duration_cast<std::chrono::nanoseconds>(end_time.time_since_epoch()),
                                                end_time_rtsc);

                                            // Convert MTS_CREATE and MTS_UPDATE (milliseconds since epoch) to nanoseconds
                                            auto order_creation_time_ns = std::chrono::nanoseconds(mts_create * 1'000'000LL);
                                            auto exchange_update_time_ns = std::chrono::nanoseconds(mts_update * 1'000'000LL);

                                            // Calculate exchange latency
                                            auto exchange_latency = exchange_update_time_ns - order_creation_time_ns;
                                            latency_measure->setExchangeLatency(internal_order_id, exchange_latency);
                                            latency_measure->setFinalCost(internal_order_id, price_avg);
                                            if (req_source == singular::types::RequestSource::RS_LIMIT_ORDER ||
                                                req_source == singular::types::RequestSource::RS_MARKET_ORDER)
                                            {
                                                do_send_native_order_latency(internal_order_id);
                                            }
                                            if (ord_status.find("PARTIALLY FILLED") != std::string::npos || ord_status.find("EXECUTED") != std::string::npos)
                                            {

                                                std::regex exec_regex(R"(EXECUTED @ ([0-9.]+)\((-?[0-9.]+)\))");
                                                std::smatch match;
                                                if (std::regex_search(ord_status, match, exec_regex) && match.size() > 2)
                                                {
                                                    double exec_price = std::stod(match[1].str());
                                                    double exec_amount = std::stod(match[2].str());
                                                    double fee = 0.0; // Fee data unavailable

                                                    send_fill(internal_order_id, exec_price, exec_amount, fee, mts_update);
                                                    singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::BITFINEX_DEBUG, "Partially Filled or Executed Order : [OrderID: " + std::to_string(internal_order_id) + ", Exec Price: " + std::to_string(exec_price) + ", Exec Amount: " + std::to_string(exec_amount) + "]");
                                                }
                                            }
                                            else if (ord_status.find("ACTIVE") != std::string::npos)
                                            {
                                                send_place_ack(internal_order_id, price, std::abs(amount));
                                                nlohmann::json reformatted_order;
                                                reformatted_order["client_id"] = std::to_string(client_id); // Assuming client_id is unique per order
                                                reformatted_order["symbol"] = symbol;                      // Replace with your symbol variable
                                                reformatted_order["id"] = std::to_string(id);         // Replace with your unique order id
                                                open_orders_array.push_back(reformatted_order);
                                                singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::BITFINEX_DEBUG, "New Order : [OrderID: " + std::to_string(internal_order_id) + ", Price: " + std::to_string(price) + "]");
                                            }
                                        }
                                    }
                                }

                                // Similar checks for other event types...
                                else if (event_type == "oc")
                                {
                                    if (message.size() > 2)
                                    {
                                        auto [id, gid, client_id, symbol, mts_create, mts_update, amount, amount_orig, order_type, type_prev, mts_tif, flags, status, price, price_avg, price_trailing, price_aux_limit, notify,
                                              hidden, placed_id, routing, meta] = extract_order_data(message[2]);
                                        long long internal_order_id = client_to_internal_id_map_[client_id];
                                        auto req_source = client_id_to_source_map_[client_id];
                                        std::string request_source = singular::types::get_request_source_string[req_source];
                                        auto end_time_rtsc = singular::utility::LatencyMeasure::captureTimestamp();

                                        // Use chrono for end time in nanoseconds
                                        auto end_time = std::chrono::steady_clock::now();
                                        latency_measure->setEndTime(
                                            internal_order_id,
                                            std::chrono::duration_cast<std::chrono::nanoseconds>(end_time.time_since_epoch()),
                                            end_time_rtsc);

                                        // Convert MTS_CREATE and MTS_UPDATE (milliseconds since epoch) to nanoseconds
                                        auto order_creation_time_ns = std::chrono::nanoseconds(mts_create * 1'000'000LL);
                                        auto exchange_update_time_ns = std::chrono::nanoseconds(mts_update * 1'000'000LL);

                                        // Calculate exchange latency
                                        auto exchange_latency = exchange_update_time_ns - order_creation_time_ns;

                                        // Log or process the calculated latency

                                        // Check for executed status in order status description
                                        const std::string ord_status = message[2][13].get<std::string>(); // STATUS field

                                        // For executed market orders (Market orders are considered "cancelled" after execution)
                                        if (ord_status.find("EXECUTED") != std::string::npos)
                                        {
                                            // Market order executed, treat as completed
                                            latency_measure->setExchangeLatency(internal_order_id, exchange_latency);
                                            latency_measure->setFinalCost(internal_order_id, price_avg);

                                            // Send native order latency if request source matches criteria
                                            if (req_source == singular::types::RequestSource::RS_LIMIT_ORDER ||
                                                req_source == singular::types::RequestSource::RS_MARKET_ORDER)
                                            {
                                                do_send_native_order_latency(internal_order_id);
                                            }
                                            send_fill(internal_order_id, price, amount, 0.0, mts_update); // Send the fill acknowledgment
                                            std::string order_type_str = (order_type.find("LIMIT") != std::string::npos) ? "Limit" : "Market";
                                            std::string log_message = "Executed " + order_type_str + " Order : [OrderID: " +
                                                                      std::to_string(internal_order_id) + ", Price: " + std::to_string(price) + "]";

                                            singular::utility::log_event(
                                                log_service_name,
                                                singular::utility::OEMSEvent::BITFINEX_DEBUG,
                                                log_message);
                                        }
                                        else if (ord_status.find("CANCELED") != std::string::npos)
                                        {
                                            // This could be a regular canceled order
                                            send_cancel_ack(internal_order_id);
                                            singular::utility::log_event(
                                                log_service_name,
                                                singular::utility::OEMSEvent::BITFINEX_DEBUG,
                                                "Order Canceled : [OrderID: " + std::to_string(internal_order_id) + ", Price: " + std::to_string(price) + "]");
                                        }
                                    }
                                }

                                else if (event_type == "on")
                                {
                                    if (message.size() > 2)
                                    {
                                        // Handle Order New
                                        auto [id, gid, client_id, symbol, mts_create, mts_update, amount, amount_orig, order_type, type_prev, mts_tif, flags, status, price, price_avg, price_trailing, price_aux_limit, notify,
                                              hidden, placed_id, routing, meta] = extract_order_data(message[2]);
                                        auto internal_order_id = client_to_internal_id_map_[client_id];
                                        auto req_source = client_id_to_source_map_[client_id];
                                        std::string request_source = singular::types::get_request_source_string[req_source];
                                        auto end_time_rtsc = singular::utility::LatencyMeasure::captureTimestamp();
                                        // Use chrono for end time in nanoseconds
                                        auto end_time = std::chrono::steady_clock::now();
                                        latency_measure->setEndTime(
                                            internal_order_id,
                                            std::chrono::duration_cast<std::chrono::nanoseconds>(end_time.time_since_epoch()),
                                            end_time_rtsc);

                                        // Convert MTS_CREATE and MTS_UPDATE (milliseconds since epoch) to nanoseconds
                                        auto order_creation_time_ns = std::chrono::nanoseconds(mts_create * 1'000'000LL);
                                        auto exchange_update_time_ns = std::chrono::nanoseconds(mts_update * 1'000'000LL);

                                        // Calculate exchange latency
                                        auto exchange_latency = exchange_update_time_ns - order_creation_time_ns;

                                        // Log or process the calculated latency
                                        latency_measure->setExchangeLatency(internal_order_id, exchange_latency);
                                        if (req_source == singular::types::RequestSource::RS_LIMIT_ORDER ||
                                            req_source == singular::types::RequestSource::RS_MARKET_ORDER)
                                        {
                                            do_send_native_order_latency(internal_order_id);
                                        }
                                        send_place_ack(internal_order_id, price, std::abs(amount));

                                        nlohmann::json reformatted_order;
                                        reformatted_order["client_id"] = std::to_string(client_id); // Assuming client_id is unique per order
                                        reformatted_order["symbol"] = symbol;                      // Replace with your symbol variable
                                        reformatted_order["id"] = std::to_string(id);         // Replace with your unique order id
                                        open_orders_array.push_back(reformatted_order);

                                        singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::BITFINEX_DEBUG, "New Order notification  : [OrderID: " + std::to_string(internal_order_id) + ", Price: " + std::to_string(price) + "]");
                                    }
                                }
                                order_data_["response"] = "SUCCESS";
                                order_data_["open_orders"] = open_orders_array;

                                // Common log processing for the extracted order data (for debugging or audit purposes)
                                long long internal_order_id = 0;
                                nlohmann::json log_data;
                                if (message.size() > 2)
                                {
                                    auto [id, gid, client_id, symbol, mts_create, mts_update, amount, amount_orig, order_type, type_prev, mts_tif, flags, status, price, price_avg, price_trailing, price_aux_limit, notify,
                                          hidden, placed_id, routing, meta] = extract_order_data(message[2]);

                                    internal_order_id = client_to_internal_id_map_[client_id];
                                    auto req_source = client_id_to_source_map_[client_id];
                                    std::string request_source = singular::types::get_request_source_string[req_source];
                                    log_data["order_id"] = id;
                                    log_data["gid"] = gid;
                                    log_data["cid"] = client_id;
                                    log_data["cid"] = client_id;
                                    log_data["symbol"] = symbol;
                                    log_data["mts_create"] = mts_create;
                                    log_data["mts_update"] = mts_update;
                                    log_data["amount"] = amount;
                                    log_data["amount_orig"] = amount_orig;
                                    log_data["order_type"] = order_type;
                                    log_data["type_prev"] = type_prev;
                                    log_data["mts_tif"] = mts_tif;
                                    log_data["flags"] = flags;
                                    log_data["status"] = status;
                                    // log_data["placeholder1"] = placeholder1;
                                    // log_data["placeholder2"] = placeholder2;
                                    // log_data["placeholder3"] = placeholder3;
                                    log_data["price"] = price;
                                    log_data["price_avg"] = price_avg;
                                    log_data["price_trailing"] = price_trailing;
                                    log_data["price_aux_limit"] = price_aux_limit;
                                    log_data["notify"] = notify;
                                    log_data["hidden"] = hidden;
                                    log_data["placed_id"] = placed_id;
                                    log_data["routing"] = routing;
                                    log_data["meta"] = meta;
                                    log_data["algorithm_id"] = nullptr;
                                    log_data["request_source"] = request_source;

                                    for (const auto &pair : singular::types::getAlgorithmReferenceMap())
                                    {
                                        if (std::find(pair.second.begin(), pair.second.end(), internal_order_id) != pair.second.end())
                                        {
                                            log_data["algorithm_id"] = pair.first;
                                            break; // Remove break if you want to find all keys
                                        }
                                    }
                                }
                                for (const auto &session_id_value : session_map)
                                {
                                    auto it = singular::network::globalWebSocketChannels.find(session_id_value);
                                    nlohmann::json final_data = nlohmann::json::object();
                                    nlohmann::json subs_data = nlohmann::json::array();
                                    subs_data.push_back({{"channel", "order"}, {"data", log_data}});
                                    final_data["exchange"] = "BITFINEX";
                                    final_data["name"] = name_;
                                    final_data["data"] = subs_data;
                                    if (internal_to_credential_id_map_.find(internal_order_id) != internal_to_credential_id_map_.end())
                                    {
                                        final_data["credential_id"] = internal_to_credential_id_map_[internal_order_id];
                                    }
                                    if (it != singular::network::globalWebSocketChannels.end() &&
                                        it->second)
                                    {
                                        singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Sending to Global Websocket Server");
                                        it->second->send(final_data.dump());
                                    }
                                }
                            }

                            else if (message[1] == "ws")
                            { // Wallet Snapshot
                                singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Processing wallet snapshot.");

                                if (message.size() > 2 && message[2].is_array())
                                {
                                    // Iterate through the wallet snapshot array
                                    for (const auto &wallet_array : message[2])
                                    {
                                        if (wallet_array.size() >= 5)
                                        {
                                            std::string wallet_type = wallet_array[0].get<std::string>();
                                            std::string currency = wallet_array[1].get<std::string>();
                                            double balance = wallet_array[2].get<double>();
                                            double unsettled_interest = wallet_array[3].is_null() ? 0.0 : wallet_array[3].get<double>();
                                            double balance_available = wallet_array[4].is_null() ? 0.0 : wallet_array[4].get<double>();

                                            // Update the local account info or add the wallet
                                            bool wallet_found = false;
                                            for (auto &wallet : account_info_["data"])
                                            {
                                                if (wallet["wallet_type"] == wallet_type && wallet["currency"] == currency)
                                                {
                                                    wallet["balance"] = balance;                       // Update balance
                                                    wallet["unsettled_interest"] = unsettled_interest; // Update unsettled interest
                                                    wallet["balance_available"] = balance_available;   // Update balance available
                                                    wallet_found = true;
                                                    break;
                                                }
                                            }
                                            if (!wallet_found)
                                            {
                                                // Add new wallet entry
                                                nlohmann::json new_wallet = {
                                                    {"wallet_type", wallet_type},
                                                    {"currency", currency},
                                                    {"balance", balance},
                                                    {"unsettled_interest", unsettled_interest},
                                                    {"balance_available", balance_available}};
                                                account_info_["data"].push_back(new_wallet);
                                            }
                                        }
                                    }
                                }
                            }
                            else if (message[1] == "wu")
                            { // Wallet Update

                                if (message.size() > 2 && message[2].is_array())
                                {
                                    const auto &wallet_array = message[2];
                                    if (wallet_array.size() >= 5)
                                    {
                                        std::string wallet_type = wallet_array[0].get<std::string>();
                                        std::string currency = wallet_array[1].get<std::string>();
                                        double balance = wallet_array[2].get<double>();
                                        double unsettled_interest = wallet_array[3].is_null() ? 0.0 : wallet_array[3].get<double>();
                                        double balance_available = wallet_array[4].is_null() ? 0.0 : wallet_array[4].get<double>();

                                        // Update the wallet balance for the given currency
                                        bool wallet_found = false;
                                        for (auto &wallet : account_info_["data"])
                                        {
                                            if (wallet["wallet_type"] == wallet_type && wallet["currency"] == currency)
                                            {
                                                wallet["balance"] = balance;                       // Update balance
                                                wallet["unsettled_interest"] = unsettled_interest; // Update unsettled interest
                                                wallet["balance_available"] = balance_available;   // Update balance available
                                                wallet_found = true;
                                                break;
                                            }
                                        }
                                        if (!wallet_found)
                                        {
                                            // Add new wallet entry if not found
                                            nlohmann::json new_wallet = {
                                                {"wallet_type", wallet_type},
                                                {"currency", currency},
                                                {"balance", balance},
                                                {"unsettled_interest", unsettled_interest},
                                                {"balance_available", balance_available}};
                                            account_info_["data"].push_back(new_wallet);
                                        }
                                    }
                                }
                                singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Wallet updated.");
                            }
                            else if (message[1] == "bu") // Balance Update
                            {
                                if (message.size() > 2 && message[2].is_array())
                                {
                                    const auto &balance_array = message[2];
                                    if (balance_array.size() >= 2)
                                    {
                                        double aum = balance_array[0].get<double>();     // Total Assets Under Management
                                        double aum_net = balance_array[1].get<double>(); // Net Assets Under Management

                                        // Update balance information
                                        account_info_["balance"]["aum"] = aum;
                                        account_info_["balance"]["aum_net"] = aum_net;

                                        // Log the updated balance info
                                        singular::utility::log_event(
                                            log_service_name,
                                            singular::utility::OEMSEvent::WS_CONNECTION_DEBUG,
                                            "Balance updated: AUM = " + std::to_string(aum) +
                                                ", AUM_NET = " + std::to_string(aum_net));
                                    }
                                }
                                else
                                {
                                    singular::utility::log_event(
                                        log_service_name,
                                        singular::utility::OEMSEvent::WS_CONNECTION_ERROR,
                                        "Invalid balance update message format.");
                                }
                            }

                            else if (message[1] == "miu")
                            { // Margin Info Update

                                if (message.size() > 2 && message[2].is_array())
                                {
                                    const auto &update_array = message[2];

                                    // Check if it's a "base" update
                                    if (update_array[0] == "base" && update_array[1].is_array())
                                    {
                                        const auto &base_update_array = update_array[1];
                                        if (base_update_array.size() >= 5)
                                        {
                                            double user_pl = base_update_array[0].is_null() ? 0.0 : base_update_array[0].get<double>();
                                            double user_swaps = base_update_array[1].is_null() ? 0.0 : base_update_array[1].get<double>();
                                            double margin_balance = base_update_array[2].is_null() ? 0.0 : base_update_array[2].get<double>();
                                            double margin_net = base_update_array[3].is_null() ? 0.0 : base_update_array[3].get<double>();
                                            double margin_required = base_update_array[4].is_null() ? 0.0 : base_update_array[4].get<double>();

                                            // Dump parsed base margin info into account_info_
                                            account_info_["margin"]["base"] = {
                                                {"user_pl", user_pl},
                                                {"user_swaps", user_swaps},
                                                {"margin_balance", margin_balance},
                                                {"margin_net", margin_net},
                                                {"margin_required", margin_required}};
                                        }
                                    }
                                    // Check if it's a "sym" (symbol) update
                                    else if (update_array[0] == "sym" && update_array[2].is_array())
                                    {
                                        std::string symbol = update_array[1].get<std::string>();
                                        const auto &sym_update_array = update_array[2];

                                        if (sym_update_array.size() >= 4)
                                        {
                                            double tradable_balance = sym_update_array[0].is_null() ? 0.0 : sym_update_array[0].get<double>();
                                            double gross_balance = sym_update_array[1].is_null() ? 0.0 : sym_update_array[1].get<double>();
                                            double buy = sym_update_array[2].is_null() ? 0.0 : sym_update_array[2].get<double>();
                                            double sell = sym_update_array[3].is_null() ? 0.0 : sym_update_array[3].get<double>();

                                            // Dump parsed symbol-specific margin info into account_info_
                                            account_info_["margin"]["symbols"][symbol] = {
                                                {"tradable_balance", tradable_balance},
                                                {"gross_balance", gross_balance},
                                                {"buy", buy},
                                                {"sell", sell}};
                                        }
                                    }
                                }

                                // Log margin info update
                                singular::utility::log_event(
                                    log_service_name,
                                    singular::utility::OEMSEvent::WS_CONNECTION_DEBUG,
                                    "Margin info updated.");
                            }

                            else if (message[1] == "ps")
                            {
                                // Position snapshot, comes once after succesful authentication

                                for (const auto &position : message[2])
                                {
                                    std::string symbol = position[0]; // SYMBOL
                                    if (symbol[0] == 't')
                                    {
                                        symbol = symbol.substr(1); // Remove the 't' from the start of the symbol
                                    }

                                    const double position_value = position[2]; // AMOUNT

                                    if (!position_data_.contains("positions") || !position_data_["positions"].is_array())
                                    {
                                        position_data_["positions"] = nlohmann::json::array();
                                    }

                                    // Search if a position corresponding to the symbol already
                                    // exists
                                    bool found = false;
                                    size_t idx;
                                    if (positions_symbol_to_position_idx_map_.find(symbol) != positions_symbol_to_position_idx_map_.end())
                                    {
                                        found = true;
                                        idx = positions_symbol_to_position_idx_map_[symbol];
                                    }

                                    if (position_value == 0)
                                    {
                                        // Position closed, erase the position entry for the symbol from the
                                        // position_data_ json
                                        if (found)
                                        {
                                            position_data_["positions"].erase(idx);

                                            // Remove the entry for the symbol from the positions map,
                                            // as the position no longer exists
                                            positions_symbol_to_position_idx_map_.erase(symbol);

                                            // Update the indexes of symbols (in the positions map) for symbols
                                            // which were present after the symbol whose position we just erased.
                                            for (auto it = std::next(position_data_["positions"].begin(), idx);
                                                 it != position_data_["positions"].end();
                                                 ++it)
                                            {
                                                const std::string &sym = (*it)["symbol"];
                                                positions_symbol_to_position_idx_map_[sym]--;
                                            }
                                        }
                                    } // if (position_value == 0) ends
                                    else
                                    {
                                        // Add or update the position for the symbol
                                        if (found)
                                        {
                                            // Update the position
                                            position_data_["positions"][idx] = nlohmann::json{
                                                {"amount", position_value},
                                                {"base_price", position[3]},
                                                {"collateral", position[16]},
                                                {"collateral_min", position[17]},
                                                {"leverage", position[9].is_null() ? 0.0 : position[9].get<double>()},
                                                {"margin_funding", position[4]},
                                                {"margin_funding_type", position[5]},
                                                {"pl", position[6].is_null() ? 0.0 : position[6].get<double>()},
                                                {"pl_perc", position[7].is_null() ? 0.0 : position[7].get<double>()},
                                                {"position_id", position[11]},
                                                {"price_liq", position[8].is_null() ? 0.0 : position[8].get<double>()},
                                                {"status", position[1]},
                                                {"symbol", symbol}};
                                        }
                                        else
                                        {
                                            // Add the position
                                            position_data_["positions"].push_back(nlohmann::json{
                                                {"amount", position_value},
                                                {"base_price", position[3]},
                                                {"collateral", position[16]},
                                                {"collateral_min", position[17]},
                                                {"leverage", position[9].is_null() ? 0.0 : position[9].get<double>()},
                                                {"margin_funding", position[4]},
                                                {"margin_funding_type", position[5]},
                                                {"pl", position[6].is_null() ? 0.0 : position[6].get<double>()},
                                                {"pl_perc", position[7].is_null() ? 0.0 : position[7].get<double>()},
                                                {"position_id", position[11]},
                                                {"price_liq", position[8].is_null() ? 0.0 : position[8].get<double>()},
                                                {"status", position[1]},
                                                {"symbol", symbol}});

                                            // Update the positions map to store the index of the newly
                                            // added position (corresponding to the symbol)
                                            positions_symbol_to_position_idx_map_[symbol] = (position_data_["positions"].size() - 1);
                                        }
                                    }
                                } // iterating over positions ends

                                position_data_["empty"] = (position_data_["positions"].size() == 0);
                            }
                            else if (message[1] == "pn" || message[1] == "pu" || message[1] == "pc")
                            {
                                try
                                {
                                    std::string symbol = message[2][0]; // SYMBOL
                                    if (symbol[0] == 't')
                                    {
                                        symbol = symbol.substr(1); // Remove the 't' from the start of the symbol
                                    }

                                    const double position_value = message[2][2]; // AMOUNT

                                    // Initialize positions as an array if not already
                                    if (!position_data_.contains("positions") || !position_data_["positions"].is_array())
                                    {
                                        position_data_["positions"] = nlohmann::json::array();
                                    }

                                    // Search if a position corresponding to the symbol already exists
                                    bool found = false;
                                    size_t idx = 0;
                                    if (positions_symbol_to_position_idx_map_.find(symbol) != positions_symbol_to_position_idx_map_.end())
                                    {
                                        found = true;
                                        idx = positions_symbol_to_position_idx_map_[symbol];
                                    }

                                    if (position_value == 0)
                                    {
                                        // Position closed, erase the position entry for the symbol
                                        if (found)
                                        {
                                            position_data_["positions"].erase(idx);

                                            // Remove the entry for the symbol from the positions map
                                            positions_symbol_to_position_idx_map_.erase(symbol);

                                            // Update the indexes of symbols for entries after the erased one
                                            for (auto it = std::next(position_data_["positions"].begin(), idx);
                                                 it != position_data_["positions"].end();
                                                 ++it)
                                            {
                                                const std::string &sym = (*it)["symbol"];
                                                positions_symbol_to_position_idx_map_[sym]--;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        // Add or update the position for the symbol
                                        nlohmann::json position_entry = {
                                            {"amount", position_value},
                                            {"base_price", message[2][3]},
                                            {"collateral", message[2][16]},
                                            {"collateral_min", message[2][17]},
                                            {"leverage", message[2][9].is_null() ? 0.0 : message[2][9].get<double>()},
                                            {"margin_funding", message[2][4]},
                                            {"margin_funding_type", message[2][5]},
                                            {"pl", message[2][6].is_null() ? 0.0 : message[2][6].get<double>()},
                                            {"pl_perc", message[2][7].is_null() ? 0.0 : message[2][7].get<double>()},
                                            {"position_id", message[2][11]},
                                            {"price_liq", message[2][8].is_null() ? 0.0 : message[2][8].get<double>()},
                                            {"status", message[2][1]},
                                            {"symbol", symbol}};

                                        if (found)
                                        {
                                            // Update the existing position
                                            position_data_["positions"][idx] = position_entry;
                                        }
                                        else
                                        {
                                            // Add a new position
                                            position_data_["positions"].push_back(position_entry);

                                            // Update the positions map
                                            positions_symbol_to_position_idx_map_[symbol] = position_data_["positions"].size() - 1;
                                        }
                                    }

                                    // Update the empty status
                                    position_data_["empty"] = (position_data_["positions"].size() == 0);
                                }
                                catch (std::exception &e)
                                {
                                    std::cout << "Got exception in position .. " << e.what() << std::endl;
                                }
                            }
                            else if (message[1] == "n")
                            {

                                // notifications
                                // https://docs.bitfinex.com/reference/ws-auth-notifications
                                const std::string notif_type = message[2][1];
                                const std::string notif_status = message[2][6];
                                const std::string notif_text = message[2][7];
                                long long client_id = message[2][4][2];

                                bool notif_status_err = (notif_status == "ERROR" || notif_status == "FAILURE");
                                long long internal_order_id = client_to_internal_id_map_[client_id];
                                if (notif_status_err)
                                {
                                    if (notif_type == "oc-req")
                                    {
                                        // order cancel failed
                                        send_place_reject(internal_order_id, singular::types::RejectType::OTHER);
                                        singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::CANCEL_ORDER_ERROR, notif_text);
                                        send_reject_response(message[2]);
                                    }
                                    else if (notif_type == "on-req")
                                    {
                                        // order new (place) failed
                                        send_place_reject(internal_order_id, singular::types::RejectType::OTHER);
                                        singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::PLACE_ORDER_ERROR, notif_text);
                                        send_reject_response(message[2]);
                                    }
                                    else
                                    {
                                        // a notification indicating some failure
                                        singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Received failure/error notification: " + buffer);
                                    }
                                } // if (notif_status_err) ends
                                else
                                {
                                    // Notification does not indicate error
                                    singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Received notification " + buffer);
                                }
                            } // else if (message[1] == "n") ends
                        }
                        else if (orderbook_channel_id_to_symbol_map_.find(channel_id) != orderbook_channel_id_to_symbol_map_.end())
                        {

                            // Retrieve the symbol associated with the channel_id
                            auto it = orderbook_channel_id_to_symbol_map_.find(channel_id);
                            std::string symbol;

                            if (it != orderbook_channel_id_to_symbol_map_.end())
                            {
                                symbol = it->second;
                            }
                            else
                            {
                                std::cerr << "Channel ID " << channel_id << " not found in orderbook_channel_id_to_symbol_map_!" << std::endl;
                                return; // Exit processing for this message
                            }

                            // Ignore "hb" (heartbeat) messages
                            if (message[1].is_string() && message[1] == "hb")
                            {
                                // Skip processing as this is just a heartbeat message
                                return;
                            }

                            // Ensure `message[1]` exists and is valid
                            if (!message[1].is_array())
                            {
                                // Silently ignore non-array messages that are not heartbeats
                                return;
                            }

                            // Process snapshots or bulk updates
                            if (message[1][0].is_array())
                            {
                                // A snapshot or a bulk update (multiple bids/asks)
                                for (const auto &elem : message[1])
                                {
                                    if (!elem.is_array() || elem.size() < 3)
                                    {
                                        // Skip invalid entries silently
                                        continue;
                                    }

                                    double price_level = elem[0];
                                    unsigned int count = elem[1];
                                    double amount = elem[2];

                                    // Determine if it's a bid or an ask
                                    bool bid = (amount > 0);
                                    amount = std::abs(amount);

                                    send_orderbook_update(singular::types::Exchange::BITFINEX, symbol, false,
                                                          bid ? singular::types::Side::BUY : singular::types::Side::SELL,
                                                          price_level, amount);
                                }
                            }
                            else if (message[1].size() == 3)
                            {
                                // Process a single update
                                double price_level = message[1][0];
                                unsigned int count = message[1][1];
                                double amount = message[1][2];

                                // Determine if it's a bid or an ask
                                bool bid = (amount > 0);
                                amount = std::abs(amount);

                                send_orderbook_update(singular::types::Exchange::BITFINEX, symbol, false,
                                                      bid ? singular::types::Side::BUY : singular::types::Side::SELL,
                                                      price_level, amount);
                            }
                        }
                        else if (trades_channel_id_to_symbol_map_.find(channel_id) != trades_channel_id_to_symbol_map_.end())
                        {

                            // Received data on the trades channel for a particular symbol
                            std::string symbol = trades_channel_id_to_symbol_map_[channel_id];
                            // https://docs.bitfinex.com/reference/ws-public-trades

                            // We only have to care about trading pairs (not funding currencies) and
                            // parse responses accordingly.

                            if (message[1].is_array())
                            {
                                // It is a snapshot and trade[1] is an array
                                // of trade arrays

                                // Iterate over the trades in the snapshot
                                for (const auto trade : message[1])
                                {
                                    double price = trade[3];

                                    // positive means buy, negative means sell
                                    double amount = trade[2];
                                    bool buy = (amount > 0);

                                    // assign absolute value
                                    amount = std::abs(amount);

                                    long long timestamp_ms = trade[1];               // Trade[1] is the timestamp
                                    std::time_t timestamp_sec = timestamp_ms / 1000; // Convert to seconds

                                    // Format timestamp to ISO 8601
                                    std::tm *tm = std::gmtime(&timestamp_sec); // Convert to UTC time structure
                                    char buffer[30];
                                    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", tm);

                                    std::string entry_date(buffer);

                                    send_trade(singular::types::Exchange::BITFINEX,
                                               symbol,
                                               (buy) ? singular::types::Side::BUY : singular::types::Side::SELL,
                                               price,
                                               amount,
                                               entry_date);
                                } // iterating over trades ends
                            } // if message is trades snapshot ends
                            else if (message[1].is_string() &&
                                     (message[1] == "te" || message[1] == "tu"))
                            {
                                // trade executed or trade updated, message[2] is a trade array
                                double price = message[2][3];

                                // positive means buy, negative means sell
                                double amount = message[2][2];
                                bool buy = (amount > 0);

                                // assign absolute value
                                amount = std::abs(amount);

                                long long timestamp_ms = message[2][1];          // Trade[1] is the timestamp
                                std::time_t timestamp_sec = timestamp_ms / 1000; // Convert to seconds

                                // Format timestamp to ISO 8601
                                std::tm *tm = std::gmtime(&timestamp_sec); // Convert to UTC time structure
                                char buffer[30];
                                std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", tm);

                                std::string entry_date(buffer);
                                send_trade(singular::types::Exchange::BITFINEX,
                                           symbol,
                                           (buy) ? singular::types::Side::BUY : singular::types::Side::SELL,
                                           price,
                                           amount,
                                           entry_date);
                            } // else if (trade update or trade executed) ends
                            else
                            {
                                // It may be a funding trade executed/updated ("fte", "ftu"), ignore
                                // that, do nothing
                            }
                        } // else if (message on trades channel for a symbol) ends
                        else if (ticker_channel_id_to_symbol_map_.find(channel_id) != ticker_channel_id_to_symbol_map_.end())
                        {
                            // Retrieve the symbol associated with the channel_id
                            auto it = ticker_channel_id_to_symbol_map_.find(channel_id);
                            std::string symbol;

                            if (it != ticker_channel_id_to_symbol_map_.end())
                            {
                                symbol = it->second;
                            }
                            else
                            {
                                std::cerr << "Channel ID " << channel_id << " not found in ticker_channel_id_to_symbol_map_!" << std::endl;
                                return; // Exit processing for this message
                            }

                            // Ignore "hb" (heartbeat) messages
                            if (message[1].is_string() && message[1] == "hb")
                            {
                                return; // Skip processing for heartbeats
                            }

                            // Check if `message[1]` is an array containing ticker data
                            if (message[1].is_array() && message[1].size() >= 10)
                            {
                                const auto &ticker_data = message[1];

                                double new_best_bid = ticker_data[0].get<double>();          // BID
                                double bid_size = ticker_data[1].get<double>();              // BID_SIZE
                                double new_best_ask = ticker_data[2].get<double>();          // ASK
                                double ask_size = ticker_data[3].get<double>();              // ASK_SIZE
                                double daily_change = ticker_data[4].get<double>();          // DAILY_CHANGE
                                double daily_change_relative = ticker_data[5].get<double>(); // DAILY_CHANGE_RELATIVE
                                double last_price = ticker_data[6].get<double>();            // LAST_PRICE
                                double volume = ticker_data[7].get<double>();                // VOLUME
                                double high_24h = ticker_data[8].get<double>();              // HIGH
                                double low_24h = ticker_data[9].get<double>();               // LOW

                                if (volume != 0 && new_best_ask != 0 && new_best_bid != 0)
                                {
                                    // std::cout << "Symbol : " << symbol << "\nNew ask : " << new_best_ask << "\nAsk size : " << ask_size << "\nNew bid : " << new_best_bid << "\nBid size : " << bid_size << "\nVolume : " << volume << std::endl;
                                    send_ticker(singular::types::Exchange::BITFINEX, symbol,
                                                new_best_ask, ask_size, new_best_bid, bid_size, volume / 86400);
                                }
                            }
                        }
                        else
                        {
                            // Unknown array response
                            singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Unknown array response: " + buffer);
                        }
                    } // else if (message.is_array()) ends
                    else
                    {
                        // Unknown kind of response
                        singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Received unknown bitfinex response " + buffer);
                    }
                }
                catch (const nlohmann::json::parse_error &e)
                {
                    singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_ERROR, std::string("JSON parse error: ") + e.what());
                    return; // Exit the function as parsing failed
                }
                // std::cout << "Message recieved in parse websocket function : " << message.dump() << std::endl;
            }

            void Gateway::initialize_callback_funcs()
            {
            }

            std::string Gateway::convert_timestamp_to_string(long long timestamp_ms)
            {
                // Convert milliseconds to seconds
                std::time_t timestamp_sec = static_cast<std::time_t>(timestamp_ms / 1000);

                // Convert to UTC time structure
                std::tm *tm = std::gmtime(&timestamp_sec);

                // Format the timestamp as ISO 8601
                char buffer[30];
                std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", tm);

                // Return as a string
                return std::string(buffer);
            }

            unsigned long long Gateway::get_client_id(singular::types::OrderId order_id)
            {
                const auto now = std::chrono::system_clock::now();
                const auto timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                              now.time_since_epoch())
                                              .count();

                unsigned int normalized_order_id = order_id % 1000;

                return (timestamp_ms / 1000) * 1000 + normalized_order_id + 1;
            }

            void Gateway::do_place(singular::types::Symbol symbol, singular::types::InstrumentType type,
                                   singular::types::OrderId order_id, singular::types::OrderType order_type,
                                   singular::types::Side side, double price, double quantity, singular::types::RequestSource source,
                                   std::string credential_id, std::string td_mode)
            {

                auto client_id = get_client_id(order_id);
                std::cout << "Order ID is : " << order_id << std::endl;
                std::string type_string = singular::types::get_order_type_string[order_type];
                std::transform(type_string.begin(), type_string.end(), type_string.begin(), ::toupper);

                // Get date of the order
                auto now = std::chrono::system_clock::now();
                std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
                std::tm now_tm = *std::localtime(&now_time_t);

                std::ostringstream date_stream;
                date_stream << std::put_time(&now_tm, "%Y-%m-%d");
                std::string client_order_date = date_stream.str();

                const std::string amount = (side == singular::types::Side::BUY ? std::to_string(quantity) : "-" + std::to_string(quantity));

                nlohmann::json params;
                if (type_string == "LIMIT")
                {
                    params["price"] = std::to_string(price);
                }
                if (std::string(mode_) == "DEV")
                {
                    type_string = "EXCHANGE " + type_string;
                }
                // Ensure all string conversions are explicit and safe
                params["cid"] = client_id;  // Convert to string explicitly
                params["type"] = std::string(type_string);  // Ensure type_string is copied
                params["symbol"] = "t" + std::string(symbol);  // Explicit string conversion
                params["amount"] = std::string(amount);  // Ensure amount is copied

                nlohmann::json message;
                message[0] = 0;
                message[1] = "on";
                message[2] = std::string("");  // Explicit empty string
                message[3] = std::move(params);  // Move params to avoid any copying issues

                #ifdef TEST_BUILD
                    this->mock_generated_messages.push_back(message.dump()); // Store the generated message for tests
                    return;
                #else
                    auto end_time_rtsc = singular::utility::LatencyMeasure::captureTimestamp();
                    latency_measure->stopMeasurement(order_id, end_time_rtsc);
                    private_client_->send(message.dump());
                #endif

                client_id_to_side_map_[client_id] = side;
                client_id_to_price_map_[client_id] = price;
                client_id_to_qty_map_[client_id] = quantity;
                client_id_to_symbol_map_[client_id] = symbol;
                client_to_internal_id_map_[client_id] = order_id;
                internal_to_client_id_map_[order_id] = client_id;
                client_id_to_source_map_[client_id] = source;
                client_id_to_date_map_[client_id] = client_order_date;

                if (credential_id != "")
                {
                    internal_to_credential_id_map_[order_id] = credential_id;
                }

                std::string source_string = singular::types::get_request_source_string[source];
                if ((source_string == "market") || (source_string == "limit"))
                {
                    stream_order_data({{"id", client_id}}, "received");
                }

                char log_message[100];
                int chars_count = std::sprintf(log_message, "Sent a place %s order for %s@%s at %.2f", amount.c_str(), symbol.c_str(), type_string.c_str(), price);
                log_message[chars_count] = '\0';
                singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::PLACE_ORDER_DEBUG, log_message);
            }

            void Gateway::do_websocket_task_latency(singular::utility::TimePoint latency_info, std::string credential_id)
            {
                try
                {
                    for (const auto &session_id_value : order_execution_quality_session_map)
                    {
                        auto it = singular::network::globalWebSocketChannels.find(session_id_value);
                        nlohmann::json final_data = nlohmann::json::object();
                        nlohmann::json subs_data = nlohmann::json::array();
                        nlohmann::json latency_data = nlohmann::json::array();
                        nlohmann::json slippage_value = (latency_info.slippage_percentage == std::numeric_limits<double>::max())
                                                            ? nlohmann::json()
                                                            : nlohmann::json(latency_info.slippage_percentage);

                        latency_data.push_back({
                            {"start_time", latency_info.start_time.count()},                 // Start time in nanoseconds
                            {"end_time", latency_info.end_time.count()},                     // End time in nanoseconds
                            {"internal_latency", latency_info.internal_latency.count()},     // Internal latency in microseconds
                            {"exchange_latency", latency_info.exchange_latency.count()},     // Exchange latency in microseconds
                            {"round_trip_latency", latency_info.round_trip_latency.count()}, // Round Trip Latency in microseconds
                            {"algorithm_id", latency_info.algo_id},                          // Algorithm ID
                            {"slippage_percentage", slippage_value}                          // Slippage
                        });
                        subs_data.push_back({{"channel", "order_execution_quality"}, {"data", latency_data}});
                        final_data["exchange"] = "BITFINEX";
                        final_data["data"] = subs_data;
                        final_data["name"] = name_;
                        if (credential_id != "")
                        {
                            final_data["credential_id"] = credential_id;
                        }
                        if (it != singular::network::globalWebSocketChannels.end() && it->second)
                        {
                            singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Sending to Global Websocket Server");
                            it->second->send(final_data.dump());
                        }
                    }
                }
                catch (const std::exception &e)
                {
                    singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::DEFAULT_ERROR, "Error while sending data over global websocket: " + std::string(e.what()));
                }
            }

            void Gateway::do_cancel(singular::types::OrderId order_id, singular::types::RequestSource source)
            {
                auto it = internal_to_client_id_map_.find(order_id);
                if (it == internal_to_client_id_map_.end())
                {
                    singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::CANCEL_ORDER_ERROR,
                                                 "Order ID not found in internal_to_client_id_map.");
                    return;
                }

                // Retrieve the client ID
                auto client_id = it->second;

                // Search for the client_id in the map to find the associated date
                auto date_it = client_id_to_date_map_.find(client_id);
                std::string cid_date;
                if (date_it != client_id_to_date_map_.end())
                {
                    cid_date = date_it->second;
                }
                else
                {
                    std::cerr << "Client ID not found: " << client_id << std::endl;
                    return;
                }

                nlohmann::json params;
                params["cid"] = client_id;
                params["cid_date"] = cid_date;

                nlohmann::json message;
                message[0] = 0;
                message[1] = "oc";
                message[2] = nullptr;
                message[3] = params;

                #ifdef TEST_BUILD
                    this->mock_generated_messages.push_back(message.dump()); // Store the generated message for tests
                #else
                    private_client_->send(message.dump());
                #endif

                std::string log_message = "Sent a cancel order request for Order ID: " + std::to_string(order_id) +
                                          " with Client ID: " + std::to_string(client_id);
                singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::CANCEL_ORDER_DEBUG, log_message);
            }

            void Gateway::do_cancel(std::string exchange_order_id, singular::types::Instrument *instrument, singular::types::RequestSource source)
            {

                nlohmann::json params;
                params["id"] = std::stoll(exchange_order_id);

                nlohmann::json message;
                message[0] = 0;
                message[1] = "oc";
                message[2] = nullptr;
                message[3] = params;

                // Send the cancel request
                private_client_->send(message.dump());

                // Clear all open orders

                if (!order_data_.empty() && !open_orders_array.empty())
                {
                    order_data_.clear();
                    open_orders_array.clear();
                }

                std::string log_message = "Sent a cancel order  for symbol " + exchange_order_id;

                singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::CANCEL_ORDER_DEBUG, log_message);
            }

            void Gateway::do_modify(singular::types::Order *order, double quantity, double price, singular::types::RequestSource source)
            {
                auto client_id = internal_to_client_id_map_[order->id_];
                auto symbol = internal_id_symbol_map_[order->id_];
                auto credential_id = internal_to_credential_id_map_[client_id];
                modify_order_vect.push_back(client_id);
                do_cancel(order->id_, source);
                if (order->instrument_ == nullptr)
                {
                    singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::MODIFY_ORDER_ERROR, "Order instrument is null in do_modify.");
                    return; // Exit the function as instrument data is missing
                }
                do_place(order->instrument_->symbol_, order->instrument_->type_, order->id_, order->type_, order->side_, price, quantity, source, credential_id);
            }

            void Gateway::parse_http_response(const HttpResponse &response, const std::string op)
            {
            }

            void Gateway::do_send_native_order_latency(long long internal_order_id)
            {
                try
                {
                    auto latency_info = latency_measure->get_latency(internal_order_id);
                    if (latency_info) // Check if latency_info has a value
                    {
                        auto it = internal_to_credential_id_map_.find(internal_order_id);
                        if (it == internal_to_credential_id_map_.end())
                        {
                            singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::BITFINEX_ERROR, "Credential ID not found for internal_order_id: " + std::to_string(internal_order_id));
                            return;
                        }
                        auto credential_id = it->second;
                        do_websocket_task_latency(*latency_info, credential_id); // Dereference the optional
                    }
                }
                catch (const std::invalid_argument &e)
                {
                    // Handle invalid argument exception for time string conversion
                    singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::DEFAULT_ERROR, "Error: Invalid time string provided for order_id " + std::to_string(internal_order_id) + "Exception: " + std::string(e.what()));
                }
                catch (const std::out_of_range &e)
                {
                    // Handle out-of-range exception for time value
                    singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::DEFAULT_ERROR, "Error: Time value out of range for order_id " + std::to_string(internal_order_id) + "Exception: " + std::string(e.what()));
                }
            }

            void Gateway::do_subscribe_orderbooks(std::vector<singular::types::Symbol> &symbols)
            {
                for (const auto &symbol : symbols)
                {
                    // Prepare the subscription message for the order book
                    nlohmann::json message = {
                        {"event", "subscribe"},
                        {"channel", "book"},
                        {"symbol", "t" + symbol} // Assuming symbols don't include the "t" prefix
                    };

                    // Send the subscription message via WebSocket
                    #ifdef TEST_BUILD
                        this->mock_generated_messages.push_back(message.dump()); // Store the generated message for tests
                    #else
                        public_client_->send(message.dump());
                    #endif

                    // Log the subscription
                    singular::utility::log_event(
                        log_service_name,
                        singular::utility::OEMSEvent::ORDERBOOK_SUBSCRIBE_SUCCESS,
                        "Subscribing order book channel",
                        message.dump());
                }
            }

            void Gateway::do_unsubscribe_orderbooks(std::vector<singular::types::Symbol> &symbols)
            {

                for (const auto &symbol : symbols)
                {
                    long long channel_id;
                    auto it = orderbook_symbol_to_channel_id_map_.find(symbol);
                    if (it != orderbook_symbol_to_channel_id_map_.end())
                    {
                        channel_id = it->second;
                    }

                    // Construct the subscription message
                    nlohmann::json message = {
                        {"event", "unsubscribe"},
                        {"chanId", channel_id}};
                    #ifdef TEST_BUILD
                        this->mock_generated_messages.push_back(message.dump()); // Store the generated message for tests
                    #else  
                    // Send the message using the public client
                    public_client_->send(message.dump());
                    #endif
                    // Log the subscription
                    singular::utility::log_event(
                        log_service_name,
                        singular::utility::OEMSEvent::ORDERBOOK_CHANNEL,
                        "Unsubscribing order book channel",
                        message.dump());
                }
            }

            void Gateway::do_subscribe_tickers(std::vector<singular::types::Symbol> &symbols)
            {
                for (const auto &symbol : symbols)
                {
                    // Prepare the subscription message for the order book
                    nlohmann::json message = {
                        {"event", "subscribe"},
                        {"channel", "ticker"},
                        {"symbol", "t" + symbol} // Assuming symbols don't include the "t" prefix
                    };

                    #ifdef TEST_BUILD
                        this->mock_generated_messages.push_back(message.dump()); // Store the generated message for tests
                    #else
                        // Send the subscription message via WebSocket
                        public_client_->send(message.dump());
                    #endif

                    // Log the subscription
                    singular::utility::log_event(
                        log_service_name,
                        singular::utility::OEMSEvent::TICKER_SUBSCRIBE_SUCCESS,
                        "Subscribing ticker channel",
                        message.dump());
                }
            }

            void Gateway::do_unsubscribe_tickers(std::vector<singular::types::Symbol> &symbols)
            {
                for (const auto &symbol : symbols)
                {
                    long long channel_id;
                    auto it = ticker_symbol_to_channel_id_map_.find(symbol);
                    if (it != ticker_symbol_to_channel_id_map_.end())
                    {
                        channel_id = it->second;
                    }

                    // Construct the subscription message
                    nlohmann::json message = {
                        {"event", "unsubscribe"},
                        {"chanId", channel_id}};

                    #ifdef TEST_BUILD
                        this->mock_generated_messages.push_back(message.dump()); // Store the generated message for tests
                    #else
                        // Send the message using the public client
                        public_client_->send(message.dump());
                    #endif
                    // Log the subscription
                    singular::utility::log_event(
                        log_service_name,
                        singular::utility::OEMSEvent::TICKER_CHANNEL,
                        "Unsubscribing ticker channel",
                        message.dump());
                }
            }

            void Gateway::do_subscribe_trades(std::vector<singular::types::Symbol> &symbols)
            {
                for (const auto &symbol : symbols)
                {
                    // Prepend 't' to the symbol if required by the API
                    std::string formatted_symbol = "t" + symbol;

                    // Construct the subscription message
                    nlohmann::json message = {
                        {"event", "subscribe"},
                        {"channel", "trades"},
                        {"symbol", formatted_symbol}};

                    #ifdef TEST_BUILD
                        this->mock_generated_messages.push_back(message.dump()); // Store the generated message for tests
                    #else
                        // Send the message using the public client
                        public_client_->send(message.dump());
                    #endif

                    // Log the subscription event
                    singular::utility::log_event(
                        log_service_name,
                        singular::utility::OEMSEvent::LASTTRADES_SUBSCRIBE_SUCCESS,
                        "Subscribing trades channel",
                        message.dump());
                }
            }

            void Gateway::do_unsubscribe_trades(std::vector<singular::types::Symbol> &symbols)
            {
                for (const auto &symbol : symbols)
                {
                    long long channel_id;
                    auto it = trades_symbol_to_channel_id_map_.find(symbol);
                    if (it != trades_symbol_to_channel_id_map_.end())
                    {
                        // Key exists, safe to access the value
                        channel_id = it->second;
                        // Use map_symbol here
                    }

                    // Construct the subscription message
                    nlohmann::json message = {
                        {"event", "unsubscribe"},
                        {"chanId", channel_id}};

                    #ifdef TEST_BUILD   
                        this->mock_generated_messages.push_back(message.dump()); // Store the generated message for tests
                    #else
                        // Send the message using the public client
                        public_client_->send(message.dump());
                    #endif

                    // Log the subscription event
                    singular::utility::log_event(
                        log_service_name,
                        singular::utility::OEMSEvent::LASTTRADES_CHANNEL,
                        "Unsubscribing trades channel",
                        message.dump());
                }
            }

            void Gateway::do_subscribe_top_of_book(std::vector<singular::types::Symbol> &symbols)
            {
            }

            void Gateway::do_subscribe_funding(std::vector<singular::types::Symbol> &symbols)
            {
            }

            singular::types::GatewayStatus Gateway::status()
            {
                if (authenticate_)
                {
                    if ((public_status_ == singular::types::GatewayStatus::ONLINE) &&
                        (private_status_ == singular::types::GatewayStatus::ONLINE) &&
                        (authenticated_))
                    {
                        return singular::types::GatewayStatus::ONLINE;
                    }
                    else
                    {
                        return singular::types::GatewayStatus::OFFLINE;
                    }
                }
                else
                {
                    if (public_status_ == singular::types::GatewayStatus::ONLINE)
                    {
                        return singular::types::GatewayStatus::ONLINE;
                    }
                    else
                    {
                        return singular::types::GatewayStatus::OFFLINE;
                    }
                }
            }

            nlohmann::json Gateway::get_open_orders()
            {
                return order_data_;
            }

            void Gateway::unset_order_execution_quality_channel_status(std::string session_id)
            {
                auto it = std::remove(order_execution_quality_session_map.begin(), order_execution_quality_session_map.end(), session_id);
                order_execution_quality_session_map.erase(it, order_execution_quality_session_map.end());
            }
            void Gateway::set_order_execution_quality_channel_status(std::string session_id, std::string credential_id)
            {
                order_execution_quality_session_map.push_back(session_id);
            }

            void Gateway::send_final_latency_info(singular::types::AlgorithmId algo_id, char *credential_id)
            {
                auto latency_info = latency_measure->get_avg_latency_by_algorithm(algo_id);
                if (latency_info && credential_id != nullptr && credential_id[0] != '\0')
                {
                    // std::cout<<"Sending avg latency info"<<std::endl;
                    do_websocket_task_latency(*latency_info, credential_id);
                }
                else
                {
                    singular::utility::log_event(
                        log_service_name,
                        singular::utility::OEMSEvent::OMS_ERROR,
                        "Latency info or credential_id is missing.");
                }
            }

            void Gateway::send_algo_execution_status(const nlohmann::json &message)
            {
                nlohmann::json final_data = {
                    {"message", message["message"]},
                    {"is_initialized", message["is_initialized"]},
                    {"request_source", message["request_source"]},
                    {"algorithm_id", message["algorithm_id"]},
                    {"symbol", message["symbol"]},
                    {"is_completed", message["is_completed"]},
                    {"credential_id", message["credential_id"]},
                    {"name", message["name"]},
                    {"exchange", message["exchange"]}
                    };

                // Loop through the sessions and send the data
                for (const auto &session_id_value : session_map)
                {
                    auto it = singular::network::globalWebSocketChannels.find(session_id_value);

                    // Send the final data over the WebSocket if a session is found
                    if (it != singular::network::globalWebSocketChannels.end() && it->second)
                    {
                        singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Sending Twap Edge Response to Global Websocket Server");
                        it->second->send(final_data.dump());
                    }
                }
            }

            void Gateway::set_order_channel_status(std::string session_id, std::string credential_id)
            {
                session_map.push_back(session_id);
            }

            void Gateway::unset_order_channel_status(std::string session_id)
            {
                auto it = std::remove(session_map.begin(), session_map.end(), session_id);
                session_map.erase(it, session_map.end());
            }

            void Gateway::logout()
            {
                if (!is_purged_)
                    send_gateway_disconnect(singular::types::Exchange::BITFINEX, name_);
            }

            nlohmann::json Gateway::get_account_data() { return account_info_; }

            nlohmann::json Gateway::get_position_data()
            {
                return position_data_;
            }

            nlohmann::json Gateway::get_order_data()
            {
                return nlohmann::json();
            }

            nlohmann::json Gateway::get_orderbook_data()
            {
                return nlohmann::json();
            }

            nlohmann::json Gateway::get_last_trades_data()
            {
                return nlohmann::json();
            }

            void Gateway::stream_order_data(nlohmann::json message, const std::string order_state)
            {
                auto client_id = message["id"];
                auto internal_order_id = client_to_internal_id_map_[client_id];
                auto symbol = client_id_to_symbol_map_[client_id];
                auto price = client_id_to_price_map_[client_id];
                auto qty = client_id_to_qty_map_[client_id];
                auto req_source = client_id_to_source_map_[client_id];
                std::string request_source = singular::types::get_request_source_string[req_source];
                auto side = singular::types::SideDescription(static_cast<int>(client_id_to_side_map_[client_id]));
                message["algorithm_id"] = NULL;
                for (const auto &pair : singular::types::getAlgorithmReferenceMap())
                {
                    if (std::find(pair.second.begin(), pair.second.end(), internal_order_id) != pair.second.end())
                    {
                        message["algorithm_id"] = pair.first;
                        break; // Remove break if you want to find all keys containing the value
                    }
                }
                message["symbol"] = symbol;
                message["quantity"] = qty;
                message["price"] = price;
                message["client_id"] = client_id;
                message["status"] = order_state;
                message["request_source"] = request_source;
                message["side"] = side;
                message["order_id"] = std::to_string(get_client_id(internal_order_id));
                nlohmann::json final_data = nlohmann::json::object();
                nlohmann::json subs_data = nlohmann::json::array();
                subs_data.push_back({{"channel", "order"}, {"data", message}});
                final_data["exchange"] = "BITFINEX";
                final_data["name"] = name_;
                final_data["data"] = subs_data;
                if (internal_to_credential_id_map_.find(internal_order_id) != internal_to_credential_id_map_.end())
                {
                    final_data["credential_id"] = internal_to_credential_id_map_[internal_order_id];
                }

                if (order_state != "received")
                {
                    auto redis_score = singular::utility::timestamp<std::chrono::seconds>();
                    redis_helper_.save_to_sorted_set("bitfinex_order_last_min_data", final_data, static_cast<double>(redis_score));
                }

                for (const auto &session_id_value : session_map)
                {
                    auto it = singular::network::globalWebSocketChannels.find(session_id_value);
                    if (it != singular::network::globalWebSocketChannels.end() && it->second)
                    {
                        singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Sending to Global Websocket Server");
                        try
                        {
                            #ifdef TEST_BUILD
                                this->mock_generated_messages.push_back(final_data.dump());
                            #else
                                it->second->send(final_data.dump());
                            #endif
                        }
                        catch (const std::exception &e)
                        {
                            singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::BITFINEX_ERROR, "Failed to serialize JSON data: " + std::string(e.what()));
                            // Handle the serialization/send failure
                        }
                    }
                }
            }

            void Gateway::send_reject_response(const nlohmann::json &message)
            {
                try
                {

                    auto extract_notify_info = [](const nlohmann::json &message)
                    {
                        // Validate message structure
                        if (!message.is_array() || message.size() < 8)
                        {
                            throw std::runtime_error("Invalid `message` structure: Expected array with at least 8 elements.");
                        }

                        // Extract `notify_info` (nested array at index 4)
                        if (!message[4].is_array())
                        {
                            throw std::runtime_error("Invalid `message` structure: Expected nested array at index 4.");
                        }
                        const auto &notify_info = message[4];

                        // Extract error message (if available at index 7)
                        std::string error_message = message.size() > 7 && message[7].is_string() ? message[7].get<std::string>() : "";
                        uint64_t id = notify_info.size() > 0 && notify_info[0].is_number() ? notify_info[0].get<uint64_t>() : 0;
                        uint64_t gid = notify_info.size() > 1 && notify_info[1].is_number() ? notify_info[1].get<uint64_t>() : 0;
                        uint64_t client_id = notify_info.size() > 2 && notify_info[2].is_number() ? notify_info[2].get<uint64_t>() : 0;
                        std::string symbol = notify_info.size() > 3 && notify_info[3].is_string() ? notify_info[3].get<std::string>() : "";
                        uint64_t mts_create = notify_info.size() > 4 && notify_info[4].is_number() ? notify_info[4].get<uint64_t>() : 0;
                        uint64_t mts_update = notify_info.size() > 5 && notify_info[5].is_number() ? notify_info[5].get<uint64_t>() : 0;
                        double amount = notify_info.size() > 6 && notify_info[6].is_number() ? notify_info[6].get<double>() : 0.0;
                        double amount_orig = notify_info.size() > 7 && notify_info[7].is_number() ? notify_info[7].get<double>() : 0.0;
                        std::string type = notify_info.size() > 8 && notify_info[8].is_string() ? notify_info[8].get<std::string>() : "";
                        std::string type_prev = notify_info.size() > 9 && notify_info[9].is_string() ? notify_info[9].get<std::string>() : "";
                        // Placeholder fields
                        // double placeholder1 = notify_info.size() > 10 && notify_info[10].is_number() ? notify_info[10].get<double>() : 0.0;
                        // double placeholder2 = notify_info.size() > 11 && notify_info[11].is_number() ? notify_info[11].get<double>() : 0.0;

                        int flags = notify_info.size() > 12 && notify_info[12].is_number() ? notify_info[12].get<int>() : 0;

                        std::string status = notify_info.size() > 13 && notify_info[13].is_string() ? notify_info[13].get<std::string>() : "";

                        // Trim leading and trailing whitespace
                        status.erase(0, status.find_first_not_of(" \t")); // Trim leading whitespace
                        status.erase(status.find_last_not_of(" \t") + 1); // Trim trailing whitespace

                        // Extract the part before '@', and trim the space just before '@' if exists
                        size_t at_pos = status.find('@');
                        if (at_pos != std::string::npos)
                        {
                            status = status.substr(0, at_pos); // Take the substring before '@'
                            // Trim any trailing space that might be left before '@'
                            status.erase(status.find_last_not_of(" \t") + 1); // Trim trailing space before '@'
                        }

                        // Placeholder fields
                        // double placeholder3 = notify_info.size() > 14 && notify_info[14].is_number() ? notify_info[14].get<double>() : 0.0;
                        // double placeholder4 = notify_info.size() > 15 && notify_info[15].is_number() ? notify_info[15].get<double>() : 0.0;

                        double price = notify_info.size() > 16 && notify_info[16].is_number() ? notify_info[16].get<double>() : 0.0;
                        double price_avg = notify_info.size() > 17 && notify_info[17].is_number() ? notify_info[17].get<double>() : 0.0;
                        double price_trailing = notify_info.size() > 18 && notify_info[18].is_number() ? notify_info[18].get<double>() : 0.0;
                        double price_aux_limit = notify_info.size() > 19 && notify_info[19].is_number() ? notify_info[19].get<double>() : 0.0;

                        // Placeholder fields
                        // double placeholder5 = notify_info.size() > 20 && notify_info[20].is_number() ? notify_info[20].get<double>() : 0.0;
                        // double placeholder6 = notify_info.size() > 21 && notify_info[21].is_number() ? notify_info[21].get<double>() : 0.0;
                        // double placeholder7 = notify_info.size() > 22 && notify_info[22].is_number() ? notify_info[22].get<double>() : 0.0;

                        int notify = notify_info.size() > 23 && notify_info[23].is_number() ? notify_info[23].get<int>() : 0;
                        int hidden = notify_info.size() > 24 && notify_info[24].is_number() ? notify_info[24].get<int>() : 0;

                        uint64_t placed_id = notify_info.size() > 25 && notify_info[25].is_number() ? notify_info[25].get<uint64_t>() : 0;

                        // Return extracted data
                        return std::make_tuple(
                            id, gid, client_id, symbol, mts_create, mts_update, amount, amount_orig, type, type_prev, flags, status,
                            price, price_avg, price_trailing, price_aux_limit, notify, hidden, placed_id, error_message);
                    };

                    auto [id, gid, client_id, symbol, mts_create, mts_update, amount, amount_orig, type, type_prev, flags, status,
                          price, price_avg, price_trailing, price_aux_limit, notify,
                          hidden, placed_id, error_message] = extract_notify_info(message);
                    const std::string order_state = "order_reject";

                    // Log the error message if it's present
                    if (!error_message.empty())
                    {
                        std::cout << "Error message: " << error_message << std::endl;
                    }

                    // Validate `client_id` before accessing any map
                    if (client_id == 0 || client_to_internal_id_map_.find(client_id) == client_to_internal_id_map_.end())
                    {
                        throw std::runtime_error("Invalid or missing `client_id` in message structure.");
                    }

                    // Process the order data...
                    // Example: Log parsed fields
                    auto internal_order_id = client_to_internal_id_map_[client_id];
                    auto req_source = client_id_to_source_map_[client_id];
                    std::string request_source = singular::types::get_request_source_string[req_source];

                    nlohmann::json log_data;
                    log_data["order_id"] = internal_order_id;
                    log_data["gid"] = gid;
                    log_data["client_id"] = client_id;
                    log_data["symbol"] = symbol;
                    log_data["mts_create"] = mts_create;
                    log_data["mts_update"] = mts_update;
                    log_data["amount"] = amount;
                    log_data["amount_orig"] = amount_orig;
                    log_data["order_type"] = type;
                    log_data["type_prev"] = type_prev;
                    log_data["flags"] = flags;
                    log_data["status"] = order_state;
                    // log_data["placeholder1"] = placeholder1;
                    // log_data["placeholder2"] = placeholder2;
                    // log_data["placeholder3"] = placeholder3;
                    // log_data["placeholder4"] = placeholder4;
                    // log_data["placeholder5"] = placeholder5;
                    // log_data["placeholder6"] = placeholder6;
                    // log_data["placeholder7"] = placeholder7;
                    log_data["price"] = price;
                    log_data["price_avg"] = price_avg;
                    log_data["price_trailing"] = price_trailing;
                    log_data["price_aux_limit"] = price_aux_limit;
                    log_data["notify"] = notify;
                    log_data["hidden"] = hidden;
                    log_data["placed_id"] = placed_id;
                    log_data["error"] = error_message;
                    log_data["algorithm_id"] = nullptr;
                    log_data["request_source"] = request_source;
                    for (const auto &pair : singular::types::getAlgorithmReferenceMap())
                    {
                        if (std::find(pair.second.begin(), pair.second.end(), internal_order_id) != pair.second.end())
                        {
                            log_data["algorithm_id"] = pair.first;
                            break; // Remove break if you want to find all keys
                        }
                    }
                    for (const auto &session_id_value : session_map)
                    {
                        auto it = singular::network::globalWebSocketChannels.find(session_id_value);
                        nlohmann::json final_data = nlohmann::json::object();
                        nlohmann::json subs_data = nlohmann::json::array();
                        subs_data.push_back({{"channel", "order"}, {"data", log_data}});
                        final_data["exchange"] = "BITFINEX";
                        final_data["name"] = name_;
                        final_data["data"] = subs_data;
                        if (internal_to_credential_id_map_.find(internal_order_id) != internal_to_credential_id_map_.end())
                        {
                            final_data["credential_id"] = internal_to_credential_id_map_[internal_order_id];
                        }
                        if (it != singular::network::globalWebSocketChannels.end() &&
                            it->second)
                        {
                            singular::utility::log_event(log_service_name, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Sending to Global Websocket Server");
                            #ifdef TEST_BUILD
                                this->mock_generated_messages.push_back(final_data.dump());
                            #else
                                it->second->send(final_data.dump());
                            #endif
                        }
                    }

                    // Additional processing logic here...
                }
                catch (std::exception &e)
                {
                    std::cout << "Got the exception in stream order data  : " << e.what() << std::endl;
                }
            }

        } // namespace binance
    } // namespace gateway
} // namespace singular
