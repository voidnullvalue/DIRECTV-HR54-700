/*
 * nvtool - inspect / patch the HR54 NVRAM "WIFI" object.
 *
 * The saved wireless profile is not a file: it lives in NVRAM and is reached
 * through libDtvNVRamMgr.so (NVGetObject / NVSetObject).  The object is an
 * opaque, versioned struct with no published field names, so this tool dumps
 * raw bytes and (once the layout is known) patches fields in place.
 *
 * build:
 *   zig cc -target mips-linux-musleabi -mcpu=mips32 -static -O2 \
 *       wifi/nvtool.c -o wifi/nvtool
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

#define NVRAM_LIB "/opt/nvram/lib/libDtvNVRamMgr.so"
#define OBJSZ 8192

typedef int (*nvinit_t)(void);
typedef int (*nvget_t)(const char *, void *, int, void *);
typedef int (*nvset_t)(const char *, void *, int, void *);

static void hexdump(const char *tag, const unsigned char *b, int n)
{
	int i, j;
	printf("---- %s: %d bytes ----\n", tag, n);
	for (i = 0; i < n; i += 16) {
		printf("%04x  ", i);
		for (j = 0; j < 16; j++)
			printf(i + j < n ? "%02x " : "   ", b[i + j]);
		printf(" |");
		for (j = 0; j < 16 && i + j < n; j++) {
			unsigned char c = b[i + j];
			printf("%c", (c >= 32 && c < 127) ? c : '.');
		}
		printf("|\n");
	}
}

/* print every printable run of >=4 chars, with its offset */
static void printstrings(const unsigned char *b, int n)
{
	int i = 0;
	while (i < n) {
		int j = i;
		while (j < n && b[j] >= 32 && b[j] < 127)
			j++;
		if (j - i >= 4) {
			printf("  str @%04x: \"%.*s\"\n", i, j - i, b + i);
			i = j;
		} else {
			i++;
		}
	}
}

int main(int argc, char **argv)
{
	static const char *names[] = {
		"WIFI", "Wifi", "wifi", "NV_OBJ_WIFI", "WLAN", "NETWORK", NULL
	};
	static unsigned char buf[OBJSZ];
	void *h;
	nvinit_t NVInit;
	nvget_t NVGetObject;
	int i, r, dumpbytes = 512;

	if (argc > 1)
		dumpbytes = atoi(argv[1]);
	if (dumpbytes <= 0 || dumpbytes > OBJSZ)
		dumpbytes = OBJSZ;

	h = dlopen(NVRAM_LIB, RTLD_NOW);
	if (!h) {
		printf("dlopen failed: %s\n", dlerror());
		return 2;
	}
	NVInit = (nvinit_t)dlsym(h, "NVInit");
	NVGetObject = (nvget_t)dlsym(h, "NVGetObject");
	if (!NVGetObject) {
		printf("dlsym NVGetObject failed: %s\n", dlerror());
		return 2;
	}
	printf("dlopen ok, NVInit=%s\n", NVInit ? "yes" : "no");
	if (NVInit)
		printf("NVInit -> %d\n", NVInit());

	for (i = 0; names[i]; i++) {
		memset(buf, 0, sizeof buf);
		r = NVGetObject(names[i], buf, (int)sizeof buf, 0);
		printf("== NVGetObject(\"%s\") -> %d\n", names[i], r);
		if (r != 0)
			continue;
		hexdump(names[i], buf, dumpbytes);
		printstrings(buf, dumpbytes);
		/* only the first hit needs a full dump */
		dumpbytes = OBJSZ;
		printf("---- printable runs in full %d-byte buffer ----\n", OBJSZ);
		printstrings(buf, OBJSZ);
		break;
	}
	return 0;
}
