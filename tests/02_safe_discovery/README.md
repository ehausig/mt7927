# Safe Discovery Tests

Read-only exploration tests that discover chip features without modifying state.

## Tests
- `test_memory_scan.c` - Scan memory regions (read-only)
- `test_reg_dump.c` - Dump all safe registers
- `test_config_read.c` - Read configuration data
- `test_fw_detect.c` - Detect firmware regions

## Usage
```bash
./run_tests.sh
```
