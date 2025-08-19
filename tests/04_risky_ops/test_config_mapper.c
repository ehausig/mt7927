/*
 * Test: Configuration Register Mapper
 * Category: 04_risky_ops
 * Purpose: Map configuration registers (0x00-0xFF) to BAR2 offsets
 * Strategy: Systematically probe BAR2 to find register mappings
 * Focus: Register 0x81 first (appears 13 times - firmware control)
 * Risk: Medium - May trigger unexpected behavior
 * Duration: ~10 seconds
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927
#define CONFIG_OFFSET    0x080000

/* Critical registers from config analysis */
#define REG_81_FIRMWARE  0x81  /* Appears 13 times - most critical */
#define REG_00_CORE      0x00  /* Appears 28 times */
#define REG_13_CLOCK     0x13  /* Appears 5 times */
#define REG_30_INTERRUPT 0x30  /* Appears 4 times */
#define REG_60_MAC       0x60  /* Appears 4 times */

/* Known safe BAR2 registers */
#define SAFE_SCRATCH_1   0x0020
#define SAFE_SCRATCH_2   0x0024
#define SAFE_MODE_1      0x0070
#define SAFE_MODE_2      0x0074

/* Potential BAR2 mapping ranges to test */
struct mapping_range {
    u32 start;
    u32 end;
    u32 step;
    const char *name;
};

static struct mapping_range search_ranges[] = {
    {0x0000, 0x0100, 0x04, "Control registers"},      /* Main control area */
    {0x0400, 0x0600, 0x04, "Extended control"},       /* Extended control */
    {0x0800, 0x0A00, 0x04, "DMA control"},           /* DMA area */
    {0x2000, 0x2100, 0x04, "MCU registers"},         /* MCU communication */
    {0x7000, 0x7100, 0x04, "WiFi control"},          /* WiFi specific */
};

/* Structure to track potential mappings */
struct register_mapping {
    u8 config_reg;     /* Configuration register (0x00-0xFF) */
    u32 bar2_offset;   /* Mapped BAR2 offset */
    u32 confidence;    /* Confidence level (0-100) */
    const char *notes;
};

static struct register_mapping found_mappings[256];
static int mapping_count = 0;

/* Check if a BAR2 offset might correspond to a config register */
static int probe_register_mapping(void __iomem *bar2, u32 offset, u8 target_reg)
{
    u32 original, test_val, readback;
    int score = 0;
    
    /* Skip known danger zones */
    if (offset == 0x00a4 || offset == 0x00b8 || 
        offset == 0x00cc || offset == 0x00dc) {
        return -1; /* Danger zone */
    }
    
    /* Read original value */
    original = ioread32(bar2 + offset);
    if (original == 0xffffffff) {
        return -1; /* Invalid or error */
    }
    
    /* For register 0x81 (firmware), look for specific patterns */
    if (target_reg == REG_81_FIRMWARE) {
        /* Firmware registers often have specific bits set */
        if ((original & 0xFF000000) == 0xFF000000 ||
            (original & 0x0000FF00) == 0x0000FF00) {
            score += 30; /* Firmware-like pattern */
        }
        
        /* Check if it's in a reasonable range */
        if (offset >= 0x0200 && offset <= 0x0300) {
            score += 20; /* Firmware status area */
        }
    }
    
    /* For register 0x00 (core), look for control patterns */
    if (target_reg == REG_00_CORE) {
        if (offset < 0x0100) {
            score += 30; /* Core registers usually at start */
        }
    }
    
    /* Try a safe read-modify-write test on scratch registers only */
    if (offset == SAFE_SCRATCH_1 || offset == SAFE_SCRATCH_2) {
        test_val = 0x5A5A5A5A;
        iowrite32(test_val, bar2 + offset);
        wmb();
        readback = ioread32(bar2 + offset);
        
        if (readback == test_val) {
            score += 50; /* Fully writable */
        }
        
        /* Restore original */
        iowrite32(original, bar2 + offset);
        wmb();
    }
    
    return score;
}

