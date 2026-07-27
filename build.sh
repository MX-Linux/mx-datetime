#!/bin/bash
# Build script - see below for command information.
set -e

ARCH_BUILD=false
DEBIAN_BUILD=false

ARGS=()
for arg in "$@"; do
	case "$arg" in
		--debian)
			DEBIAN_BUILD=true
			;;
		--arch)
			ARCH_BUILD=true
			;;
		*)
			ARGS+=("$arg")
			;;
	esac
done
set -- "${ARGS[@]}"

if [ "$DEBIAN_BUILD" = true ]; then
	echo "Building Debian package..."

	if ! command -v debuild &> /dev/null; then
		echo "Error: debuild not found. Please install the devscripts package."
		exit 1
	fi

	debuild -us -uc

	PKG_DEST_DIR="$PWD/debs"
	mkdir -p "$PKG_DEST_DIR"
	mv ../mx-datetime_*.deb "$PKG_DEST_DIR"/ 2>/dev/null || true
	mv ../mx-datetime_*.changes "$PKG_DEST_DIR"/ 2>/dev/null || true
	mv ../mx-datetime_*.dsc "$PKG_DEST_DIR"/ 2>/dev/null || true
	mv ../mx-datetime_*.tar.* "$PKG_DEST_DIR"/ 2>/dev/null || true
	mv ../mx-datetime_*.buildinfo "$PKG_DEST_DIR"/ 2>/dev/null || true
	mv ../mx-datetime_*.build "$PKG_DEST_DIR"/ 2>/dev/null || true

	echo "Debian package build completed!"
	echo "Package: $(ls "$PKG_DEST_DIR"/*.deb 2>/dev/null || echo 'not found')"
	exit 0
fi

if [ "$ARCH_BUILD" = true ]; then
	echo "Building Arch Linux package..."

	if ! command -v makepkg &> /dev/null; then
		echo "Error: makepkg not found. Please install base-devel package."
		exit 1
	fi

	ARCH_VERSION=$(git describe --tags --abbrev=0 2>/dev/null)
	if [ -z "$ARCH_VERSION" ]; then
		echo "Error: no git tags found; cannot determine version for Arch build."
		exit 1
	fi
	echo "Using version ${ARCH_VERSION} from latest git tag"

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
		echo "  --debian     - Build Debian package"
		echo "  --arch       - Build Arch Linux package"
		exit 1
		;;
esac
