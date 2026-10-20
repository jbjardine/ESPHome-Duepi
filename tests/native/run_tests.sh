#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
esphome_python=${ESPHOME_PYTHON:-"$repo_root/.venv-esphome/bin/python"}
compiler=${CXX:-c++}

if [ ! -x "$esphome_python" ]; then
  echo "ESPHOME_PYTHON must point to ESPHome 2026.9.1 Python" >&2
  exit 2
fi

esphome_root=$("$esphome_python" -c 'import pathlib, esphome; print(pathlib.Path(esphome.__file__).resolve().parent)')
esphome_site_packages=$(dirname "$esphome_root")
arduinojson_root=${ARDUINOJSON_INCLUDE:-"/Users/jbjardine/.cache/duepi-room-native/ArduinoJson-7.4.3/src"}
if [ ! -f "$arduinojson_root/ArduinoJson.h" ]; then
  echo "ArduinoJson headers are missing from $arduinojson_root (set ARDUINOJSON_INCLUDE to the ArduinoJson src directory)" >&2
  exit 2
fi

build_dir=$(mktemp -d "${TMPDIR:-/tmp}/duepi-room-native.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM

"$compiler" -std=c++20 -Wall -Wextra -Wno-unused-parameter -Wno-macro-redefined -DUSE_HOST \
  -include "$repo_root/tests/native/esphome_script_test_compat.h" \
  -I "$repo_root" \
  -I "$esphome_site_packages" \
  -I "$esphome_root/components/json" \
  -I "$arduinojson_root" \
  "$repo_root/tests/native/test_duepi_room.cpp" \
  "$repo_root/tests/native/esphome_log_stub.cpp" \
  "$esphome_root/components/script/script.cpp" \
  "$repo_root/components/duepi_room/controller.cpp" \
  "$repo_root/components/duepi_room/relay_parser.cpp" \
  "$esphome_root/components/json/json_util.cpp" \
  -o "$build_dir/test_duepi_room"

"$build_dir/test_duepi_room"
