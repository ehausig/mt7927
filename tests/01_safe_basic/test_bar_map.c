/*
 * Test: BAR Mapping Verification
 * Category: 01_safe_basic
 * Purpose: Verify BAR configuration matches expected values
 * Expected: BAR0=2MB at 0x80000000, BAR2=32KB at 0x80200000
 * Risk: None - Read-only PCI config access
 * Duration: ~1 second
 */

#include <linux/module.h>
#include <linux/pci.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927

/* Expected BAR configuration from our discoveries */
#define EXPECTED_BAR0_SIZE (2 * 1024 * 1024)    /* 2MB */
#define EXPECTED_BAR2_SIZE (32 * 1024)          /* 32KB */

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    int ret;
    int test_passed = 1;
    void __iomem *bar0 = NULL, *bar2 = NULL;
    u32 val;
    
    printk(KERN_INFO "\n=== MT7927 TEST: BAR Mapping ===\n");
    printk(KERN_INFO "Category: 01_safe_basic\n");
    printk(KERN_INFO "Risk: None\n\n");
    
    ret = pci_enable_device(pdev);
    if (ret) {
        printk(KERN_ERR "FAIL: Cannot enable device\n");
        return ret;
    }
    
    /* Check BAR0 */
    printk(KERN_INFO "BAR0 Verification:\n");
    if (pci_resource_len(pdev, 0) == EXPECTED_BAR0_SIZE) {
        printk(KERN_INFO "  Size: %d MB ✓ PASS\n", EXPECTED_BAR0_SIZE / (1024*1024));
    } else {
        printk(KERN_ERR "  Size: %llu bytes ✗ FAIL (expected %d)\n",
               (unsigned long long)pci_resource_len(pdev, 0), EXPECTED_BAR0_SIZE);
        test_passed = 0;
    }
    printk(KERN_INFO "  Address: 0x%08x\n", (u32)pci_resource_start(pdev, 0));
    
    /* Check BAR2 */
    printk(KERN_INFO "\nBAR2 Verification:\n");
    if (pci_resource_len(pdev, 2) == EXPECTED_BAR2_SIZE) {
        printk(KERN_INFO "  Size: %d KB ✓ PASS\n", EXPECTED_BAR2_SIZE / 1024);
    } else {
        printk(KERN_ERR "  Size: %llu bytes ✗ FAIL (expected %d)\n",
               (unsigned long long)pci_resource_len(pdev, 2), EXPECTED_BAR2_SIZE);
        test_passed = 0;
    }
    printk(KERN_INFO "  Address: 0x%08x\n", (u32)pci_resource_start(pdev, 2));
    
    /* Try to map BARs */
    printk(KERN_INFO "\nBAR Mapping Test:\n");
    
    ret = pci_request_regions(pdev, "test_bar_map");
    if (ret) {
        printk(KERN_ERR "  Request regions: ✗ FAIL\n");
        test_passed = 0;
        goto out;
    }
    printk(KERN_INFO "  Request regions: ✓ PASS\n");
    
    bar0 = pci_iomap(pdev, 0, 0);
    if (!bar0) {
        printk(KERN_ERR "  Map BAR0: ✗ FAIL\n");
        test_passed = 0;
    } else {
        printk(KERN_INFO "  Map BAR0: ✓ PASS\n");
        
        /* Quick sanity check - read from known locations */
        val = ioread32(bar0 + 0x080000);  /* Config region */
        if (val == 0x16006004) {
            printk(KERN_INFO "  BAR0[0x080000]: 0x%08x ✓ Config found\n", val);
        } else {
            printk(KERN_INFO "  BAR0[0x080000]: 0x%08x\n", val);
        }
    }
    
    bar2 = pci_iomap(pdev, 2, 0);
    if (!bar2) {
        printk(KERN_ERR "  Map BAR2: ✗ FAIL\n");
        test_passed = 0;
    } else {
        printk(KERN_INFO "  Map BAR2: ✓ PASS\n");
        
        /* Check status register */
        val = ioread32(bar2);
        if (val == 0x00511163) {
            printk(KERN_INFO "  BAR2[0x0000]: 0x%08x ✓ Status OK\n", val);
        } else if (val == 0xffffffff) {
            printk(KERN_ERR "  BAR2[0x0000]: 0x%08x ✗ Error state!\n", val);
            test_passed = 0;
        } else {
            printk(KERN_INFO "  BAR2[0x0000]: 0x%08x\n", val);
        }
    }
    
    /* Cleanup */
    if (bar2) pci_iounmap(pdev, bar2);
    if (bar0) pci_iounmap(pdev, bar0);
    pci_release_regions(pdev);
    
out:
    /* Final result */
    if (test_passed) {
        printk(KERN_INFO "\n✓ TEST PASSED: BAR mapping verified\n");
    } else {
        printk(KERN_ERR "\n✗ TEST FAILED: BAR mapping issues detected\n");
    }
    
    pci_disable_device(pdev);
    return -ENODEV;
}

static void test_remove(struct pci_dev *pdev) {}

static struct pci_device_id test_ids[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { 0, }
};

static struct pci_driver test_driver = {
    .name = "test_bar_map",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Test: BAR Mapping Verification");
MODULE_AUTHOR("MT7927 Linux Driver Project");
