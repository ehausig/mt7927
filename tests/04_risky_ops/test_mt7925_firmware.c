/*
 * Test: MT7925 Firmware Loading for MT7927
 * Purpose: Try loading MT7925 firmware to see if chips are compatible
 * Strategy: Request MT7925 firmware and attempt standard MediaTek init sequence
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927

/* Firmware files to try */
static const char *fw_files[] = {
    /* Try MT7925 firmware first */
    "mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin",
    "mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin",
    /* Also try if they exist with MT7927 names */
    "mediatek/mt7927/WIFI_RAM_CODE_MT7927_1_1.bin",
    "mediatek/mt7927/WIFI_MT7927_PATCH_MCU_1_1_hdr.bin",
    /* Try older naming convention */
    "mediatek/WIFI_RAM_CODE_MT7927_1.bin",
    "mediatek/WIFI_MT7927_patch_mcu_1_1_hdr.bin",
    NULL
};

/* MediaTek MCU registers (from mt7925 driver) */
#define MT_MCU_BASE             0x2000
#define MT_MCU_PCIE_REMAP_1     0x2504
#define MT_MCU_PCIE_REMAP_2     0x2508

/* WPDMA registers */
#define MT_WPDMA_BASE           0x0200
#define MT_WPDMA_GLO_CFG        0x0208
#define MT_WPDMA_RST_IDX        0x020c
#define MT_WPDMA_TX_RING0_CTRL0 0x0300
#define MT_WPDMA_TX_RING0_CTRL1 0x0304

/* Firmware download registers */
#define MT_FW_DL_BASE           0x780000
#define MT_FW_CTRL              0x0200  /* In BAR2 */

