# Alpaca Options Stream Parser

High-performance C application for parsing live WebSocket streaming data from Alpaca's options trading API.

## Features

- **Real-time Options Data**: Live trade and quote data streaming
- **WebSocket Integration**: Automatic authentication and subscription management
- **MsgPack Protocol**: Efficient binary message parsing with minimal overhead
- **Auto Symbol Discovery**: REST API integration for option contract discovery
- **Human-Readable Display**: Converts cryptic option symbols to readable format
- **Strike Price Filtering**: Advanced filtering by expiration dates and strike prices
- **Mock Data Mode**: Simulated live data for development when markets are closed
- **Clean Architecture**: Modular codebase with separated concerns

## Architecture

```
├── include/               # Header files
│   ├── types.h           # Type definitions and structures
│   ├── websocket.h       # WebSocket connection management
│   ├── api_client.h      # REST API client for symbol fetching
│   ├── symbol_parser.h   # Option symbol parsing utilities
│   ├── display.h         # UI and display functions
│   ├── message_parser.h  # MsgPack message parsing
│   └── mock_data.h       # Mock data generation for development
├── src/                  # Source files
│   ├── main.c           # Application entry point
│   ├── websocket.c      # WebSocket implementation
│   ├── api_client.c     # REST API client implementation
│   ├── symbol_parser.c  # Symbol parsing logic
│   ├── display.c        # Display and UI functions
│   ├── message_parser.c # Message parsing implementation
│   └── mock_data.c      # Mock data generation implementation
├── obj/                 # Object files (created during build)
├── get_option_symbols.c # Standalone symbol lookup tool
└── Makefile            # Build configuration
```

## Prerequisites

- **libwebsockets** - WebSocket client library
- **msgpack-c** - MsgPack binary serialization
- **OpenSSL** - SSL/TLS support
- **curl** - HTTP client for REST API
- **cjson** - JSON parsing

## Installation

1. **Install dependencies:**
```bash
make install-deps
```

2. **Build the application:**
```bash
make
```

3. **Clean build (if needed):**
```bash
make clean && make
```

## Usage

### Real-time Streaming

The main application supports three modes:

#### 1. Direct Symbol Mode
Stream specific option symbols directly:
```bash
./alpaca_options_stream YOUR_KEY YOUR_SECRET AAPL241220C00150000 AAPL241220P00150000
```

#### 2. Auto-fetch by Date
Automatically fetch and stream options by expiration date:
```bash
./alpaca_options_stream YOUR_KEY YOUR_SECRET AAPL 2025-08-01 2025-08-02
```

#### 3. Auto-fetch by Date + Strike Price
Filter options by both expiration date and strike price range:
```bash
./alpaca_options_stream YOUR_KEY YOUR_SECRET QQQ 2025-08-01 2025-08-02 560.00 562.00
```

#### 4. Mock Mode (Development)
Generate simulated live data for development when markets are closed:
```bash
./alpaca_options_stream --mock AAPL241220C00150000 AAPL241220P00150000 QQQ250801C00560000
```

### Symbol Discovery

Use the standalone tool to discover available option symbols:
```bash
# Find options by date only
./get_option_symbols YOUR_KEY YOUR_SECRET AAPL 2024-12-20 2024-12-20

# Find options with strike price filter
./get_option_symbols YOUR_KEY YOUR_SECRET AAPL 2024-12-20 2024-12-20 150.00 160.00
```

## Option Symbol Format

### Input Format
Raw option symbols follow the format: `SYMBOL[YY][MM][DD][C/P][00000000]`

Examples:
- `AAPL241220C00150000` = AAPL Call expiring Dec 20, 2024 with $150.00 strike
- `QQQ250801P00560000` = QQQ Put expiring Aug 1, 2025 with $560.00 strike

### Display Format
The application converts these to human-readable format:
- `AAPL241220C00150000` → `AAPL 12/20/24 $150.00 Call`
- `QQQ250801P00560000` → `QQQ 08/01/25 $560.00 Put`

## Live Data Display

The application shows a real-time updating table:

