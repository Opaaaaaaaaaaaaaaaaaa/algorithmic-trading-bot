# Algorithmic Trading Bot

C++ trading system that processes Reddit sentiment and executes trades via the Interactive Brokers API.

## Features

- Reddit comment ingestion
- GPT-based signal extraction
- Automated order execution (IBKR)
- Position tracking and rebalancing
- Telegram notifications

## Notes

API keys are managed via environment variables and are not included in this repository.

## Setup

This project depends on the Interactive Brokers TWS API.

1. Download the IBKR TWS API:
   https://interactivebrokers.github.io/

2. Include the API headers and libraries in your project.

3. Configure include and linker paths in Visual Studio.

4. Set environment variables for API.

## Dependencies

- Interactive Brokers TWS API (not included)
- CPR (HTTP client)
- nlohmann/json
