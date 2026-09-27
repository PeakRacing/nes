#!/bin/sh
# usage: ./run_tests.sh [all|cpu|ppu|apu|rom|mapper|stress|corpus] [report.csv]
#        ./run_tests.sh corpus out/report.csv --update-baseline --frames 180
set -eu
filter=${1:-all}
report=${2:-out/report.csv}
shift || true
shift || true
cd "$(dirname "$0")"
xmake f -m debug -y >/dev/null
xmake build nes-tests
exec ./out/bin/nes-tests --filter "$filter" --report "$report" "$@"
