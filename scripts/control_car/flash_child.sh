#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
port="${1:-}"

echo "Flashing CHILD firmware"
echo "BOARD_NAME=xiaozhi-car-mini-s3"
echo "SDKCONFIG=sdkconfig.xiaozhi_car_mini_s3_vi"
echo "BUILD_DIR=$repo_root/build-xiaozhi-car-mini-s3"
echo "PORT=${port:-auto-detect}"

exec "$repo_root/scripts/xiaozhi_car_mini_s3_local.sh" flash "$port"
