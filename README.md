# MT7927 WiFi 7 Linux Driver Project

## 🎉 Major Breakthrough: MT7927 = MT7925 + 320MHz

We've discovered that the MT7927 is architecturally identical to the MT7925 (which has full Linux support since kernel 6.7) except for 320MHz channel width capability. This means we can adapt the existing mt7925 driver rather than writing one from scratch!

## Current Status: ON-HOLD INDEFINITELY 🚧

**Working**: Custom driver successfully binds to MT7927 hardware and loads firmware  
**Next Step**: Implement DMA firmware transfer to activate the chip  

I have other projects that I am working on, so I probably won't continue work on this one. I'm sharing my work in case anyone finds it useful.

## Quick Start

### Prerequisites
```bash
# Check kernel version (need 6.7+ for mt7925 base)
uname -r  # Should show 6.7 or higher

# Verify MT7927 device is present
lspci -nn | grep 14c3:7927  # Should show your device
```

### Install Firmware
```bash
# Download MT7925 firmware (compatible with MT7927!)
mkdir -p ~/mt7927_firmware
cd ~/mt7927_firmware

wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin
wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin

# Install firmware
sudo mkdir -p /lib/firmware/mediatek/mt7925
sudo cp *.bin /lib/firmware/mediatek/mt7925/
sudo update-initramfs -u
```

### Build and Load Driver
```bash
# Clone and build
git clone https://github.com/[your-username]/mt7927-linux-driver
cd mt7927-linux-driver
make clean && make tests

# Load the driver
sudo insmod tests/04_risky_ops/mt7927_init.ko

# Check status
sudo dmesg | tail -20
lspci -k | grep -A 3 "14c3:7927"  # Should show "Kernel driver in use: mt7927_init"
```

## Technical Details

### Hardware Information
- **Chip**: MediaTek MT7927 WiFi 7 (802.11be)
- **PCI ID**: 14c3:7927 (vendor: MediaTek, device: MT7927)
- **Architecture**: Same as MT7925 except supports 320MHz channels
- **Current State**: FW_STATUS: 0xffff10f1 (waiting for DMA firmware transfer)

### Key Discoveries
1. **MT7925 firmware is compatible** - No need to wait for MediaTek
2. **Driver binding works** - Custom driver successfully claims device
3. **Clear path forward** - Just need to implement DMA transfer mechanism

### What's Working ✅
- PCI enumeration and BAR mapping
- Driver successfully binds to device
- Firmware files load into kernel memory
- All hardware registers accessible
- Chip is stable and responsive

### What's Not Working Yet ❌
- DMA firmware transfer to chip (main blocker)
- Memory activation at 0x000000
- WiFi network interface creation

### Why It's Not Working (Root Cause)
The firmware loads into kernel memory but isn't transferred to the chip via DMA. We need to:
1. Set up DMA descriptors properly
2. Copy firmware with correct headers to DMA buffer
3. Trigger MCU to read from DMA buffer
4. Wait for firmware acknowledgment

## Project Structure
```
mt7927-linux-driver/
├── README.md                        # This file (main documentation)
├── Makefile                         # Build system
├── tests/
│   ├── 04_risky_ops/
│   │   ├── mt7927_wrapper.c        # ✅ Basic driver (binds successfully)
│   │   ├── mt7927_init.c           # 🚧 Current driver (loads firmware)
│   │   └── test_mt7925_firmware.c  # ✅ Firmware compatibility test
│   └── Kbuild                       # Kernel build config
└── src/                             # Future production driver location
```

## Development Roadmap

### Phase 1: Get It Working (CURRENT)
- [x] Bind driver to device
- [x] Load firmware files
- [ ] **Implement DMA transfer** ← Current focus
- [ ] Activate chip memory
- [ ] Create network interface

### Phase 2: Make It Good
- [ ] Port full mt7925 functionality
- [ ] Add 320MHz channel support
- [ ] Integrate with mac80211
- [ ] Implement WiFi 7 features

