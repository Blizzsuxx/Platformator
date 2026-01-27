#!/bin/bash

# Script to automatically fix common C++ warnings using clang-tidy

# Find all .cpp files in the current directory
CPP_FILES=$(find . -maxdepth 1 -name "*.cpp" -type f)

# clang-tidy checks for common warnings:
# - readability-redundant-member-init: redundant member initializers
# - modernize-use-nullptr: use nullptr instead of NULL/0
# - readability-braces-around-statements: consistent bracing
# - misc-definitions-in-headers: definitions that should be in cpp files
# - cppcoreguidelines-pro-type-member-init: uninitialized members

CHECKS="readability-redundant-member-init,modernize-use-nullptr,readability-braces-around-statements"

echo "Fixing C++ warnings in all source files..."
echo "==========================================="

for file in $CPP_FILES; do
    echo "Processing: $file"
    clang-tidy "$file" \
        -checks="$CHECKS" \
        --fix \
        --fix-errors \
        -- -std=c++20 2>/dev/null
done

echo ""
echo "Done! Re-run 'make' to check for remaining warnings."