#!/usr/local/bin/bash

cd ../ || exit 

echo -e "[INFO] Se van a cargar los modulos desarrollados.\n"
make load

cd tests/ || exit

echo -e "[INFO] Se ejecuta un programa que simula carga de cpu.\n"

stress --cpu 3 --timeout 80s &

echo -e "[INFO] Se lanzan algunos programas con metadata.\n"

for i in 1 2 3; do 
    ./lowperf &
    ./hiperf &
done

./critical &

time ./primes 10000000

./hiperf

cd ../ || exit 

echo -e "[INFO] Se descargan los modulos.\n"
make unload

tail -20 /var/log/stats/manager.log
