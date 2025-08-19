/*
 * Test: PCIe-level initialization
 * Purpose: Try initialization through PCIe config space manipulation
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
    u16 cmd, status;
    int ret, pos;
    
    printk(KERN_INFO "\n=== MT7927 PCIe Initialization Test ===\n");
    
    /* Find power management capability */
    pos = pci_find_capability(pdev, PCI_CAP_ID_PM);
    if (pos) {
        printk(KERN_INFO "PM capability at 0x%02x\n", pos);
    }
    
    /* Read current PCI command register */
    pci_read_config_word(pdev, PCI_COMMAND, &cmd);
    pci_read_config_word(pdev, PCI_STATUS, &status);
    printk(KERN_INFO "PCI CMD: 0x%04x, STATUS: 0x%04x\n", cmd, status);
    
    /* Try D3->D0 power cycle */
    printk(KERN_INFO "\nAttempting power cycle...\n");
    pci_set_power_state(pdev, PCI_D3hot);
    msleep(100);
    pci_set_power_state(pdev, PCI_D0);
    msleep(100);
    
    /* Enable device with all features */
    ret = pci_enable_device(pdev);
    if (ret) {
        printk(KERN_ERR "Failed to enable after power cycle\n");
        return ret;
    }
    
    pci_set_master(pdev);
    pci_write_config_word(pdev, PCI_COMMAND, 
                          PCI_COMMAND_IO | PCI_COMMAND_MEMORY | 
                          PCI_COMMAND_MASTER | PCI_COMMAND_SERR);
    
    /* Request all BARs */
    ret = pci_request_regions(pdev, "test_pcie_init");
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
    
    /* Check state after power cycle */
    val = ioread32(bar2);
    printk(KERN_INFO "After power cycle - Chip: 0x%08x\n", val);
    printk(KERN_INFO "  FW_STATUS: 0x%08x\n", ioread32(bar2 + 0x0200));
    printk(KERN_INFO "  Memory: 0x%08x\n", ioread32(bar0));
    
    /* Try secondary bus reset via config space */
    printk(KERN_INFO "\nAttempting bus reset...\n");
    pci_reset_function(pdev);
    msleep(100);
    
    val = ioread32(bar2);
    printk(KERN_INFO "After reset - Chip: 0x%08x\n", val);
    if (val != 0xffffffff) {
        printk(KERN_INFO "  FW_STATUS: 0x%08x\n", ioread32(bar2 + 0x0200));
        printk(KERN_INFO "  Memory: 0x%08x\n", ioread32(bar0));
    }
    
    /* Try writing to BAR2 mirror in BAR0 */
    printk(KERN_INFO "\nTrying BAR2 mirror writes...\n");
    iowrite32(0x00000001, bar0 + 0x010200);  /* FW_STATUS via mirror */
    wmb();
    msleep(10);
    
    val = ioread32(bar2 + 0x0200);
    printk(KERN_INFO "FW_STATUS after mirror write: 0x%08x\n", val);
    
    /* Last attempt: Write to control register */
    printk(KERN_INFO "\nToggling control bits...\n");
    val = ioread32(bar2 + 0x00d4);
    printk(KERN_INFO "Control reg: 0x%08x\n", val);
    iowrite32(val & ~0x80000000, bar2 + 0x00d4);  /* Clear bit 31 */
    wmb();
    msleep(10);
    iowrite32(val, bar2 + 0x00d4);  /* Restore */
    wmb();
    msleep(10);
    
    /* Check for any activation */
    val = ioread32(bar0);
    if (val != 0x00000000) {
        printk(KERN_INFO "✅ MEMORY ACTIVATED! 0x%08x\n", val);
    } else {
        printk(KERN_INFO "Memory still inactive\n");
    }
    
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
    .name = "test_pcie_init",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 PCIe Initialization Test");
