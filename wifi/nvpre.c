/*
 * nvpre.so - LD_PRELOAD shim for poking libDtvNVRamMgr.so on the HR54.
 *
 * Why a preload instead of a normal program: the receiver runs uClibc and the
 * only compiler available here targets musl.  A statically linked musl binary
 * cannot dlopen() ("Dynamic loading not supported"), so a standalone tool cannot
 * bind libDtvNVRamMgr.so at runtime.  A preloaded shared object has no such
 * problem: the box's own ld-uClibc resolves our undefined symbols against the
 * libraries the host process already mapped, and DT_NEEDED pulls in the NVRAM
 * manager itself.
 *
 * Built -nostdlib on purpose: we must not acquire a DT_NEEDED on musl's libc.so,
 * which does not exist on the receiver.  The few libc calls we make (write) are
 * resolved at load time from the host process's uClibc.  Note the linker drops
 * DT_NEEDED entirely if nothing in here references the library, so NVInit() is
 * always called even when we do not need it.
 *
 * THIS BUILD IS READ-ONLY.  It never calls NVWriteBytesToEEPROM or NVSetObject.
 *
 * What it establishes about the receiver's NVRAM (all verified on the box):
 *   - uClibc PIC: gp is a module-wide constant 0x34020, .got sits at link-time
 *     offset 0x2c030, and GOT[-32724] holds the link-time base 0x10000.  The
 *     load base comes from dladdr(); GOT[n] is reached as got[n] with the
 *     indices below.  Both tables used here live in .data and are built at
 *     runtime, so they can only be read on the box.
 *   - The 32-byte accessor table (GOT_ACCTABLE) is indexed by objType, with the
 *     accessor at entry+20.  Matching that against the exported symbols gives
 *     objType 84 -> WifiAccessor, and entry+0 is the objType number itself,
 *     which is the id NVGetObject wants.  NVGetObject rejects id >= 100 with
 *     code 5 and client >= 9 with code 13, which is why an earlier sweep of
 *     100..65535 uniformly returned 5 and told us nothing.
 *   - NVGetObject is gated: code 3 is "read permission denied" (see
 *     NVCheckReadPermissions) and code 12 means the NVRAM semaphore could not be
 *     taken.  Neither is reachable from an arbitrary process such as sleep, so
 *     the object has to be read with the raw primitives instead.
 *   - NVReadBytesFromEEPROM(offset, len, buf) is the raw reader.  The argument
 *     order matters: an earlier attempt passed the buffer as the 2nd argument,
 *     which returns 0 without writing anything and looks like an empty device.
 *   - The per-objType metadata table (GOT_METATABLE, 20 bytes per entry) holds
 *     the object's EEPROM offset at +4, its stored-checksum offset at +8, the
 *     object size at +12, and the checksum length at +16.
 *   - Checksums are a plain 32-bit byte sum, not a CRC:
 *         size = (meta[12] + 7) & ~7;  read meta[4], size bytes;  sum them.
 *     CalcChecksumEEPROM/GetChecksumEEPROM/UpdateChecksumEEPROM implement it.
 *   - The WiFi object is a flat blob; WifiAccessor memcpys fields in and out of
 *     it using a 3 x 10-byte descriptor table.  Its own disassembly computes the
 *     object offset as (u16_from_table + 15133) & 0xffff, and 15133 == 0x3B1D is
 *     exactly where the old SSID sits in /dev/nds/nvram0, which confirms the
 *     whole chain from accessor to byte offset.
 *   - There is a matching write path: when the accessor's mode word is 1 it
 *     calls NVWriteBytesToEEPROM(offset, size, buf) on the same offset.
 *
 * The device itself turned out to be readable with no library at all:
 * /dev/nds/nvram0 is a 64 KiB char device (major 203) and plain `strings` finds
 * the saved SSID in it, so a backup can be taken with dd and diffed on a host.
 *
 * build: ./build.sh
 * use:   LD_LIBRARY_PATH=/opt/nvram/lib LD_PRELOAD=/var/hr54-transfer/nvpre.so sleep 6
 */
#define NULL ((void *)0)

/* -nostdlib, so no libc headers: declare just what we need.  MIPS is 32-bit. */
typedef unsigned int size_t;
typedef unsigned int uintptr_t;

