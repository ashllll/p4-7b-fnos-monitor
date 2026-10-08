#!/usr/bin/env bash
# Activate ESP-IDF beforehand, or supply a local activation script via FNOS_IDF_ENV.
# esp_wifi_remote requires ESP_IDF_VERSION in major.minor form even for packaged IDF.
set -euo pipefail
if [ -n "${FNOS_IDF_ENV:-}" ]; then
  [ -f "$FNOS_IDF_ENV" ] || { echo "IDF activation script not found" >&2; exit 1; }
  . "$FNOS_IDF_ENV"
fi
if [ -z "${IDF_PATH:-}" ] || [ ! -f "$IDF_PATH/tools/idf.py" ]; then
  echo "Activate ESP-IDF first, or set FNOS_IDF_ENV to your activation script." >&2
  exit 1
fi
FNOS_IDF_PYTHON="${FNOS_IDF_PYTHON:-${IDF_PYTHON_ENV_PATH:+$IDF_PYTHON_ENV_PATH/bin/python}}"
FNOS_IDF_PYTHON="${FNOS_IDF_PYTHON:-python3}"
if [ -z "${ESP_IDF_VERSION:-}" ]; then
  FNOS_IDF_FULL_VERSION=$("$FNOS_IDF_PYTHON" "$IDF_PATH/tools/idf.py" --version)
  ESP_IDF_VERSION=$(printf '%s\n' "$FNOS_IDF_FULL_VERSION" | sed -nE 's/^ESP-IDF v?([0-9]+\.[0-9]+).*/\1/p')
  [ -n "$ESP_IDF_VERSION" ] || { echo "Cannot determine ESP-IDF version" >&2; exit 1; }
  export ESP_IDF_VERSION
fi
if [ "$(uname -s)" = Darwin ] && [ -d /Library/Developer/CommandLineTools ]; then
  export DEVELOPER_DIR="${DEVELOPER_DIR:-/Library/Developer/CommandLineTools}"
fi
exec "$FNOS_IDF_PYTHON" "$IDF_PATH/tools/idf.py" "$@"
