/*
 * Test: Chip Identification
 * Category: 01_safe_basic
 * Purpose: Verify chip ID through multiple methods
 * Expected: Chip ID 0x792714c3, BAR2[0x0098] = 0x792714c3
 * Risk: None - Read-only access
 * Duration: ~1 second
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kern_levels.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927
#define EXPECTED_CHIP_ID 0x792714c3

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar2 = NULL;
    u32 val;
    u16 vendor, device;
    int ret;
    int test_passed = 1;
    
    printk(KERN_INFO "\n=== MT7927 TEST: Chip Identification ===\n");
    printk(KERN_INFO "Category: 01_safe_basic\n");
    printk(KERN_INFO "Risk: None\n\n");
    
    ret = pci_enable_device(pdev);
    if (ret) {
        printk(KERN_ERR "FAIL: Cannot enable device\n");
        return ret;
    }
    
    /* Method 1: PCI Config Space */
    printk(KERN_INFO "Method 1: PCI Config Space\n");
    pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor);
    pci_read_config_word(pdev, PCI_DEVICE_ID, &device);
    val = (device << 16) | vendor;
    
    if (val == EXPECTED_CHIP_ID) {
        printk(KERN_INFO "  Config space ID: 0x%08x ✓ PASS\n", val);
    } else {
        printk(KERN_ERR "  Config space ID: 0x%08x ✗ FAIL\n", val);
        test_passed = 0;
    }
    
    /* Method 2: Direct PCI Config Read at offset 0 */
    printk(KERN_INFO "\nMethod 2: Direct Config Read\n");
    pci_read_config_dword(pdev, 0x00, &val);
    
    if (val == EXPECTED_CHIP_ID) {
        printk(KERN_INFO "  Direct read ID: 0x%08x ✓ PASS\n", val);
    } else {
        printk(KERN_ERR "  Direct read ID: 0x%08x ✗ FAIL\n", val);
        test_passed = 0;
    }
    
    /* Method 3: BAR2 Chip ID Register at 0x0098 */
    printk(KERN_INFO "\nMethod 3: BAR2 Register 0x0098\n");
    
    ret = pci_request_regions(pdev, "test_chip_id");
    if (ret) {
        printk(KERN_INFO "  Cannot request regions (non-fatal)\n");
    } else {
        bar2 = pci_iomap(pdev, 2, 0);
        if (!bar2) {
            printk(KERN_INFO "  Cannot map BAR2 (non-fatal)\n");
        } else {
            /* Check chip state first */
            val = ioread32(bar2);
            if (val == 0xffffffff) {
                printk(KERN_ERR "  Chip in error state!\n");
                test_passed = 0;
            } else {
                /* Read chip ID from BAR2[0x0098] */
                val = ioread32(bar2 + 0x0098);
                if (val == EXPECTED_CHIP_ID) {
                    printk(KERN_INFO "  BAR2[0x0098]: 0x%08x ✓ PASS\n", val);
                } else {
                    printk(KERN_ERR "  BAR2[0x0098]: 0x%08x ✗ FAIL\n", val);
                    test_passed = 0;
                }
                
                /* Bonus: Check PCI config mirror at BAR2[0x1000] */
                val = ioread32(bar2 + 0x1000);
                printk(KERN_INFO "  BAR2[0x1000] (PCI mirror): 0x%08x %s\n", val,
                       val == EXPECTED_CHIP_ID ? "✓" : "✗");
            }
            
            pci_iounmap(pdev, bar2);
        }
        pci_release_regions(pdev);
    }
    
    /* Summary */
    printk(KERN_INFO "\nChip Information:\n");
    printk(KERN_INFO "  Vendor: MediaTek (0x%04x)\n", MT7927_VENDOR_ID);
    printk(KERN_INFO "  Device: MT7927 (0x%04x)\n", MT7927_DEVICE_ID);
    printk(KERN_INFO "  Full ID: 0x%08x\n", EXPECTED_CHIP_ID);
    
    pci_disable_device(pdev);
    
    /* Final result */
    if (test_passed) {
        printk(KERN_INFO "\n✓ TEST PASSED: Chip correctly identified as MT7927\n");
    } else {
        printk(KERN_ERR "\n✗ TEST FAILED: Chip identification mismatch\n");
    }
    
    return -ENODEV;
}

static void test_remove(struct pci_dev *pdev) {}

static struct pci_device_id test_ids[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { 0, }
};

static struct pci_driver test_driver = {
    .name = "test_chip_id",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Test: Chip Identification");
MODULE_AUTHOR("MT7927 Linux Driver Project");
