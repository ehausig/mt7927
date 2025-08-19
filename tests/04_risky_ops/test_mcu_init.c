/*
 * Test: MCU Initialization and Firmware Load Trigger
 * Purpose: Initialize MCU and trigger firmware load sequence
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927

/* Based on MT7925 patterns */
#define MT_MCU_CMD       0x2000
#define MT_MCU_RESP      0x2004
#define MT_FW_STATUS     0x0200
#define MT_DMA_ENABLE    0x0204

/* MCU commands (from MT7925) */
#define MCU_CMD_INIT     0x00000001
#define MCU_CMD_FW_START 0x00000002
#define MCU_CMD_DMA_INIT 0x00000004

static int init_mcu(void __iomem *bar2)
{
    u32 val;
    int i;
    
    printk(KERN_INFO "Initializing MCU...\n");
    
    /* Step 1: Send MCU init command */
    iowrite32(MCU_CMD_INIT, bar2 + MT_MCU_CMD);
    wmb();
    msleep(10);
    
    /* Step 2: Enable all DMA channels */
    iowrite32(0xFF, bar2 + MT_DMA_ENABLE);
    wmb();
    msleep(10);
    
    /* Step 3: Clear firmware status to signal driver ready */
    iowrite32(0x00000000, bar2 + MT_FW_STATUS);
    wmb();
    msleep(10);
    
    /* Step 4: Send firmware start command */
    iowrite32(MCU_CMD_FW_START, bar2 + MT_MCU_CMD);
    wmb();
    msleep(10);
    
    /* Step 5: Write magic pattern to trigger init */
    iowrite32(0x12345678, bar2 + 0x0020);  /* Scratch1 */
    iowrite32(0x87654321, bar2 + 0x0024);  /* Scratch2 */
    wmb();
    msleep(10);
    
    /* Check for MCU response */
    for (i = 0; i < 10; i++) {
        val = ioread32(bar2 + MT_FW_STATUS);
        printk(KERN_INFO "  FW_STATUS: 0x%08x\n", val);
        
        if (val != 0xffff10f1 && val != 0x00000000) {
            printk(KERN_INFO "✓ MCU responded! Status: 0x%08x\n", val);
            return 1;
        }
        msleep(100);
    }
    
    return 0;
}

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar0, *bar2;
    u32 val, main_mem, dma_mem;
    int ret;
    
    ret = pci_enable_device(pdev);
    if (ret) return ret;
    
    pci_set_master(pdev);
    
    ret = pci_request_regions(pdev, "test_mcu_init");
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
    
    printk(KERN_INFO "\n=== MT7927 MCU Initialization Test ===\n");
    
    /* Check initial state */
    val = ioread32(bar2);
    if (val == 0xffffffff) {
        printk(KERN_ERR "Chip in error state!\n");
        goto cleanup;
    }
    
    printk(KERN_INFO "Initial state:\n");
    printk(KERN_INFO "  Chip: 0x%08x\n", val);
    printk(KERN_INFO "  FW_STATUS: 0x%08x\n", ioread32(bar2 + MT_FW_STATUS));
    printk(KERN_INFO "  DMA_ENABLE: 0x%08x\n", ioread32(bar2 + MT_DMA_ENABLE));
    printk(KERN_INFO "  Memory: 0x%08x\n", ioread32(bar0));
    
    /* Try MCU initialization */
    if (init_mcu(bar2)) {
        /* Check if memory activated */
        main_mem = ioread32(bar0);
        dma_mem = ioread32(bar0 + 0x020000);
        
        if (main_mem != 0x00000000) {
            printk(KERN_INFO "\n✅✅✅ BREAKTHROUGH! Memory activated!\n");
            printk(KERN_INFO "  BAR0[0x000000]: 0x%08x\n", main_mem);
            printk(KERN_INFO "  BAR0[0x020000]: 0x%08x\n", dma_mem);
        }
    }
    
    /* Final state */
    printk(KERN_INFO "\nFinal state:\n");
    printk(KERN_INFO "  FW_STATUS: 0x%08x\n", ioread32(bar2 + MT_FW_STATUS));
    printk(KERN_INFO "  DMA_ENABLE: 0x%08x\n", ioread32(bar2 + MT_DMA_ENABLE));
    printk(KERN_INFO "  Memory: 0x%08x\n", ioread32(bar0));
    
    /* Try to trigger firmware load by writing to firmware region */
    printk(KERN_INFO "\nTrying firmware trigger write...\n");
    iowrite32(0x00000001, bar0 + 0x0C0004);  /* Write to FW region */
    wmb();
    msleep(100);
    
    val = ioread32(bar0);
    if (val != 0x00000000) {
        printk(KERN_INFO "✅ Memory changed after FW write: 0x%08x\n", val);
    }
    
cleanup:
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
    .name = "test_mcu_init",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 MCU Initialization Test");
