#!/bin/bash
# Builds the Qt GUI as an AppImage inside an AlmaLinux 8 container.
# AlmaLinux 8 ships glibc 2.28 and the official Qt 6.8 binaries target the
# same baseline, so the AppImage runs on any distribution with glibc >= 2.28.
#
# Run from the repository root:
#   docker run --rm -v "$PWD":/src -w /src almalinux:8.10 ci/build-appimage.sh

set -euo pipefail

QT_VERSION=6.8.3
AQTINSTALL_VERSION=3.3.0
LINUXDEPLOY_URL=https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
LINUXDEPLOY_QT_URL=https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage

SRC_DIR=$PWD
WORK_DIR=${WORK_DIR:-/tmp/appimage-build}
OUTPUT_DIR=${OUTPUT_DIR:-$SRC_DIR/dist}
APPIMAGE_NAME=Hantek_365F-x86_64.AppImage

dnf -y install dnf-plugins-core epel-release
dnf config-manager --set-enabled powertools
dnf -y install \
    gcc-toolset-12-gcc-c++ cmake make file python3.11 python3.11-pip \
    mesa-libGL-devel mesa-libEGL fontconfig freetype dbus-libs \
    libxkbcommon libxkbcommon-x11 xcb-util-wm xcb-util-image \
    xcb-util-keysyms xcb-util-renderutil xcb-util-cursor
# Qt 6.8 requires GCC >= 9; the AlmaLinux 8 system compiler is GCC 8.
source /opt/rh/gcc-toolset-12/enable

# aqtinstall writes its log into the current directory; keep it out of the checkout
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

python3.11 -m pip install "aqtinstall==$AQTINSTALL_VERSION"
python3.11 -m aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 \
    -m qtcharts qtserialport -O "$WORK_DIR/qt"
QT_DIR=$WORK_DIR/qt/$QT_VERSION/gcc_64

cmake -S "$SRC_DIR/hantek365_gui" -B "$WORK_DIR/build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_DIR"
cmake --build "$WORK_DIR/build" -j"$(nproc)"

mkdir -p "$WORK_DIR/tools"
curl -fsSL -o "$WORK_DIR/tools/linuxdeploy" "$LINUXDEPLOY_URL"
curl -fsSL -o "$WORK_DIR/tools/linuxdeploy-plugin-qt" "$LINUXDEPLOY_QT_URL"
chmod +x "$WORK_DIR/tools/"*

# Containers have no FUSE; the tools are AppImages themselves.
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH=$WORK_DIR/tools:$PATH
export QMAKE=$QT_DIR/bin/qmake
export LD_LIBRARY_PATH=$QT_DIR/lib
export OUTPUT=$APPIMAGE_NAME

PACKAGING=$SRC_DIR/hantek365_gui/packaging
rm -rf AppDir
linuxdeploy --appdir AppDir \
    --executable build/hantek365_gui \
    --desktop-file "$PACKAGING/hantek365_gui.desktop" \
    --icon-file "$PACKAGING/hantek365_gui.png" \
    --plugin qt \
    --output appimage

mkdir -p "$OUTPUT_DIR"
mv "$APPIMAGE_NAME" "$OUTPUT_DIR/"
# The container runs as root; hand the result to the owner of the checkout.
chown -R --reference="$SRC_DIR" "$OUTPUT_DIR"
echo "Built $OUTPUT_DIR/$APPIMAGE_NAME"
