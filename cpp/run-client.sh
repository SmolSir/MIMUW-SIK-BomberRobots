#!/bin/bash
cd build
cmake ..
make
./robots-client         \
    -d 127.0.0.1:10002  \
    -n SmolSir          \
    -p 10022            \
    -s 127.0.0.1:10222