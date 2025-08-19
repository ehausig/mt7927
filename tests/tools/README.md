# MT7927 Exploration Tools

These are not tests but rather exploration and debugging utilities that helped discover the chip's behavior.

## Available Tools

### mt7927_data_dumper.c
Comprehensive chip state dump tool. Reads all safe registers and memory regions.
```bash
make -C .. tools/mt7927_data_dumper.ko
sudo insmod mt7927_data_dumper.ko
sudo dmesg | tail -100
sudo rmmod mt7927_data_dumper
```

### mt7927_deep_explorer.c
Deep memory exploration tool. Discovers active memory regions and patterns.
```bash
make -C .. tools/mt7927_deep_explorer.ko
sudo insmod mt7927_deep_explorer.ko
sudo dmesg | tail -100
sudo rmmod mt7927_deep_explorer
```

### mt7927_final_analysis.c
Summary analysis tool. Provides comprehensive overview of chip state.
```bash
make -C .. tools/mt7927_final_analysis.ko
sudo insmod mt7927_final_analysis.ko
sudo dmesg | tail -100
sudo rmmod mt7927_final_analysis
```

### dump_state.sh
Wrapper script for quick state dumps.
```bash
./dump_state.sh
```

## Building Tools

From the tests directory:
```bash
make tools
```

Or individually:
```bash
make tools/mt7927_data_dumper.ko
```

## Usage Notes

These tools are for exploration and debugging:
- They perform read-only operations (safe)
- They generate extensive kernel log output
- They're useful for understanding chip state
- They're not part of the test suite

## Safety

All tools here are READ-ONLY and safe to run without risk of putting the chip into error state.
