# Safe Basic Tests

These tests are always safe to run and verify fundamental functionality.
They should NEVER cause chip errors or require reboots.

## Tests
- `test_pci_enum.c` - PCI enumeration verification
- `test_bar_map.c` - BAR mapping check
- `test_chip_id.c` - Chip identification
- `test_scratch_rw.c` - Scratch register read/write

## Usage
```bash
./run_tests.sh
```
