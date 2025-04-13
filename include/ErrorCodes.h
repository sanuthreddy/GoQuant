#pragma once

#include <map>

#include <gateway/include/ExchangeErrors.h>

namespace singular::gateway::bitfinex {

    // Mapping BITFINEX Error codes to GoQuant basic errors
    const std::map<int, Errors> BITFINEX_ERR_MAP = {

        // 100xx - New Error/Info Codes
        {10000, DefaultError}, // Unknown error
        {10001, ExchangeError}, // Generic error
        {10020, InvalidOrder}, // Request parameters error
        {10050, ExchangeError}, // Configuration setup failed
        {10100, AuthenticationError}, // Failed authentication
        {10111, AuthenticationError}, // Error in authentication request payload
        {10112, AuthenticationError}, // Error in authentication request signature
        {10113, AuthenticationError}, // Error in authentication request encryption
        {10114, AuthenticationError}, // Error in authentication request nonce
        {10200, AuthenticationError}, // Error in un-authentication request
        {10300, ExchangeError}, // Failed channel subscription
        {10301, ExchangeError}, // Failed channel subscription: already subscribed
        {10302, ExchangeError}, // Failed channel subscription: unknown channel
        {10305, ExchangeError}, // Failed channel subscription: reached limit of open channels
        {10400, ExchangeError}, // Failed channel un-subscription: channel not found
        {10401, ExchangeError}, // Failed channel un-subscription: not subscribed
        {11000, ExchangeError}, // Not ready, try again later

        // 200xx - Websocket Event Codes
        {20051, ExchangeError}, // Websocket server stopping... please reconnect later
        {20060, ExchangeError}, // Websocket server resyncing... please reconnect later
        {20061, ExchangeError}, // Websocket server resync complete. please reconnect

    };

}
