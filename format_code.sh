#!/bin/bash
# Script to format all C++ files in the preemptive_executor project
# Usage: ./format_code.sh [--check]

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if clang-format is installed
if ! command -v clang-format &> /dev/null; then
    echo -e "${RED}Error: clang-format is not installed${NC}"
    echo "Please install clang-format:"
    echo "  Ubuntu/Debian: sudo apt-get install clang-format"
    echo "  macOS: brew install clang-format"
    exit 1
fi

# Find all C++ files in the project (excluding concurrentqueue subdirectory)
FILES=$(find include src -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) 2>/dev/null)

if [ -z "$FILES" ]; then
    echo -e "${YELLOW}No C++ files found to format${NC}"
    exit 0
fi

if [ "$1" == "--check" ]; then
    echo -e "${YELLOW}Checking code formatting...${NC}"
    UNFORMATTED_FILES=()
    for file in $FILES; do
        if ! clang-format --dry-run --Werror "$file" &>/dev/null; then
            UNFORMATTED_FILES+=("$file")
        fi
    done
    
    if [ ${#UNFORMATTED_FILES[@]} -eq 0 ]; then
        echo -e "${GREEN}All files are properly formatted!${NC}"
        exit 0
    else
        echo -e "${RED}The following files are not properly formatted:${NC}"
        for file in "${UNFORMATTED_FILES[@]}"; do
            echo "  - $file"
        done
        echo ""
        echo "Run './format_code.sh' (without --check) to format them."
        exit 1
    fi
else
    echo -e "${YELLOW}Formatting C++ files...${NC}"
    for file in $FILES; do
        echo "Formatting: $file"
        clang-format -i "$file"
    done
    echo -e "${GREEN}Formatting complete!${NC}"
fi

