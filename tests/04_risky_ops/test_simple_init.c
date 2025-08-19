/*
 * Test: Simple initialization attempts
 * Purpose: Try basic initialization without risky PCIe operations
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar0, *bar2;
    u32 val, orig_ctrl;
    int ret, i;
    
    printk(KERN_INFO "\n=== MT7927 Simple Init Test ===\n");
    
    ret = pci_enable_device(pdev);
    if (ret) return ret;
    
    pci_set_master(pdev);
    
    ret = pci_request_regions(pdev, "test_simple");
    if (ret) {
        pci_disable_device(pdev);
        return ret;
    }
    
    bar0 = pci_iomap(pdev, 0, 0);
    bar2 = pci_iomap(pdev, 2, 0);
    
    if (!bar0 || !bar2) {
        if (bar0) pci_iounmap(pdev, bar0);
        if (bar2) pci_iounmap(pdev, bar2);
        pci_release_regions(pdev);
        pci_disable_device(pdev);
        return -ENOMEM;
    }
    
    printk(KERN_INFO "Initial: Memory=0x%08x, FW=0x%08x\n", 
           ioread32(bar0), ioread32(bar2 + 0x0200));
    
    /* Theory: Maybe we need to clear the firmware stub first */
    printk(KERN_INFO "\nClearing firmware stub header...\n");
    iowrite32(0x00000000, bar0 + 0x0C0000);
    wmb();
    msleep(100);
    
    val = ioread32(bar0);
    if (val != 0x00000000) {
        printk(KERN_INFO "✅ Memory activated after FW clear: 0x%08x\n", val);
        goto cleanup;
    }
    
    /* Theory: Toggle control register bit 15 (possible init bit) */
    printk(KERN_INFO "Toggling control bit 15...\n");
    orig_ctrl = ioread32(bar2 + 0x00d4);
    iowrite32(orig_ctrl ^ 0x00008000, bar2 + 0x00d4);
    wmb();
    msleep(100);
    iowrite32(orig_ctrl, bar2 + 0x00d4);
    wmb();
    msleep(100);
    
    val = ioread32(bar0);
    if (val != 0x00000000) {
        printk(KERN_INFO "✅ Memory activated: 0x%08x\n", val);
        goto cleanup;
    }
    
    /* Theory: Write sequence to scratch registers */
    printk(KERN_INFO "Magic sequence to scratch...\n");
    for (i = 0; i < 4; i++) {
        iowrite32(i, bar2 + 0x0020);
        iowrite32(~i, bar2 + 0x0024);
        wmb();
        msleep(10);
    }
    
    val = ioread32(bar0);
    if (val != 0x00000000) {
        printk(KERN_INFO "✅ Memory activated: 0x%08x\n", val);
    } else {
        printk(KERN_INFO "Memory still inactive\n");
    }
    
cleanup:
    printk(KERN_INFO "Final: Memory=0x%08x, FW=0x%08x\n",
           ioread32(bar0), ioread32(bar2 + 0x0200));
    
    pci_iounmap(pdev, bar2);
    pci_iounmap(pdev, bar0);
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    
    return -ENODEV;
}

static void test_remove(struct pci_dev *pdev) {}

static struct pci_device_id test_ids[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { 0, }
};

static struct pci_driver test_driver = {
    .name = "test_simple_init",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Simple Init Test");
