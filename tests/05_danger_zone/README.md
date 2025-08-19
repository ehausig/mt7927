# Danger Zone Tests

⚠️ WARNING: These tests WILL cause chip errors requiring power cycle!

## Purpose
Document exactly what causes chip failures for future reference.

## Planned Tests
- `test_bad_registers.c` - Verify danger registers cause errors
- `test_write_active_regions.c` - Confirm writing to 0x080000/0x180000 fails

## Safety Level
CRITICAL - Only run when prepared to power cycle the system.
