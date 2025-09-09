#!/usr/local/bin/bash

num_runs=10

total_time=0

for i in $(seq $num_runs); do
    execution_time=$(./primes 10000000 | awk '/:/ {print $2}')

    echo "Run $i: $execution_time sec."
    total_time=$(echo "$total_time + $execution_time" | bc)
done

average_time=$(echo "scale=2; $total_time / $num_runs" | bc)

echo "Average time of execution for $num_runs runs: $average_time seconds"

