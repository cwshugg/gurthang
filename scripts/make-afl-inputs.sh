#!/bin/bash
# Script to take a series of text files, in some directory, and turn them into
# comux files with varying properties.
#
#   Connor Shugg

# Global variables
comux=$(realpath $(dirname $0))/../comux
c_none="\033[0m"
c_gray="\033[90m"
c_green="\033[32m"

# ============================ Helper Functions ============================ # 
# Takes bytes from a file specified by a length and offset, then places them
# into the specified output file.
function __extract_file_bytes()
{
    local infile=$1
    local outfile=$2
    local length=$3
    local offset=$4

    # invoke 'dd' to get the job done
    dd if=${infile} \
       of=${outfile} \
       bs=1 \
       count=${length} \
       skip=${offset} \
       2> /dev/null
}

# Helper function that takes in a comux file and a number indicating the
# number of chunks to split it into, and creates an output file.
# Expects the given comux file to have 1 connection and 1 chunk. Undefined
# behavior will occur if a different file is given.
function __comux_fragment()
{
    local infile=$1
    local outfile=$2
    local count=$3
    local offset=0
    local tmpfile=$(dirname ${infile})/tmp

    # calculate the input file's size
    local infile_size=$(wc -c < ${infile})
    local piece_size=$((infile_size/count))

    # iterate and extract chunks
    for (( idx=0; idx<${count}; idx++ )); do
        # compute the size of this chunk
        local chunk_size=${piece_size}
        if [ ${infile_size} -lt ${chunk_size} ]; then
            chunk_size=${infile_size}
        fi

        # extract the bytes into a temporary file
        __extract_file_bytes ${infile} ${tmpfile} ${chunk_size} ${offset}
        
        # modify the comux file
        if [ ${idx} -eq 0 ]; then
            # convert the first time
            ${comux} -c -i ${tmpfile} -o ${outfile}
        elif [ ${idx} -eq $((count-1)) ]; then
            # append with the correct flags the final time
            # (the LD_PRELOAD should AWAIT_RESPONSE on the final chunk's release)
            ${comux} -a ${outfile} -o ${outfile} -i ${tmpfile} -S ${idx} -F AWAIT_RESPONSE
        else
            # append the second, third, fourth, etc., times
            ${comux} -a ${outfile} -o ${outfile} -i ${tmpfile} -S ${idx}
        fi

        # increment/decrement
        infile_size=$((infile_size-chunk_size))
        offset=$((offset+chunk_size))
    done
    
    # remove the temporary file
    rm -f ${tmpfile}
}

# Takes in an integer and uses it to select N random files in the given output
# directory. This is used to randomly choose comux files to combine.
function __select_random_files()
{
    local idir=$1
    local count=$2
    
    # create a local array of file names and iterate 'count' times
    local fpaths=()
    for (( idx=0; idx<${count}; idx++ )); do
        # run 'ls' and pipe its contents into 'shuf' to get a random
        # permutation (we'll specify to 'shuf' to output only 1 line)
        fpaths+=("$(ls ${idir} | shuf -n 1)")
    done

    # echo the array (caller must capture this)
    echo ${fpaths[@]}
}

