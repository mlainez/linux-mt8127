# Contributing to this MT8127 kernel fork (humans and AI agents)

This is a downstream Linux kernel fork for the **MediaTek MT8127** (Cortex-A7)
and the **Lenovo Tab3 7 Essential (TB3-710F)**. It tracks mainline LTS stable
branches directly and owns its patches. It feeds the `buildroot-android`
project (`buildroot-external/configs/tb3_710f_defconfig`).

Read this file before creating branches or commits.

---

## 1. Branching strategy

Names are **version-first**, slash-namespaced. The SoC is the whole fork, so
it is *not* repeated in branch names; the **kernel version is the top axis**
because several can be maintained in parallel.

```
<kver>/topic/<feature>   →   <kver>/staging   →   <kver>/rc   →   <kver>/release
        (one feature)          (integrate all)      (candidate)      (blessed)
```

### Series

| Series | Role |
|---|---|
| **6.18** | current LTS (projected EOL Dec 2028): **the series to ship from** |
| **7.4** | the release that is expected to become the next LTS (last release of 2026). Open `7.4/baseline` once 7.4 is out and port the topics (§2, "Porting"). |

Stick to the latest LTS, or the release that is going to become one.

| Tier | Example | Purpose | Lifecycle |
|------|---------|---------|-----------|
| **base** | `stable/linux-6.18.y` | the stable branch topics fork from | tracked; never committed to. Topics are pinned to a tested base tag (e.g. `v6.18.55`); advancing the base is a deliberate, re-tested step. |
| **baseline** | `6.18/baseline` | stable + fork meta (this file, CI) | receives automated stable merges (§5) |
| **topic** | `6.18/topic/mt8127-usb2-phy` | exactly one feature/fix-set; clean, rebasable, **upstreamable** | long-lived; rebased onto base |
| **staging** | `6.18/staging` | merge **all** topics; flashed to the tablet for combined testing; receives automated stable merges | fast-moving; may be reset/rebuilt |
| **rc** | `6.18/rc` | release candidate | promoted from staging when it passes on-device validation (§1.1) |
| **release** | `6.18/release` | blessed, stable; what ships | fast-forwarded from rc when validated; tag releases here (`tb3-710f-6.18-r1`) |

Rules:

- **A topic branch holds one feature and nothing else.** Keep it clean enough
  to submit upstream. No integration merges, other topics or device-only hacks.
- **Do not commit directly to `staging`/`rc`/`release`.** They are built by
  merging topics (`git merge --no-ff <kver>/topic/*`). The only non-topic
  commits allowed on integration branches are fork meta and the automated
  stable merges.
- **Testing a single topic:** build/flash the topic branch directly. Cut a
  throwaway `<kver>/test/<x>` only if a topic needs disposable device hacks;
  exploratory work goes in `<kver>/research/<x>`.
- **Device variants are DTBs, not branches.**
- **What each consumer builds:** buildroot → `<kver>/rc` or `<kver>/release`.
  For test builds pin `BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION` to the **commit
  SHA**: Buildroot's git cache snapshots branch refs and silently reuses stale
  sources.
- **The kernel configuration lives in the buildroot external tree**
  (`board/tb3_710f/linux.config`), not in this repository.

### 1.1 Promotion gates

Promotion is **evidence-driven**; each tier has a gate that must be passed
*on the tablet* before moving up:

1. **fix/feature → topic**: compiles warning-free for its own files with the
   ARM cross toolchain (`arm-linux-gnueabihf-`); one logical change per
   commit (bisectable); DTS changes build their DTB on every commit.
2. **topic → staging**: `git merge --no-ff` into staging, then build a **full
   image** from staging and validate on the tablet. A topic is never
   considered "in" until the *merged* image passed.
3. **staging → rc**: promote (`git branch -f <kver>/rc <kver>/staging`) only
   after:
   - the new feature demonstrably works (not just probes);
   - a **regression sweep** of everything previously working: boot to the
     persistent root, 4 CPUs online, USB networking and ssh, eMMC read and
     write with read-back checksums, no `I/O error` or `msdc` errors, no
     unexpected warnings in `dmesg`;
   - a **stability soak** of **at least 60 minutes** of growing uptime with
     the feature active (full frequency range with DVFS, no pins). This SoC
     can reset *silently* (the bootloader leaves the watchdog armed and a
     bad clock/voltage state shows up as a reboot with no panic). A boot
     that "looks fine" for 60 s is not evidence. Instrument soaks with a
     device-side fsync'd log (uptime, frequency, temperature every 30 s) so
     a reset leaves evidence, and check for unexplained boots.
4. **rc → release**: human decision after wider testing; tag on release.

If a gate fails, the offending topic gets fixed *on the topic branch* and
re-merged; staging may be rebuilt. `rc`/`release` never receive unvalidated
work (the automated stable sync is the sole exception).

### 1.2 Device-testing protocol

