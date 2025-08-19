/*
 * Test: Memory Activation Probe
 * Category: 04_risky_ops
 * Purpose: Try different approaches to activate memory
 * Strategy: Test various theories about what triggers memory activation
 * Risk: Medium-High - May cause chip errors
 * Duration: ~5 seconds
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927

/* Key registers we've identified */
#define FW_STATUS    0x0200  /* Currently 0xffff10f1 */
#define DMA_ENABLE   0x0204  /* Currently 0xf5 */
#define FW_REG1      0x0008  /* Currently 0x0000032b */
#define FW_REG2      0x000c  /* Currently 0x000c0000 */
#define CONTROL_REG  0x00d4  /* Currently 0x80006000 - bit 31 set */

/* Memory activation theories to test */
enum activation_theory {
    THEORY_FW_ACK,           /* Firmware needs acknowledgment */
    THEORY_DMA_ALL_CHANNELS, /* Enable all DMA channels */
    THEORY_CONTROL_BIT,      /* Toggle control register bits */
    THEORY_MEMORY_WINDOW,    /* Configure memory window registers */
    THEORY_SEQUENCE_WRITE,   /* Write specific sequence to scratch */
    THEORY_MAX
};

static const char *theory_names[] = {
    "Firmware Acknowledgment",
    "Enable All DMA Channels",
    "Control Register Bits",
    "Memory Window Configuration",
    "Sequence Write to Scratch"
};

static int check_memory_active(void __iomem *bar0, const char *context)
{
    u32 main_mem = ioread32(bar0);
    u32 dma_mem = ioread32(bar0 + 0x020000);
    
    if (main_mem != 0x00000000 && main_mem != 0xffffffff) {
        printk(KERN_INFO "\n✅✅✅ MEMORY ACTIVATED! [%s]\n", context);
        printk(KERN_INFO "BAR0[0x000000] = 0x%08x\n", main_mem);
        return 1;
    }
    
    if (dma_mem != 0x00000000 && dma_mem != 0xffffffff) {
        printk(KERN_INFO "✅ DMA memory active! [%s]\n", context);
        printk(KERN_INFO "BAR0[0x020000] = 0x%08x\n", dma_mem);
        return 1;
    }
    
    return 0;
}

