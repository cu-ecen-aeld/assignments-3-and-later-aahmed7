#!/bin/bash

filesdir=$1
searchstr=$2

if [ -z $filesdir ] || [ -z $searchstr ];then
    echo "Missing input arguments"
    exit 1
fi

if [ ! -d $filesdir ];then
    echo "First argument needs to be a directory on the system"
    exit 1
fi

num_files=$(find $filesdir -type f | wc -l)
num_lines=$(grep -ra $searchstr $filesdir | wc -l)

echo "The number of files are $num_files and the number of matching lines are $num_lines"
exit 0
