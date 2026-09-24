# Build, flash and persist the BLE_HID firmware (DA14585 + SPI flash)

## 1. Compile

Keil MDK 5 with Arm Compiler 6 (here installed per-user at
`C:\Users\<you>\AppData\Local\Keil_v5`). Close the project in µVision first, or Keil
overwrites the `.uvprojx`.

```
cd Keil_5
"/c/Users/<you>/AppData/Local/Keil_v5/UV4/UV4.exe" -b BLE_HID.uvprojx -t DA14585 -j0 -o build_log.txt
```

Success = `0 Error(s), 0 Warning(s)` at the end of `build_log.txt`. Output:
`Keil_5/out_DA14585/Objects/BLE_HID_585.bin` (and `.hex`, `.axf`). Only the `DA14585`
target exists in the project.

(`UV4 -f` does nothing useful here: the DA14585 has no internal flash to program.)

## 2. Flash

The DA14585 runs from RAM at `0x07FC0000`; on every power-on its boot ROM copies an image
from external SPI flash into RAM. So "flashing" is: RAM-load the build once, let the firmware
copy itself into SPI flash (`selflash_run()`), then power-cycle.

```
bash Keil_5/flash.sh          # J-Link (V9.60 default; override with JLINK=...)
```

`flash.sh` freezes the watchdog, remaps SysRAM to address 0, loads the `.bin` at
`0x07FC0000`, sets SP/PC from the vector table and runs (same steps as the SDK's
`sdk/common_project_files/misc/sysram_case23.ini`). Alternatively start a µVision debug
session (debugger `LOAD` does the same RAM load).

Serial log: UART2 TX = P0.4, 115200 8N1 (`CFG_PRINTF`) - **off by default** in this project
(`src/config/da14585_config_basic.h`, disabled for production); re-enable and reflash to get
any of the log below. Close terminals before running the script if you need the J-Link, and
only one program can hold the COM port. Expected log (with `CFG_PRINTF` on):

```
BLE_HID vX.Y.Z user_app_init! ...
selflash: installing 28472 bytes at 4000     <- first run of a new build
selflash: done
```

and `selflash: up to date` on later boots. The first install takes a few seconds; don't cut
power during it. If it fails, just run `flash.sh` again (a RAM load never depends on flash).
With `CFG_PRINTF` off there's no log either way - the install still happens, just silently.

3. **Power-cycle** (debugger disconnected). The device should advertise as `BT-SNES-XXXXXX`
   (MAC-suffixed) with no debugger. Remove the device in Windows and re-pair once after
   flashing a build that changed bonding/flash layout, or the device name/appearance.

## How self-flash works (`src/selflash.c`)

The boot ROM reads the product header at flash `0x38000` (signature `70 52`, two image slot
addresses) and boots the newest valid slot: a 64-byte SUOTA header
(`70 51`, valid flag `AA`, image id, size, CRC32, version) followed by the body.
`selflash_run()` (called from `user_app_init()`):

0. runs only after a J-Link RAM load: `flash.sh` writes `0x5E1FF1A5` to `selflash_magic`
   (uninitialised retained RAM, address taken from the `.map`) before starting the image. A
   ROM boot from flash (power-on, OTA reboot, deep-sleep wake) leaves garbage there, so an
   image the ROM booted - including one installed over the air - is never rewritten. A RAM
   load from the µVision debugger doesn't set it, so it doesn't install; use `flash.sh`;
1. reads the product header; skips if missing/invalid;
2. CRC32s the running image (`0x07FC0000` .. start of ZI = exactly the `.bin` contents);
3. skips if slot 0 already has the same size/CRC/version **and** slot 0 is what the ROM boots
   (after an OTA update the other slot can hold a newer image id - then it reinstalls);
4. otherwise erases slot 0, writes the header (valid flag `FF`), the body, re-reads and
   verifies the CRC, and only then sets the valid flag `AA`, so an interrupted install is
   ignored. The image id is one above the newest valid slot, in the ROM/SUOTA order
   (higher is newer, 0 follows 0xFF, equal ids rank slot 1 newer - `app_find_old_img()`).

Safety: slot addresses come from the product header (they differ between units); install
is skipped if the slot is unaligned, beyond the flash size, or would overlap
`0x1D000-0x1EFFF` (bond DB + saved CCC) or `0x38000-0x3AFFF` (product header/pinouts).

## Flash map used by this project

| Address | Content |
|--------:|---------|
| `0x04000`+ (from product header) | firmware image slot 0 (selflash; OTA uses the older slot) |
| `0x1D000` | saved HID + battery notification (CCC) values |
| `0x1E000` | SDK bond database |
| `0x1F000`+ (from product header) | firmware image slot 1 (OTA) |
| `0x38000` | product header (never written by this code) |

## Limits

- The image is CRC'd from RAM after start-up; the few initialised-data bytes are already
  modified, but this is stable across boots so the up-to-date check holds.
- Overlapping slot layouts (e.g. `0x2000`/`0x4000`): installing into slot 0 overwrites slot 1.
- The product header must already exist on the flash; this code never creates it.
- Product header on this unit (read over J-Link): slots `0x04000` and `0x1F000`. Any image
  that fits the DA14585's 96 KB RAM ends below `0x1C000` / `0x37000`, clear of the
  bond/CCC sectors and the product header, so OTA needs no extra range check here. Re-check
  on a unit with a different product header.
- Verified on hardware: self-programming works (RAM load, `selflash: installing ... done`, boots from flash).

## Signed OTA images

`tools/suota.py --sign` appends `BTSNSIG1` + a 64-byte ECDSA P-256 signature (r || s,
big-endian) of SHA-256(firmware). The SUOTA header's code size covers the whole file, so
the boot ROM also copies the 72-byte trailer into RAM - right where zero-initialised data
starts, which the C startup clears, so it's harmless.

On the pad, `ota_check_new_image()` (`src/ota_verify.c`) runs from the SUOTA end callback
after the SDK has checked the CRC and set the valid flag: SHA-256 of the slot in flash, then
`uECC_verify()` from the ROM (the BLE stack's micro-ecc; checked on the chip with a
Python-made signature: big-endian inputs, 1.4 s, ~1.3 KB of stack - hence `STACK_SIZE=0x800`
in the project's assembler defines, up from the SDK's 0x600). The watchdog is frozen during
the verify. On failure it writes `0x00` over the valid flag (`0xAA`), which needs no erase.
The tool gets two statuses after "end": the SDK's CRC result, then the signature verdict
(`0x02` OK, `0x09` rejected).

Micro-ecc's own Thumb assembly doesn't build with Arm Compiler 6 LTO (`.syntax divided`),
and a second copy would clash with the ROM's symbols anyway - use the ROM's.

## Flashing gotchas

- Run `flash.sh` from Git Bash (from PowerShell, `bash` may resolve to WSL, which has no `/e/...`).
  Check the J-Link output shows `wreg MSP, <hex>` and `SetPC <hex>`; empty arguments mean the
  `.bin` was not read and the old flash image boots instead.
- `flash.sh` needs the UART (COM port) closed only for logging, not for flashing.
- To just reboot the pad without reflashing: J-Link `connect`, `r`, `g`. It boots from SPI flash.
- COM port here is a CH340 (COM36). Log capture needs no extra tools:
  PowerShell `System.IO.Ports.SerialPort` at 115200.