/* from libDtvNVRamMgr.so */
extern int NVInit(void);
extern int NVReadBytesFromEEPROM(unsigned offset, unsigned len, void *buf);
extern int CalcChecksumEEPROM(int objType, unsigned *result);
extern int GetChecksumEEPROM(int objType, void *buf);

/* from the host process's libc */
extern long write(int fd, const void *buf, unsigned long n);
typedef struct {
	const char *dli_fname;
	void *dli_fbase;
	const char *dli_sname;
	void *dli_saddr;
} Dl_info;
extern int dladdr(const void *addr, Dl_info *info);

#define GOT_OFF 0x2c030
#define GOT_LINKBASE 7	/* GOT[-32724]: link-time base */
#define GOT_ACCTABLE 328	/* GOT[-31440]: 32-byte accessor table */
#define GOT_METATABLE 181	/* GOT[-32092]: 20-byte metadata table */

#define LINKBASE 0x10000
#define STRIDE 32
#define WIFI_OBJTYPE 84
#define MAXBUF 16384

static unsigned char buf[MAXBUF];
static char out[65536];
static int olen;

static Dl_info info_g;
static unsigned load_g;

static void putn(const char *s, int n)
{
	int i;
	for (i = 0; i < n && olen < (int)sizeof(out) - 1; i++)
		out[olen++] = s[i];
}

static void putstr(const char *s)
{
	while (*s && olen < (int)sizeof(out) - 1)
		out[olen++] = *s++;
}

static void puthex(unsigned v, int digits)
{
	static const char *d = "0123456789abcdef";
	int i;
	for (i = digits - 1; i >= 0; i--)
		out[olen++] = d[(v >> (i * 4)) & 0xf];
}

static void putdec(long v)
{
	char tmp[24];
	int n = 0, i;
	if (v == 0) {
		out[olen++] = '0';
		return;
	}
	if (v < 0) {
		out[olen++] = '-';
		v = -v;
	}
	while (v > 0 && n < 20) {
		tmp[n++] = (char)('0' + v % 10);
		v /= 10;
	}
	for (i = n - 1; i >= 0; i--)
		out[olen++] = tmp[i];
}

static void flush(void)
{
	if (olen) {
		write(1, out, (unsigned long)olen);
		olen = 0;
	}
}

static void hexdump(unsigned base, const unsigned char *b, int n)
{
	int i, j;
	for (i = 0; i < n; i += 16) {
		putn("  ", 2);
		puthex(base + (unsigned)i, 4);
		putn("  ", 2);
		for (j = 0; j < 16; j++) {
			if (i + j < n) {
				puthex(b[i + j], 2);
				out[olen++] = ' ';
			} else {
				putn("   ", 3);
			}
		}
		putn(" |", 2);
		for (j = 0; j < 16 && i + j < n; j++) {
			unsigned char c = b[i + j];
			out[olen++] = (c >= 32 && c < 127) ? (char)c : '.';
		}
		out[olen++] = '|';
		out[olen++] = '\n';
		flush();
	}
}

static void strings(unsigned base, const unsigned char *b, int n, int min)
{
	int i = 0, j;
	while (i < n) {
		j = i;
		while (j < n && b[j] >= 32 && b[j] < 127)
			j++;
		if (j - i >= min) {
			putn("  @", 3);
			puthex(base + (unsigned)i, 4);
			putstr(": \"");
			putn((const char *)(b + i), j - i);
			putstr("\"\n");
			flush();
			i = j;
		} else {
			i++;
		}
	}
}

static void poison(int n)
{
	int i;
	for (i = 0; i < n; i++)
		buf[i] = 0xa5;
}

static int touched(int n)
{
	int i;
	for (i = 0; i < n; i++)
		if (buf[i] != 0xa5)
			return 1;
	return 0;
}

