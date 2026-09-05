#!/usr/bin/env bash
# Build the P4 image that is compatible with the Waveshare board's factory C6.
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
core_dir="${ARDUINO_ESP32_PATH:-$script_dir/.deps/arduino}"

if [ ! -d "$core_dir/.git" ]; then
    mkdir -p "$(dirname "$core_dir")"
    git clone --depth 1 --branch 3.3.11 https://github.com/espressif/arduino-esp32.git "$core_dir"
fi

# The factory C6 speaks ESP-Hosted 1.4.x. Overlay just the Arduino adapter and
# its component manifest; the rest stays upstream Arduino 3.3.11.
cp "$script_dir/overlay/esp32-hal-hosted.c" "$core_dir/cores/esp32/esp32-hal-hosted.c"
cp "$script_dir/overlay/idf_component.yml" "$core_dir/idf_component.yml"

async_tcp_dir="$script_dir/components/tonex_async_tcp/vendor"
async_web_dir="$script_dir/components/tonex_async_web/vendor"
if [ ! -d "$async_tcp_dir/.git" ]; then
    git clone --depth 1 --branch v3.3.2 https://github.com/mathieucarbou/AsyncTCP.git "$async_tcp_dir"
fi
if [ ! -d "$async_web_dir/.git" ]; then
    git clone --depth 1 --branch v3.6.0 https://github.com/mathieucarbou/ESPAsyncWebServer.git "$async_web_dir"
fi

: "${IDF_PATH:?Source the ESP-IDF export.sh before invoking this script.}"
export ARDUINO_SKIP_IDF_VERSION_CHECK=1

idf.py -C "$script_dir" -B "$script_dir/build" \
    -D "EXTRA_COMPONENT_DIRS=$core_dir" \
    -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults" \
    "$@"