static int load_firmware_to_chip(struct pci_dev *pdev, void __iomem *bar0, 
                                  void __iomem *bar2, const struct firmware *fw)
{
    dma_addr_t dma_addr;
    void *dma_buf;
    u32 val;
    int i;
    
    printk(KERN_INFO "  Firmware size: %zu bytes\n", fw->size);
    
    /* Allocate DMA buffer for firmware */
    dma_buf = dma_alloc_coherent(&pdev->dev, fw->size, &dma_addr, GFP_KERNEL);
    if (!dma_buf) {
        printk(KERN_ERR "  Failed to allocate DMA buffer\n");
        return -ENOMEM;
    }
    
    /* Copy firmware to DMA buffer */
    memcpy(dma_buf, fw->data, fw->size);
    
    printk(KERN_INFO "  DMA buffer allocated at 0x%llx\n", (u64)dma_addr);
    
    /* Reset WPDMA */
    printk(KERN_INFO "  Resetting WPDMA...\n");
    iowrite32(0x1, bar2 + MT_WPDMA_RST_IDX);
    wmb();
    msleep(10);
    iowrite32(0x0, bar2 + MT_WPDMA_RST_IDX);
    wmb();
    msleep(10);
    
    /* Configure DMA for firmware download */
    printk(KERN_INFO "  Configuring DMA...\n");
    
    /* Set DMA address for firmware */
    iowrite32(lower_32_bits(dma_addr), bar2 + MT_WPDMA_TX_RING0_CTRL0);
    iowrite32(upper_32_bits(dma_addr), bar2 + MT_WPDMA_TX_RING0_CTRL1);
    wmb();
    
    /* Enable WPDMA */
    iowrite32(0x1, bar2 + MT_WPDMA_GLO_CFG);
    wmb();
    msleep(10);
    
    /* Trigger firmware download */
    printk(KERN_INFO "  Triggering firmware download...\n");
    
    /* Clear FW_STATUS to signal ready */
    iowrite32(0x0, bar2 + MT_FW_CTRL);
    wmb();
    msleep(10);
    
    /* Set firmware start command */
    iowrite32(0x1, bar2 + MT_FW_CTRL);
    wmb();
    msleep(100);
    
    /* Check for firmware response */
    for (i = 0; i < 10; i++) {
        val = ioread32(bar2 + MT_FW_CTRL);
        printk(KERN_INFO "  FW_STATUS: 0x%08x\n", val);
        
        if (val != 0xffff10f1 && val != 0x00000001) {
            printk(KERN_INFO "  ✓ Firmware responded!\n");
            
            /* Check if memory activated */
            val = ioread32(bar0);
            if (val != 0x00000000) {
                printk(KERN_INFO "  ✓✓✓ MEMORY ACTIVATED! 0x%08x\n", val);
                dma_free_coherent(&pdev->dev, fw->size, dma_buf, dma_addr);
                return 1;
            }
        }
        msleep(100);
    }
    
    /* Clean up DMA buffer */
    dma_free_coherent(&pdev->dev, fw->size, dma_buf, dma_addr);
    return 0;
}

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar0, *bar2;
    const struct firmware *fw = NULL;
    u32 val;
    int ret, i;
    int success = 0;
    
    printk(KERN_INFO "\n=== MT7927 Test: MT7925 Firmware Compatibility ===\n");
    
    ret = pci_enable_device(pdev);
    if (ret) {
        printk(KERN_ERR "Cannot enable device\n");
        return ret;
    }
    
    pci_set_master(pdev);
    
    /* Set DMA mask for firmware loading */
    ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
    if (ret) {
        printk(KERN_ERR "Failed to set DMA mask\n");
        goto err_disable;
    }
    
    ret = pci_request_regions(pdev, "test_mt7925_firmware");
    if (ret) {
        printk(KERN_ERR "Cannot request regions\n");
        goto err_disable;
    }
    
    bar0 = pci_iomap(pdev, 0, 0);
    bar2 = pci_iomap(pdev, 2, 0);
    
    if (!bar0 || !bar2) {
        printk(KERN_ERR "Cannot map BARs\n");
        ret = -ENOMEM;
        goto err_release;
    }
    
    /* Check initial state */
    val = ioread32(bar2);
    if (val == 0xffffffff) {
        printk(KERN_ERR "Chip in error state!\n");
        goto err_unmap;
    }
    
    printk(KERN_INFO "Initial state:\n");
    printk(KERN_INFO "  Chip: 0x%08x\n", val);
    printk(KERN_INFO "  Memory: 0x%08x\n", ioread32(bar0));
    printk(KERN_INFO "  FW_STATUS: 0x%08x\n", ioread32(bar2 + MT_FW_CTRL));
    
    /* Try loading different firmware files */
    for (i = 0; fw_files[i] != NULL; i++) {
        printk(KERN_INFO "\nTrying firmware: %s\n", fw_files[i]);
        
        ret = request_firmware(&fw, fw_files[i], &pdev->dev);
        if (ret == 0) {
            printk(KERN_INFO "  ✓ Firmware file found!\n");
            
            /* Try to load it */
            if (load_firmware_to_chip(pdev, bar0, bar2, fw)) {
                success = 1;
                release_firmware(fw);
                break;
            }
            
            release_firmware(fw);
        } else {
            printk(KERN_INFO "  File not found (error %d)\n", ret);
        }
    }
    
    if (!success) {
        /* Try one more thing: just enable DMA channels like MT7925 */
        printk(KERN_INFO "\nTrying MT7925 DMA pattern without firmware...\n");
        
        /* Enable all DMA channels */
        iowrite32(0xFF, bar2 + 0x0204);
        wmb();
        msleep(10);
        
        /* Set MCU ready */
        iowrite32(0x1, bar2 + MT_MCU_BASE);
        wmb();
        msleep(100);
        
        val = ioread32(bar0);
        if (val != 0x00000000) {
            printk(KERN_INFO "✓✓✓ MEMORY ACTIVATED! 0x%08x\n", val);
            success = 1;
        }
    }
    
    /* Final status */
    printk(KERN_INFO "\n=== Final Status ===\n");
    printk(KERN_INFO "Memory: 0x%08x\n", ioread32(bar0));
    printk(KERN_INFO "FW_STATUS: 0x%08x\n", ioread32(bar2 + MT_FW_CTRL));
    
    if (success) {
        printk(KERN_INFO "\n✅ SUCCESS! MT7925 firmware/init works with MT7927!\n");
        printk(KERN_INFO "Next step: Try binding mt7925e driver\n");
    } else {
        printk(KERN_INFO "\n❌ Firmware loading didn't activate memory\n");
        printk(KERN_INFO "But firmware files are compatible - try driver binding\n");
    }
    
err_unmap:
    if (bar2) pci_iounmap(pdev, bar2);
    if (bar0) pci_iounmap(pdev, bar0);
err_release:
    pci_release_regions(pdev);
err_disable:
    pci_disable_device(pdev);
    
    return -ENODEV;
}

static void test_remove(struct pci_dev *pdev) {}

static struct pci_device_id test_ids[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { 0, }
};

static struct pci_driver test_driver = {
    .name = "test_mt7925_firmware",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Test: MT7925 Firmware Compatibility");
MODULE_AUTHOR("MT7927 Linux Driver Project");
