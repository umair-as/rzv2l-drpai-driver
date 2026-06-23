# rzv2l-drpai-driver

Out-of-tree Linux kernel module for the **Renesas RZ/V2L DRP-AI** AI
accelerator, forward-ported to recent kernels (6.x).

## Provenance

The driver source (`drpai-core.{c,h}`, `drpai-if.c`, `drpai-reg.h`,
`include/linux/drpai.h`, `include/uapi/linux/drpai.h`) was obtained from the
**Renesas RZ/V2L AI SDK** source release available on the Renesas portal (the
full source zip) — specifically the GPL-2.0 DRP-AI kernel driver carried in
that release's Yocto recipe package. The driver is © 2021 Renesas Electronics
Corporation.

`drpai-core.{c,h}`, `drpai-reg.h`, and both `drpai.h` headers are
byte-identical to the Renesas source. `drpai-if.c` carries a single documented
change — the `class_create()` forward-port described under *My modifications* —
and is otherwise identical. The initial commit holds the verbatim import; the
follow-up commit adds that forward-port plus this README, `Kbuild`, `Makefile`,
and `.gitignore` (all mine, not Renesas's).

## License

**GPL-2.0** (`MODULE_LICENSE("GPL v2")`; see `COPYING`). The UAPI header
`include/uapi/linux/drpai.h` is `GPL-2.0 WITH Linux-syscall-note`. GPL-2.0
permits redistribution and modification — this repository is one such
redistribution. A public mirror of the same Renesas driver already exists at
`Vekatech/renesas-rz_linux-cip` (branch `vkrzv2l_v2l_v5.10`, an older 5.10
snapshot), which serves as redistribution precedent.

## My modifications

- **Repackaged as a single out-of-tree module.** Upstream builds the two
  objects into vmlinux (`CONFIG_DRPAI=y`); here they link into one `drpai.ko`.
  See `Kbuild` for why.
- **Forward-port to Linux 6.12** (builds clean against linux-cip 6.12; the
  changes below are version-guarded where the API change is version-specific, so
  the source still builds against the original 6.1 tree):
  - `class_create()` lost its `struct module *owner` arg in 6.4 (commit
    `1aaba11da9aa`) — guarded.
  - `DEFINE_SEMAPHORE()` gained an initial-count arg in 6.4 — guarded.
  - `platform_driver::remove` became `void`-returning in 6.11 — guarded
    signature + return.
  - Added `#include <linux/vmalloc.h>` (`vmalloc`/`vfree` are no longer pulled
    in transitively on 6.x).
  - Reordered six `const static` declarations to `static const`
    (`-Wold-style-declaration`, always valid).

Note: `drpai.ko` also needs two arm64 point-of-coherency cache ops
(`dcache_clean_inval_poc`, `dcache_inval_poc`) that the in-tree driver reaches
from vmlinux but are not exported to modules. Exporting them is a kernel-side
change and lives in the consuming BSP, not here.

## Status

**Unmaintained. No support. No warranty.** Carried for my own RZ/V2L board
bring-up on a linux-cip 6.12 boot chain. Not affiliated with or endorsed by
Renesas. Use at your own risk.

## Build

Standalone cross build against a configured kernel tree:

```sh
make KERNELSRC=/path/to/linux ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
```

Under Yocto, a `kernel-module-drpai` recipe drives the build against
`virtual/kernel` instead.

## Runtime

The module registers a platform driver matching DT `compatible =
"renesas,rzv2l-drpai"` and creates the char device **`/dev/drpai0`**. The
consuming system must provide the DRP-AI device-tree node plus its
`reserved-memory` (CMA) region; those are board integration and live in the
board's BSP, not here.
