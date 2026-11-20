#!/usr/bin/env bash
# No board/ESC access; isolated host tests of receive-only parsing.
set -euo pipefail
cd "$(dirname "$0")/../.."
export PYTHONPATH=Tools/hobbywing_v5
python3 -m unittest discover -s Tools/hobbywing_v5 -p 'test_*.py' -v
for source in Tools/hobbywing_v5/test_*.cpp; do
  output="/tmp/hwv5_$(basename "${source%.cpp}")"
  c++ -std=c++11 -O1 -g -Wall -Wextra -Werror -pedantic "$source" -o "$output"
  "$output"
done
echo "Hobbywing V5 host tests complete."
