# dehumidifier-monitor

Monitor and alert on dehumidifier status.

## Layout

- `firmware/arduino-uno/`: PlatformIO firmware for the Arduino Uno.
- `services/telegram-proxy/`: Rust HTTP proxy that forwards dehumidifier events to Telegram.

## Telegram Proxy

The Telegram proxy listens for local HTTP events and sends alerts to Telegram. It reads configuration from environment variables, with `services/telegram-proxy/.env` available for local development.

Create a local environment file from the example:

```sh
cp services/telegram-proxy/.env.example services/telegram-proxy/.env
$EDITOR services/telegram-proxy/.env
```

Run locally:

```sh
cd services/telegram-proxy
cargo run
```

Build a release binary:

```sh
cd services/telegram-proxy
cargo build --release
```

From the repo root, install on a Raspberry Pi:

```sh
sudo mkdir -p /opt/dehumidifier-monitor/telegram-proxy
sudo install -m 755 services/telegram-proxy/target/release/telegram-proxy \
  /opt/dehumidifier-monitor/telegram-proxy/telegram-proxy
sudo install -m 600 services/telegram-proxy/.env \
  /opt/dehumidifier-monitor/telegram-proxy/.env
```

Install the systemd service:

```sh
sudo cp services/telegram-proxy/deploy/systemd/telegram-proxy.service \
  /etc/systemd/system/telegram-proxy.service
sudo systemctl daemon-reload
sudo systemctl enable --now telegram-proxy
```

Check service status and logs:

```sh
systemctl status telegram-proxy
journalctl -u telegram-proxy -f
```
