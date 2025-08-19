/*
 * MT7927 Firmware Trigger Test
 * Purpose: Try to trigger firmware execution with proper WPDMA enable
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar0, *bar2;
    u32 val, orig_val;
    int ret, i;
    
    ret = pci_enable_device(pdev);
    if (ret) return ret;
    
    pci_set_master(pdev);
    ret = pci_request_regions(pdev, "test_trigger_fw");
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
    
    printk(KERN_INFO "MT7927: Firmware Trigger Test\n\n");
    
    /* Check initial state */
    printk(KERN_INFO "Initial state:\n");
    printk(KERN_INFO "  Memory: 0x%08x\n", ioread32(bar0));
    printk(KERN_INFO "  FW_STATUS: 0x%08x\n", ioread32(bar2 + 0x0200));
    printk(KERN_INFO "  DMA_ENABLE: 0x%08x\n", ioread32(bar2 + 0x0204));
    printk(KERN_INFO "  WPDMA_GLO_CFG: 0x%08x\n", ioread32(bar2 + 0x0208));
    
    /* Step 1: Enable WPDMA (it's currently 0!) */
    printk(KERN_INFO "\nStep 1: Enabling WPDMA...\n");
    iowrite32(0x00000001, bar2 + 0x0208);  /* Enable WPDMA */
    wmb();
    msleep(10);
    val = ioread32(bar2 + 0x0208);
    printk(KERN_INFO "  WPDMA_GLO_CFG after enable: 0x%08x\n", val);
    
    /* Step 2: Enable all DMA channels */
    printk(KERN_INFO "\nStep 2: Enabling all DMA channels...\n");
    iowrite32(0xFF, bar2 + 0x0204);
    wmb();
    msleep(10);
    val = ioread32(bar2 + 0x0204);
    printk(KERN_INFO "  DMA_ENABLE after: 0x%08x\n", val);
    
    /* Step 3: Try different MCU trigger approaches */
    printk(KERN_INFO "\nStep 3: Trying MCU triggers...\n");
    
    /* Approach A: Write to MCU registers in sequence */
    iowrite32(0x00000000, bar2 + 0x0790);  /* Clear MCU_CMD */
    wmb();
    msleep(10);
    iowrite32(0x00000001, bar2 + 0x0790);  /* Set MCU_CMD */
    wmb();
    msleep(50);
    
    val = ioread32(bar0);
    if (val != 0x00000000) {
        printk(KERN_INFO "  ✅ MEMORY ACTIVATED after MCU trigger: 0x%08x\n", val);
        goto done;
    }
    
    /* Approach B: Try register 0x81 pattern from config */
    printk(KERN_INFO "\nStep 4: Applying config pattern for reg 0x81...\n");
    /* Config shows reg 0x81 gets OR'd with 0x02 multiple times */
    /* Maybe this maps to a different BAR2 register? */
    
    /* Try offset 0x0204 (DMA_ENABLE) + 0x81*4 = 0x0408 */
    orig_val = ioread32(bar2 + 0x0408);
    printk(KERN_INFO "  BAR2[0x0408]: 0x%08x\n", orig_val);
    iowrite32(orig_val | 0x02, bar2 + 0x0408);
    wmb();
    msleep(50);
    
    val = ioread32(bar0);
    if (val != 0x00000000) {
        printk(KERN_INFO "  ✅ MEMORY ACTIVATED after reg 0x81 pattern: 0x%08x\n", val);
        goto done;
    }
    
    /* Approach C: Write to firmware region to trigger */
    printk(KERN_INFO "\nStep 5: Writing to firmware region...\n");
    /* The firmware at 0x0C0000 has 0x0012cbd8 at offset 8 */
    /* Try writing to offset 0x0C to trigger execution */
    iowrite32(0x00000001, bar0 + 0x0C000C);
    wmb();
    msleep(50);
    
    val = ioread32(bar0);
    if (val != 0x00000000) {
        printk(KERN_INFO "  ✅ MEMORY ACTIVATED after FW write: 0x%08x\n", val);
        goto done;
    }
    
    /* Approach D: Toggle WPDMA reset */
    printk(KERN_INFO "\nStep 6: Toggling WPDMA reset...\n");
    iowrite32(0x00000001, bar2 + 0x020C);  /* Set reset */
    wmb();
    msleep(10);
    iowrite32(0x00000000, bar2 + 0x020C);  /* Clear reset */
    wmb();
    msleep(10);
    iowrite32(0x00000001, bar2 + 0x0208);  /* Re-enable WPDMA */
    wmb();
    msleep(50);
    
    val = ioread32(bar0);
    if (val != 0x00000000) {
        printk(KERN_INFO "  ✅ MEMORY ACTIVATED after WPDMA reset: 0x%08x\n", val);
        goto done;
    }
    
    /* Check FW_STATUS for any changes */
    val = ioread32(bar2 + 0x0200);
    if (val != 0xffff10f1) {
        printk(KERN_INFO "  FW_STATUS changed to: 0x%08x\n", val);
    }
    
done:
    printk(KERN_INFO "\nFinal state:\n");
    printk(KERN_INFO "  Memory: 0x%08x\n", ioread32(bar0));
    printk(KERN_INFO "  FW_STATUS: 0x%08x\n", ioread32(bar2 + 0x0200));
    printk(KERN_INFO "  DMA_ENABLE: 0x%08x\n", ioread32(bar2 + 0x0204));
    printk(KERN_INFO "  WPDMA_GLO_CFG: 0x%08x\n", ioread32(bar2 + 0x0208));
    
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
    .name = "test_trigger_fw",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Firmware Trigger Test");
