#!/bin/bash
# A simple shell script to launch AFL++ with my mutator module.
#
#   Connor Shugg

c_none="\033[0m"
c_err="\033[31m"

mutator=./gurthang-mutator.so

# if the mutator can't be found, complain
if [ ! -f ${mutator} ]; then
    echo -e "${c_err}Error:${c_none} couldn't find mutator library: ${mutator}."
    exit 1
fi
