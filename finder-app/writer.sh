#!/bin/bash

writefile=$1
writestr=$2

if [ -z $writefile ] || [ -z $writestr ];then
    echo "Missing input arguments"
    exit 1
fi

writedir=$(dirname $writefile)
if [ ! -d $writedir ];then
    mkdir -p $writedir
    if [ $? -ne 0 ];then
        echo "Unable to create directory"
        exit 1
    fi
fi

if [ ! -f $writefile ];then
    touch $writefile
    if [ $? -ne 0 ];then
        echo "Unable to create file"
        exit 1
    fi
fi

echo $writestr > $writefile