```
=== Alpaca Options Live Data ===
Symbols: 6 | Press Ctrl+C to exit

OPTION CONTRACT                     LAST TRADE   BID             ASK             SPREAD      LAST UPDATE    
----------------------------------- ------------ --------------- --------------- ------------ ---------------
QQQ 08/01/25 $560.00 Call          $2.45 x10    $2.40 x5        $2.50 x3        $0.10       09:28:35.123   
QQQ 08/01/25 $561.00 Call          N/A          $1.35 x8        $1.45 x2        $0.10       09:28:34.891   
QQQ 08/01/25 $562.00 Call          $0.95 x5     $0.90 x10       $1.00 x7        $0.10       09:28:35.445   
QQQ 08/01/25 $560.00 Put           N/A          $0.85 x3        $0.95 x4        $0.10       09:28:33.234   
QQQ 08/01/25 $561.00 Put           $1.20 x15    $1.15 x6        $1.25 x9        $0.10       09:28:35.667   
QQQ 08/01/25 $562.00 Put           N/A          $1.45 x2        $1.55 x5        $0.10       09:28:34.789   

Live streaming... (data updates in real-time)
```

## Mock Mode for Development

When markets are closed or you need to test the application without real API keys, use mock mode:

```bash
./alpaca_options_stream --mock AAPL241220C00150000 AAPL241220P00150000 QQQ250801C00560000
```

**Mock Mode Features:**
- **No API Keys Required**: Works without Alpaca credentials
- **Realistic Data**: Generates price movements with configurable volatility
- **Live Updates**: Simulates real-time trade and quote data
- **Multiple Symbols**: Supports multiple option contracts simultaneously
- **Price Continuity**: Maintains realistic price progression over time

**Mock Data Characteristics:**
- Trade prices: Move with realistic volatility (default 2%)
- Quote spreads: Maintain 1-5% bid-ask spreads
- Sizes: Random trade/quote sizes (1-150 contracts)
- Timestamps: Real-time RFC-3339 formatted timestamps
- Exchanges: Rotates through realistic exchange codes (N, C, A, P, B)
- Conditions: Uses appropriate trade/quote condition codes

This mode is perfect for:
- Development and testing
- Demonstrations and presentations
- Learning the application interface
- Testing new features without market dependency

## Data Fields

**Trade Data:**
- Price and size of last executed trade
- Exchange and timestamp information
- Trade conditions

**Quote Data:**
- Best bid/ask prices and sizes
- Bid/ask exchanges
- Quote timestamps and conditions
- Calculated bid-ask spread

## Performance Notes

- **Low Latency**: Designed for minimal processing overhead
- **Memory Efficient**: Optimized memory usage during streaming
- **Single-threaded**: Predictable event loop performance
- **Binary Protocol**: MsgPack provides faster parsing than JSON

## Error Handling

The application handles:
- WebSocket connection failures and reconnection
- Authentication errors with detailed messages
- Malformed MsgPack messages
- API rate limiting and error responses
- Network interruptions

## Development

### Building from Source
```bash
# Development build with debug info
make CFLAGS="-Wall -Wextra -std=c99 -g -Iinclude"

# Show build help
make help
```

### Project Structure
- **Modular Design**: Each component has a specific responsibility
- **Header Files**: Clean API definitions in `include/`
- **Source Files**: Implementation details in `src/`
- **Object Files**: Compiled objects in `obj/` (auto-created)

### Adding New Features
1. Define types in `include/types.h`
2. Add function declarations to appropriate header
3. Implement in corresponding source file
4. Update Makefile dependencies if needed

## License

Open source - suitable for educational and commercial use.

## API Documentation

For Alpaca API details, see:
- [Alpaca Options WebSocket Documentation](https://docs.alpaca.markets/docs/real-time-option-data)
- [Alpaca Options REST API](https://docs.alpaca.markets/reference/optionscontracts)

## Troubleshooting

**Connection Issues:**
- Verify API credentials are correct
- Check network connectivity
- Ensure WebSocket endpoint is accessible

**Build Issues:**
- Run `make install-deps` to install dependencies
- Verify compiler and library versions
- Check that all header files are found

**Data Issues:**
- Confirm option symbols exist and are valid
- Check expiration dates are in correct format (YYYY-MM-DD)
- Verify market hours for live data availability