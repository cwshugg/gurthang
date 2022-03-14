#!/bin/bash
# A simple script that takes in an executable path and an AFL++ crash output
# file and runs it with the executable in GDB with the LD_PRELOAD library.

# check for arguments
if [ $# -lt 2 ]; then
    echo "Usage: $0 PATH_TO_SERVER_EXECUTABLE PATH_TO_CRASH_FILE"
    exit 1
fi

# save variables and run in GDB
bfile=$1
cfile=$2
sslib=~/fuzzing/vt_fuzzing/afl/mutator/gurthang-preload.so
LD_PRELOAD=${sslib} gdb -q ${bfile} \
                   -batch \
                   -ex "set args -p 13650 < ${cfile}" \
                   -ex "run" \
                   -ex "bt" \