/* Search for a specific register mapping */
static void find_register_mapping(void __iomem *bar2, u8 target_reg)
{
    int i, j;
    int best_score = 0;
    u32 best_offset = 0;
    
    printk(KERN_INFO "\nSearching for register 0x%02x mapping...\n", target_reg);
    
    for (i = 0; i < ARRAY_SIZE(search_ranges); i++) {
        struct mapping_range *range = &search_ranges[i];
        
        for (j = range->start; j <= range->end; j += range->step) {
            int score = probe_register_mapping(bar2, j, target_reg);
            
            if (score > 0) {
                printk(KERN_INFO "  Candidate at BAR2[0x%04x]: score %d\n", j, score);
                
                if (score > best_score) {
                    best_score = score;
                    best_offset = j;
                }
            }
        }
    }
    
    if (best_score > 0) {
        found_mappings[mapping_count].config_reg = target_reg;
        found_mappings[mapping_count].bar2_offset = best_offset;
        found_mappings[mapping_count].confidence = best_score;
        found_mappings[mapping_count].notes = "Auto-detected";
        mapping_count++;
        
        printk(KERN_INFO "  ✓ Best match: BAR2[0x%04x] (confidence: %d%%)\n", 
               best_offset, best_score);
    } else {
        printk(KERN_INFO "  ✗ No mapping found\n");
    }
}