__attribute__((constructor)) static void nvpre_init(void)
{
	unsigned *got;
	unsigned char *tbl, *e;
	unsigned dataoff, csumoff, datasize, csumlen;
	unsigned sum = 0, stored = 0, mine = 0;
	int rc, i;

	write(1, "nvpre: ctor entered (read-only)\n", 31);
	flush();
	if (dladdr((void *)(size_t)&NVInit, &info_g) == 0 || !info_g.dli_fbase) {
		write(1, "nvpre: dladdr failed\n", 21);
		return;
	}
	load_g = (unsigned)(size_t)info_g.dli_fbase;
	got = (unsigned *)(size_t)(load_g + GOT_OFF);

	putstr("nvpre: read-only probe; load=0x");
	puthex(load_g, 8);
	putstr(" linkbase=0x");
	puthex(got[GOT_LINKBASE], 8);
	putstr(" NVInit=");
	putdec(NVInit());
	putstr("\n");
	flush();

	/* which object is this, and which accessor handles it */
	tbl = (unsigned char *)(size_t)got[GOT_ACCTABLE];
	e = tbl + WIFI_OBJTYPE * STRIDE;
	putstr("nvpre: objType ");
	putdec(WIFI_OBJTYPE);
	putstr(" -> NVGetObject id 0x");
	puthex(*(unsigned *)e, 8);
	putstr(", accessor at link 0x");
	puthex(*(unsigned *)(e + 20) - load_g, 6);
	putstr(" (WifiAccessor is 0xc704)\n");
	flush();

	/*
	 * The metadata table read via GOT_METATABLE came back as garbage, so use
	 * the offset that two independent routes agree on instead: WifiAccessor
	 * computes it as (u16_table + 15133) & 0xffff, and 15133 == 0x3B1D is
	 * exactly where the saved SSID sits in the NVRAM image.  Read it back
	 * through the library with the correct argument order and check that the
	 * byte sum reproduces the checksum the library itself reports.
	 */
	dataoff = 0x3b1d;
	datasize = 256;
	csumoff = 0x3b19;
	csumlen = 4;
	putstr("nvpre: object at EEPROM 0x");
	puthex(dataoff, 4);
	putstr(" (from WifiAccessor's 15133 constant)\n");
	flush();

	poison(MAXBUF);
	rc = NVReadBytesFromEEPROM(dataoff, datasize, buf);
	putstr("nvpre: NVReadBytesFromEEPROM(0x");
	puthex(dataoff, 4);
	putstr(", ");
	putdec(datasize);
	putstr(", buf) = ");
	putdec(rc);
	putstr(touched(MAXBUF) ? "  [buffer written]\n" : "  [buffer UNTOUCHED]\n");
	flush();
	if (touched(MAXBUF)) {
		putstr("---- object head (offset 0x3b1d) ----\n");
		hexdump(dataoff, buf, 112);
		putstr("---- printable runs >= 3 ----\n");
		strings(dataoff, buf, datasize, 3);
		/* sum over every length the checksum is consistent with */
		putstr("---- byte sums ----\n");
		for (i = 96; i <= 104; i++) {
			unsigned t = 0;
			int j;
			for (j = 0; j < i; j++)
				t += buf[j];
			putstr("  sum of first ");
			putdec(i);
			putstr(" bytes = 0x");
			puthex(t, 8);
			putstr("\n");
		}
		for (i = 248; i <= 256; i += 8) {
			unsigned t = 0;
			int j;
			for (j = 0; j < i; j++)
				t += buf[j];
			putstr("  sum of first ");
			putdec(i);
			putstr(" bytes = 0x");
			puthex(t, 8);
			putstr("\n");
		}
		flush();
	}

	/* the 4 checksum bytes that precede the object */
	poison(MAXBUF);
	rc = NVReadBytesFromEEPROM(csumoff, 4, buf);
	putstr("nvpre: checksum field at 0x");
	puthex(csumoff, 4);
	putstr(" read=");
	putdec(rc);
	putstr(" value=0x");
	puthex(*(unsigned *)buf, 8);
	putstr("\n");
	flush();

	rc = CalcChecksumEEPROM(WIFI_OBJTYPE, &sum);
	putstr("nvpre: CalcChecksumEEPROM(");
	putdec(WIFI_OBJTYPE);
	putstr(", &sum) = ");
	putdec(rc);
	putstr("  computed 0x");
	puthex(sum, 8);
	putstr("\n");
	putstr("\nnvpre: done\n");
	flush();
}
