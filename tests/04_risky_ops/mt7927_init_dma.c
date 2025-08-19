/*
 * MT7927 Driver with Enhanced DMA Implementation
 * Based on mt7925 DMA patterns from mt76 driver
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/skbuff.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927

/* Key registers from mt7925/mt76 */
#define MT_WPDMA_GLO_CFG        0x0208
#define MT_WPDMA_RST_IDX        0x020c
#define MT_WPDMA_TX_RING0_BASE  0x0300
#define MT_WPDMA_TX_RING0_CNT   0x0304
#define MT_WPDMA_TX_RING0_CIDX  0x0308
#define MT_WPDMA_TX_RING0_DIDX  0x030c

#define MT_FW_STATUS            0x0200
#define MT_DMA_ENABLE           0x0204
#define MT_MCU_CMD              0x0790
#define MT_MCU_SEMAPHORE        0x07b0

/* DMA descriptor format (from mt76) */
struct mt76_desc {
    __le32 buf0;
    __le32 ctrl;
    __le32 buf1;
    __le32 info;
} __packed __aligned(4);

/* DMA control bits */
#define MT_DMA_CTL_SD_LEN0      GENMASK(15, 0)
#define MT_DMA_CTL_LAST_SEC0    BIT(16)
#define MT_DMA_CTL_DMA_DONE     BIT(31)

/* Firmware download header (from mt76_connac) */
struct mt76_fw_header {
    __le32 ilm_len;
    __le32 dlm_len;
    __le16 build_ver;
    __le16 fw_ver;
    u8 build_time[16];
    u8 reserved[64];
} __packed;

/* Firmware files */
static const char *fw_ram = "mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin";
static const char *fw_patch = "mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin";

struct mt7927_dev {
    struct pci_dev *pdev;
    void __iomem *bar0;
    void __iomem *bar2;
    
    /* DMA ring */
    dma_addr_t tx_ring_dma;
    struct mt76_desc *tx_ring;
    
    /* Firmware buffers */
    dma_addr_t fw_dma;
    void *fw_buf;
    size_t fw_size;
};

static int mt7927_dma_init(struct mt7927_dev *dev)
{
    int ret;
    
    dev_info(&dev->pdev->dev, "Initializing DMA...\n");
    
    /* Allocate TX descriptor ring (256 descriptors) */
    dev->tx_ring = dma_alloc_coherent(&dev->pdev->dev,
                                       256 * sizeof(struct mt76_desc),
                                       &dev->tx_ring_dma,
                                       GFP_KERNEL);
    if (!dev->tx_ring) {
        dev_err(&dev->pdev->dev, "Failed to allocate TX ring\n");
        return -ENOMEM;
    }
    
    memset(dev->tx_ring, 0, 256 * sizeof(struct mt76_desc));
    
    /* Reset WPDMA */
    iowrite32(0x1, dev->bar2 + MT_WPDMA_RST_IDX);
    wmb();
    msleep(10);
    iowrite32(0x0, dev->bar2 + MT_WPDMA_RST_IDX);
    wmb();
    msleep(10);
    
    /* Configure TX ring base address */
    iowrite32(lower_32_bits(dev->tx_ring_dma), dev->bar2 + MT_WPDMA_TX_RING0_BASE);
    iowrite32(upper_32_bits(dev->tx_ring_dma), dev->bar2 + MT_WPDMA_TX_RING0_BASE + 4);
    iowrite32(256, dev->bar2 + MT_WPDMA_TX_RING0_CNT);  /* Ring size */
    iowrite32(0, dev->bar2 + MT_WPDMA_TX_RING0_CIDX);   /* CPU index */
    iowrite32(0, dev->bar2 + MT_WPDMA_TX_RING0_DIDX);   /* DMA index */
    wmb();
    
    /* Enable DMA channels */
    iowrite32(0xFF, dev->bar2 + MT_DMA_ENABLE);
    wmb();
    
    /* Enable WPDMA */
    iowrite32(0x1, dev->bar2 + MT_WPDMA_GLO_CFG);
    wmb();
    msleep(10);
    
    dev_info(&dev->pdev->dev, "DMA initialized (ring at 0x%llx)\n", 
             (u64)dev->tx_ring_dma);
    
    return 0;
}

static int mt7927_mcu_init(struct mt7927_dev *dev)
{
    u32 val;
    int i;
    
    dev_info(&dev->pdev->dev, "Initializing MCU...\n");
    
    /* Clear MCU semaphore */
    iowrite32(0x1, dev->bar2 + MT_MCU_SEMAPHORE);
    wmb();
    
    /* Send MCU power on command */
    iowrite32(0x1, dev->bar2 + MT_MCU_CMD);
    wmb();
    msleep(10);
    
    /* Wait for MCU ready */
    for (i = 0; i < 20; i++) {
        val = ioread32(dev->bar2 + MT_MCU_SEMAPHORE);
        if (val & 0x1) {
            dev_info(&dev->pdev->dev, "MCU ready (0x%08x)\n", val);
            return 0;
        }
        msleep(10);
    }
    
    dev_warn(&dev->pdev->dev, "MCU init timeout\n");
    return -ETIMEDOUT;
}

