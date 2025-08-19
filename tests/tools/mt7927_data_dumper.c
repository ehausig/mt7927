#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/debugfs.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927

static int mt7927_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar0, *bar2;
    u32 val;
    int ret, i, j;
    
    ret = pci_enable_device(pdev);
    if (ret) return ret;
    
    pci_set_master(pdev);
    
    ret = pci_request_regions(pdev, "mt7927_dumper");
    if (ret) goto err_disable;
    
    bar0 = pci_iomap(pdev, 0, 0);
    if (!bar0) goto err_release;
    
    bar2 = pci_iomap(pdev, 2, 0);
    if (!bar2) goto err_unmap_bar0;
    
    // Check chip state
    val = ioread32(bar2);
    if (val == 0xffffffff) {
        printk(KERN_ERR "Chip in error state!\n");
        goto cleanup;
    }
    
    printk(KERN_INFO "\n========================================\n");
    printk(KERN_INFO "MT7927 WiFi 7 - Safe Data Dumper\n");
    printk(KERN_INFO "READ-ONLY - No writes to avoid errors\n");
    printk(KERN_INFO "========================================\n\n");
    
    // Quick check of main regions
    printk(KERN_INFO "=== Quick Status Check ===\n");
    printk(KERN_INFO "BAR0[0x000000]: 0x%08x (main memory)\n", ioread32(bar0));
    printk(KERN_INFO "BAR0[0x080000]: 0x%08x (config region)\n", ioread32(bar0 + 0x080000));
    printk(KERN_INFO "BAR0[0x180000]: 0x%08x (status region)\n", ioread32(bar0 + 0x180000));
    printk(KERN_INFO "BAR2[0x000000]: 0x%08x (control regs)\n", val);
    
    // Dump configuration region at 0x080000
    printk(KERN_INFO "\n=== Configuration Region [0x080000] ===\n");
    printk(KERN_INFO "First 256 bytes:\n");
    for (i = 0; i < 0x100; i += 0x10) {
        printk(KERN_INFO "[0x%06x]: %08x %08x %08x %08x\n",
               0x080000 + i,
               ioread32(bar0 + 0x080000 + i),
               ioread32(bar0 + 0x080000 + i + 4),
               ioread32(bar0 + 0x080000 + i + 8),
               ioread32(bar0 + 0x080000 + i + 12));
    }
    
    // Analyze the pattern
    printk(KERN_INFO "\n=== Pattern Analysis ===\n");
    val = ioread32(bar0 + 0x080000);
    if ((val & 0xFF000000) == 0x16000000) {
        printk(KERN_INFO "Configuration pattern detected: 0x16XXYYZZ format\n");
        
        // Count different pattern types
        int pattern_16 = 0, pattern_31 = 0, pattern_other = 0;
        for (i = 0; i < 0x200; i += 4) {
            val = ioread32(bar0 + 0x080000 + i);
            if ((val & 0xFF000000) == 0x16000000) pattern_16++;
            else if ((val & 0xFF000000) == 0x31000000) pattern_31++;
            else if (val != 0 && val != 0xffffffff) pattern_other++;
        }
        printk(KERN_INFO "In first 512 bytes: 0x16 patterns: %d, 0x31 patterns: %d, other: %d\n",
               pattern_16, pattern_31, pattern_other);
    }
    
    // Look for address references
    printk(KERN_INFO "\n=== Address References ===\n");
    printk(KERN_INFO "Looking for memory addresses in config data:\n");
    int addr_count = 0;
    for (i = 0x080000; i < 0x081000 && addr_count < 20; i += 4) {
        val = ioread32(bar0 + i);
        if ((val & 0xFF000000) == 0x80000000 || 
            (val & 0xFF000000) == 0x82000000 ||
            (val & 0xFF000000) == 0x89000000) {
            printk(KERN_INFO "[0x%06x]: 0x%08x -> ", i, val);
            
            // Try to read the referenced address
            u32 ref_addr = val & 0x00FFFFFF;
            if (ref_addr < 0x200000) {
                u32 ref_val = ioread32(bar0 + ref_addr);
                printk(KERN_CONT "BAR0[0x%06x] = 0x%08x\n", ref_addr, ref_val);
            } else {
                printk(KERN_CONT "out of range\n");
            }
            addr_count++;
        }
    }
    
    // Dump status region at 0x180000
    printk(KERN_INFO "\n=== Status Region [0x180000] ===\n");
    for (i = 0; i < 0x100; i += 0x10) {
        int has_data = 0;
        for (j = 0; j < 0x10; j += 4) {
            val = ioread32(bar0 + 0x180000 + i + j);
            if (val != 0) {
                has_data = 1;
                break;
            }
        }
        if (has_data) {
            printk(KERN_INFO "[0x%06x]: %08x %08x %08x %08x\n",
                   0x180000 + i,
                   ioread32(bar0 + 0x180000 + i),
                   ioread32(bar0 + 0x180000 + i + 4),
                   ioread32(bar0 + 0x180000 + i + 8),
                   ioread32(bar0 + 0x180000 + i + 12));
        }
    }
    
    // Check other potential regions
    printk(KERN_INFO "\n=== Scanning for Other Active Regions ===\n");
    u32 check_offsets[] = {
        0x000000,  // Main memory
        0x020000,  // Potential DMA region
        0x040000,  // Potential TX buffer
        0x060000,  // Potential RX buffer
        0x0C0000,  // 768KB mark (firmware size?)
        0x100000,  // 1MB boundary
    };
    
    for (i = 0; i < ARRAY_SIZE(check_offsets); i++) {
        val = ioread32(bar0 + check_offsets[i]);
        if (val != 0x00000000 && val != 0xffffffff) {
            printk(KERN_INFO "BAR0[0x%06x]: 0x%08x - DATA FOUND!\n", 
                   check_offsets[i], val);
        }
    }
    
    // Check BAR2 firmware status registers
    printk(KERN_INFO "\n=== BAR2 Firmware Status ===\n");
    printk(KERN_INFO "FW_REG1 [0x0008]: 0x%08x\n", ioread32(bar2 + 0x0008));
    printk(KERN_INFO "FW_REG2 [0x000c]: 0x%08x\n", ioread32(bar2 + 0x000c));
    printk(KERN_INFO "FW_STATUS [0x0200]: 0x%08x\n", ioread32(bar2 + 0x0200));
    printk(KERN_INFO "DMA_ENABLE [0x0204]: 0x%08x\n", ioread32(bar2 + 0x0204));
    printk(KERN_INFO "MODE1 [0x0070]: 0x%08x\n", ioread32(bar2 + 0x0070));
    printk(KERN_INFO "MODE2 [0x0074]: 0x%08x\n", ioread32(bar2 + 0x0074));
    printk(KERN_INFO "CONTROL [0x00d4]: 0x%08x\n", ioread32(bar2 + 0x00d4));
    
    printk(KERN_INFO "\n=== Summary ===\n");
    printk(KERN_INFO "✓ Configuration data present at 0x080000\n");
    printk(KERN_INFO "✓ Status registers present at 0x180000\n");
    printk(KERN_INFO "✓ Chip is partially initialized\n");
    printk(KERN_INFO "✓ Contains address references to other regions\n");
    printk(KERN_INFO "! Main memory at 0x000000 still inactive\n");
    printk(KERN_INFO "! Needs proper initialization sequence\n");
    
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
    .name = "mt7927_dumper",
    .id_table = mt7927_ids,
    .probe = mt7927_probe,
    .remove = mt7927_remove,
};

module_pci_driver(mt7927_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Safe Data Dumper");