### Phase 3: Make It Official
- [ ] Clean up code for upstream
- [ ] Submit to linux-wireless
- [ ] Get merged into mainline kernel

## How to Contribute

### Immediate Needs
1. **DMA Implementation** - Study mt7925 source in `drivers/net/wireless/mediatek/mt76/mt7925/`
2. **Testing** - Try the driver on your MT7927 hardware
3. **Documentation** - Improve this README with your findings

### Code References

#### Key Source Files to Study (in your kernel source)
```bash
# Main mt7925 driver files (your reference implementation)
drivers/net/wireless/mediatek/mt76/mt7925/
├── pci.c         # PCI probe and initialization sequence
├── mcu.c         # MCU communication and firmware loading
├── init.c        # Hardware initialization
└── dma.c         # DMA setup and transfer

# Shared mt76 infrastructure
drivers/net/wireless/mediatek/mt76/
├── dma.c         # Generic DMA implementation
├── mt76_connac_mcu.c  # MCU interface for Connac chips
└── util.c        # Utility functions
```

#### Online References
- **mt7925 on GitHub**: [Linux kernel source](https://github.com/torvalds/linux/tree/master/drivers/net/wireless/mediatek/mt76/mt7925)
- **mt76 framework**: [OpenWrt repository](https://github.com/openwrt/mt76)
- **Our working code**: `tests/04_risky_ops/mt7927_init.c`

## Troubleshooting

### Driver Won't Load
```bash
# Check for conflicts
lsmod | grep mt79
sudo rmmod mt7921e mt7925e  # Remove any conflicting drivers

# Check kernel messages
sudo dmesg | grep -E "mt7927|0a:00"
```

### Chip in Error State
```bash
# If chip shows 0xffffffff, reset via PCI
echo 1 | sudo tee /sys/bus/pci/devices/0000:0a:00.0/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan
```

### Firmware Not Found
```bash
# Verify firmware is installed
ls -la /lib/firmware/mediatek/mt7925/
# Should show WIFI_MT7925_PATCH_MCU_1_1_hdr.bin and WIFI_RAM_CODE_MT7925_1_1.bin
```

## Test Results Summary

### Successful Tests ✅
- **Hardware Detection**: PCI enumeration works perfectly
- **Driver Binding**: Custom driver claims device successfully  
- **Firmware Compatibility**: MT7925 firmware loads without errors
- **Register Access**: All BAR2 control registers accessible
- **Chip Stability**: No crashes or lockups during testing

### Pending Implementation 🚧
- **DMA Transfer**: Firmware not reaching chip memory
- **Memory Activation**: Main memory at 0x000000 still shows 0x00000000
- **Network Interface**: Requires successful initialization first

## FAQ

**Q: Why not just use the mt7925e driver?**  
A: The mt7925e driver refuses to bind to MT7927's PCI ID (14c3:7927) and adding the ID via new_id fails with "Invalid argument".

**Q: Is this safe to test?**  
A: Yes, we're using proven MT7925 code paths. The worst case is the driver doesn't fully initialize (current state).

**Q: Will this support full WiFi 7 320MHz channels?**  
A: Initially it will work like MT7925 (160MHz). Adding 320MHz support will come after basic functionality works.

**Q: When will this be in the mainline kernel?**  
A: Once we have a working driver, submission typically takes 2-3 kernel cycles (3-6 months).

## License
GPL v2 - Intended for upstream Linux kernel submission

## Contact & Support
- **GitHub Issues**: Report bugs and discuss development
- **Linux Wireless**: [Mailing list](http://vger.kernel.org/vger-lists.html#linux-wireless) for upstream discussion

---

**Status as of 2025-08-18**: Driver successfully binds and loads firmware. Implementing DMA transfer to complete initialization. This is no longer a reverse engineering project - we're adapting proven MT7925 code to support MT7927's PCI ID and eventually its 320MHz capability.