static int mt7927_load_firmware_dma(struct mt7927_dev *dev, 
                                     const struct firmware *fw)
{
    struct mt76_desc *desc;
    u32 val;
    int i;
    
    dev_info(&dev->pdev->dev, "Loading firmware via DMA (%zu bytes)...\n", fw->size);
    
    /* Allocate DMA buffer for firmware */
    dev->fw_size = ALIGN(fw->size, 4);
    dev->fw_buf = dma_alloc_coherent(&dev->pdev->dev,
                                      dev->fw_size,
                                      &dev->fw_dma,
                                      GFP_KERNEL);
    if (!dev->fw_buf) {
        dev_err(&dev->pdev->dev, "Failed to allocate firmware DMA buffer\n");
        return -ENOMEM;
    }
    
    /* Copy firmware to DMA buffer */
    memcpy(dev->fw_buf, fw->data, fw->size);
    
    /* Setup DMA descriptor */
    desc = &dev->tx_ring[0];
    desc->buf0 = cpu_to_le32(lower_32_bits(dev->fw_dma));
    desc->buf1 = cpu_to_le32(upper_32_bits(dev->fw_dma));
    desc->ctrl = cpu_to_le32(MT_DMA_CTL_SD_LEN0 | MT_DMA_CTL_LAST_SEC0);
    desc->ctrl |= cpu_to_le32(dev->fw_size & MT_DMA_CTL_SD_LEN0);
    desc->info = 0;
    wmb();
    
    /* Trigger DMA transfer */
    dev_info(&dev->pdev->dev, "Triggering DMA transfer...\n");
    iowrite32(1, dev->bar2 + MT_WPDMA_TX_RING0_DIDX);  /* Update DMA index */
    wmb();
    
    /* Signal firmware download start */
    iowrite32(0x12345678, dev->bar2 + 0x0020);  /* Magic to scratch */
    wmb();
    
    /* Clear FW_STATUS to signal ready for firmware */
    iowrite32(0x0, dev->bar2 + MT_FW_STATUS);
    wmb();
    msleep(10);
    
    /* Set firmware load command */
    iowrite32(0x1, dev->bar2 + MT_FW_STATUS);
    wmb();
    
    /* Wait for firmware response */
    for (i = 0; i < 100; i++) {
        val = ioread32(dev->bar2 + MT_FW_STATUS);
        dev_info(&dev->pdev->dev, "  FW_STATUS: 0x%08x\n", val);
        
        if (val != 0xffff10f1 && val != 0x00000001) {
            dev_info(&dev->pdev->dev, "Firmware status changed!\n");
            
            /* Check if memory activated */
            val = ioread32(dev->bar0);
            if (val != 0x00000000) {
                dev_info(&dev->pdev->dev, "✅ Memory activated: 0x%08x\n", val);
                return 0;
            }
        }
        
        /* Check DMA completion */
        desc = &dev->tx_ring[0];
        if (le32_to_cpu(desc->ctrl) & MT_DMA_CTL_DMA_DONE) {
            dev_info(&dev->pdev->dev, "DMA transfer completed\n");
        }
        
        msleep(50);
    }
    
    return -ETIMEDOUT;
}

static int mt7927_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct mt7927_dev *dev;
    const struct firmware *fw = NULL;
    int ret;
    u32 val;
    
    dev_info(&pdev->dev, "MT7927 WiFi 7 device found\n");
    
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
    
    /* Initialize DMA */
    ret = mt7927_dma_init(dev);
    if (ret) {
        dev_err(&pdev->dev, "DMA init failed\n");
        goto err_unmap;
    }
    
    /* Initialize MCU */
    ret = mt7927_mcu_init(dev);
    if (ret) {
        dev_warn(&pdev->dev, "MCU init failed, continuing anyway\n");
    }
    
    /* Load RAM firmware */
    ret = request_firmware(&fw, fw_ram, &pdev->dev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to load RAM firmware: %s\n", fw_ram);
        goto err_dma;
    }
    
    ret = mt7927_load_firmware_dma(dev, fw);
    release_firmware(fw);
    
    if (ret == 0) {
        dev_info(&pdev->dev, "✅ MT7927 successfully initialized!\n");
    } else {
        /* Load patch firmware as fallback */
        ret = request_firmware(&fw, fw_patch, &pdev->dev);
        if (ret == 0) {
            dev_info(&pdev->dev, "Trying patch firmware...\n");
            ret = mt7927_load_firmware_dma(dev, fw);
            release_firmware(fw);
        }
    }
    
    /* Check final state */
    val = ioread32(dev->bar0);
    dev_info(&pdev->dev, "Final memory state: 0x%08x\n", val);
    val = ioread32(dev->bar2 + MT_FW_STATUS);
    dev_info(&pdev->dev, "Final FW_STATUS: 0x%08x\n", val);
    
    if (ret) {
        dev_warn(&pdev->dev, "Initialization incomplete but device claimed\n");
    }
    
    dev_info(&pdev->dev, "MT7927 driver bound successfully\n");
    return 0;
    
err_dma:
    if (dev->tx_ring)
        dma_free_coherent(&pdev->dev, 256 * sizeof(struct mt76_desc),
                          dev->tx_ring, dev->tx_ring_dma);
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
    
    dev_info(&pdev->dev, "Removing MT7927 device\n");
    
    if (dev->fw_buf)
        dma_free_coherent(&pdev->dev, dev->fw_size, 
                          dev->fw_buf, dev->fw_dma);
    
    if (dev->tx_ring)
        dma_free_coherent(&pdev->dev, 256 * sizeof(struct mt76_desc),
                          dev->tx_ring, dev->tx_ring_dma);
    
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
    .name = "mt7927_init_dma",
    .id_table = mt7927_ids,
    .probe = mt7927_probe,
    .remove = mt7927_remove,
};

module_pci_driver(mt7927_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 WiFi 7 Driver with DMA Implementation");
MODULE_AUTHOR("MT7927 Linux Driver Project");
MODULE_FIRMWARE("mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin");
MODULE_FIRMWARE("mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin");
