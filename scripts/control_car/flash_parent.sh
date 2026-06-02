#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
port="${1:-}"

export CONTROL_CAR_BUILD_DIR="$repo_root/build-car-parent"
export CONTROL_CAR_SDKCONFIG="sdkconfig.car_parent_128x64_vi"
export CONTROL_CAR_BOARD_NAME="xiaozhi-car-parent-128x64"
export CONTROL_CAR_BOARD_TYPE="bread-compact-wifi"
export CONTROL_CAR_ROLE="PARENT"

echo "Flashing PARENT firmware"
echo "BOARD_NAME=$CONTROL_CAR_BOARD_NAME"
echo "SDKCONFIG=$CONTROL_CAR_SDKCONFIG"
echo "BUILD_DIR=$CONTROL_CAR_BUILD_DIR"
echo "PORT=${port:-auto-detect}"

exec "$repo_root/scripts/bread_128x64_local.sh" flash "$port"
