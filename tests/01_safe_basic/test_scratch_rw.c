/*
 * Test: Scratch Register Read/Write
 * Category: 01_safe_basic
 * Purpose: Test known-safe writable scratch registers
 * Expected: BAR2[0x0020] and BAR2[0x0024] are writable
 * Risk: None - Only writes to safe scratch registers
 * Duration: ~1 second
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927

/* Known safe scratch registers from our discoveries */
#define SCRATCH_REG1 0x0020
#define SCRATCH_REG2 0x0024

/* Test patterns */
static const u32 test_patterns[] = {
    0x00000000,  /* All zeros */
    0xFFFFFFFF,  /* All ones */
    0x5A5A5A5A,  /* Alternating 01011010 */
    0xA5A5A5A5,  /* Alternating 10100101 */
    0x12345678,  /* Sequential */
    0xDEADBEEF,  /* Classic test pattern */
    0xCAFEBABE,  /* Another classic */
    0x00FF00FF,  /* Byte alternating */
};

static int test_scratch_register(void __iomem *bar2, u32 offset, const char *name)
{
    u32 original, readback;
    int i;
    int passed = 1;
    
    /* Save original value */
    original = ioread32(bar2 + offset);
    printk(KERN_INFO "\nTesting %s (0x%04x):\n", name, offset);
    printk(KERN_INFO "  Original value: 0x%08x\n", original);
    
    /* Test each pattern */
    for (i = 0; i < ARRAY_SIZE(test_patterns); i++) {
        iowrite32(test_patterns[i], bar2 + offset);
        wmb(); /* Ensure write completes */
        
        readback = ioread32(bar2 + offset);
        
        if (readback == test_patterns[i]) {
            printk(KERN_INFO "  Pattern 0x%08x: ✓ PASS\n", test_patterns[i]);
        } else {
            printk(KERN_ERR "  Pattern 0x%08x: ✗ FAIL (got 0x%08x)\n",
                   test_patterns[i], readback);
            passed = 0;
        }
    }
    
    /* Restore original value */
    iowrite32(original, bar2 + offset);
    wmb();
    
    readback = ioread32(bar2 + offset);
    if (readback == original) {
        printk(KERN_INFO "  Restored to: 0x%08x ✓\n", original);
    } else {
        printk(KERN_INFO "  Restore failed: 0x%08x (expected 0x%08x)\n",
               readback, original);
    }
    
    return passed;
}

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar2 = NULL;
    u32 val;
    int ret;
    int test_passed = 1;
    
    printk(KERN_INFO "\n=== MT7927 TEST: Scratch Register R/W ===\n");
    printk(KERN_INFO "Category: 01_safe_basic\n");
    printk(KERN_INFO "Risk: None (safe registers only)\n\n");
    
    ret = pci_enable_device(pdev);
    if (ret) {
        printk(KERN_ERR "FAIL: Cannot enable device\n");
        return ret;
    }
    
    pci_set_master(pdev);
    
    ret = pci_request_regions(pdev, "test_scratch_rw");
    if (ret) {
        printk(KERN_ERR "FAIL: Cannot request regions\n");
        goto out_disable;
    }
    
    bar2 = pci_iomap(pdev, 2, 0);
    if (!bar2) {
        printk(KERN_ERR "FAIL: Cannot map BAR2\n");
        goto out_release;
    }
    
    /* Check chip state first */
    val = ioread32(bar2);
    if (val == 0xffffffff) {
        printk(KERN_ERR "Chip in error state! Cannot proceed.\n");
        test_passed = 0;
        goto out_unmap;
    }
    printk(KERN_INFO "Chip state OK (status: 0x%08x)\n", val);
    
    /* Test Scratch Register 1 */
    if (!test_scratch_register(bar2, SCRATCH_REG1, "Scratch Register 1")) {
        test_passed = 0;
    }
    
    /* Test Scratch Register 2 */
    if (!test_scratch_register(bar2, SCRATCH_REG2, "Scratch Register 2")) {
        test_passed = 0;
    }
    
    /* Cross-check: Write different values to both */
    printk(KERN_INFO "\nCross-check test:\n");
    iowrite32(0x11111111, bar2 + SCRATCH_REG1);
    iowrite32(0x22222222, bar2 + SCRATCH_REG2);
    wmb();
    
    val = ioread32(bar2 + SCRATCH_REG1);
    if (val == 0x11111111) {
        printk(KERN_INFO "  REG1 independence: ✓ PASS\n");
    } else {
        printk(KERN_ERR "  REG1 independence: ✗ FAIL\n");
        test_passed = 0;
    }
    
    val = ioread32(bar2 + SCRATCH_REG2);
    if (val == 0x22222222) {
        printk(KERN_INFO "  REG2 independence: ✓ PASS\n");
    } else {
        printk(KERN_ERR "  REG2 independence: ✗ FAIL\n");
        test_passed = 0;
    }
    
    /* Clear both registers */
    iowrite32(0x00000000, bar2 + SCRATCH_REG1);
    iowrite32(0x00000000, bar2 + SCRATCH_REG2);
    wmb();
    
out_unmap:
    pci_iounmap(pdev, bar2);
out_release:
    pci_release_regions(pdev);
out_disable:
    pci_disable_device(pdev);
    
    /* Final result */
    if (test_passed) {
        printk(KERN_INFO "\n✓ TEST PASSED: Scratch registers are fully functional\n");
    } else {
        printk(KERN_ERR "\n✗ TEST FAILED: Scratch register issues detected\n");
    }
    
    return -ENODEV;
}

static void test_remove(struct pci_dev *pdev) {}

static struct pci_device_id test_ids[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { 0, }
};

static struct pci_driver test_driver = {
    .name = "test_scratch_rw",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Test: Scratch Register R/W");
MODULE_AUTHOR("MT7927 Linux Driver Project");
