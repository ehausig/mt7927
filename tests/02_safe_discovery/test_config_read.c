/*
 * Test: Configuration Data Read
 * Category: 02_safe_discovery
 * Purpose: Read and verify configuration data at 0x080000
 * Expected: Find 0x16XXYYZZ command patterns and 0x31000100 delimiters
 * Risk: None - Read-only access to BAR0
 * Duration: ~2 seconds
 */

#include <linux/module.h>
#include <linux/pci.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927
#define CONFIG_OFFSET    0x080000

static void decode_config_cmd(u32 val, int offset)
{
    if ((val & 0xFF000000) == 0x16000000) {
        u8 cmd = (val >> 16) & 0xFF;
        u8 reg = (val >> 8) & 0xFF;
        u8 data = val & 0xFF;
        printk(KERN_INFO "  [0x%06x]: 0x%08x - CMD:0x%02x REG:0x%02x VAL:0x%02x\n",
               CONFIG_OFFSET + offset, val, cmd, reg, data);
    } else if (val == 0x31000100) {
        printk(KERN_INFO "  [0x%06x]: 0x%08x - DELIMITER\n",
               CONFIG_OFFSET + offset, val);
    }
}

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar0 = NULL;
    u32 val;
    int ret, i;
    int cmd_count = 0, delim_count = 0;
    int test_passed = 1;
    
    printk(KERN_INFO "\n=== MT7927 TEST: Configuration Data Read ===\n");
    printk(KERN_INFO "Category: 02_safe_discovery\n");
    printk(KERN_INFO "Risk: None (read-only)\n\n");
    
    ret = pci_enable_device(pdev);
    if (ret) {
        printk(KERN_ERR "FAIL: Cannot enable device\n");
        return ret;
    }
    
    pci_set_master(pdev);
    
    ret = pci_request_regions(pdev, "test_config_read");
    if (ret) {
        printk(KERN_ERR "FAIL: Cannot request regions\n");
        goto out_disable;
    }
    
    bar0 = pci_iomap(pdev, 0, 0);
    if (!bar0) {
        printk(KERN_ERR "FAIL: Cannot map BAR0\n");
        goto out_release;
    }
    
    /* Check if config region is accessible */
    val = ioread32(bar0 + CONFIG_OFFSET);
    if (val == 0xffffffff || val == 0x00000000) {
        printk(KERN_ERR "Config region not accessible: 0x%08x\n", val);
        test_passed = 0;
        goto out_unmap;
    }
    
    printk(KERN_INFO "Configuration Data at 0x%06x:\n", CONFIG_OFFSET);
    printk(KERN_INFO "First value: 0x%08x\n\n", val);
    
    /* Expected first command based on discoveries */
    if (val == 0x16006004) {
        printk(KERN_INFO "✓ Found expected first command: 0x16006004\n");
    } else {
        printk(KERN_INFO "⚠ Unexpected first value (expected 0x16006004)\n");
    }
    
    /* Read first 128 commands/words */
    printk(KERN_INFO "\nDecoding first 32 commands:\n");
    for (i = 0; i < 128; i += 4) {
        val = ioread32(bar0 + CONFIG_OFFSET + i);
        
        if ((val & 0xFF000000) == 0x16000000) {
            cmd_count++;
            if (i < 128) {  /* Only print first 32 */
                decode_config_cmd(val, i);
            }
        } else if (val == 0x31000100) {
            delim_count++;
            if (i < 128) {
                decode_config_cmd(val, i);
            }
        }
    }
    
    /* Scan more broadly for statistics */
    printk(KERN_INFO "\nScanning full configuration area (0x1000 bytes)...\n");
    for (i = 128; i < 0x1000; i += 4) {
        val = ioread32(bar0 + CONFIG_OFFSET + i);
        
        if ((val & 0xFF000000) == 0x16000000) {
            cmd_count++;
        } else if (val == 0x31000100) {
            delim_count++;
        }
    }
    
    /* Report findings */
    printk(KERN_INFO "\nConfiguration Analysis:\n");
    printk(KERN_INFO "  Total commands found: %d\n", cmd_count);
    printk(KERN_INFO "  Total delimiters found: %d\n", delim_count);
    
    /* Verify expected patterns */
    if (cmd_count > 50) {  /* We expect many commands */
        printk(KERN_INFO "  ✓ Command count looks correct\n");
    } else {
        printk(KERN_INFO "  ⚠ Fewer commands than expected\n");
        test_passed = 0;
    }
    
    if (delim_count > 5) {  /* We expect several delimiters */
        printk(KERN_INFO "  ✓ Delimiter count looks correct\n");
    } else {
        printk(KERN_INFO "  ⚠ Fewer delimiters than expected\n");
        test_passed = 0;
    }
    
    /* Check for address references */
    printk(KERN_INFO "\nChecking for address references:\n");
    int addr_count = 0;
    for (i = 0x1e0; i < 0x300; i += 4) {  /* Known area with addresses */
        val = ioread32(bar0 + CONFIG_OFFSET + i);
        if ((val & 0xFF000000) == 0x80000000 || 
            (val & 0xFF000000) == 0x82000000) {
            if (addr_count < 5) {  /* Print first few */
                printk(KERN_INFO "  [0x%06x]: 0x%08x - Address reference\n",
                       CONFIG_OFFSET + i, val);
            }
            addr_count++;
        }
    }
    printk(KERN_INFO "  Total address references found: %d\n", addr_count);
    
out_unmap:
    pci_iounmap(pdev, bar0);
out_release:
    pci_release_regions(pdev);
out_disable:
    pci_disable_device(pdev);
    
    /* Final result */
    if (test_passed) {
        printk(KERN_INFO "\n✓ TEST PASSED: Configuration data verified\n");
    } else {
        printk(KERN_ERR "\n✗ TEST FAILED: Configuration data issues\n");
    }
    
    return -ENODEV;
}

static void test_remove(struct pci_dev *pdev) {}

static struct pci_device_id test_ids[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { 0, }
};

static struct pci_driver test_driver = {
    .name = "test_config_read",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Test: Configuration Data Read");
MODULE_AUTHOR("MT7927 Linux Driver Project");
