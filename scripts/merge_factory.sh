#!/bin/sh
# Builds a single "factory" image (bootloader + partitions + app) that can be written
# at offset 0x0, e.g. by the browser flasher or:
#   esptool.py --chip esp32s3 write_flash 0x0 firmware-factory.bin
set -e
ENV=${1:-t-encoder-pro}
OUT=${2:-.pio/build/$ENV/firmware-factory.bin}
BUILD=.pio/build/$ENV
PIO_HOME=${PLATFORMIO_CORE_DIR:-$HOME/.platformio}
BOOT_APP0=$PIO_HOME/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin
ESPTOOL=$PIO_HOME/packages/tool-esptoolpy/esptool.py

python3 "$ESPTOOL" --chip esp32s3 merge_bin -o "$OUT" \
  --flash_mode qio --flash_freq 80m --flash_size 16MB \
  0x0 "$BUILD/bootloader.bin" \
  0x8000 "$BUILD/partitions.bin" \
  0xe000 "$BOOT_APP0" \
  0x10000 "$BUILD/firmware.bin"
echo "wrote $OUT"
