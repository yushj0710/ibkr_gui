# IBKR Options Desk Plan

## Architecture

- Native Qt 6 Widgets desktop application in C++20.
- Official IBKR TWS API C++ client owns the live TWS/IB Gateway connection, market data, portfolio state, margin previews, and order submission.
- Python worker uses the official `ibapi` package only for local persistence and analytics. `ib_insync` is excluded.
- TWS/Gateway owns authentication. The application never collects or stores IBKR credentials.
- One managed account is selected per session.
- Live, paper, IB Gateway, and custom host/port/client ID profiles are supported.

## Current Implemented Features

- Qt/CMake/MinGW project builds on Windows.
- Automatic `windeployqt` deployment of Qt DLLs, plugins, SQL drivers, and MinGW runtime files.
- Connection dialog and connection status header.
- Managed-account selection.
- Account metrics header.
- Equity and option position models.
- Option parent/leg tree with local grouping metadata.
- Editable equity earnings dates.
- Watchlist symbol qualification and quote updates.
- Watchlist expected-price calculations and near-strike option candidates.
- Bid-side `SELL` and ask-side `BUY` order tickets.
- IBKR `whatIf` margin preview before order submission.
- Exact draft fingerprinting and server-side approval gate for order transmission.
- IBKR execution-side normalization for `BOT`/`SLD`.
- FIFO realized P&L with option contract multiplier handling.
- Watchlist performance preservation during quote updates.
- Per-position subscription cleanup after a position reaches zero.
- Realized P&L statistics button.
- Python worker storage regression test registered with CTest.

## Remaining Work

### Python Worker Boundary

- Launch the Python worker from C++ with `QProcess`.
- Add framed JSON-RPC or equivalent local IPC between C++ and Python.
- Move strategy metadata, watchlist metadata, snapshots, execution records, and analytics ownership to the worker.
- Remove the duplicate in-process C++ persistence schema after migration.
- Add worker startup, shutdown, timeout, malformed-response, and restart handling.

### Session Reliability

- Add explicit states for initializing, ready, reconnecting, and stale data.
- Implement reconnect retry with bounded backoff.
- Re-request account, position, market-data, P&L, and watchlist subscriptions after reconnect.
- Mark values stale or unavailable when updates exceed their expected freshness interval.
- Add request pacing and market-data-line budgeting.
- Add structured API error handling for entitlement, pacing, contract, and order errors.

### Portfolio and Market Data

- Add `reqAccountSummary` for focused account metrics and respect IBKR's update cadence.
- Add `reqPositions` or account-scoped position synchronization and end-of-snapshot handling.
- Define and consistently display the current-price mark policy for bid, ask, last, and delayed data.
- Persist timestamped account, position, quote, and P&L snapshots.
- Add data-quality/source timestamps to displayed values.

### Options and Strategies

- Verify saved strategy groups against live account positions on every startup and update.
- Keep unverified groups leg-only and prohibit parent actions.
- Implement parent close actions.
- Implement roll tickets with explicit close and open legs.
- Use valid IBKR `BAG` combo orders where appropriate and clearly identify non-atomic rolls.
- Display and refresh option underlying prices and maintenance margin accurately.
- Preserve child alert state on parent rows when underlying price is within 5% of strike.

### Watchlists

- Add user-named watchlist tabs.
- Add add/remove/reorder tab controls.
- Add expand-all behavior with equity parent rows and option child rows.
- Move options from a modal-only view into expandable table/tree rows.
- Debounce and replace stale margin previews when quantity, side, or price changes.
- Support both-side and trend-side filtering exactly as specified.

### Trade Records and Statistics

- Persist execution and commission callbacks through the Python worker.
- Implement authoritative FIFO realized P&L by account, contract, asset class, and interval.
- Track option multiplier and contract metadata rather than assuming 100 for every option.
- Detect expiry/disappearance of option positions and record provisional realized P&L from the last observed unrealized P&L.
- Mark provisional expiry records and supersede them with later execution, assignment, or exercise information.
- Implement the expiry-interval statistic: sum current unrealized P&L of open options whose expiration falls within the selected date interval.
- Add chart/table views for interval statistics.
- Document that standard TWS callbacks cannot reconstruct pre-install history; optionally add Flex Query import later.

### Build and Release

- Replace machine-specific absolute dependency paths with configurable cache variables or toolchain files.
- Add a release preset and deployment output directory.
- Copy the official IBKR client DLL and any non-Qt runtime dependencies into the release bundle.
- Add packaging and a clean-machine smoke test.

## Safety Requirements

- No order is transmitted without a successful matching IBKR `whatIf` preview.
- Any quantity, price, order-type, action, contract, or account change invalidates the preview.
- Final submission requires explicit user confirmation.
- Orders must be validated against current live position quantity and contract details.
- Parent strategy actions are disabled when local metadata does not match IBKR positions.
- Paper-account integration testing is required before live trading.

## Test Plan

- C++ domain tests for quote merging, P&L updates, 5% risk alerts, strategy verification, FIFO matching, option multipliers, and order fingerprints.
- Qt model/dialog tests for sorting, editing, selection, tree expansion, stale values, bid/ask actions, and review gating.
- Python worker tests for migrations, account scoping, snapshots, executions, strategies, and malformed requests.
- Callback fixture tests for normal data, delayed data, entitlement errors, pacing errors, order rejection, and reconnect.
- Paper-account integration tests for login, account selection, streaming prices, position updates, margin previews, disconnect/reconnect, and manually cancelled orders.
- Live mode remains disabled for acceptance testing until paper validation is complete.
