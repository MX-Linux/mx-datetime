#!/bin/bash
# Build script - see below for command information.
set -e

ARCH_BUILD=false

ARGS=()
for arg in "$@"; do
	case "$arg" in
		--arch)
			ARCH_BUILD=true
			;;
		*)
			ARGS+=("$arg")
			;;
	esac
done
set -- "${ARGS[@]}"

if [ "$ARCH_BUILD" = true ]; then
	echo "Building Arch Linux package..."

	if ! command -v makepkg &> /dev/null; then
		echo "Error: makepkg not found. Please install base-devel package."
		exit 1
	fi

	if [ ! -f debian/changelog ]; then
		echo "Error: debian/changelog not found; cannot determine version for Arch build."
		exit 1
	fi

	ARCH_VERSION=$(sed -n '1{s/^[^(]*(\([^)]*\)).*/\1/p}' debian/changelog)
	if [ -z "$ARCH_VERSION" ]; then
		echo "Error: could not parse version from debian/changelog."
		exit 1
	fi
	echo "Using version ${ARCH_VERSION} from debian/changelog"

	ARCH_BUILDDIR=$(mktemp -d -p "$PWD" archpkgbuild.XXXXXX)
	trap 'rm -rf "$ARCH_BUILDDIR"' EXIT

	rm -rf pkg *.pkg.tar.zst

	PKG_DEST_DIR="$PWD/build"
	mkdir -p "$PKG_DEST_DIR"

	BUILDDIR="$ARCH_BUILDDIR" PKGDEST="$PKG_DEST_DIR" PKGVER="$ARCH_VERSION" makepkg -f

	echo "Cleaning makepkg artifacts..."
	rm -rf pkg

	echo "Arch Linux package build completed!"
	echo "Package: $(ls "$PKG_DEST_DIR"/*.pkg.tar.zst 2>/dev/null || echo 'not found')"
	echo "Binary available at: build/mx-datetime"
	exit 0
fi

case "${1:-all}" in
	clean)
		echo "Performing ultimate clean..."
		rm -rf _build_
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
		echo "Options:"
		echo "  --arch       - Build Arch Linux package"
		exit 1
		;;
esac
