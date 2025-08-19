#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar0, *bar2;
    u32 val, orig_fw_status;
    int ret, i;
    
    ret = pci_enable_device(pdev);
    if (ret) return ret;
    
    pci_set_master(pdev);
    pci_request_regions(pdev, "test_mcu");
    
    bar0 = pci_iomap(pdev, 0, 0);
    bar2 = pci_iomap(pdev, 2, 0);
    
    if (!bar0 || !bar2) {
        if (bar0) pci_iounmap(pdev, bar0);
        if (bar2) pci_iounmap(pdev, bar2);
        pci_release_regions(pdev);
        pci_disable_device(pdev);
        return -ENOMEM;
    }
    
    printk(KERN_INFO "MT7927: Testing MCU direct commands\n");
    
    orig_fw_status = ioread32(bar2 + 0x0200);
    printk(KERN_INFO "Initial FW_STATUS: 0x%08x\n", orig_fw_status);
    
    /* Theory 1: Maybe FW_STATUS needs specific bit pattern */
    printk(KERN_INFO "\nTrying different FW_STATUS patterns:\n");
    u32 patterns[] = {
        0x00000000,  /* Clear all */
        0x00000001,  /* Set bit 0 */
        0x00000002,  /* Set bit 1 */
        0x00010000,  /* Clear upper, set bit 16 */
        0x10f10000,  /* Keep 10f1, clear ffff */
        0xffff0000,  /* Clear lower 16 bits */
        0x0000ffff,  /* Clear upper 16 bits */
    };
    
    for (i = 0; i < ARRAY_SIZE(patterns); i++) {
        printk(KERN_INFO "  Writing 0x%08x...\n", patterns[i]);
        iowrite32(patterns[i], bar2 + 0x0200);
        wmb();
        msleep(50);
        
        val = ioread32(bar2 + 0x0200);
        if (val != orig_fw_status) {
            printk(KERN_INFO "    -> FW_STATUS changed to: 0x%08x\n", val);
        }
        
        val = ioread32(bar0);
        if (val != 0x00000000) {
            printk(KERN_INFO "    ✅ MEMORY ACTIVATED: 0x%08x\n", val);
            goto success;
        }
    }
    
    /* Theory 2: Maybe we need to write to MCU command registers */
    printk(KERN_INFO "\nTrying MCU command registers:\n");
    for (i = 0x0790; i <= 0x07b0; i += 0x10) {
        val = ioread32(bar2 + i);
        printk(KERN_INFO "  BAR2[0x%04x] = 0x%08x\n", i, val);
    }
    
    /* Try writing MCU wake command */
    iowrite32(0x00000001, bar2 + 0x0790);
    wmb();
    msleep(100);
    
    val = ioread32(bar0);
    if (val != 0x00000000) {
        printk(KERN_INFO "✅ MCU command worked! Memory: 0x%08x\n", val);
        goto success;
    }
    
    /* Restore original FW_STATUS */
    iowrite32(orig_fw_status, bar2 + 0x0200);
    
success:
    printk(KERN_INFO "\nFinal state:\n");
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
    .name = "test_mcu_direct",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 MCU Direct Test");
