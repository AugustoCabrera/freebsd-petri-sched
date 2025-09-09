#!/usr/local/bin/bash
cd toggle_active_cpu/ && make unload && cd ..
cd cpu_stats/ && make unload && cd ..
cd thread_stats/ && make unload && cd ..