#!/bin/bash -e

if [ $# -lt 1 ]; then
    echo "Missing argument: number of tests"
    exit 1
fi

num_tests=$1

cd $(dirname ${BASH_SOURCE[0]})
for i in $(seq 1 $num_tests); do
    time ../grr -r "[a-z][a-zA-Z0-9_]*" -d .. -i -y
done > /dev/null 2> .stats

./stats.py
rm .stats
