# esp32-ads1115-ct-sensor-rms — measure AC load current RMS with a CT clamp through an ADS1115 and publish over MQTT

Sample sketch for an [ESP32 dev board](https://fluidwire.com/) that reads an **SCT-013-000 CT clamp** through an **ADS1115** 16-bit ADC, computes the **RMS current** over a window of samples, and publishes it to MQTT as a JSON message every 10 seconds.

Pattern shown: sampling a biased AC waveform, RMS computation, burden resistor sizing and per-window publishing — handy for energy-audit thesis work without touching mains conductors.

## What you need

- 1x ESP32 dev board
- 1x ADS1115 ADC breakout (I2C)
- 1x SCT-013-000 split-core CT clamp (100A:50mA, 3.5mm jack)
- 1x 22Ω burden resistor, 2x 10kΩ resistors (bias divider), 1x 10µF capacitor
- 1x 3.5mm jack breakout
- MQTT broker
- WiFi network

## Wiring

| ADS1115 pin | ESP32 pin            |
|-------------|----------------------|
| VDD         | 3V3                  |
| GND         | GND                  |
| SDA         | GPIO21 (default I2C) |
| SCL         | GPIO22 (default I2C) |
| ADDR        | GND (address 0x48)   |
| A0          | CT bias node         |

Bias + burden network:

- 10kΩ from 3V3 to the bias node, 10kΩ from the bias node to GND, 10µF from the bias node to GND.
- CT jack **tip** → bias node (and ADS1115 A0); CT jack **sleeve** → GND.
- **22Ω burden resistor** across the CT jack (tip to sleeve).

The divider lifts the AC signal to 1.65V so the ADS1115 only sees positive voltages.

## Required libraries

Install via Library Manager:

- **Adafruit ADS1X15** by Adafruit (pulls in **Adafruit BusIO**)
- **PubSubClient** by Nick O'Leary

## Setup

1. Copy `config.h.example` → `config.h`. Fill in WiFi + MQTT.
2. Wire the ADS1115 and CT network. Clamp the CT around **one** conductor of the load.
3. Flash. Serial Monitor at **115200 baud**.

## Expected serial output

```
[ads1115] ready
[wifi] connecting to your-wifi-ssid
.....
[wifi] IP: 192.168.1.50
[mqtt] connecting... ok
[mqtt] {"irms_a":1.234,"apparent_va":283.8,"samples":400,"uptime_s":12}
```

## Sample payload

```json
{"irms_a":1.234,"apparent_va":283.8,"samples":400,"uptime_s":120}
```

`apparent_va` is `irms_a × MAINS_VOLTAGE` (default 230V) — it is apparent power, not real power, because this sketch does not measure voltage or phase.

## Notes

- **Burden sizing**: with 22Ω and the ADS1115 at ±4.096V (clamped by the 3.3V supply), the usable swing is about ±1.65V, i.e. roughly 100A RMS. For small loads, a larger burden (e.g. 100Ω) improves resolution — update `BURDEN_OHMS` to match.
- **Sampling window**: 400 samples at 860 SPS is about 0.5 s, covering 25+ cycles at 50Hz (and 60Hz). Longer windows give steadier readings.
- **Calibration**: compare against a known meter and adjust `CT_RATIO` or `BURDEN_OHMS` until they agree. Resistor tolerance matters.
- **Noise floor**: readings below `NOISE_FLOOR_A` (0.05A) are reported as 0.
- **Not bench-tested**: this sample compiles but has not been verified on hardware — validate against a reference meter before relying on it.

## Safety note

Mains can kill. Clamp the CT around a **single** live conductor only (never the whole cable), never leave a CT clamped on a live wire with the burden resistor disconnected, and de-energise the circuit while wiring. Use a properly rated enclosure.

## Security note

`config.h` holds your WiFi and MQTT credentials and is git-ignored — never commit it. Only `config.h.example` (with placeholder values) belongs in the repo. For anything beyond a demo broker, put the broker behind TLS and a username/password.

## License

MIT — see [LICENSE](LICENSE).

## Got stuck?

Drop by [fluidwire.com](https://fluidwire.com/) — happy to help.
