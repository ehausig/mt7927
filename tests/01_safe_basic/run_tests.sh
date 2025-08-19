#!/bin/bash
#
# MT7927 Safe Basic Tests Runner
# These tests should NEVER cause chip errors
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="$SCRIPT_DIR/../../logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_FILE="$LOG_DIR/safe_basic_$TIMESTAMP.log"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}MT7927 Safe Basic Tests${NC}"
echo "========================"
echo "Timestamp: $TIMESTAMP"
echo ""

# Create log directory
mkdir -p "$LOG_DIR"

# Check chip state first
echo "Checking chip state..."
CHIP_ID=$(sudo setpci -s 0a:00.0 00.l 2>/dev/null || echo "none")

if [ "$CHIP_ID" = "792714c3" ]; then
    echo -e "${GREEN}✓ Chip responding normally${NC}"
elif [ "$CHIP_ID" = "ffffffff" ]; then
    echo -e "${RED}✗ Chip in error state${NC}"
    echo "Please power cycle the system"
    exit 1
else
    echo -e "${RED}✗ Chip not found${NC}"
    exit 1
fi

echo "" | tee -a "$LOG_FILE"

# Function to run a test
run_test() {
    local module=$1
    local description=$2
    
    echo -e "\n${YELLOW}Test: $description${NC}" | tee -a "$LOG_FILE"
    echo "Module: $module" | tee -a "$LOG_FILE"
    
    # Clear dmesg
    sudo dmesg -C
    
    # Load and unload module
    if sudo insmod "${module}.ko" 2>>"$LOG_FILE"; then
        sleep 1
        sudo rmmod "$module" 2>>"$LOG_FILE" || true
    fi
    
    # Capture and analyze output
    sudo dmesg | tee -a "$LOG_FILE" | grep -E "TEST|PASS|FAIL" | while read line; do
        if echo "$line" | grep -q "TEST PASSED"; then
            echo -e "${GREEN}$line${NC}"
        elif echo "$line" | grep -q "TEST FAILED"; then
            echo -e "${RED}$line${NC}"
        else
            echo "$line"
        fi
    done
    
    # Return status
    if sudo dmesg | grep -q "TEST PASSED"; then
        return 0
    else
        return 1
    fi
}

# Test counters
TOTAL=0
PASSED=0

# Run each test
echo -e "\n${GREEN}Starting tests...${NC}\n"

# Test 1: PCI Enumeration
if [ -f "test_pci_enum.ko" ]; then
    ((TOTAL++))
    if run_test "test_pci_enum" "PCI Enumeration"; then
        ((PASSED++))
    fi
fi

# Test 2: BAR Mapping
if [ -f "test_bar_map.ko" ]; then
    ((TOTAL++))
    if run_test "test_bar_map" "BAR Mapping"; then
        ((PASSED++))
    fi
fi

# Test 3: Chip ID
if [ -f "test_chip_id.ko" ]; then
    ((TOTAL++))
    if run_test "test_chip_id" "Chip Identification"; then
        ((PASSED++))
    fi
fi

# Test 4: Scratch Registers
if [ -f "test_scratch_rw.ko" ]; then
    ((TOTAL++))
    if run_test "test_scratch_rw" "Scratch Register R/W"; then
        ((PASSED++))
    fi
fi

# Summary
echo ""
echo "================================" | tee -a "$LOG_FILE"
echo -e "Results: ${GREEN}$PASSED${NC}/$TOTAL tests passed" | tee -a "$LOG_FILE"

if [ $PASSED -eq $TOTAL ]; then
    echo -e "${GREEN}✓ All tests passed!${NC}" | tee -a "$LOG_FILE"
    exit 0
else
    FAILED=$((TOTAL - PASSED))
    echo -e "${RED}✗ $FAILED test(s) failed${NC}" | tee -a "$LOG_FILE"
    echo "See $LOG_FILE for details"
    exit 1
fi
