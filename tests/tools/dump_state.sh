#!/bin/bash
# Wrapper for comprehensive state dump
# Based on original mt7927_data_dumper.c

echo "MT7927 State Dump Tool"
echo "======================"

if [ ! -f "../archive/mt7927_data_dumper.ko" ]; then
    echo "Building module..."
    make -C ../.. mt7927_data_dumper.ko
fi

sudo dmesg -C
sudo insmod ../archive/mt7927_data_dumper.ko
sleep 1
sudo dmesg | tee mt7927_state_$(date +%Y%m%d_%H%M%S).log
sudo rmmod mt7927_data_dumper

echo "State dump complete."
