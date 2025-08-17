#!/bin/bash

python set_extra_latency.py --reset-all

# Run the experiment with 0 latency
./run_experiment.sh

# run with 6ms delay in compose-review-service
python set_extra_latency.py --service-name compose-review-service --latency-ms 6
./run_experiment.sh

# run with 12ms delay in text-service
python set_extra_latency.py --service-name text-service --latency-ms 12
./run_experiment.sh

# run with 18ms delay in compose-review-service