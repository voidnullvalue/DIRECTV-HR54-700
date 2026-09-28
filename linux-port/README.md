# HR54 Linux-port roadmap

Current gate: deterministic boot-time root execution, the v3 LAN beachhead, and persistent inventory collection are confirmed on hardware. The live receiver is BCM7346B2 with dual BMIPS5000 cores, 1024 MiB physical RAM, and 544 MiB available to Linux after vendor multimedia reservations.

Completed sequence:

1. V3 root shell at TCP/5777, TFTP at UDP/1069, and `/var/hr54-inventory` all completed on hardware.
2. The live inventory is retained as `logs/live-hr54-inventory-v3.tar` and extracted alongside it.
3. Stock kexec is unavailable: the only kexec kallsyms are weak stubs and a deliberately invalid MIPS o32 `kexec_load` request returned `ENOSYS` (exit 89), not `EINVAL`.

Next research sequence:

1. Identify a RAM-only bootstrap alternative compatible with BCM7346B2 and this production 3.3.8 kernel; do not modify CFE, MTD, or partition 4.
2. Bring up a minimal custom kernel in this order: CPU, RAM, interrupts, timer, UART; then SATA, Ethernet, USB, watchdog, and RTC. Buildroot is the intended first modern userspace.

Beachhead source and build artifacts are under `linux-port/beachhead/`. The on-disk plugin is documented in `poc-hardware/STATE.md`; it preserves the original vendor indexer and library after launching the root services.

Known base platform facts: BCM7346B2 STB platform; dual Broadcom BMIPS5000 V1.1 cores; 32-bit big-endian MIPS32 o32; vendor Linux 3.3.8-3.0; uClibc 0.9.32.1; 1024 MiB physical RAM; and 544 MiB Linux-available RAM after 192 MiB, 224 MiB, and 60 MiB vendor reservations. Board-level bootstrap details remain to be mapped.