/* Try to infer mappings from patterns */
static void infer_mappings_from_patterns(void __iomem *bar2)
{
    printk(KERN_INFO "\n=== Inferring Mappings from Patterns ===\n");
    
    /* Based on MediaTek chip patterns, try common mappings */
    struct {
        u8 reg;
        u32 likely_offset;
        const char *reason;
    } inferences[] = {
        {0x00, 0x0000, "Core control usually at base"},
        {0x01, 0x0004, "Sequential after 0x00"},
        {0x81, 0x0204, "Near FW_STATUS at 0x0200"},
        {0x13, 0x004C, "Clock control pattern"},
        {0x30, 0x00C0, "Interrupt control pattern"},
        {0x60, 0x0180, "MAC config pattern"},
    };
    
    int i;
    for (i = 0; i < ARRAY_SIZE(inferences); i++) {
        u32 val = ioread32(bar2 + inferences[i].likely_offset);
        
        if (val != 0xffffffff && val != 0x00000000) {
            printk(KERN_INFO "Register 0x%02x -> BAR2[0x%04x]? (value: 0x%08x) - %s\n",
                   inferences[i].reg, inferences[i].likely_offset, val, 
                   inferences[i].reason);
            
            found_mappings[mapping_count].config_reg = inferences[i].reg;
            found_mappings[mapping_count].bar2_offset = inferences[i].likely_offset;
            found_mappings[mapping_count].confidence = 40; /* Inference only */
            found_mappings[mapping_count].notes = inferences[i].reason;
            mapping_count++;
        }
    }
}

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar0 = NULL, *bar2 = NULL;
    u32 val;
    int ret, i;
    
    printk(KERN_INFO "\n=== MT7927 TEST: Configuration Register Mapper ===\n");
    printk(KERN_INFO "Category: 04_risky_ops\n");
    printk(KERN_INFO "Risk: Medium - Probing for register mappings\n");
    printk(KERN_INFO "Focus: Finding how config registers map to BAR2\n\n");
    
    ret = pci_enable_device(pdev);
    if (ret) {
        printk(KERN_ERR "FAIL: Cannot enable device\n");
        return ret;
    }
    
    pci_set_master(pdev);
    
    ret = pci_request_regions(pdev, "test_config_mapper");
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
    
    /* Check chip state */
    val = ioread32(bar2);
    if (val == 0xffffffff) {
        printk(KERN_ERR "Chip in error state! Aborting.\n");
        goto out_unmap;
    }
    
    printk(KERN_INFO "Chip state OK (status: 0x%08x)\n\n", val);
    
    /* First, establish known mappings */
    printk(KERN_INFO "=== Known Safe Mappings ===\n");
    printk(KERN_INFO "Scratch1: Config reg 0x20? -> BAR2[0x0020]\n");
    printk(KERN_INFO "Scratch2: Config reg 0x24? -> BAR2[0x0024]\n");
    printk(KERN_INFO "Mode1:    Config reg 0x70? -> BAR2[0x0070]\n");
    printk(KERN_INFO "Mode2:    Config reg 0x74? -> BAR2[0x0074]\n");
    
    /* Add known mappings */
    found_mappings[0] = (struct register_mapping){0x20, 0x0020, 100, "Known scratch"};
    found_mappings[1] = (struct register_mapping){0x24, 0x0024, 100, "Known scratch"};
    found_mappings[2] = (struct register_mapping){0x70, 0x0070, 100, "Known mode"};
    found_mappings[3] = (struct register_mapping){0x74, 0x0074, 100, "Known mode"};
    mapping_count = 4;
    
    /* Search for critical register mappings */
    printk(KERN_INFO "\n=== Searching for Critical Registers ===\n");
    
    /* Priority 1: Register 0x81 (firmware control) */
    find_register_mapping(bar2, REG_81_FIRMWARE);
    
    /* Priority 2: Register 0x00 (core control) */
    find_register_mapping(bar2, REG_00_CORE);
    
    /* Priority 3: Other important registers */
    find_register_mapping(bar2, REG_13_CLOCK);
    find_register_mapping(bar2, REG_30_INTERRUPT);
    find_register_mapping(bar2, REG_60_MAC);
    
    /* Try pattern-based inference */
    infer_mappings_from_patterns(bar2);
    
    /* Analyze BAR2 registers that might be unmapped config registers */
    printk(KERN_INFO "\n=== Analyzing Unmapped BAR2 Registers ===\n");
    
    u32 interesting_offsets[] = {
        0x0008, 0x000c,  /* FW_REG1, FW_REG2 */
        0x00d4,          /* Control with bit 31 set */
        0x0200, 0x0204,  /* FW_STATUS, DMA_ENABLE */
        0x0230,          /* Alt DMA control */
        0x0504, 0x0508,  /* PCIe remap registers */
    };
    
    for (i = 0; i < ARRAY_SIZE(interesting_offsets); i++) {
        val = ioread32(bar2 + interesting_offsets[i]);
        if (val != 0x00000000 && val != 0xffffffff) {
            printk(KERN_INFO "BAR2[0x%04x]: 0x%08x - Potential config register\n",
                   interesting_offsets[i], val);
        }
    }
    
    /* Summary of findings */
    printk(KERN_INFO "\n=== MAPPING SUMMARY ===\n");
    printk(KERN_INFO "Found %d potential mappings:\n", mapping_count);
    printk(KERN_INFO "\nConfig Reg | BAR2 Offset | Confidence | Notes\n");
    printk(KERN_INFO "-----------|-------------|------------|-------\n");
    
    for (i = 0; i < mapping_count; i++) {
        printk(KERN_INFO "   0x%02x    |   0x%04x    |    %3d%%    | %s\n",
               found_mappings[i].config_reg,
               found_mappings[i].bar2_offset,
               found_mappings[i].confidence,
               found_mappings[i].notes);
    }
    
    /* Generate hypothesis for register 0x81 */
    printk(KERN_INFO "\n=== HYPOTHESIS FOR REGISTER 0x81 ===\n");
    printk(KERN_INFO "Register 0x81 appears 13 times in config (firmware control)\n");
    printk(KERN_INFO "Most likely candidates:\n");
    printk(KERN_INFO "1. BAR2[0x0204] - Near FW_STATUS, currently shows DMA_ENABLE\n");
    printk(KERN_INFO "2. BAR2[0x0208] - Sequential after DMA_ENABLE\n");
    printk(KERN_INFO "3. BAR2[0x0081] - Direct mapping (1:1)\n");
    printk(KERN_INFO "4. BAR2[0x0810] - Shifted mapping (x10)\n");
    
    /* Next steps */
    printk(KERN_INFO "\n=== NEXT STEPS ===\n");
    printk(KERN_INFO "1. Test these mappings with actual config commands\n");
    printk(KERN_INFO "2. Focus on register 0x81 first (most critical)\n");
    printk(KERN_INFO "3. Create test_config_execute.c using these mappings\n");
    printk(KERN_INFO "4. Monitor for memory activation at BAR0[0x000000]\n");
    
    /* Check if chip is still healthy */
    val = ioread32(bar2);
    if (val == 0xffffffff) {
        printk(KERN_ERR "\n⚠️ WARNING: Chip entered error state during mapping!\n");
    } else {
        printk(KERN_INFO "\n✓ TEST PASSED: Chip still healthy after mapping search\n");
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
    .name = "test_config_mapper",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Test: Configuration Register Mapper");
MODULE_AUTHOR("MT7927 Linux Driver Project");
