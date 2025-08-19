/*
 * Test: Execute full configuration sequence
 * Purpose: Execute all 79 configuration commands we decoded
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927
#define CONFIG_START     0x080000

/* Map config registers to BAR2 offsets based on patterns */
static u32 map_config_reg(u8 reg)
{
    switch(reg) {
        case 0x00: return 0x0000;
        case 0x01: return 0x0004;
        case 0x20: return 0x0020;
        case 0x24: return 0x0024;
        case 0x70: return 0x0070;
        case 0x74: return 0x0074;
        case 0x81: return 0x0200;  /* FW_STATUS register */
        default:
            if (reg < 0x80) return reg * 4;
            return 0x0200 + (reg - 0x80) * 4;
    }
}

static void execute_config(void __iomem *bar0, void __iomem *bar2)
{
    u32 cmd, val;
    u8 cmd_type, reg, data;
    int i, cmd_count = 0;
    
    printk(KERN_INFO "Executing configuration commands...\n");
    
    /* Read and execute each command */
    for (i = 0; i < 0x400; i += 4) {
        cmd = ioread32(bar0 + CONFIG_START + i);
        
        if ((cmd & 0xFF000000) == 0x16000000) {
            cmd_type = (cmd >> 16) & 0xFF;
            reg = (cmd >> 8) & 0xFF;
            data = cmd & 0xFF;
            
            /* Focus on register 0x81 (firmware control) */
            if (reg == 0x81) {
                printk(KERN_INFO "  Cmd %d: Reg 0x81, Type 0x%02x, Data 0x%02x\n",
                       cmd_count, cmd_type, data);
                
                /* Try writing directly to FW_STATUS */
                val = ioread32(bar2 + 0x0200);
                
                switch(cmd_type) {
                    case 0x00: val = data; break;
                    case 0x01: val |= data; break;
                    case 0x10: val &= data; break;
                    case 0x11: val ^= data; break;
                    case 0x20: val |= (1 << (data & 0x1F)); break;
                    case 0x21: val &= ~(1 << (data & 0x1F)); break;
                }
                
                iowrite32(val, bar2 + 0x0200);
                wmb();
                msleep(5);
                
                /* Check if memory activated */
                val = ioread32(bar0);
                if (val != 0x00000000) {
                    printk(KERN_INFO "✅ MEMORY ACTIVATED after cmd %d!\n", cmd_count);
                    printk(KERN_INFO "   BAR0[0]: 0x%08x\n", val);
                    return;
                }
            }
            cmd_count++;
        } else if (cmd == 0x31000100) {
            printk(KERN_INFO "  Phase delimiter at cmd %d\n", cmd_count);
        }
        
        if (cmd_count > 100) break;  /* Safety limit */
    }
}

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar0, *bar2;
    u32 val;
    int ret;
    
    ret = pci_enable_device(pdev);
    if (ret) return ret;
    
    pci_set_master(pdev);
    pci_request_regions(pdev, "test_full_config");
    
    bar0 = pci_iomap(pdev, 0, 0);
    bar2 = pci_iomap(pdev, 2, 0);
    
    if (!bar0 || !bar2) {
        if (bar0) pci_iounmap(pdev, bar0);
        if (bar2) pci_iounmap(pdev, bar2);
        pci_release_regions(pdev);
        pci_disable_device(pdev);
        return -ENOMEM;
    }
    
    printk(KERN_INFO "\n=== MT7927 Full Configuration Execution ===\n");
    
    /* Check state */
    val = ioread32(bar2);
    if (val == 0xffffffff) {
        printk(KERN_ERR "Chip in error state!\n");
        goto cleanup;
    }
    
    printk(KERN_INFO "Before: FW_STATUS=0x%08x, Memory=0x%08x\n",
           ioread32(bar2 + 0x0200), ioread32(bar0));
    
    /* Execute configuration */
    execute_config(bar0, bar2);
    
    /* Check final state */
    printk(KERN_INFO "\nAfter: FW_STATUS=0x%08x, Memory=0x%08x\n",
           ioread32(bar2 + 0x0200), ioread32(bar0));
    
    /* Last attempt: Write to firmware region */
    printk(KERN_INFO "\nWriting to firmware header...\n");
    iowrite32(0x00000000, bar0 + 0x0C0000 + 0x0C);  /* Clear byte at +0x0C */
    wmb();
    msleep(100);
    
    val = ioread32(bar0);
    if (val != 0x00000000) {
        printk(KERN_INFO "✅ Memory activated! 0x%08x\n", val);
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
    .name = "test_full_config",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Full Config Execution");