# Function that creates randomized combinations of standard comux files.
function __make_random_combined_comux()
{
    local idir=$1
    local count=$2
    local outfile=$3

    # get an array of randomly-chosen files, then allocate an array(s) to keep
    # track of the current chunk for each file, and the total number of chunks
    fpaths=$(__select_random_files ${idir} ${count})
    declare -a fps=( $(for fp in ${fpaths[@]}; do echo "${idir}/${fp}"; done) )
    declare -a cindexes=( $(for (( idx=0; idx<${count}; idx++ )); do echo 0; done) )
    declare -a cmaxes=( $(for (( idx=0; idx<${count}; idx++ )); do echo 0; done) )
    declare -a cconns=( $(for (( idx=0; idx<${count}; idx++ )); do echo -1; done) )
    local total_chunks=0

    # iterate through each file and count the number of chunks present
    local idx=0
    for fp in ${fpaths[@]}; do
        fp=${idir}/${fp}
        # run comux -s on the file, extract the chunk number, and add it to the
        # array of chunk maxes
        out="$(${comux} -s -i ${fp} | grep -o -E 'num_chunks: [0-9]+')"
        cmaxes[${idx}]=$(echo ${out} | cut -d ' ' -f 2)
        total_chunks=$((total_chunks+cmaxes[idx]))

        idx=$((idx+1))
    done

    echo -en "Generating random-combination file ${c_gray}${outfile}${c_none}"

    # next we'll iterate until we've combined all chunks into one file
    local chunks_handled=0
    local next_conn_idx=0
    while [ ${chunks_handled} -lt ${total_chunks} ]; do
        # generate a random number to indicate which file we'll pull a chunk
        # from next. Do this until we find a file whose chunks haven't all
        # been used already
        fidx=-1
        while [ ${fidx} -eq -1 ]; do
            fidx=$(shuf -i 0-$((count-1)) -n 1)
            # if the counter for this file has already hit the max, that means
            # we've already used up its chunks
            if [ ${cindexes[${fidx}]} -eq ${cmaxes[${fidx}]} ]; then
                fidx=-1
            fi
        done

        # with this file index, extract the correct chunk from the file and
        # save it to a temporary file
        local fpath=${fps[${fidx}]}
        local tmpfile=$(dirname ${fpath})/tmp
        ${comux} -x ${cindexes[${fidx}]} -i ${fpath} -o ${tmpfile}

        # decide what to set for the chunk's connection, scheduling, and flags
        # field, based on which chunk and which file we're looking at
        if [ ${cconns[${fidx}]} -eq -1 ]; then
            # assign the next-available connection ID to the current file
            cconns[${fidx}]=${next_conn_idx}
            next_conn_idx=$((next_conn_idx+1))
        fi
        chunk_conn=${cconns[${fidx}]}
        chunk_sched=${chunks_handled}
        chunk_flags=""
        if [ ${cindexes[${fidx}]} -eq $((cmaxes[fidx]-1)) ]; then
            # if this is the last chunk for a particular file, make sure to set
            # the AWAIT_RESPONSE flag
            chunk_flags=" -F AWAIT_RESPONSE"
        fi

        # add this chunk to the comux file
        if [ ${chunks_handled} -eq 0 ]; then
            # convert on the first case
            ${comux} -c -i ${tmpfile} -o ${outfile} \
                     -C ${chunk_conn} -S ${chunk_sched} ${chunk_flags}
            # also, set the number of connections
            ${comux} -N ${count} -i ${outfile} -o ${outfile}
        else
            # append on all following cases
            ${comux} -a ${outfile} -i ${tmpfile} -o ${outfile} \
                     -C ${chunk_conn} -S ${chunk_sched} ${chunk_flags}
        fi

        rm -f ${tmpfile} 
        # increment the chunk index for the selected file and increment the
        # total number of chunks handled
        cindexes[${fidx}]=$((cindexes[fidx]+1))
        chunks_handled=$((chunks_handled+1))

        echo -n "."
    done

    echo -e " ${c_green}combined ${total_chunks} chunks from ${count} files.${c_none}"
}

# =============================== Runner Code =============================== #
# check for arguments
if [ $# -lt 2 ]; then
    echo "Usage: $0 INPUT_DIRECTORY OUTPUT_DIRECTORY"
    exit -1
fi
idir=$1
odir=$2

# make sure the input directory exists
if [ ! -d ${idir} ]; then
    echo "Error: couldn't find input directory: ${idir}"
    exit -1
fi

# make sure the output directory DOESN'T already exist
if [ -d ${odir} ]; then
    echo "Error: output directory already exists: ${odir}"
    exit -1
fi
mkdir ${odir}

# iterate through each input file in the input directory
for infile in ${idir}/*; do
    # if it's a directory, skip it
    if [ -d ${infile} ]; then
        continue
    fi

    # get the base name and remove the extension
    fname="$(basename ${infile%.*})"
    fpath=${odir}/${fname}.comux

    echo -en "Converting ${c_gray}${fname}${c_none}... "

    # COMUX FILE 1: 1 connection, 1 chunk
    outfile=${fpath}.1c1c
    ${comux} -c -i ${infile} -o ${outfile} -F AWAIT_RESPONSE
    echo -en "${c_green}1c1c${c_none} "

    # COMUX FILE 2: 1 connection, 2 chunks
    outfile=${fpath}.1c2c
    __comux_fragment ${infile} ${outfile} 2
    echo -en "${c_green}1c2c${c_none} "

    # COMUX FILE 3: 1 connection, 4 chunks
    outfile=${fpath}.1c4c
    __comux_fragment ${infile} ${outfile} 4
    echo -e "${c_green}1c4c${c_none} "
done

# next, we'll generate some randomly-chosen files that combine multiple comux
# files (from the ones generated above)
for conn_count in {2..4}; do
    random_gen_count=40
    idx=0
    while [ ${idx} -lt ${random_gen_count} ]; do
        # come up with an output file name and generate it
        outfile=${odir}/random${conn_count}c_${idx}.comux
        __make_random_combined_comux ${odir} ${conn_count} ${outfile}
    
        idx=$((idx+1))
    done
done

