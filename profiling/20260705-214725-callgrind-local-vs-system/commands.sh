#!/bin/sh
set -eu
# Filled in by the profiling run.
meson setup build-callgrind-release -Dbuildtype=release
meson compile -C build-callgrind-release
cc -O3 -DNDEBUG -Iinclude profiling/20260705-214725-callgrind-local-vs-system/bench_encode.c -L/home/me/projects/lame/build-callgrind-release/libmp3lame -Wl,-rpath,/home/me/projects/lame/build-callgrind-release/libmp3lame -lmp3lame -lm -o profiling/20260705-214725-callgrind-local-vs-system/bench_local
cc -O3 -DNDEBUG -I/usr/include/lame profiling/20260705-214725-callgrind-local-vs-system/bench_encode.c -o profiling/20260705-214725-callgrind-local-vs-system/bench_system -lmp3lame
valgrind --tool=callgrind --collect-atstart=no --toggle-collect=run_encode --callgrind-out-file=profiling/20260705-214725-callgrind-local-vs-system/callgrind.local.out profiling/20260705-214725-callgrind-local-vs-system/bench_local testcase.wav 64
callgrind_annotate --auto=yes profiling/20260705-214725-callgrind-local-vs-system/callgrind.local.out > profiling/20260705-214725-callgrind-local-vs-system/callgrind.local.annotate.txt
valgrind --tool=callgrind --collect-atstart=no --toggle-collect=run_encode --callgrind-out-file=profiling/20260705-214725-callgrind-local-vs-system/callgrind.system.out profiling/20260705-214725-callgrind-local-vs-system/bench_system testcase.wav 64
callgrind_annotate --auto=yes profiling/20260705-214725-callgrind-local-vs-system/callgrind.system.out > profiling/20260705-214725-callgrind-local-vs-system/callgrind.system.annotate.txt
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses profiling/20260705-214725-callgrind-local-vs-system/bench_local testcase.wav 256
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses profiling/20260705-214725-callgrind-local-vs-system/bench_system testcase.wav 256
