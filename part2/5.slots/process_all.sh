#!/bin/bash

for file in "$@"; do
    ./complete "$file" > tmp-process.txt && mv tmp-process.txt "$file"
done
