# Risky Operations

Tests that might require chip reset if they fail.

## Planned Tests
- `test_memory_activate.c` - Attempt to activate main memory
- `test_firmware_ack.c` - Try firmware acknowledgment sequences
- `test_dma_enable.c` - Modify DMA configuration

## Safety Level
High - May require PCI rescan or power cycle if something goes wrong.