static int test_theory(void __iomem *bar0, void __iomem *bar2, enum activation_theory theory)
{
    u32 original, val;
    int result = 0;
    
    printk(KERN_INFO "\n=== Testing Theory: %s ===\n", theory_names[theory]);
    
    switch (theory) {
        case THEORY_FW_ACK:
            /* Try different firmware acknowledgment patterns */
            original = ioread32(bar2 + FW_STATUS);
            printk(KERN_INFO "Current FW_STATUS: 0x%08x\n", original);
            
            /* Try clearing upper bits */
            val = original & 0x0000FFFF;
            printk(KERN_INFO "Writing 0x%08x to FW_STATUS\n", val);
            iowrite32(val, bar2 + FW_STATUS);
            wmb();
            msleep(50);
            
            if (check_memory_active(bar0, "FW_STATUS clear upper"))
                return 1;
            
            /* Try setting acknowledgment pattern */
            val = 0x00000001;
            printk(KERN_INFO "Writing 0x%08x to FW_STATUS\n", val);
            iowrite32(val, bar2 + FW_STATUS);
            wmb();
            msleep(50);
            
            if (check_memory_active(bar0, "FW_STATUS ack"))
                return 1;
            
            /* Restore original */
            iowrite32(original, bar2 + FW_STATUS);
            wmb();
            break;
            
        case THEORY_DMA_ALL_CHANNELS:
            /* Enable all DMA channels */
            original = ioread32(bar2 + DMA_ENABLE);
            printk(KERN_INFO "Current DMA_ENABLE: 0x%02x\n", original);
            
            val = 0xFF;  /* Enable all 8 channels */
            printk(KERN_INFO "Enabling all DMA channels: 0x%02x\n", val);
            iowrite32(val, bar2 + DMA_ENABLE);
            wmb();
            msleep(50);
            
            if (check_memory_active(bar0, "All DMA channels"))
                return 1;
            
            /* Try specific pattern that might trigger init */
            val = 0x3F;  /* Channels 0-5 */
            printk(KERN_INFO "Trying DMA pattern: 0x%02x\n", val);
            iowrite32(val, bar2 + DMA_ENABLE);
            wmb();
            msleep(50);
            
            if (check_memory_active(bar0, "DMA pattern 0x3F"))
                return 1;
            
            /* Restore */
            iowrite32(original, bar2 + DMA_ENABLE);
            wmb();
            break;
            
        case THEORY_CONTROL_BIT:
            /* Toggle control register bits */
            original = ioread32(bar2 + CONTROL_REG);
            printk(KERN_INFO "Current CONTROL: 0x%08x\n", original);
            
            /* Clear bit 31 (currently set) */
            val = original & ~0x80000000;
            printk(KERN_INFO "Clearing bit 31: 0x%08x\n", val);
            iowrite32(val, bar2 + CONTROL_REG);
            wmb();
            msleep(50);
            
            if (check_memory_active(bar0, "Control bit 31 clear"))
                return 1;
            
            /* Toggle bit 15 (memory enable?) */
            val = original ^ 0x00008000;
            printk(KERN_INFO "Toggling bit 15: 0x%08x\n", val);
            iowrite32(val, bar2 + CONTROL_REG);
            wmb();
            msleep(50);
            
            if (check_memory_active(bar0, "Control bit 15 toggle"))
                return 1;
            
            /* Restore */
            iowrite32(original, bar2 + CONTROL_REG);
            wmb();
            break;
            
        case THEORY_MEMORY_WINDOW:
            /* Configure PCIe memory window registers */
            printk(KERN_INFO "Configuring memory windows...\n");
            
            /* PCIe remap registers */
            iowrite32(0x00000000, bar2 + 0x0504);  /* Base address */
            iowrite32(0x00200000, bar2 + 0x0508);  /* Size 2MB */
            wmb();
            msleep(50);
            
            if (check_memory_active(bar0, "Memory window config"))
                return 1;
            
            /* Try MT7925 pattern */
            iowrite32(0x00000200, bar2 + 0x2504);
            wmb();
            msleep(50);
            
            if (check_memory_active(bar0, "MT7925 memory pattern"))
                return 1;
            break;
            
        case THEORY_SEQUENCE_WRITE:
            /* Write specific sequence to scratch registers */
            printk(KERN_INFO "Writing activation sequence to scratch...\n");
            
            /* Magic sequence that might trigger init */
            iowrite32(0xDEADBEEF, bar2 + 0x0020);
            iowrite32(0xCAFEBABE, bar2 + 0x0024);
            wmb();
            msleep(10);
            
            iowrite32(0x12345678, bar2 + 0x0020);
            iowrite32(0x87654321, bar2 + 0x0024);
            wmb();
            msleep(10);
            
            iowrite32(0x00000001, bar2 + 0x0020);  /* Init command? */
            iowrite32(0x00000000, bar2 + 0x0024);
            wmb();
            msleep(50);
            
            if (check_memory_active(bar0, "Scratch sequence"))
                return 1;
            
            /* Clear scratch */
            iowrite32(0x00000000, bar2 + 0x0020);
            iowrite32(0x00000000, bar2 + 0x0024);
            wmb();
            break;
            
        default:
            break;
    }
    
    return result;
}

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar0 = NULL, *bar2 = NULL;
    u32 val;
    int ret, i;
    int memory_activated = 0;
    
    printk(KERN_INFO "\n=== MT7927 TEST: Memory Activation Probe ===\n");
    printk(KERN_INFO "Category: 04_risky_ops\n");
    printk(KERN_INFO "Testing different theories for memory activation\n\n");
    
    ret = pci_enable_device(pdev);
    if (ret) {
        printk(KERN_ERR "FAIL: Cannot enable device\n");
        return ret;
    }
    
    pci_set_master(pdev);
    
    ret = pci_request_regions(pdev, "test_memory_probe");
    if (ret) {
        printk(KERN_ERR "FAIL: Cannot request regions\n");
        goto out_disable;
    }
    
    bar0 = pci_iomap(pdev, 0, 0);
    if (!bar0) {
        printk(KERN_ERR "FAIL: Cannot map BAR0\n");
        goto out_release;
    }
    
    bar2 = pci_iomap(pdev, 2, 0);
    if (!bar2) {
        printk(KERN_ERR "FAIL: Cannot map BAR2\n");
        goto out_unmap0;
    }
    
    /* Check initial state */
    val = ioread32(bar2);
    if (val == 0xffffffff) {
        printk(KERN_ERR "Chip in error state! Aborting.\n");
        goto out_unmap;
    }
    
    printk(KERN_INFO "Initial chip state: 0x%08x\n", val);
    printk(KERN_INFO "Initial memory: BAR0[0x000000]=0x%08x\n", ioread32(bar0));
    
    /* Test each theory */
    for (i = 0; i < THEORY_MAX && !memory_activated; i++) {
        memory_activated = test_theory(bar0, bar2, i);
        
        /* Check chip health after each test */
        val = ioread32(bar2);
        if (val == 0xffffffff) {
            printk(KERN_ERR "Chip entered error state during test!\n");
            break;
        }
    }
    
    /* Final analysis */
    printk(KERN_INFO "\n=== Final Analysis ===\n");
    
    /* Check various status indicators */
    printk(KERN_INFO "Chip status: 0x%08x\n", ioread32(bar2));
    printk(KERN_INFO "FW_STATUS: 0x%08x\n", ioread32(bar2 + FW_STATUS));
    printk(KERN_INFO "DMA_ENABLE: 0x%08x\n", ioread32(bar2 + DMA_ENABLE));
    printk(KERN_INFO "CONTROL: 0x%08x\n", ioread32(bar2 + CONTROL_REG));
    printk(KERN_INFO "BAR0[0x000000]: 0x%08x\n", ioread32(bar0));
    printk(KERN_INFO "BAR0[0x020000]: 0x%08x\n", ioread32(bar0 + 0x020000));
    
    /* Check if we found anything interesting */
    for (i = 0; i < 0x100; i += 4) {
        val = ioread32(bar0 + i);
        if (val != 0x00000000 && val != 0xffffffff) {
            printk(KERN_INFO "Found data at BAR0[0x%06x]: 0x%08x\n", i, val);
        }
    }
    
    if (memory_activated) {
        printk(KERN_INFO "\n✅ SUCCESS! Memory activation achieved!\n");
        printk(KERN_INFO "Document the exact sequence that worked\n");
    } else {
        printk(KERN_INFO "\n❌ Memory not activated\n");
        printk(KERN_INFO "Need to explore more theories\n");
    }
    
out_unmap:
    pci_iounmap(pdev, bar2);
out_unmap0:
    pci_iounmap(pdev, bar0);
out_release:
    pci_release_regions(pdev);
out_disable:
    pci_disable_device(pdev);
    
    return -ENODEV;
}

static void test_remove(struct pci_dev *pdev) {}

static struct pci_device_id test_ids[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { 0, }
};

static struct pci_driver test_driver = {
    .name = "test_memory_probe",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Test: Memory Activation Probe");
MODULE_AUTHOR("MT7927 Linux Driver Project");
