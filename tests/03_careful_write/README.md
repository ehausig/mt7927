# Careful Write Tests

Tests that write to known-safe areas only.

## Planned Tests
- `test_mode_toggle.c` - Toggle MODE1/MODE2 registers
- `test_dma_probe.c` - Carefully probe DMA configuration
- `test_config_execute.c` - Try executing config commands

## Safety Level
Medium - These tests modify chip state but only in known-safe ways.
