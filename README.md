
# GQ-BITFINEX-COINM-GATEWAY

This repo contains the BITFINEX Gateway Implementation for the OEMS V3 Arch.
=======
# GoQuant
---

# **GoQuant: Quantitative Trading System with Bitfinex Integration**

**GoQuant** is a high-performance quantitative trading system designed to trade across multiple markets, including spot, futures, and options. It integrates seamlessly with **Bitfinex** to provide advanced order execution, modification, and real-time market data updates.

## Table of Contents
- [Overview](#overview)
- [Key Features](#key-features)
- [Bitfinex Integration](#bitfinex-integration)
- [GoQuant System Architecture](#goquant-system-architecture)
- [Files and Their Roles](#files-and-their-roles)
- [Contribution to `gatewayom.cpp`](#contribution-to-gatewayomcpp)
- [Installation](#installation)
- [Usage](#usage)
- [API Integration](#api-integration)
- [Performance](#performance)
- [Contributions](#contributions)
- [License](#license)

## Overview

GoQuant is a quantitative trading system designed for efficient and low-latency order execution in **spot**, **futures**, and **options** markets. The system integrates directly with **Bitfinex** for trading, providing features such as order placement, modification, and cancellation, as well as real-time orderbook updates via WebSockets.

### Core Features:
- **Low-Latency Order Execution**: Optimized for minimal delay in placing and managing orders.
- **Cross-Market Trading**: Supports spot, futures, and options markets, with seamless integration with Bitfinex.
- **Real-Time Data**: WebSocket API for live updates on orderbooks and positions.
- **WebSocket API**: Client-side real-time data streaming.

## Key Features

- **Low-Latency Execution**: Designed to handle high-frequency trading strategies by minimizing order execution delays.
- **Market Data Integration**: Retrieves real-time orderbook data via APIs (Bitfinex and others) to support algorithmic trading decisions.
- **API Integration with Bitfinex**: Integrates with Bitfinex for accessing account balances, orderbook data, and real-time trading functionalities.
- **Order Management**: Functions to place, modify, and cancel orders with high reliability.
- **Risk Management Algorithms**: Implements risk controls to prevent excessive exposure.

## Bitfinex Integration

The **Bitfinex** integration enables **GoQuant** to interact with the **Bitfinex API** for real-time trading and market data. This integration provides the following features:
1. **Account Balance Retrieval**: Securely fetches account balances for both spot and margin accounts.
2. **Orderbook Data**: Retrieves live orderbook data for spot and futures markets.
3. **Order Management**: Places, modifies, and cancels orders through the Bitfinex RESTful and WebSocket APIs.
4. **WebSocket Communication**: Enables subscription to live orderbook updates and trading data streams.

### Contribution to Bitfinex Integration:
- **High-Performance WebSocket Infrastructure**: Developed a robust WebSocket server for subscribing to live orderbook updates.
- **API Gateway**: Created a custom C++ solution for accessing Bitfinex account data, including balances, orders, and transactions.
- **Low-Latency Trading Gateway**: Optimized for real-time order execution to minimize latency in placing and modifying orders.

## GoQuant System Architecture

The GoQuant system is composed of multiple interconnected components designed to ensure efficient order execution and data retrieval:
1. **Order Execution & Management**: Built with low-latency C++ backend, handling order placement, modification, and cancellation in real-time.
2. **Market Data Provider**: Fetches live market data via APIs (Bitfinex and others) to support algorithmic trading decisions.
3. **WebSocket Server**: Provides real-time updates to subscribed clients, ensuring that they receive live market data.
4. **Trading Algorithm**: Uses real-time market data and trading strategies to automatically place orders.

## Files and Their Roles

### **1. Gateway.cpp**
   - Core file for **order execution and management**.
   - Handles placing, modifying, and canceling orders on the exchange.
   - Communicates with the exchange API for orderbook retrieval and updates.

### **2. Gateway_DM.cpp**
   - Handles **data management** for the system.
   - Interfaces with external data sources to gather real-time market data.
   - Processes and stores market data, ensuring it is available for trading decision-making.

### **3. Gateway_OM.cpp**
   - Responsible for **order management** (OM).
   - Processes orders, modifies them as needed, and manages their lifecycle from creation to cancellation.
   - Integrates with the exchange API and internal algorithms to ensure orders are aligned with trading strategies.

---

## Contribution to `Gateway_OM.cpp`

In the **GoQuant** project, I contributed to the **Order Management Gateway (Gateway_OM.cpp)** by implementing the following features in **`Gateway_OM.cpp`**:

1. **Optimized Order Execution**:
   - Reduced order execution latency by implementing efficient algorithms, ensuring faster matching of orders.
2. **Order Modification & Cancellation**:
   - Developed functionality to dynamically modify and cancel orders in real-time based on market conditions.
3. **API Communication**:
   - Integrated Bitfinex’s REST and WebSocket APIs for order execution and market data retrieval.
4. **Error Handling & Logging**:
   - Implemented robust error handling for reliable order execution and improved logging for better traceability.
5. **Real-Time Order Management**:
   - Ensured seamless integration of the order management system with trading algorithms for real-time order adjustments.

---

## Installation

### Prerequisites

To build the GoQuant system with Bitfinex integration, you’ll need the following dependencies:
- **C++ Compiler** (e.g., GCC, Clang)
- **CMake** for building the project
- **Boost** libraries for data handling
- **libcurl** for HTTP communication with Bitfinex API
- **WebSocket++** for WebSocket communication

To install dependencies, run:

```bash
sudo apt-get install build-essential cmake libboost-all-dev libwebsocketpp-dev libcurl4-openssl-dev
```

### Clone the Repository

Clone the repository to your local machine:

```bash
git clone https://github.com/your-username/GoQuant.git
cd GoQuant
```

### Build the Project

To compile the project, run:

```bash
mkdir build
cd build
cmake ..
make
```

This will create the executable for the GoQuant system.

## Usage

Once built, you can run GoQuant:

```bash
./GoQuant
```

Make sure to configure your **API keys**, **exchange details**, and **trading parameters** in the `config.json` file:

```json
{
  "api_key": "your-api-key",
  "api_secret": "your-api-secret",
  "exchange": "Bitfinex",
  "trade_instruments": ["spot", "futures", "options"]
}
```

### Real-Time Market Data

Run the WebSocket client to subscribe to live updates:

```bash
./websocket_client
```

This will start streaming live orderbook and trading data.

## API Integration

The GoQuant system provides several API endpoints for interaction:

- **POST /order**: Place new orders.
- **GET /orderbook**: Retrieve orderbook data for selected instruments.
- **GET /positions**: View current positions and account details.

## Performance

GoQuant is optimized for high-frequency trading:
- **Order Execution Latency**: <1ms.
- **Market Data Handling**: Supports thousands of updates per second.
- **Scalability**: Designed to handle multiple markets and large trading volumes.

## Contributions

Feel free to fork the project and contribute to its development by:
1. Forking the repository
2. Creating a new branch
3. Making changes
4. Submitting a pull request

## License

This project is licensed under the MIT License.

---

