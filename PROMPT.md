# MT7927 WiFi 7 Linux Driver Development - Session Bootstrap

You are helping develop the first Linux driver for the MediaTek MT7927 WiFi 7 chip. This project has made a major breakthrough: we discovered that MT7927 is architecturally identical to MT7925 (which has full Linux support) except for 320MHz channel width support.

## Critical Discovery
**MT7927 = MT7925 + 320MHz channels**. This means we can adapt the existing mt7925 driver rather than reverse engineering from scratch. MT7925 firmware is compatible with MT7927.

## Current Status (as of last session)

### What's Working ✅
- Driver successfully binds to hardware
- DMA descriptor ring allocates properly
- MCU responds as ready (0x00000001)
- Firmware loads into kernel memory (1.2MB + 212KB files)
- Chip is stable, no crashes

### Current Blocker 🚧
**The chip is in a pre-initialization state and won't accept firmware:**
- FW_STATUS register stuck at `0xffff10f1` (read-only)
- WPDMA_GLO_CFG register stays at `0x00000000` (won't enable)
- DMA_ENABLE stuck at `0xf5` (can't modify)
- Memory at BAR0[0x000000] never activates

**Key Issue**: Critical registers appear write-protected. The chip seems to be waiting for an unknown initialization trigger before accepting firmware.

## Instructions for AI Assistant

### 1. Understand Current State
**ALWAYS** start by checking:
- `README.md` - Contains detailed technical findings and register analysis
- `tests/04_risky_ops/mt7927_init_dma.c` - Our most complete driver attempt
- Recent test results showing register states

### 2. Project Structure
```
mt7927_project/
├── README.md               # Detailed documentation with register analysis
├── PROMPT.md              # This file
├── Makefile               # Main build system
├── tests/                 # Test modules and drivers
│   ├── Kbuild            # Kernel build configuration
│   ├── 04_risky_ops/     # Active development
│   │   ├── mt7927_init_dma.c      # Best driver attempt (with DMA)
│   │   ├── test_trigger_fw.c      # Firmware trigger attempts
│   │   ├── test_read_config.c     # Configuration reader
│   │   └── test_mcu_direct.c      # MCU communication tests
│   └── [other test directories]
└── src/                   # Future production driver location
```

### 3. Current Investigation Focus

#### The Register Write Protection Problem
**Critical finding**: Key registers won't accept writes
- `BAR2[0x0208]` (WPDMA_GLO_CFG): Stays at 0x00000000, should be 0x00000001
- `BAR2[0x0200]` (FW_STATUS): Stuck at 0xffff10f1, completely read-only
- `BAR2[0x0204]` (DMA_ENABLE): Stuck at 0xf5, should be 0xFF

**This suggests** the chip needs a specific unlock sequence or prerequisite initialization step.

#### Configuration at 0x080000
- Contains 79 initialization commands in format 0x16CCRRDD
- Register 0x81 appears 13 times (OR with 0x02)
- But we can't execute these because registers are protected

### 4. Key Technical Context
- **Hardware**: MediaTek MT7927 (PCI ID: 14c3:7927)
- **Kernel**: 6.12+ with mt76 source available
- **Firmware**: Using MT7925 firmware files (compatible!)
- **DMA**: Descriptor ring allocates but transfers don't work
- **MCU**: Responds as ready but doesn't process firmware

### 5. Development Approach
- **Focus on unlock sequence** - Find what enables register writes
- **Study mt7925 source** - The answer is likely in the probe sequence
- **Small incremental tests** - Each test should check one theory
- **Document all findings** - Update README.md with discoveries

### 6. Working Commands
```bash
# Check MT7927 detection
lspci -nn | grep 14c3:7927

# Build drivers/tests
cd ~/mt7927_project
make clean && make tests

# Load best driver attempt
sudo rmmod mt7927_init_dma 2>/dev/null
sudo insmod tests/04_risky_ops/mt7927_init_dma.ko
sudo dmesg | tail -40

# Check binding status
lspci -k | grep -A 3 "14c3:7927"

# If chip enters error state
echo 1 | sudo tee /sys/bus/pci/devices/0000:0a:00.0/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan
```

### 7. Next Investigation Priorities

1. **Find the unlock sequence**
   - What makes WPDMA_GLO_CFG writable?
   - Is there a power/clock enable step?
   - Check PCIe config space initialization

2. **Decode 0xffff10f1**
   - This specific value must mean something
   - The 0xffff prefix suggests error/pre-init state
   - Compare with mt7925 initialization states

3. **Alternative register mappings**
   - Maybe mt7927 uses different register offsets?
   - Check if there's a register window/bank switch

4. **Study mt7925 probe sequence**
   - Look at the very first steps, before firmware
   - Focus on power management and clock setup

### 8. Available Resources

- **mt76 source**: `/lib/modules/$(uname -r)/build/drivers/net/wireless/mediatek/mt76/`
- **mt7925 driver**: `mt76/mt7925/` directory
- **Our tests**: 24+ test modules exploring different theories
- **Configuration**: Valid init commands at BAR0[0x080000]

## How to Assist

### When User Asks About Status
1. Current blocker: Registers are write-protected
2. FW_STATUS stuck at 0xffff10f1 
3. WPDMA won't enable (stays at 0x00000000)
4. Need to find unlock/prerequisite sequence

### When User Wants to Test
1. Create focused test modules for specific theories
2. Always check register states before/after
3. Use artifact windows for new files
4. Keep tests small and incremental

### Key Messages to Convey

⚠️ **Current Challenge**:
- Chip is in locked/protected state
- Registers won't accept writes
- Need to find initialization trigger

🔍 **Investigation Needed**:
- Study mt7925 probe sequence deeply
- Find what enables register writes
- Understand 0xffff10f1 meaning

💡 **Likely Solutions**:
- Missing power/clock enable step
- Need specific unlock sequence
- Different register mapping
- PCIe config space initialization

## Project Mission

Find the initialization sequence that unlocks the MT7927's registers, enabling WPDMA and allowing firmware transfer via DMA. Once registers are writable, the existing DMA code should work.

---

**Remember**: The hardware works, firmware is compatible, and driver binds successfully. We just need to find the magic sequence that transitions the chip from protected/pre-init state to accepting firmware. Check README.md for detailed register analysis and test results.
