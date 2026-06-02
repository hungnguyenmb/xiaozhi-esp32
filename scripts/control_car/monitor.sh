#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"

role="${1:-child}"
port="${2:-}"

if [[ "$role" == /dev/* ]]; then
    port="$role"
    role="child"
fi

case "$role" in
    parent|PARENT)
        export CONTROL_CAR_BUILD_DIR="$repo_root/build-car-parent"
        export CONTROL_CAR_SDKCONFIG="sdkconfig.car_parent_128x64_vi"
        export CONTROL_CAR_BOARD_NAME="xiaozhi-car-parent-128x64"
        export CONTROL_CAR_BOARD_TYPE="bread-compact-wifi"
        export CONTROL_CAR_ROLE="PARENT"
        exec "$repo_root/scripts/bread_128x64_local.sh" monitor "$port"
        ;;
    child|CHILD)
        exec "$repo_root/scripts/xiaozhi_car_mini_s3_local.sh" monitor "$port"
        ;;
    *)
        echo "Usage: $0 [parent|child] [PORT]" >&2
        echo "       $0 [PORT]  # defaults to child for backward compatibility" >&2
        exit 2
        ;;
esac
