#!/bin/bash
#
# MT7927 Analysis Runner
# Runs discovery and analysis tests to understand initialization
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="$SCRIPT_DIR/../logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_FILE="$LOG_DIR/analysis_$TIMESTAMP.log"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}MT7927 Initialization Analysis${NC}"
echo "================================"
echo "Timestamp: $TIMESTAMP"
echo ""

# Create log directory
mkdir -p "$LOG_DIR"

# Check chip state first
echo -e "${YELLOW}Checking chip state...${NC}"
CHIP_ID=$(sudo setpci -s 0a:00.0 00.l 2>/dev/null || echo "none")

if [ "$CHIP_ID" = "792714c3" ]; then
    echo -e "${GREEN}✓ Chip responding normally${NC}"
elif [ "$CHIP_ID" = "ffffffff" ]; then
    echo -e "${RED}✗ Chip in error state${NC}"
    echo "Attempting PCI rescan..."
    echo 1 | sudo tee /sys/bus/pci/devices/0000:0a:00.0/remove > /dev/null
    sleep 2
    echo 1 | sudo tee /sys/bus/pci/rescan > /dev/null
    sleep 2
    CHIP_ID=$(sudo setpci -s 0a:00.0 00.l 2>/dev/null || echo "none")
    if [ "$CHIP_ID" = "792714c3" ]; then
        echo -e "${GREEN}✓ Chip recovered${NC}"
    else
        echo -e "${RED}✗ Chip still not responding, power cycle required${NC}"
        exit 1
    fi
else
    echo -e "${RED}✗ Chip not found${NC}"
    exit 1
fi

echo "" | tee -a "$LOG_FILE"

# Function to run a test
run_test() {
    local module=$1
    local description=$2
    local category=$3

    echo -e "\n${YELLOW}[$category] $description${NC}" | tee -a "$LOG_FILE"
    echo "Module: $module" | tee -a "$LOG_FILE"
    echo "----------------------------------------" | tee -a "$LOG_FILE"

    # Clear dmesg
    sudo dmesg -C

    # Load and unload module
    if sudo insmod "${module}.ko" 2>>"$LOG_FILE"; then
        sleep 2
        sudo rmmod "$module" 2>>"$LOG_FILE" || true
    fi

    # Capture output
    sudo dmesg | tee -a "$LOG_FILE"
    
    # Check result
    if sudo dmesg | grep -q "TEST PASSED"; then
        echo -e "${GREEN}✓ Test completed successfully${NC}"
        return 0
    elif sudo dmesg | grep -q "TEST FAILED"; then
        echo -e "${RED}✗ Test failed${NC}"
        return 1
    else
        echo -e "${YELLOW}⚠ Test completed with warnings${NC}"
        return 0
    fi
}

# Build tests if needed
echo -e "\n${BLUE}Building test modules...${NC}"
cd "$SCRIPT_DIR"

# Show build output for debugging
echo "Cleaning old modules..."
make clean 2>&1 | tail -5

echo "Building new modules..."
if make all 2>&1 | tee /tmp/build.log | tail -10; then
    echo -e "${GREEN}✓ Build successful${NC}"
else
    echo -e "${RED}✗ Build failed${NC}"
    echo "Full build log:"
    cat /tmp/build.log
    exit 1
fi

# Run analysis tests
echo -e "\n${BLUE}Starting analysis tests...${NC}\n"

# Phase 1: Decode configuration
if [ -f "02_safe_discovery/test_config_decode.ko" ]; then
    run_test "02_safe_discovery/test_config_decode" \
             "Configuration Command Decoder" \
             "DISCOVERY"
fi

# Phase 2: Compare with MT7925
if [ -f "02_safe_discovery/test_mt7925_patterns.ko" ]; then
    run_test "02_safe_discovery/test_mt7925_patterns" \
             "MT7925 Pattern Comparison" \
             "DISCOVERY"
fi

# Phase 3: Try memory activation (if user confirms)
if [ -f "03_careful_write/test_memory_activate.ko" ]; then
    echo ""
    echo -e "${YELLOW}⚠️  The next test will modify chip state!${NC}"
    echo "It tries to activate main memory through various methods."
    echo "The chip may need PCI rescan if it fails."
    read -p "Run memory activation test? (y/N): " -n 1 -r
    echo ""
    
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        run_test "03_careful_write/test_memory_activate" \
                 "Memory Activation Attempt" \
                 "CAREFUL WRITE"
                 
        # Check if chip needs recovery
        CHIP_ID=$(sudo setpci -s 0a:00.0 00.l 2>/dev/null || echo "none")
        if [ "$CHIP_ID" = "ffffffff" ]; then
            echo -e "${YELLOW}Chip entered error state, attempting recovery...${NC}"
            echo 1 | sudo tee /sys/bus/pci/devices/0000:0a:00.0/remove > /dev/null
            sleep 2
            echo 1 | sudo tee /sys/bus/pci/rescan > /dev/null
            sleep 2
        fi
    else
        echo "Skipping memory activation test"
    fi
fi

# Summary
echo ""
echo "================================" | tee -a "$LOG_FILE"
echo -e "${GREEN}Analysis complete!${NC}" | tee -a "$LOG_FILE"
echo "Full log saved to: $LOG_FILE"

# Extract key findings
echo -e "\n${BLUE}Key Findings:${NC}"
grep -E "KEY FINDINGS|HYPOTHESIS|commands found|Delimiter" "$LOG_FILE" | tail -20

echo -e "\n${BLUE}Next Steps:${NC}"
echo "1. Review the configuration command structure in the log"
echo "2. Identify which registers control memory activation"
echo "3. Create targeted tests for specific initialization sequences"
echo "4. Study MT7925 driver source for similar patterns"

echo -e "\n${YELLOW}Tip:${NC} To view detailed results:"
echo "  less $LOG_FILE"
echo "  grep -A5 'Phase' $LOG_FILE"
