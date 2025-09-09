#!/bin/sh

print_error () {
    if [ "$1" != 1 ]; then
        echo "[ERROR] $2"
        exit 1
    fi
}

echo "[INFO] Compiling plugin with new changes"

cd ..
sh setup.sh
echo "Plugin compiled"
echo ""

echo "[INFO] Testing the plugin: 4 tests of failure and 1 of success"
echo ""

cd test || exit

sh test.sh test.c test_program schedData/hiperf schedData/lowperf
print_error $? "The plugin shouldve failed because of two sched info payloads"
echo "[SUCCESS] Cant have two sched info payloads"
echo ""

sh test.sh test.c test_program dataToInsert/decl_payD.txt
print_error $? "The plugin shouldve failed because of file size"
echo "[SUCCESS] File size too big"
echo ""

sh test.sh test.c test_program dataToInsert/decl_payB.txt schedData/lowperf
print_error $? "The plugin shouldve failed because of sched info payload after bin payload"
echo "[SUCCESS] sched info payload cant be after bin payload"
echo ""

sh test.sh test.c test_program schedData/hiperf dataToInsert/decl_payB.txt dataToInsert/decl_payA.txt dataToInsert/decl_payE.txt dataToInsert/decl_payC.txt
print_error $? "The plugin shouldve failed because of the num of payloads"
echo "[SUCCESS] the max num of payloads is 4"
echo ""

echo "[INFO] This shouldnt fail. test_program results in a executable with \
        sched info payload and 3 bin payloads"
sh test.sh test.c test_program schedData/hiperf dataToInsert/decl_payB.txt dataToInsert/decl_payA.txt dataToInsert/decl_payE.txt 