#!/usr/bin/env bash
# 用 VS Code 打开当前项目（脚本所在目录）
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if command -v code >/dev/null 2>&1; then
    code "$PROJECT_DIR"
elif [ -d "/Applications/Visual Studio Code.app" ]; then
    open -a "Visual Studio Code" "$PROJECT_DIR"
else
    echo "未找到 VS Code，请先安装：https://code.visualstudio.com/" >&2
    exit 1
fi
