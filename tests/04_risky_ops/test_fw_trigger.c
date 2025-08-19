/*
 * Test: Trigger firmware loading sequence
 * Purpose: Use MT7925 patterns to trigger firmware initialization
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927

/* From MT7925 driver analysis */
#define MT_CONN_ON_MISC  0x7c0600f0  /* This is an absolute address */
#define MT_TOP_MISC2_FW_N9_RDY  0x3  /* GENMASK(1, 0) = bits 0 and 1 */

/* MCU commands */
#define MCU_CMD_FW_START_REQ  0x02
#define MCU_CMD_PATCH_START_REQ  0x05

/* WPDMA registers (based on patterns) */
#define MT_WPDMA_GLO_CFG  0x0208
#define MT_WPDMA_RST_IDX  0x020C

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar0, *bar2;
    u32 val, fw_status;
    int ret, i;
    
    printk(KERN_INFO "\n=== MT7927 Firmware Trigger Test ===\n");
    
    ret = pci_enable_device(pdev);
    if (ret) return ret;
    
    pci_set_master(pdev);
    
    ret = pci_request_regions(pdev, "test_fw_trigger");
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
    
    printk(KERN_INFO "Initial state:\n");
    printk(KERN_INFO "  Memory: 0x%08x\n", ioread32(bar0));
    printk(KERN_INFO "  FW_STATUS: 0x%08x\n", ioread32(bar2 + 0x0200));
    
    /* Step 1: Reset WPDMA */
    printk(KERN_INFO "\nResetting WPDMA...\n");
    iowrite32(0x00000001, bar2 + MT_WPDMA_RST_IDX);
    wmb();
    msleep(10);
    iowrite32(0x00000000, bar2 + MT_WPDMA_RST_IDX);
    wmb();
    msleep(10);
    
    /* Step 2: Enable WPDMA */
    printk(KERN_INFO "Enabling WPDMA...\n");
    iowrite32(0x00000001, bar2 + MT_WPDMA_GLO_CFG);
    wmb();
    msleep(10);
    
    /* Step 3: Send firmware start command via scratch registers */
    printk(KERN_INFO "Sending FW_START command...\n");
    iowrite32(MCU_CMD_FW_START_REQ, bar2 + 0x0020);
    iowrite32(0x00000001, bar2 + 0x0024);  /* Trigger */
    wmb();
    msleep(100);
    
    /* Step 4: Clear firmware status to signal ready */
    printk(KERN_INFO "Clearing FW_STATUS...\n");
    iowrite32(0x00000000, bar2 + 0x0200);
    wmb();
    msleep(100);
    
    /* Step 5: Check if firmware responded */
    fw_status = ioread32(bar2 + 0x0200);
    printk(KERN_INFO "FW_STATUS after trigger: 0x%08x\n", fw_status);
    
    /* Step 6: Poll for N9 ready (this might be at a different location) */
    printk(KERN_INFO "Checking for N9 ready...\n");
    
    /* Try reading at various potential locations */
    for (i = 0; i < 10; i++) {
        /* Check main memory */
        val = ioread32(bar0);
        if (val != 0x00000000) {
            printk(KERN_INFO "✅ MEMORY ACTIVATED! 0x%08x\n", val);
            goto success;
        }
        
        /* Check firmware status */
        val = ioread32(bar2 + 0x0200);
        if (val != 0xffff10f1 && val != 0x00000000) {
            printk(KERN_INFO "✅ FW_STATUS changed! 0x%08x\n", val);
        }
        
        msleep(100);
    }
    
    /* Step 7: Try writing to firmware region to trigger load */
    printk(KERN_INFO "\nWriting FW load trigger to firmware region...\n");
    iowrite32(MCU_CMD_FW_START_REQ, bar0 + 0x0C0010);
    wmb();
    msleep(100);
    
    val = ioread32(bar0);
    if (val != 0x00000000) {
        printk(KERN_INFO "✅ MEMORY ACTIVATED! 0x%08x\n", val);
    } else {
        printk(KERN_INFO "Memory still inactive\n");
    }
    
success:
    printk(KERN_INFO "\nFinal state:\n");
    printk(KERN_INFO "  Memory: 0x%08x\n", ioread32(bar0));
    printk(KERN_INFO "  FW_STATUS: 0x%08x\n", ioread32(bar2 + 0x0200));
    printk(KERN_INFO "  DMA_ENABLE: 0x%08x\n", ioread32(bar2 + 0x0204));
    
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
    .name = "test_fw_trigger",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Firmware Trigger Test");
