#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927

static void decode_config_pattern(u32 val, int offset)
{
    if ((val & 0xFF000000) == 0x16000000) {
        u8 cmd = (val >> 16) & 0xFF;
        u8 reg = (val >> 8) & 0xFF;
        u8 val_byte = val & 0xFF;
        printk(KERN_INFO "  [0x%06x]: 0x%08x -> CMD:0x%02x REG:0x%02x VAL:0x%02x\n",
               offset, val, cmd, reg, val_byte);
    } else if ((val & 0xFF000000) == 0x31000000) {
        printk(KERN_INFO "  [0x%06x]: 0x%08x -> DELIMITER/END\n", offset, val);
    }
}

static int mt7927_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar0, *bar2;
    u32 val, val2;
    int ret, i;
    
    ret = pci_enable_device(pdev);
    if (ret) return ret;
    
    pci_set_master(pdev);
    
    ret = pci_request_regions(pdev, "mt7927_deep");
    if (ret) goto err_disable;
    
    bar0 = pci_iomap(pdev, 0, 0);
    if (!bar0) goto err_release;
    
    bar2 = pci_iomap(pdev, 2, 0);
    if (!bar2) goto err_unmap_bar0;
    
    val = ioread32(bar2);
    if (val == 0xffffffff) {
        printk(KERN_ERR "Chip in error state!\n");
        goto cleanup;
    }
    
    printk(KERN_INFO "\n===========================================\n");
    printk(KERN_INFO "MT7927 Deep Explorer - READ ONLY\n");
    printk(KERN_INFO "===========================================\n\n");
    
    // Explore the NEW region at 0x0C0000!
    printk(KERN_INFO "=== NEW DISCOVERY: Region at 0x0C0000 ===\n");
    printk(KERN_INFO "This appears to be the firmware region!\n\n");
    
    for (i = 0; i < 0x200; i += 0x10) {
        u32 v1 = ioread32(bar0 + 0x0C0000 + i);
        u32 v2 = ioread32(bar0 + 0x0C0000 + i + 4);
        u32 v3 = ioread32(bar0 + 0x0C0000 + i + 8);
        u32 v4 = ioread32(bar0 + 0x0C0000 + i + 12);
        
        if (v1 || v2 || v3 || v4) {
            printk(KERN_INFO "[0x%06x]: %08x %08x %08x %08x\n",
                   0x0C0000 + i, v1, v2, v3, v4);
        }
    }
    
    // Decode the first configuration patterns
    printk(KERN_INFO "\n=== Decoding Configuration Patterns ===\n");
    printk(KERN_INFO "Pattern format appears to be: 0x16CCRRRVV\n");
    printk(KERN_INFO "  CC = Command, RR = Register, VV = Value\n\n");
    
    for (i = 0; i < 0x40; i += 4) {
        val = ioread32(bar0 + 0x080000 + i);
        decode_config_pattern(val, 0x080000 + i);
    }
    
    // Check what the address references are pointing to
    printk(KERN_INFO "\n=== Following Address Chain ===\n");
    
    // We saw addresses like 0x80020704 pointing to 0x020000 region
    printk(KERN_INFO "Checking if 0x020000 region needs enabling...\n");
    
    // Try reading with different offsets
    val = ioread32(bar0 + 0x020000);
    printk(KERN_INFO "BAR0[0x020000]: 0x%08x\n", val);
    
    val = ioread32(bar0 + 0x020700);
    printk(KERN_INFO "BAR0[0x020700]: 0x%08x\n", val);
    
    val = ioread32(bar0 + 0x02e000);
    printk(KERN_INFO "BAR0[0x02e000]: 0x%08x\n", val);
    
    // Check regions between our known active areas
    printk(KERN_INFO "\n=== Scanning Between Active Regions ===\n");
    
    u32 scan_points[] = {
        0x0A0000, 0x0B0000, 0x0C0000, 0x0D0000, 0x0E0000, 0x0F0000,
        0x100000, 0x110000, 0x120000, 0x130000, 0x140000, 0x150000,
        0x160000, 0x170000
    };
    
    for (i = 0; i < ARRAY_SIZE(scan_points); i++) {
        val = ioread32(bar0 + scan_points[i]);
        if (val != 0x00000000 && val != 0xffffffff) {
            printk(KERN_INFO "BAR0[0x%06x]: 0x%08x - ACTIVE!\n", 
                   scan_points[i], val);
            
            // Show a bit more
            val2 = ioread32(bar0 + scan_points[i] + 4);
            if (val2) printk(KERN_INFO "  +0x04: 0x%08x\n", val2);
        }
    }
    
    // Analyze the status region values
    printk(KERN_INFO "\n=== Status Region Analysis ===\n");
    
    val = ioread32(bar0 + 0x180000);
    printk(KERN_INFO "Status value: 0x%08x (decimal: %d)\n", val, val);
    if (val == 0x72) {
        printk(KERN_INFO "  -> Could be version 7.2 or status code 114\n");
    }
    
    val = ioread32(bar0 + 0x180040 + 4);
    printk(KERN_INFO "Date/Version code: 0x%08x\n", val);
    if (val == 0x00020638) {
        printk(KERN_INFO "  -> Possible date: 02-06-38 or version 2.6.38\n");
    }
    
    // Check for patterns in the random data
    printk(KERN_INFO "\n=== Checking Firmware/Crypto Data ===\n");
    printk(KERN_INFO "Data from 0x0807b0 appears to be firmware or keys:\n");
    
    for (i = 0; i < 0x20; i += 4) {
        val = ioread32(bar0 + 0x0807b0 + i);
        if (i < 0x10) {
            printk(KERN_INFO "  [0x%06x]: 0x%08x\n", 0x0807b0 + i, val);
        }
    }
    
    // Try to understand the DMA configuration
    printk(KERN_INFO "\n=== DMA Configuration ===\n");
    val = ioread32(bar2 + 0x0204);
    printk(KERN_INFO "DMA_ENABLE: 0x%02x = binary ", val);
    for (i = 7; i >= 0; i--) {
        printk(KERN_CONT "%d", (val >> i) & 1);
    }
    printk(KERN_CONT "\n");
    printk(KERN_INFO "Enabled channels: ");
    for (i = 0; i < 8; i++) {
        if (val & (1 << i)) printk(KERN_CONT "%d ", i);
    }
    printk(KERN_CONT "\n");
    
    // Final summary
    printk(KERN_INFO "\n=== CRITICAL FINDINGS ===\n");
    printk(KERN_INFO "1. Firmware region at 0x0C0000 with data!\n");
    printk(KERN_INFO "2. Configuration commands at 0x080000\n");
    printk(KERN_INFO "3. Address references to 0x020000 region (currently empty)\n");
    printk(KERN_INFO "4. Status/version info at 0x180000\n");
    printk(KERN_INFO "5. DMA channels 0,2,4,5,6,7 are enabled\n");
    printk(KERN_INFO "6. Main memory at 0x000000 awaiting activation\n");
    
cleanup:
    pci_iounmap(pdev, bar2);
err_unmap_bar0:
    pci_iounmap(pdev, bar0);
err_release:
    pci_release_regions(pdev);
err_disable:
    pci_disable_device(pdev);
    return -ENODEV;
}

static void mt7927_remove(struct pci_dev *pdev) {}

static struct pci_device_id mt7927_ids[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { 0, }
};

static struct pci_driver mt7927_driver = {
    .name = "mt7927_deep",
    .id_table = mt7927_ids,
    .probe = mt7927_probe,
    .remove = mt7927_remove,
};

module_pci_driver(mt7927_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Deep Explorer");
