#!/bin/bash

# Build script for mx-datetime
# Usage: ./build.sh [command]
# Commands:
#   clean        - Ultimate clean (rm -rf build)
#   configure    - Configure only (cmake --preset default)
#   make-clean   - Clean only, make clean equivalent
#   make         - Build only (cmake --build --preset default)
#   all          - Configure and build (cmake --workflow --preset default)
#   fresh        - Clean first then configure and build (cmake --workflow --preset default --fresh)

set -e

case "${1:-all}" in
    clean)
        echo "Performing ultimate clean..."
        rm -rf build
        ;;
    
    configure)
        echo "Configuring project..."
        cmake --preset default
        ;;
    
    make-clean)
        echo "Cleaning build artifacts..."
        cmake --build --preset default --target clean
        ;;
    
    make)
        echo "Building project..."
        cmake --build --preset default
        ;;
    
    all)
        echo "Configuring and building project..."
        cmake --workflow --preset default
        ;;
    
    fresh)
        echo "Fresh build (clean first, then configure and build)..."
        cmake --workflow --preset default --fresh
        ;;
    
    *)
        echo "Usage: $0 [command]"
        echo "Commands:"
        echo "  clean        - Ultimate clean (rm -rf build)"
        echo "  configure    - Configure only"
        echo "  make-clean   - Clean build artifacts only"
        echo "  make         - Build only"
        echo "  all          - Configure and build (default)"
        echo "  fresh        - Clean first then configure and build"
        exit 1
        ;;
esac