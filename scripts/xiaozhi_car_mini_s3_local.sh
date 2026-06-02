#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build-xiaozhi-car-mini-s3"
sdkconfig_file="sdkconfig.xiaozhi_car_mini_s3_vi"
sdkconfig_defaults="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.xiaozhi_car_mini_s3_vi"
board_name="xiaozhi-car-mini-s3"
board_type="xiaozhi-car-mini-s3"

find_latest_file() {
    local base="$1"
    local pattern="$2"
    find "$base" -type f -path "$pattern" 2>/dev/null | sort | tail -n 1
}

resolve_idf_path() {
    if [[ -n "${IDF_PATH:-}" && -d "${IDF_PATH:-}" ]]; then
        printf '%s\n' "$IDF_PATH"
        return
    fi

    if [[ -d "$HOME/esp/v5.5.2/esp-idf" ]]; then
        printf '%s\n' "$HOME/esp/v5.5.2/esp-idf"
        return
    fi

    find "$HOME/esp" -maxdepth 2 -type d -path '*/esp-idf' 2>/dev/null | sort | tail -n 1
}

resolve_python_env() {
    if [[ -n "${IDF_PYTHON_ENV_PATH:-}" && -x "${IDF_PYTHON_ENV_PATH:-}/bin/python" ]]; then
        printf '%s\n' "$IDF_PYTHON_ENV_PATH"
        return
    fi

    if [[ -x "$HOME/.espressif/python_env/idf5.5_py3.13_env/bin/python" ]]; then
        printf '%s\n' "$HOME/.espressif/python_env/idf5.5_py3.13_env"
        return
    fi

    local python_bin
    python_bin="$(find "$HOME/.espressif/python_env" -maxdepth 2 -type f -path '*/bin/python' 2>/dev/null | sort | tail -n 1)"
    if [[ -n "$python_bin" ]]; then
        dirname "$(dirname "$python_bin")"
    fi
}

resolve_tool_dir() {
    local file_path="$1"
    if [[ -n "$file_path" ]]; then
        dirname "$file_path"
    fi
}

detect_port() {
    local first=""
    for first in /dev/cu.usbmodem* /dev/cu.usbserial* /dev/tty.usbmodem* /dev/tty.usbserial*; do
        if [[ -e "$first" ]]; then
            printf '%s\n' "$first"
            return
        fi
    done
    return 1
}

usage() {
    cat <<'EOF'
Usage:
  ./scripts/xiaozhi_car_mini_s3_local.sh env
  ./scripts/xiaozhi_car_mini_s3_local.sh build
  ./scripts/xiaozhi_car_mini_s3_local.sh flash [PORT]
  ./scripts/xiaozhi_car_mini_s3_local.sh monitor [PORT]

Default port can be passed explicitly, for example:
  ./scripts/xiaozhi_car_mini_s3_local.sh flash /dev/cu.usbmodem5C372139691
EOF
}

idf_path="$(resolve_idf_path)"
if [[ -z "$idf_path" || ! -d "$idf_path" ]]; then
    echo "Cannot find ESP-IDF under \$HOME/esp. Set IDF_PATH and retry." >&2
    exit 1
fi

idf_python_env="$(resolve_python_env)"
if [[ -z "$idf_python_env" || ! -x "$idf_python_env/bin/python" ]]; then
    echo "Cannot find ESP-IDF python env under \$HOME/.espressif/python_env. Set IDF_PYTHON_ENV_PATH and retry." >&2
    exit 1
fi

cmake_dir="$(resolve_tool_dir "$(find_latest_file "$HOME/.espressif/tools/cmake" '*/bin/cmake')")"
ninja_dir="$(resolve_tool_dir "$(find_latest_file "$HOME/.espressif/tools/ninja" '*/ninja')")"
xtensa_dir="$(resolve_tool_dir "$(find_latest_file "$HOME/.espressif/tools/xtensa-esp-elf" '*/xtensa-esp-elf/bin/xtensa-esp-elf-gcc')")"
gdb_dir="$(resolve_tool_dir "$(find_latest_file "$HOME/.espressif/tools/xtensa-esp-elf-gdb" '*/xtensa-esp-elf-gdb/bin/xtensa-esp-elf-gdb')")"
ulp_dir="$(resolve_tool_dir "$(find_latest_file "$HOME/.espressif/tools/esp32ulp-elf" '*/esp32ulp-elf/bin/esp32ulp-elf-as')")"
openocd_dir="$(resolve_tool_dir "$(find_latest_file "$HOME/.espressif/tools/openocd-esp32" '*/openocd-esp32/bin/openocd')")"

export IDF_PATH="$idf_path"
export IDF_PYTHON_ENV_PATH="$idf_python_env"
export PATH="$idf_python_env/bin"

for maybe_dir in "$cmake_dir" "$ninja_dir" "$xtensa_dir" "$gdb_dir" "$ulp_dir" "$openocd_dir" /usr/bin /bin /usr/sbin /sbin; do
    if [[ -n "$maybe_dir" && -d "$maybe_dir" ]]; then
        export PATH="$maybe_dir:$PATH"
    fi
done

cd "$repo_root"

idf_py=(
    "$idf_python_env/bin/python"
    "$IDF_PATH/tools/idf.py"
    -B "$build_dir"
    -D "SDKCONFIG=$sdkconfig_file"
    -D "SDKCONFIG_DEFAULTS=$sdkconfig_defaults"
    -D "BOARD_NAME=$board_name"
    -D "BOARD_TYPE=$board_type"
)

ensure_build() {
    if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
        "${idf_py[@]}" set-target esp32s3 build
    else
        "${idf_py[@]}" build
    fi
}

command="${1:-}"
case "$command" in
    env)
        cat <<EOF
IDF_PATH=$IDF_PATH
IDF_PYTHON_ENV_PATH=$IDF_PYTHON_ENV_PATH
cmake=${cmake_dir:-missing}
ninja=${ninja_dir:-missing}
xtensa=${xtensa_dir:-missing}
build_dir=$build_dir
sdkconfig=$sdkconfig_file
sdkconfig_defaults=$sdkconfig_defaults
EOF
        ;;
    build)
        ensure_build
        ;;
    flash)
        shift || true
        ensure_build
        port="${1:-$(detect_port || true)}"
        if [[ -z "$port" ]]; then
            echo "Cannot auto-detect ESP32 serial port. Pass it explicitly." >&2
            exit 1
        fi
        "${idf_py[@]}" -p "$port" flash
        ;;
    monitor)
        shift || true
        port="${1:-$(detect_port || true)}"
        if [[ -z "$port" ]]; then
            echo "Cannot auto-detect ESP32 serial port. Pass it explicitly." >&2
            exit 1
        fi
        "${idf_py[@]}" -p "$port" monitor
        ;;
    *)
        usage >&2
        exit 1
        ;;
esac
