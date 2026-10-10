# TempTale Ultra: specification and sensor research

Date: 2026-09-19. Research only; no firmware changes, hardware commands or flash.

## Confirmed Product Specification

For the standard TempTale Ultra, not BLE, Fit, GEO or Dry Ice:

| Parameter | Manufacturer specification |
|---|---|
| Temperature | -30 to +70 degrees C |
| Accuracy | +/-0.5 degrees C at -10..+45; +/-1 outside that band |
| Reported resolution | 0.1 degrees C/F |
| Stored readings | 16000 |
| Sampling interval | 30 seconds to 120 minutes |
| Battery / life | 3.0 V lithium coin cell, non-replaceable; 12 months |
| Multi-Use power | Two 3.6 V lithium coin cells |
| Protection | IP64 with USB cap |

Source: [Sensitech/Carrier standard Ultra datasheet, April 2023](https://www.shareddocs.com/hvac/docs/2004/Public/05/tt-ultra-life-sciences.pdf),
linked by the [manufacturer's product page](https://www.sensitech.com/en/products/monitors/conventional/).
The [Industrial datasheet](https://www.shareddocs.com/hvac/docs/2004/Public/02/tt-ultra-industrial.pdf)
agrees on these central parameters.

These are factory-product specifications, not guarantees for custom firmware.
The document does not specify CR3450 as the cell model, a sensor part number,
individual rail currents or a USB/battery power-path circuit. Do not overwrite
the user's battery identification with a model taken from a different variant.

## Independent First-hand Hardware Reports

[zvodd's Ultra teardown](https://hackaday.io/project/189442-temptale-ultra-teardown-repurpose)
reports STM32L152RC/RE variants, SPI Flash and an unidentified I2C temperature
sensor. It does not resolve our sensor identity.

[arduinocelentano's Ultra teardown notes](https://github.com/arduinocelentano/templogger-db/blob/main/docs/TempLogPro_Ulrta/overview.md)
describe STM32L152RCT6 and Winbond 25x40. This matches our established hardware
family, not necessarily every PCB revision. Its speculation that a battery
might be rechargeable is not evidence and must not guide power-path testing.

Other results covered TempLogPro 4, Direct, Ultra BLE and probe variants. Their
sensor or power details cannot be applied to this board without confirmation.
No public schematic or confirmed sensor BOM for this board revision was found
in the sources reviewed. This is a search outcome, not a claim none exists.

## Evidence From This Project

- `Src/main.c`: 7-bit I2C address 0x48 on PB6/PB7; two bytes from register 0x00;
  signed raw16 interpreted as degrees C times 256.
- Initialization configures I2C pins, with no sensor configuration/shutdown write.
- `docs/battery_run_24h_2026-08-17.md` records observed steps of 0.0625 degrees C.
- The label "LM75-compatible" in our code describes an assumed interface/format,
  not a verified LM75 manufacturer or part number.

These facts are consistent with several digital temperature sensors. The
factory's 0.1-degree output specification does not establish internal ADC
resolution. Similar numerical output does not prove identical configuration
registers or shutdown commands.

## Candidate Families, Not Identification

[TI TMP102 datasheet](https://www.ti.com/lit/ds/symlink/tmp102.pdf): 12-bit output
with 0.0625-degree steps, a possible address of 0x48, temperature register 0x00,
small six-pin SOT563 package and shutdown/one-shot support. These match some
observations but do not establish that a TMP102 is fitted.

[TI TMP112 family datasheet](https://www.ti.com/lit/ds/symlink/tmp112.pdf): also
supports this address/output format in relevant variants. Configuration is
16-bit, with a documented reset pattern of 0x60A0. Compatible parts can share
that pattern: one matching value is not a unique electronic identification.

[TI TMP75/TMP175 datasheet](https://www.ti.com/lit/ds/symlink/tmp75.pdf): another
example of LM75-style sensors supporting 0.0625-degree output, but with an
8-bit configuration register, selectable resolution and different conversion
timing. Not all members of an interface family have the same defaults.

Do not send a guessed shutdown or reset command based solely on address 0x48.

## Implications and Next Step

The current logger interval is 5 seconds (earlier battery test: 10 seconds).
That is respectively six or three times the sample frequency at the factory
minimum interval of 30 seconds. This does NOT imply six or three times the
total current: baseline current and energy per measurement must be separated.

The factory 12-month life cannot be carried over to our interval and custom
firmware. Factory accuracy/calibration also cannot be assumed preserved after
firmware replacement without validating calibration handling and measurements.

The documents found do not establish whether 16 mA from the battery branch with
USB connected is normal. They cannot prove defective isolation or diagnose a
specific GPIO setting. Sensor sleep remains a possibility, not a promised cure.

Recommended next development task: add a bounded, manually requested sensor
diagnostic read to the existing firmware. Compare selected common registers
against candidate datasheets, including width and reserved-bit patterns;
restore the temperature pointer afterwards. Do not change configuration,
thresholds, conversion mode, reset the sensor or scan arbitrary commands.
Register selection writes a pointer byte on I2C, not configuration payload.
Reads can have model-specific status side effects; review candidate behavior
and ensure the task does not interfere with scheduled measurements.

An electronic fingerprint may narrow the family even with no visible marking,
but may not distinguish compatible devices. Only after sufficient confidence
should shutdown/one-shot be implemented, with conversion-time handling, timeout,
fallback to the previous mode and a same-settings meter comparison.
