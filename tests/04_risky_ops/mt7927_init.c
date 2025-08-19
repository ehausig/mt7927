/*
 * MT7927 Initialization Driver
 * Purpose: Initialize MT7927 using MT7925-like sequence and load firmware
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927

/* Key registers from MT7925 */
#define MT_WPDMA_GLO_CFG        0x0208
#define MT_WPDMA_RST_IDX        0x020c
#define MT_FW_STATUS            0x0200
#define MT_DMA_ENABLE           0x0204
#define MT_MCU_CMD              0x2000

/* Firmware files to try */
static const char *fw_ram = "mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin";
static const char *fw_patch = "mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin";

struct mt7927_dev {
    struct pci_dev *pdev;
    void __iomem *bar0;
    void __iomem *bar2;
    const struct firmware *fw_ram_data;
    const struct firmware *fw_patch_data;
};

static int mt7927_load_firmware(struct mt7927_dev *dev)
{
    u32 val;
    int i;
    
    dev_info(&dev->pdev->dev, "Loading firmware...\n");
    
    /* Request firmware files */
    if (request_firmware(&dev->fw_ram_data, fw_ram, &dev->pdev->dev) != 0) {
        dev_err(&dev->pdev->dev, "Failed to load RAM firmware\n");
        return -ENOENT;
    }
    
    if (request_firmware(&dev->fw_patch_data, fw_patch, &dev->pdev->dev) != 0) {
        dev_err(&dev->pdev->dev, "Failed to load patch firmware\n");
        release_firmware(dev->fw_ram_data);
        return -ENOENT;
    }
    
    dev_info(&dev->pdev->dev, "Firmware files loaded (RAM: %zu bytes, Patch: %zu bytes)\n",
             dev->fw_ram_data->size, dev->fw_patch_data->size);
    
    /* Reset WPDMA */
    iowrite32(0x1, dev->bar2 + MT_WPDMA_RST_IDX);
    wmb();
    msleep(10);
    iowrite32(0x0, dev->bar2 + MT_WPDMA_RST_IDX);
    wmb();
    msleep(10);
    
    /* Enable DMA */
    iowrite32(0xFF, dev->bar2 + MT_DMA_ENABLE);  /* Enable all channels */
    wmb();
    
    /* Enable WPDMA */
    iowrite32(0x1, dev->bar2 + MT_WPDMA_GLO_CFG);
    wmb();
    msleep(10);
    
    /* Signal firmware ready */
    iowrite32(0x1, dev->bar2 + MT_FW_STATUS);
    wmb();
    msleep(10);
    
    /* Send MCU start command */
    iowrite32(0x1, dev->bar2 + MT_MCU_CMD);
    wmb();
    msleep(100);
    
    /* Check for response */
    for (i = 0; i < 10; i++) {
        val = ioread32(dev->bar2 + MT_FW_STATUS);
        dev_info(&dev->pdev->dev, "FW_STATUS: 0x%08x\n", val);
        
        if (val != 0xffff10f1) {
            dev_info(&dev->pdev->dev, "Firmware status changed!\n");
            
            /* Check if memory activated */
            val = ioread32(dev->bar0);
            if (val != 0x00000000) {
                dev_info(&dev->pdev->dev, "✓ Memory activated: 0x%08x\n", val);
                return 0;
            }
        }
        msleep(100);
    }
    
    return -ETIMEDOUT;
}

static int mt7927_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct mt7927_dev *dev;
    int ret;
    u32 val;
    
    dev_info(&pdev->dev, "MT7927 device found\n");
    
    /* Allocate device structure */
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;
    
    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);
    
    /* Enable PCI device */
    ret = pci_enable_device(pdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable PCI device\n");
        goto err_free;
    }
    
    pci_set_master(pdev);
    
    /* Set DMA mask */
    ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
    if (ret) {
        dev_err(&pdev->dev, "Failed to set DMA mask\n");
        goto err_disable;
    }
    
    /* Request regions */
    ret = pci_request_regions(pdev, "mt7927");
    if (ret) {
        dev_err(&pdev->dev, "Failed to request PCI regions\n");
        goto err_disable;
    }
    
    /* Map BARs */
    dev->bar0 = pci_iomap(pdev, 0, 0);
    dev->bar2 = pci_iomap(pdev, 2, 0);
    
    if (!dev->bar0 || !dev->bar2) {
        dev_err(&pdev->dev, "Failed to map BARs\n");
        ret = -ENOMEM;
        goto err_release;
    }
    
    /* Check chip state */
    val = ioread32(dev->bar2);
    dev_info(&pdev->dev, "Chip status: 0x%08x\n", val);
    
    if (val == 0xffffffff) {
        dev_err(&pdev->dev, "Chip in error state\n");
        ret = -EIO;
        goto err_unmap;
    }
    
    /* Check current memory state */
    val = ioread32(dev->bar0);
    dev_info(&pdev->dev, "Memory at BAR0[0]: 0x%08x\n", val);
    
    val = ioread32(dev->bar2 + MT_FW_STATUS);
    dev_info(&pdev->dev, "Initial FW_STATUS: 0x%08x\n", val);
    
    /* Try to load firmware and initialize */
    ret = mt7927_load_firmware(dev);
    if (ret == 0) {
        dev_info(&pdev->dev, "✓✓✓ MT7927 successfully initialized!\n");
        dev_info(&pdev->dev, "WiFi functionality would need mac80211 integration\n");
    } else {
        dev_warn(&pdev->dev, "Firmware initialization incomplete (ret=%d)\n", ret);
        dev_info(&pdev->dev, "Device claimed but not fully functional\n");
    }
    
    /* Even if firmware didn't fully init, keep device claimed */
    dev_info(&pdev->dev, "MT7927 driver bound successfully\n");
    return 0;
    
err_unmap:
    if (dev->bar2) pci_iounmap(pdev, dev->bar2);
    if (dev->bar0) pci_iounmap(pdev, dev->bar0);
err_release:
    pci_release_regions(pdev);
err_disable:
    pci_disable_device(pdev);
err_free:
    kfree(dev);
    return ret;
}

static void mt7927_remove(struct pci_dev *pdev)
{
    struct mt7927_dev *dev = pci_get_drvdata(pdev);
    
    dev_info(&pdev->dev, "MT7927 device removing\n");
    
    if (dev->fw_ram_data)
        release_firmware(dev->fw_ram_data);
    if (dev->fw_patch_data)
        release_firmware(dev->fw_patch_data);
    
    if (dev->bar2)
        pci_iounmap(pdev, dev->bar2);
    if (dev->bar0)
        pci_iounmap(pdev, dev->bar0);
    
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    kfree(dev);
}

static struct pci_device_id mt7927_ids[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { 0, }
};
MODULE_DEVICE_TABLE(pci, mt7927_ids);

static struct pci_driver mt7927_driver = {
    .name = "mt7927_init",
    .id_table = mt7927_ids,
    .probe = mt7927_probe,
    .remove = mt7927_remove,
};

module_pci_driver(mt7927_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 WiFi 7 Initialization Driver");
MODULE_AUTHOR("MT7927 Linux Driver Project");
MODULE_FIRMWARE("mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin");
MODULE_FIRMWARE("mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin");
