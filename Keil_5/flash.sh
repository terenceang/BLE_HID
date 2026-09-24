#!/bin/bash
# RAM-load the built DA14585 image via J-Link and run it (selflash then installs it into SPI flash).
# Usage: Keil_5/flash.sh   (from Git Bash; close any serial terminal / debugger first)
JLINK="${JLINK:-/c/Program Files/SEGGER/JLink_V960/JLink.exe}"
BIN="$(cd "$(dirname "$0")" && pwd)/out_DA14585/Objects/BLE_HID_585.bin"
MAP="$(cd "$(dirname "$0")" && pwd)/out_DA14585/Listings/BLE_HID_585.map"
# selflash_run() only installs when it finds this magic (a ROM boot never has it);
# value = SELFLASH_MAGIC in src/selflash.c
MAGIC_ADDR=$(grep -m1 -E '^ +selflash_magic +0x' "$MAP" | awk '{print $2}')
[ -n "$MAGIC_ADDR" ] || { echo "selflash_magic not found in $MAP"; exit 1; }
read SP PC <<<$(python -c "
import struct;s,p=struct.unpack('<II',open(r'$(cygpath -m "$BIN")','rb').read(8));print('%08X %08X'%(s,p&~1))")
cat > "${TMPDIR:-/tmp}/ble_hid_flash.jlink" <<JL
connect
r
h
w2 0x50003300 8
w2 0x50003102 1
w2 0x50003100 5
w2 0x50000012 0xA2
loadbin $(cygpath -m "$BIN"), 0x07FC0000
w2 0x50000012 0xA2
w4 $MAGIC_ADDR, 0x5E1FF1A5
wreg MSP, $SP
SetPC $PC
g
sleep 500
exit
JL
"$JLINK" -device Cortex-M0 -if SWD -speed 4000 -autoconnect 1 -NoGui 1 -CommandFile "$(cygpath -m "${TMPDIR:-/tmp}/ble_hid_flash.jlink")"
