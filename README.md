<img width="1290" height="700" alt="image" src="https://github.com/user-attachments/assets/622f2f76-e4a4-40e4-8c11-671d3e0094b8" />

# Teta Puro

A C app for handling real time options streaming with Greeks. [a weekend hackathon to play around with alpaca websocket, do not use for real trading]

## What it does

- Streams live options data from Alpaca's WebSocket feed
- Calculates IV, RV and Greeks real time
- Shows second-order Greeks for vol dislocation hunting
- Mock mode because markets are closed most of the time

## Greeks it calculates

- Delta, Gamma, Theta, Vega, Vanna, Charm, Volga, Speed, Zomma, Color

## IV calculation

**Newton-Raphson approach:**
- **Corrado-Miller initial guess** - uses option moneyness for smart starting point
- **Vega-based iteration** - faster convergence (usually <10 iterations)
- **Bounded fallback** - drops to bisection if Newton-Raphson fails
- **Moneyness awareness** - handles ATM vs OTM options differently

It's still Black-Scholes underneath, just with a smarter solver. The assumptions are still wrong (constant vol, no skew, etc.) but at least we get answers faster.

- 5-10x faster IV calculation in most cases
- Better convergence on weird strikes
- More reliable Greeks for vol analysis
- Still shows "N/A" for hopeless cases (deep OTM with tiny prices)

## Quick start

1. Install deps:
```bash
make install-deps
```

2. Copy config and add your keys:
```bash
cp config.example.json config.json
# edit config.json with your Alpaca API keys
```

3. Build it:
```bash
make
```

4. Try mock mode first:
```bash
./alpaca_options_stream --mock AAPL251220C00150000 AAPL251220P00150000
```

5. Live streaming (if your account has options data):
```bash
./alpaca_options_stream QQQ 2025-07-26 2025-07-28 564 566
```

## Mock mode

Since most people don't have live options data subscriptions:

```bash
./alpaca_options_stream --mock QQQ250728C00564000 QQQ250728P00565000
```

Generates realistic price movements and all the Greeks. Good for:
- Testing strategies when markets are closed
- Learning without burning through API quotas
- Developing without real money anxiety

## Filters and noise reduction

- Only processes trades ≥10 contracts (retail noise filter)
- Subscribes to trades only, not quotes (less bandwidth)
- Change detection prevents unnecessary screen updates

## Disclaimer

This calculates financial derivatives. I'm not responsible if:
- The Greeks are wrong
- You lose money using this
- Your risk management relies on this code
- The volatility smile breaks your models

Use real trading systems for real money.

## Dependencies

You'll need these libraries (installation varies by OS):
- libwebsockets, msgpack-c, c-json

On macOS with Homebrew:
```bash
brew install libwebsockets msgpack openssl curl
```

On Ubuntu/Debian:
```bash
sudo apt-get install libwebsockets-dev libmsgpack-dev libssl-dev libcurl4-openssl-dev libcjson-dev
```
