# Two to Tango

> [!WARNING]
> This is very much beta. It works on my bench radio, but it hasn't been on many other Tango 2s yet, so expect rough edges. The TBS bootloader is never touched, so holding E and plugging in USB always gets you back to a place you can flash from. If something goes sideways, please open an issue and tell me what happened.

EdgeTX for the TBS Tango 2, with TBS Crossfire *and* ExpressLRS resident in flash on the internal module. Pick the link per model in **Model Setup → Internal RF** (the second field on the Mode line, `ELRS` or `XFire`) and the radio swaps between them in about a second, no reboot.

If you just want it on your radio, use the web flasher at [tools.mustardtiger.xyz/two-to-tango](https://tools.mustardtiger.xyz/two-to-tango/). This repo is for building it yourself.

## What's in here

* `edgetx/` - submodule, EdgeTX on the [`tango2` branch](https://github.com/MUSTARDTIGERFPV/edgetx/tree/tango2) of my fork. The target lives in `radio/src/targets/tango2/`.
* `elrs/ExpressLRS/` - submodule, ExpressLRS on the [`tango2` branch](https://github.com/MUSTARDTIGERFPV/ExpressLRS/tree/tango2): SX1272 support and a handful of platform hooks.
* `elrs/` - builds that ExpressLRS tree into a flash image EdgeTX hosts (`build.py`, `platform/`).
* `abi/elrs_image.h` - the contract between EdgeTX and the ELRS image.
* `loader/` - a small resident loader that sits between the TBS bootloader and EdgeTX. It boots EdgeTX and flashes everything over USB.
* `tools/` - `tango2.py` (TBS bootloader and loader), `make_slot.py` (packs the Crossfire and ELRS images), and some CLI helpers.

No TBS code lives here. You bring the Crossfire image yourself, straight from TBS (see below).

## Flash layout

| Address | Size | What |
|---|---|---|
| `0x08000000` | 48 KB | TBS bootloader, never touched |
| `0x0800C000` | 80 KB | resident loader |
| `0x08020000` | 640 KB | EdgeTX |
| `0x080C0000` | 160 KB | TBS Crossfire 6.30 |
| `0x080E8000` | 96 KB | ExpressLRS |

Only one radio image runs at a time; they share the RAM at `0x20015000`-`0x2001FFFF`.

## Building

You need PlatformIO (`pio`), CMake, Ninja, and Python 3 with `requirements.txt` installed.

```bash
git clone https://github.com/MUSTARDTIGERFPV/two-to-tango
cd two-to-tango
pip install -r requirements.txt
./build.sh
```

`build.sh` checks out the submodules it needs on the first run (not `--recursive`: FreeRTOS's own submodules aren't used, and their paths are too long for Windows). That leaves `loader.bin`, `edgetx.bin` and `elrs.bin` in `out/`. `./build.sh --phrase "my bind phrase"` bakes a bind phrase into the ELRS image; without it the image is unbound and you bind from the ELRS Lua (or let the web flasher set it).

The loader build pulls PlatformIO's arm-none-eabi-gcc 9.2.1, and the other two builds use that same compiler. EdgeTX officially wants Arm GNU 14.2; this target has only been built and tested on 9.2.1 so far. Set `ARM_TOOLCHAIN_DIR` if you want to try another one. Builds on Windows (Git Bash) and Linux.

## The Crossfire image

Tango 2s shipped with one of two Crossfire radio boards, and TBS publishes a 6.30 build for each:

* `0x000140xx_0x0630.bin` - board V1-V2, drives the SX1272's RFO pin
* `0x000141xx_0x0630.bin` - board V3-V9, drives PA_BOOST. TBS's catalog calls this one "Crossfire Micro TX", which it isn't; TBS Agent's own device table names it `CROSSFIRE_TANGO2_TX_V3_V9`.

The wrong one transmits *nothing*. The TBS bootloader records which board you have at `0x08007F04`:

```bash
python -P tools/tango2.py mon peek 0x08007F04
```

`0x000141xx` there means the 141 file. Both are at `https://firmware.blacksheep.com.sg/firmware/<file>`. EdgeTX checks for exactly these two builds and refuses anything else.

## Flashing by hand

First time, the loader goes on through the TBS bootloader. Radio fully off (USB unplugged, battery out), hold the E button on the back (right side, under your middle finger), plug in USB, and keep holding until your computer makes its USB connect noise. That can take almost 10 seconds. Then:

```bash
python -P tools/tango2.py flash out/loader.bin
python tools/make_slot.py 0x000141xx_0x0630.bin out/elrs.bin slot.bin
python -P tools/tango2.py mon flash-image slot.bin --erase-tbs
python -P tools/tango2.py mon flash-app out/edgetx.bin
python -P tools/tango2.py mon boot
```

After that, `tango2.py mon ...` reboots a running EdgeTX into the loader on its own, so updates don't need the button.

## Status

Working on the bench: live switching both ways, ELRS linked at LQ 100 to a SuperP, Crossfire linked to a Nano RX and updated it over the air, telemetry in EdgeTX.

Not done yet:
* Crystal correction is hard-coded to my unit (-15.9 ppm); it should come from radio settings.
* Gimbals under Crossfire at full deflection haven't been checked.
* Only FCC915 on the ELRS side so far.

## License

GPLv3. EdgeTX is GPLv2 and ExpressLRS is GPLv3; their licenses apply to their trees.
