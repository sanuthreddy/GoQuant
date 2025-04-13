#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdio>

#include "bitfinex/include/Gateway_DM.h"
#include <gateway/include/GatewayFactoryManager_DM.h>

namespace singular
{
    namespace gateway
    {
        namespace bitfinex
        {

            std::unique_ptr<singular::gateway::AbstractGateway_DM> create_bitfinex_gateway_dm(
                hv::EventLoopPtr &executor, bool authenticate,
                const std::string &account_name, const std::string &key,
                const std::string &secret, const std::string &passphrase,
                const std::string &mode)
            {
                return std::make_unique<Gateway_DM>(
                    executor, authenticate, account_name, key, secret, passphrase, mode);
            }

            namespace
            {
                struct Registrar
                {
                    Registrar()
                    {
                        singular::gateway::GatewayFactoryManager_DM::register_factory(
                            singular::types::Exchange::BITFINEX, create_bitfinex_gateway_dm);
                        std::cout << "Registered BITFINEX Gateway factory." << std::endl;
                    }
                };
                Registrar registrar;
            }

            Gateway_DM::Gateway_DM(hv::EventLoopPtr &executor,
                                   bool authenticate,
                                   const std::string &name,
                                   const std::string &key,
                                   const std::string &secret,
                                   const std::string &passphrase,
                                   const std::string &mode)
                : AbstractGateway_DM(executor, 120, 20),
                  authenticate_(authenticate),
                  name_(name),
                  key_(key),
                  secret_(secret),
                  passphrase_(passphrase),
                  mode_(mode)
            {
                loadEnvFile(".env");

                public_url_ = getExchangeUrl("BITFINEX_ENV_MODE", "DEV_BITFINEX_PUBLIC_WS_URL", "PROD_BITFINEX_PUBLIC_WS_URL");
                env_mode_ = std::getenv("BITFINEX_ENV_MODE");

                if (strcmp(env_mode_, "DEV") == 0)
                {
                    sim_trading_ = true;
                }
                else
                {
                    sim_trading_ = false;
                }

                if (!public_url_)
                {
                    std::cerr << "Public URL not set. Please check your .env file." << std::endl;
                }

                public_client_ = std::make_unique<singular::network::WebsocketClient>(
                    executor, public_url_);

                // Function to initialize maps with LOAD FACTOR and INITIALIZE MAP SIZE
                initializeMaps();
                login_status_ = true;

                singular::utility::initialize_shared_memory_queues_for_account("BITFINEX",key_);
            }

            void Gateway_DM::initialize_callback_funcs()
            {
                add_callback(std::bind(&Gateway_DM::start_public_client, this));
            }

            // thread to start anad handle private client
            void Gateway_DM::start_public_client()
            {
                public_client_->close();

                public_client_->run(
                    [this](const HttpResponsePtr &response) {},
                    [this]()
                    {
                        public_status_ = singular::types::GatewayStatus::OFFLINE;
                        send_gateway_disconnect(singular::types::Exchange::BITFINEX, name_);
                    },
                    [this](const std::string &message)
                    {
                        parse_websocket(message);
                    });
            }

            singular::types::GatewayStatus Gateway_DM::status()
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

            void Gateway_DM::initializeMaps()
            {
            }

            void Gateway_DM::close_public_socket()
            {
                public_client_->close();
                public_status_ = singular::types::GatewayStatus::OFFLINE;
            }

            void Gateway_DM::purge()
            {
                close_public_socket();
                authenticated_ = false;
                login_status_ = false;
                is_purged_ = true;
            }

            void Gateway_DM::logout()
            {
                if (!is_purged_)
                    send_gateway_disconnect(singular::types::Exchange::BITFINEX, name_);
            }

