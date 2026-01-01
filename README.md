# SD Check (Nintendo Switch Homebrew)

SD Check is a lightweight **readability / integrity spot-checker** for Nintendo Switch SD cards (libnx / NRO).
It is designed to help you detect **read failures, slow/stalling reads, and “flaky” files** by actually opening and reading data from the filesystem.

> SD Check is **not** a repair tool. It will not fix FAT/exFAT corruption and it will not “heal” a failing card.

---

## What it does

- **Quick Check**
  - Verifies SD access (`sdmc:/`).
  - Optionally prints a small **root directory listing** (first 12 entries).
  - Optionally performs a small **write → read → verify** test using a temporary 4 KiB file.

- **Deep Check**
  - Recursively traverses a target directory and attempts to **open/stat/read** files.
  - Tracks:
    - open / stat / path errors
    - **persistent read errors**
    - **transient read errors** (recovered by retry)
    - **consistency mismatches** (optional)
    - performance histogram (MiB/s), stalls, longest operation
  - Produces a results screen plus a **2-page summary** (largest files, top failing paths, first failure context, etc.).

- **Logging & settings**
  - Keeps an in-session ring log and can export a log file to the SD root.
  - Settings are persisted and auto-saved.

---

## What it does NOT do

- No filesystem repair (no `chkdsk`/`fsck` equivalent).
- No deletion/renaming/modification of your existing files.
- No “raw block” test: it reads through the filesystem only.

---

## Installation

1. Build `sdcheck.nro` (see **Building** below) or use a prebuilt NRO.
2. Copy `sdcheck.nro` to your SD card, e.g.:
   - `sdmc:/switch/sdcheck.nro`  
   or  
   - `sdmc:/switch/sdcheck/sdcheck.nro`
3. Launch from the Homebrew Menu.

---

## Controls (overview)

The UI is console/text-based. Each screen shows a short control legend in the header.

### Home
- **Up/Down**: Select
- **A**: Start (Quick / Deep)
- **X**: Settings
- **Y**: Log
- **-**: Reset defaults
- **+**: Exit
- **ZL**: Help

### Settings
- **Up/Down**: Select option
- **Left/Right** (or **A** where applicable): Adjust option
- **-**: Reset defaults
- **B / +**: Back
- **Y**: Log
- **ZL**: Help

### During a check
- **Hold B / + / -**: Cancel (confirmation dialog)
- **Y**: Log (Deep Check pauses while viewing log)
- **X**: Pause (Deep Check only)
- **ZL**: Help

### Results
- **R**: Summary pages (2 pages)
- **B / +**: Back
- **X**: Settings
- **Y**: Log
- **ZL**: Help

### Log screen
- **Up/Down**: Scroll
- **L/R**: Page scroll
- **A**: Save log to file
- **-**: Clear log
- **B / +**: Back

---

## Deep Check: read policy

Deep Check reads files according to these rules:

- If **Full read = ON**: files are read fully.
- If **Full read = OFF**:
  - files **≤ Large-file threshold** are read fully
  - files **> Large-file threshold** are read in **sample mode**:
    - reads the first **64 KiB**
    - reads the last **64 KiB** (if the file is larger than 64 KiB)

### Consistency check (optional)
If enabled, SD Check re-reads the same region(s) and compares CRCs:
- in sample mode: verifies the first/last 64 KiB regions
- in full mode: verifies a small region again after the full pass

### Retries
If **Read retries** is > 0, SD Check will retry read operations and counts:
- **Transient read errors**: a read failed but succeeded on retry
- **Persistent read errors**: a read kept failing

---

## Deep scan target

You can choose what Deep Check scans:

- **All** → `sdmc:/`
- **Nintendo** → `sdmc:/Nintendo`
- **emuMMC** → `sdmc:/emuMMC`
- **switch** → `sdmc:/switch`
- **Custom (cfg)** → uses `custom_root` from the config file

Note:
- The **Custom path is read-only in the UI**. Edit it in `sdmc:/switch/sdcheck.cfg`.

---

## Skipping content (filters)

Two optional filters help avoid scanning huge or irrelevant data:

- **Skip known folders** (only when Deep target = All):
  - skips directories containing the segment: `Nintendo`, `emuMMC`, `Emutendo`

- **Skip media extensions**:
  - skips files ending with:
    - `.nsp .nsz .xci .xcz`
    - `.mp4 .mkv .avi .mov .webm`
    - `.iso .bin .img`
    - `.zip .7z .rar`

---

## UI options

- **UI top margin**: pushes the whole UI down (useful if an overlay steals the top lines).
- **UI compact mode**: Deep Check running screen uses a more compact box layout.

---

## Configuration & log files

### Config
- Path: `sdmc:/switch/sdcheck.cfg`
- Loaded automatically at startup and **saved automatically on changes**.

### Log
- Path: `sdmc:/sdcheck.log`
- Saved automatically after checks when possible.
- Can also be saved manually from the Log screen.
- The log file is **overwritten on each save** (single log file).

---

## Config file keys (sdcheck.cfg)

All settings are stored as simple `key=value` lines.
Unknown keys are ignored.

```ini
# Example sdcheck.cfg
preset=1
full_read=0
large_file_limit_mib=256
read_retries=1
consistency_check=0
chunk_mode=0
skip_known_folders=0
skip_media_exts=0
deep_target=0
custom_root=sdmc:/myfolder
write_test=0
list_root=1
ui_top_margin=1
ui_compact_mode=0