- **Always address the tablet by serial**: `fastboot -s <serial>`. The host
  that builds usually has other devices attached; an unqualified `fastboot
  flash` once overwrote another phone. The helper scripts in the buildroot
  external tree (`board/tb3_710f/scripts/flash.sh`, `reflash.sh`) do it and
  refuse to run if another fastboot device is present.
- The stock bootloader (LK) has **no `fastboot boot`**: every test image is
  flashed to the `boot` partition (16 MiB max). The persistent root is on
  `userdata` and is only rewritten deliberately (see the buildroot docs).
- **Verify the built image before flashing**: expected kernel release,
  DTB properties, expected modules.
- **Never test writes on the live root device.** The eMMC runs in SDR mode
  because DDR multi-block writes fail; always verify writes by reading back a
  checksum, and treat any `I/O error, dev mmcblk0` as a failure.
- Kernel-only module iteration on a matching running kernel over ssh is fine.

---

## 2. Commit conventions

Follow the upstream rules — <https://www.kernel.org/doc/html/latest/process/submitting-patches.html>:

- **Subject:** `subsystem: imperative summary`, ≤ ~75 chars, lower-case after
  the prefix, no trailing period. Match the prefix the subsystem already uses
  (`phy: mediatek:`, `ARM: dts: mediatek:`, `soc: mediatek:`, …).
- **Body:** wrapped at ~75 columns; explain **what and why** (and user-visible
  impact / trade-offs), not a line-by-line "how". Reference commits as
  `<12+ hex> ("oneline subject")`.
- **One logical change per commit.** Each must build on its own (bisectable).
- **`Fixes:`** for regressions/bugfixes: `Fixes: <12+ hex> ("subject")`.
  Add `Link:`/`Closes:` (prefer lore.kernel.org) when there's a discussion.

### Author identity and trailers (fork policy)

- **Work authored here** is committed as `Marc Lainez <marc.lainez@gmail.com>`.
- **Existing commits from other trees are cherry-picked, not rewritten**:
  `git cherry-pick -x` preserves the original author and records provenance.
- **Never add a new `Signed-off-by:`.** These are fork branches, not upstream
  submissions, so no DCO is required. Trailers already present on
  cherry-picked commits are kept as-is.
- **No co-authorship** — never `Co-developed-by:`/`Co-authored-by:`, and the AI
  is never a co-author.
- **Do add an AI-assistance disclosure** on Marc-authored commits when an
  assistant materially helped, per the kernel.org convention — a single
  `Assisted-by:` trailer (disclosure, not co-authorship):

  ```
  Assisted-by: Claude:claude-sonnet-5-5
  ```

  Append specialised analysis tools actually used (sparse, coccinelle…), not
  git/gcc/make. A `Signed-off-by:` is a human action taken at upstream
  submission time, never by the assistant.

### Porting a feature set between kernel versions

When bringing a topic from one series to another (e.g. `6.18/topic/x` →
`7.4/topic/x`):

1. Identify the **complete** commit chain, including base-infrastructure
   commits the topic silently depends on.
2. Cherry-pick with `-x` in original order onto the new base tag.
3. A **clean apply is not a correct port** — surrounding code drifts. Compile
   the touched objects, and treat renames and moved tables as review points.
4. Validate on the tablet through the §1.1 gates like any new topic.

---

## 3. Kernel policy for AI-assisted work

Per <https://www.kernel.org/doc/html/latest/process/coding-assistants.html>,
when work from this fork is submitted **upstream**:

- every new file gets a correct `SPDX-License-Identifier`; code is
  GPL-2.0-only compatible;
- an AI agent must never add `Signed-off-by`; the human submitter adds their
  own and takes responsibility for the code;
- AI assistance is disclosed with the `Assisted-by:` trailer (§2).

Follow the kernel coding style (`scripts/checkpatch.pl --strict` on every
patch before it is promoted) and `submitting-patches`.

---

## 4. Hardware notes that affect the work

- The SoC has no clock, pinctrl or power-domain driver in this tree yet.
  Everything runs on the state the bootloader leaves behind, with fixed clocks
  in the device tree (see `arch/arm/boot/dts/mediatek/mt8127-lenovo-tb3-710f.dts`).
- The downstream vendor kernel (3.10.54, `szazs89/kernel_lenovo_TB3_710F`) is the
  reference for register layouts and sequences; port behaviour from it, never code.
- MT8127 uses the MT6323 PMIC through the SoC's PMIC wrapper; the wrapper is
  initialised by the preloader, and the mainline driver skips its init in that case.

## 5. Stable sync

`.github/workflows/stable-sync.yml` merges `linux-6.18.y` into `6.18/baseline`,
`6.18/staging` and `6.18/rc` daily. A clean merge is pushed directly; a
conflict opens an issue and leaves the branch untouched — resolve it manually
on a topic/integration round. Topic branches are not synced: they move to a
new base tag deliberately.
