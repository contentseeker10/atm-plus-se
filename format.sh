#!/usr/bin/env bash
set -euo pipefail
shopt -s globstar nullglob

clang-format -i **/*.cpp **/*.hpp

echo "Done formatting."
