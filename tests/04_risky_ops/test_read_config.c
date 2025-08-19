/*
 * MT7927 Configuration Reader Test
 * Purpose: Read configuration commands and try to understand initialization
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar0, *bar2;
    u32 val;
    int ret, i;
    
    ret = pci_enable_device(pdev);
    if (ret) return ret;
    
    pci_set_master(pdev);
    pci_request_regions(pdev, "test_read_config");
    
    bar0 = pci_iomap(pdev, 0, 0);
    bar2 = pci_iomap(pdev, 2, 0);
    
    if (!bar0 || !bar2) {
        if (bar0) pci_iounmap(pdev, bar0);
        if (bar2) pci_iounmap(pdev, bar2);
        pci_release_regions(pdev);
        pci_disable_device(pdev);
        return -ENOMEM;
    }
    
    printk(KERN_INFO "MT7927: Reading configuration to find clues\n\n");
    
    /* Check the config commands that target register 0x81 */
    printk(KERN_INFO "Configuration commands (first 20):\n");
    for (i = 0; i < 80; i += 4) {
        val = ioread32(bar0 + 0x080000 + i);
        if ((val & 0xFF000000) == 0x16000000) {
            u8 cmd = (val >> 16) & 0xFF;
            u8 reg = (val >> 8) & 0xFF;
            u8 data = val & 0xFF;
            printk(KERN_INFO "  [0x%06x]: cmd=0x%02x reg=0x%02x data=0x%02x\n",
                   0x080000 + i, cmd, reg, data);
        }
    }
    
    /* Check firmware region */
    printk(KERN_INFO "\nFirmware region at 0x0C0000:\n");
    for (i = 0; i < 0x40; i += 0x10) {
        printk(KERN_INFO "  [0x%06x]: %08x %08x %08x %08x\n",
               0x0C0000 + i,
               ioread32(bar0 + 0x0C0000 + i),
               ioread32(bar0 + 0x0C0000 + i + 4),
               ioread32(bar0 + 0x0C0000 + i + 8),
               ioread32(bar0 + 0x0C0000 + i + 12));
    }
    
    /* Check important BAR2 registers */
    printk(KERN_INFO "\nKey BAR2 registers:\n");
    printk(KERN_INFO "  0x0200 (FW_STATUS): 0x%08x\n", ioread32(bar2 + 0x0200));
    printk(KERN_INFO "  0x0204 (DMA_ENABLE): 0x%08x\n", ioread32(bar2 + 0x0204));
    printk(KERN_INFO "  0x0208 (WPDMA_GLO_CFG): 0x%08x\n", ioread32(bar2 + 0x0208));
    printk(KERN_INFO "  0x020c (WPDMA_RST_IDX): 0x%08x\n", ioread32(bar2 + 0x020c));
    printk(KERN_INFO "  0x0790 (MCU_CMD?): 0x%08x\n", ioread32(bar2 + 0x0790));
    printk(KERN_INFO "  0x07b0 (MCU_SEM?): 0x%08x\n", ioread32(bar2 + 0x07b0));
    
    /* Try executing config commands that target register 0x81 */
    printk(KERN_INFO "\nExecuting config commands for register 0x81:\n");
    for (i = 0; i < 0x200; i += 4) {
        val = ioread32(bar0 + 0x080000 + i);
        if ((val & 0xFF00FF00) == 0x16008100) {  /* Register 0x81 command */
            u8 cmd = (val >> 16) & 0xFF;
            u8 data = val & 0xFF;
            printk(KERN_INFO "  Found reg 0x81 command: type=0x%02x data=0x%02x\n", cmd, data);
            
            /* Since all are type 0x01 with data 0x02, this means OR with 0x02 */
            /* Try applying this to FW_STATUS */
            u32 fw_val = ioread32(bar2 + 0x0200);
            fw_val |= 0x02;  /* Set bit 1 */
            iowrite32(fw_val, bar2 + 0x0200);
            wmb();
            msleep(10);
            
            /* Check if anything changed */
            val = ioread32(bar0);
            if (val != 0x00000000) {
                printk(KERN_INFO "    ✅ MEMORY ACTIVATED after setting bit 1: 0x%08x\n", val);
                goto done;
            }
            break;  /* Just try once */
        }
    }
    
done:
    printk(KERN_INFO "\nFinal check:\n");
    printk(KERN_INFO "  Memory: 0x%08x\n", ioread32(bar0));
    printk(KERN_INFO "  FW_STATUS: 0x%08x\n", ioread32(bar2 + 0x0200));
    
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
    .name = "test_read_config",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Config Reader Test");
