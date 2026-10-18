# IBKR Options Desk

A Windows Qt 6 desktop application for monitoring and trading IBKR equities and listed options through the official TWS API.

## Current behavior

The application connects directly to TWS or IB Gateway through the official C++ API. Select a managed account after connection to begin account, portfolio, position P&L, and market-data subscriptions. The account header stays visible above all workflow tabs.

Watchlist symbols are validated by IBKR before they are shown. Expanding a quoted symbol requests nearby option candidates, qualifies at most 32 contracts, streams their bid/ask values while the candidate window is open, and requests a one-contract IBKR what-if preview for the displayed maintenance-margin change. Bid-side actions open a prefilled `SELL` ticket and ask-side actions open a prefilled `BUY` ticket; neither action transmits an order directly.

## Prerequisites

- CMake 3.24 or later
- A C++20 compiler supported by Qt 6
- Qt 6.5 or later with the Widgets module
- The official IBKR TWS API C++ client, configured in a later implementation slice
- C++ Protobuf 5.29.5, built with the same MinGW toolchain as Qt. The installed TWS API generated sources require this exact runtime version.

## Build

Machine-specific paths (compiler, Qt, Protobuf, TWS API, Intel decimal library) are not committed.
Copy `CMakeUserPresets.example.json` to `CMakeUserPresets.json`, adjust the paths for your machine.
then:

```powershell
cmake --preset local-debug
cmake --build --preset local-debug
```

Run the generated `ibkr_options_gui` executable from the selected build configuration.

On Windows, the build automatically runs `windeployqt` and places the required Qt DLLs, platform plugins, SQL drivers, and MinGW runtime beside the executable. Run:

```powershell
.\build\qt-mingw-debug\ibkr_options_gui.exe
```

Run the executable from its build directory or keep the deployed DLLs and plugin folders beside it when copying the application elsewhere.

The official C++ API transport is enabled by default and connects through `EClientSocket`/`EReader`. Supply `-DIBKR_TWS_API_ROOT=<official-api-root>` when the SDK is outside `C:/TWS API`, and make the matching Protobuf 5.29.5 package discoverable through `CMAKE_PREFIX_PATH` or `Protobuf_DIR`.

## Safety and data limits

TWS or IB Gateway handles IBKR authentication. This application will never collect or persist account credentials. Order transmission is available only after a matching IBKR what-if preview and explicit confirmation; paper-account validation is required before live use.

Every order must receive an IBKR what-if preview and requires an explicit confirmation in the ticket before it is submitted. The trade-record tabs contain executions observed during the current application capture; the standard API cannot reconstruct complete pre-install history. Verify all actions in a paper account before enabling live order transmission.

## Paper-account validation

1. In TWS Paper or IB Gateway, enable socket clients and connect with the Paper profile.
2. Select the returned managed account and verify account metrics and positions populate.
3. Add a market-data-entitled stock to a watchlist, then double-click `Show options...` after a quote arrives.
4. Confirm candidate quotes and what-if margin results appear. Use a bid or ask button to inspect the prefilled ticket, request a fresh preview, then cancel it.

## Project history

This project was developed locally and first published to GitHub in October 2026, so the commit history starts at this point.

## License

Released under the MIT License.