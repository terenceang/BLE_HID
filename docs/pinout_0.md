# DA14585 module pinout (QFN40)

## Module pins

| Pin | Name | Board net | E-paper | BLE_HID (SNES) |
|---|---|---|---|---|
| 1 | P00 | SPI.CLK |  | SPI flash CLK |
| 2 | P01 | U5.13 | SCLK | NC (traced) |
| 3 | P02 | LED_Red |  | NC (traced) |
| 4 | P03 | SPI.CS |  | SPI flash CS |
| 5 | P30 | - |  | NC (traced) |
| 6 | P04 | UART_TX |  | UART2 TX (TP7, traced) |
| 7 | P05 | SPI.SO | UART_RX | UART2 RX (TP8, traced) / SPI flash DI |
| 8 | P21 | U5.12 | CS | SNES CLOCK (pad E12, traced) |
| 9 | P06 | SPI.SI |  | SPI flash DO |
| 10 | P07 | U5.11 | DC | SNES DATA (pad E10, traced) |
| 11 | 32Kp |  |  |  |
| 12 | 32Km |  |  |  |
| 13 | P22 | U5.1 | HLT_CTL | NC (traced) |
| 14 | VBAT_RF |  |  |  |
| 15 | VBAT3V |  |  |  |
| 16 | GND |  |  |  |
| 17 | RST |  |  |  |
| 18 | P23 | PWR_EN |  | free - drives Q3 (traced) |
| 19 | VDCDC |  |  |  |
| 20 | P24 | 接U4 (to U4) |  | NC (traced) |
| 21 | SWITCH |  |  |  |
| 22 | P10 | U5.10 | RST | NC (traced) |
| 23 | VBAT1V |  |  |  |
| 24 | P11 | U5.9 | BUSY | NC (traced) |
| 25 | SWDIO |  |  |  |
| 26 | SWCLK |  |  |  |
| 27 | P12 | - |  | NC (traced) |
| 28 | P13 | - |  | NC (traced) |
| 29 | 16Mp |  |  |  |
| 30 | 16Mm |  |  |  |
| 31 | VDCDC_RF |  |  |  |
| 32 | P25 | LED_Green |  | free - unpopulated D1/R4 (traced) |
| 33 | P26 | - |  | NC (traced) |
| 34 | RFIOm |  |  |  |
| 35 | RFIOp |  |  |  |
| 36 | P27 | - |  | NC (traced) |
| 37 | P28 | - |  | NC (traced) |
| 38 | VDD |  |  |  |
| 39 | P29 | LED_Blue |  | NC (traced) |
| 40 | P20 | U5.14 | SDI | SNES LATCH (pad E09, traced) |

## E-paper connector

FPC pin numbers as on the HMCLOCK sheet. The BLE_HID PCB pads `Exx` do not follow this order - see `Readme.MD` for the traced pads.

| GPIO | FPC pin | Signal | Note |
|---|---|---|---|
| P22 | 1 | HLT_CTL |  |
|  | 2 | GDR |  |
|  | 3 | RESE |  |
|  | 4 | VGL |  |
|  | 5 | VGH |  |
|  | 6 | TSCL | 温控IIC接口 (temperature sensor I2C, pins 6-7) |
|  | 7 | TSDA |  |
|  | 8 | BS | 1:3线  0:4线 (1 = 3-wire SPI, 0 = 4-wire) |
| P11 | 9 | nBUSY |  |
| P10 | 10 | nRST |  |
| P07 | 11 | D/C |  |
| P21 | 12 | nCS |  |
| P01 | 13 | SCLK |  |
| P20 | 14 | SDI |  |
|  | 15 | VDDIO |  |
|  | 16 | VCI |  |
|  | 17 | VSS |  |
|  | 18 | VDDIO |  |
|  | 19 | VPP |  |
|  | 20 | VSH |  |
|  | 21 | PREVGH |  |
|  | 22 | VSL |  |
|  | 23 | PREVGL |  |
|  | 24 | VCOM |  |

在Flash的0x39000处，保存有GPIO的配置信息 (GPIO configuration is stored in flash at 0x39000.)