            void Gateway_DM::do_subscribe_orderbooks(std::vector<singular::types::Symbol> &symbols)
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
                        log_service_name_,
                        singular::utility::OEMSEvent::ORDERBOOK_SUBSCRIBE_SUCCESS,
                        "Subscribing order book channel");
                }
            }

            void Gateway_DM::do_unsubscribe_orderbooks(std::vector<singular::types::Symbol> &symbols)
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
                        log_service_name_,
                        singular::utility::OEMSEvent::ORDERBOOK_CHANNEL,
                        "Unsubscribing order book channel");
                }
            }

            void Gateway_DM::do_subscribe_tickers(std::vector<singular::types::Symbol> &symbols)
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
                        log_service_name_,
                        singular::utility::OEMSEvent::TICKER_SUBSCRIBE_SUCCESS,
                        "Subscribing ticker channel");
                }
            }

            void Gateway_DM::do_unsubscribe_tickers(std::vector<singular::types::Symbol> &symbols)
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
                        log_service_name_,
                        singular::utility::OEMSEvent::TICKER_CHANNEL,
                        "Unsubscribing ticker channel");
                }
            }

            void Gateway_DM::do_subscribe_top_of_book(std::vector<singular::types::Symbol> &symbols)
            {
            }

            void Gateway_DM::do_subscribe_trades(std::vector<singular::types::Symbol> &symbols)
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
                        log_service_name_,
                        singular::utility::OEMSEvent::LASTTRADES_SUBSCRIBE_SUCCESS,
                        "Subscribing trades channel");
                }
            }

            void Gateway_DM::do_unsubscribe_trades(std::vector<singular::types::Symbol> &symbols)
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
                        log_service_name_,
                        singular::utility::OEMSEvent::LASTTRADES_CHANNEL,
                        "Unsubscribing trades channel");
                }
            }

            void Gateway_DM::do_subscribe_funding(std::vector<singular::types::Symbol> &symbols)
            {
            }

            nlohmann::json Gateway_DM::get_account_data()
            {
                return singular::utility::fetch_data_from_shared_memory(
                    exchange_name,key_, "account", account_data_cache, account_cache_mutex);
            }

            nlohmann::json Gateway_DM::get_position_data()
            {
                return singular::utility::fetch_data_from_shared_memory(
                    exchange_name,key_, "position", position_data_cache, position_cache_mutex);
            }

            nlohmann::json Gateway_DM::get_orderbook_data()
            {
                return nlohmann::json::array();
            }

            nlohmann::json Gateway_DM::get_last_trades_data()
            {
                return nlohmann::json::array();
            }

            std::string Gateway_DM::signature(uint64_t timestamp, std::string &request_path, std::string_view secret)
            {
                std::string payload = std::to_string(timestamp) + "GET" + request_path;
                return singular::utility::calc_hmac_sha256_base64(secret_, payload);
            }

            std::string Gateway_DM::calc_hmac_sha384_hex(const std::string &key, const std::string &data)
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

            void Gateway_DM::parse_websocket(const std::string &buffer)
            {
#ifdef TEST_BUILD
                if (buffer.empty())
                {
                    singular::utility::log_event(log_service_name_,
                                                 singular::utility::OEMSEvent::WS_CONNECTION_ERROR,
                                                 "Empty WebSocket message received.");
                    return;
                }
#endif
                auto end_time = std::chrono::high_resolution_clock::now();
                nlohmann::json message;

                if (buffer.empty())
                {
                    singular::utility::log_event(log_service_name_, singular::utility::OEMSEvent::WS_CONNECTION_ERROR, "Empty WebSocket message received.");
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
                            if (message.contains("platform") && message["platform"].contains("status"))
                            {
                                if (message["platform"]["status"] == 1)
                                {
                                    // Successful authentication
                                    authenticated_ = true;
                                    public_status_ = singular::types::GatewayStatus::ONLINE;

                                    // Extract success message if available, default to "SUCCESS"
                                    std::string success_message = "SUCCESS";
                                    singular::types::EventDetail detail(
                                        success_message,
                                        200, // Hardcoded for success
                                        success_message,
                                        singular::event::EventType::LOGIN_ACCEPT,
                                        std::nullopt);

                                    send_operation_response("SUCCESS", detail);
                                    singular::utility::log_event(log_service_name_, singular::utility::OEMSEvent::LOGIN_EXCHANGE_SUCCESS, "Login successful.");
                                }
                                else
                                {
                                    int error_code = 500; // Default to 500
                                    std::string error_message = "PLATFORM MAINTENANCE ERROR";

                                    singular::types::EventDetail detail(
                                        error_message,
                                        error_code, // Use the dynamic error code
                                        error_message,
                                        singular::event::EventType::LOGIN_FAIL,
                                        std::nullopt);
                                    send_operation_response("FAILED", detail);
                                    send_gateway_disconnect(singular::types::Exchange::BITFINEX, name_);
                                    singular::utility::log_event(log_service_name_, singular::utility::OEMSEvent::LOGIN_EXCHANGE_ERROR, "Login failed [ERROR] " + error_message);
                                }
                            }

                            if (message.contains("version"))
                            {
                                // Version of websocket
                                // singular::utility::log_event(log_service_name_, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Bitfinex Server uses Websocket protocol version " + std::to_string(static_cast<unsigned int>(message["version"])));
                            }
                            else if (message.contains("code"))
                            {
                                singular::utility::log_event(log_service_name_, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Bitfinex Server event, code " + std::to_string(static_cast<unsigned long long>(message["code"])) + message["msg"].get<std::string>());
                            }
                            else
                            {
                                // Unknown info message
                                singular::utility::log_event(log_service_name_, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Unknown info message " + buffer);
                            }
                        } // if (message["event"] == "info") ends
                        else if (message["event"] == "subscribed")
                        {
                            if (!message.contains("channel"))
                            {
                                singular::utility::log_event(log_service_name_, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Subscribed event without channel name " + buffer);
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
                                singular::utility::log_event(log_service_name_, singular::utility::OEMSEvent::ORDERBOOK_CHANNEL, log_message + " with symbol " + symbol);
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
                                singular::utility::log_event(log_service_name_, singular::utility::OEMSEvent::LASTTRADES_CHANNEL, log_message + " with symbol " + symbol);
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
                                singular::utility::log_event(log_service_name_, singular::utility::OEMSEvent::TICKER_CHANNEL, log_message + " with symbol " + symbol);
                            } // else if (channel == "ticker") ends

                        } // else if (message[event] == "subscribed") ends
                        else if (message["event"] == "unsubscribed")
                        {
                            std::string log_message;

                        } // else if (message["event"] == "unsubscribed")
                        else
                        {
                            singular::utility::log_event(log_service_name_, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Received event: " + buffer);
                        }
                    } // if (message.contains("event")) ends

                    else if (message.is_array())
                    {
                        if (message.is_array() && message.size() > 0 && message[0].is_number())
                        {
                            channel_id = message[0];
                        }
                        if (orderbook_channel_id_to_symbol_map_.find(channel_id) != orderbook_channel_id_to_symbol_map_.end())
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
                            singular::utility::log_event(log_service_name_, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Unknown array response: " + buffer);
                        }
                    } // else if (message.is_array()) ends
                    else
                    {
                        // Unknown kind of response
                        singular::utility::log_event(log_service_name_, singular::utility::OEMSEvent::WS_CONNECTION_DEBUG, "Received unknown bitfinex response " + buffer);
                    }
                }
                catch (const nlohmann::json::parse_error &e)
                {
                    singular::utility::log_event(log_service_name_, singular::utility::OEMSEvent::WS_CONNECTION_ERROR, std::string("JSON parse error: ") + e.what());
                    return; // Exit the function as parsing failed
                }
            }
        } // namespace bitfinex
    } // namespace gateway
} // namespace singular
