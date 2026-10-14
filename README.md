# rzv2l-drpai-driver

Out-of-tree Linux kernel module for the **Renesas RZ/V2L DRP-AI** AI
accelerator, forward-ported to recent kernels (6.x).

## Provenance

The driver source (`drpai-core.{c,h}`, `drpai-if.c`, `drpai-reg.h`,
`include/linux/drpai.h`, `include/uapi/linux/drpai.h`) was obtained from the
**Renesas RZ/V2L AI SDK** source release available on the Renesas portal (the
`*_linux-src.zip`), specifically the GPL-2.0 DRP-AI kernel driver carried in
that release's Yocto recipe package. The driver is © 2021 Renesas Electronics
Corporation.

Two Renesas releases are imported verbatim, each in its own commit:

- `933679e`: driver **2.20 rel.3**, from AI SDK **v7.00** (2026-03-10), where it
  shipped as a kernel patch (`0007-enable-drpai-drv.patch`).
- `c4e6a59`: driver **2.30 rel.1**, from AI SDK **v8.00** (2026-10-08), where it ships as
  the `kernel-module-drpai` recipe (`v2l/` sources). This is the current base.

`drpai-core.h`, `drpai-reg.h`, and both `drpai.h` headers are byte-identical to
the v2.30 source. `drpai-core.c` carries only the `static const` reorders;
`drpai-if.c` carries the forward-port and the fixes listed under
*My modifications*. The fixes are marked with `edge fork:` comments in the
source (smaller follow-on edits, such as error-path cleanups, are not each
marked), named after [edge-ai-yocto](https://github.com/umair-as/edge-ai-yocto), the project this fork is maintained
for (see *Status*). Everything that is
not a verbatim import is mine, not Renesas's: the forward-port, the fixes,
this README, `Kbuild`, `Makefile`, `.gitignore` and `tests/`.

## License

**GPL-2.0** (`MODULE_LICENSE("GPL v2")`; see `COPYING`). The UAPI header
`include/uapi/linux/drpai.h` is `GPL-2.0 WITH Linux-syscall-note`. GPL-2.0
permits redistribution and modification — this repository is one such
redistribution.

## My modifications

- **Repackaged as a single out-of-tree module.** Upstream builds the two
  objects into vmlinux (`CONFIG_DRPAI=y`); here they link into one `drpai.ko`.
  See `Kbuild` for why.
- **Forward-port to Linux 6.12** (builds clean against linux-cip 6.12; the
  changes below are version-guarded where the API change is version-specific,
  so the source should still build against the original 6.1 tree — not
  re-verified):
  - `class_create()` lost its `struct module *owner` arg in 6.4 (commit
    `1aaba11da9aa`) — guarded.
  - `DEFINE_SEMAPHORE()` gained an initial-count arg in 6.4 — guarded.
  - `platform_driver::remove` became `void`-returning in 6.11 — guarded
    signature + return.
  - Added `#include <linux/vmalloc.h>` (`vmalloc`/`vfree` are no longer pulled
    in transitively on 6.x).
  - Reordered six `const static` declarations to `static const`
    (`-Wold-style-declaration`, always valid).
  - `Kbuild` defines `DRPAI_CONFIG_ARCH_RZV2L`, which v2.30 uses to select the
    RZ/V2L code (Renesas's own module Makefile sets it).
- **Hardening of the vendor code** (all of it applies to v2.30 as well as
  v2.20; Renesas's v2.30 changes none of these paths) (`drpai-if.c`; none of these change the
  ioctl numbers or the UAPI structs):
  - `PREPOST_CROP` / `PREPOST_INADDR` bound the `OFFSET_ADD` taken from the
    caller's `*_param_info.txt` to the parameter object. Before, any opener could
    write chosen values anywhere in the kernel linear map.
  - Dynamic-param offsets are checked in 64 bits (they wrapped at 32).
  - `START` checks the AI-mode input buffer (carveout or the DT
    `linux-memory-region`, and mapped), minimum descriptor and `drp_param`
    sizes, and DRP-mode cfg/param addresses (carveout; `DRPAI_DRP_NOLOAD` still
    accepted). `PREPOST_INADDR` checks its input buffer the same way.
  - DRP-mode `START` no longer moves the global descriptor DMA handle, and the
    buffer is freed with the address it was allocated with.
  - `SET_SEQ` is validated before it is committed, and is serialised with
    `START`.
  - `release` always unlinks before freeing, and no path releases a semaphore
    it did not take (close, `PREPOST_CROP`, `PREPOST_INADDR`, register-dump
    `read()`).
  - Register-dump `read()` no longer overruns its buffer on a count that is not
    a multiple of 4.
  - `poll_wait()` is called outside the IRQ-safe spinlock.
  - Unknown ioctls return `-ENOTTY`; the `PREPOST_INADDR` node name is
    NUL-terminated.
  - `probe` propagates errors and creates `/dev/drpai0` last; sysfs unbind is
    disabled.
- **Behaviour changes callers may notice** (no public Renesas userspace is
  affected): `SET_SEQ` with `num == 0` returns `-EINVAL`; a register-dump
  `read()` shorter than 4 bytes returns `-EINVAL`; in DRP mode only the cfg
  slots accept `DRPAI_DRP_NOLOAD` (0), a param slot with address 0 is rejected;
  `close()` now waits for the driver's semaphores uninterruptibly instead of
  giving up after 1 s (no path holds them across a running job).
- **Fix for a v2.30 regression:** after a dynamic write, v2.30 synced the
  static path's size (normally 0) instead of the dynamic buffer's, so the
  descriptor and parameter data were not cleaned from the CPU cache before the
  DRP-AI read them.

v2.30 does its cache maintenance through the DMA API
(`dma_sync_single_for_{device,cpu}`), so unlike v2.20 the module no longer
needs the kernel to export the arm64 `dcache_*_poc` ops.

## Status

**Unmaintained. No support. No warranty.** Carried for
[edge-ai-yocto](https://github.com/umair-as/edge-ai-yocto), my Yocto BSP and edge-AI distribution for the
RZ/V2L, on a linux-cip 6.12 boot chain. Not affiliated with or endorsed by
Renesas. Use at your own risk.

**Treat access to `/dev/drpai0` as equivalent to root.** The hardening above
closes the direct kernel-memory writes, but the descriptors and parameters the
DRP-AI executes are written by userspace and their DMA addresses are not
validated, and the RZ/V2L has no IOMMU in front of the DRP-AI. So whoever can
open the device can still make the engine read or write any physical memory.
Grant the device only to a dedicated group holding the trusted inference
principal, not to a shared group such as `render`. Validating the descriptor
contents would need the descriptor format pinned down for every descriptor
type; it is not done here.

## Build

Standalone cross build against a configured kernel tree:

```sh
make KERNEL_SRC=/path/to/linux ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
```

Under Yocto, a `kernel-module-drpai` recipe drives the build against
`virtual/kernel` instead. edge-ai-yocto's recipe is
[`kernel-module-drpai_git.bb`](https://github.com/umair-as/edge-ai-yocto/blob/main/meta-edge-bsp/recipes-kernel/kernel-module-drpai/kernel-module-drpai_git.bb).

## Runtime

The module registers a platform driver matching DT `compatible =
"renesas,rzv2l-drpai"` and creates the char device **`/dev/drpai0`**. The
consuming system must provide the DRP-AI device-tree node plus its dedicated
`reserved-memory` carveout — the driver reads the region's base/size directly
(`of_address_to_resource`). It is a plain reservation (`memblock_reserve`d, not a
CMA pool) because the DT node has no `shared-dma-pool` compatible; those
are board integration and live in the board's BSP, not here. In edge-ai-yocto
the device-tree node and carveouts come from
[this kernel patch](https://github.com/umair-as/edge-ai-yocto/blob/main/meta-edge-bsp/recipes-kernel/linux/files/patches/0006-arm64-dts-rzv2l-smarc-add-drpai-udmabuf-reserved-memory.patch),
and the device-node permissions from
[this udev rule](https://github.com/umair-as/edge-ai-yocto/blob/main/meta-edge-bsp/recipes-support/edge-drpai-udev/files/71-edge-drpai.rules).

## Tests

`tests/drpai-hardening-test.c` sends one malformed request per fixed input
check and expects it to be rejected. Run it on the target as a user that can
open `/dev/drpai0`, while no other client has it open; it uses scratch space 256 MiB
into the carveout. `--vendor` skips the cases that would corrupt kernel memory
or start the engine on an unfixed driver, so the remaining cases can be run
there to show the original behaviour. The build command is in the file header.
