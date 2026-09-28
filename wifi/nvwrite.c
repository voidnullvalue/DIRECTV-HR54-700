/* Guarded, one-shot WiFi NVRAM update through the receiver's vendor library.
 * Build as a -nostdlib preload; never prints the credential.
 * NVRAM_ACTION=restore reverses the edited ranges before reboot.
 */
#define NULL ((void *)0)
typedef unsigned int size_t;
extern int NVInit(void);
extern int NVReadBytesFromEEPROM(unsigned, unsigned, void *);
extern int NVWriteBytesToEEPROM(unsigned, unsigned, const void *);
extern int CalcChecksumEEPROM(int, unsigned *);
extern int GetChecksumEEPROM(int, void *);
extern int open(const char *, int, ...);
extern long read(int, void *, unsigned long);
extern int close(int);
extern long write(int, const void *, unsigned long);
extern char *getenv(const char *);

#define SIZE 65536
#define O_RDONLY 0
static unsigned char original[SIZE], patched[SIZE], live[SIZE];
#ifdef CORRECTION
/* Second, targeted correction after the first credential field was found
 * to start at +0x21. Source includes unrelated receiver boot updates. */
#define SOURCE_PATH "/var/hr54-transfer/nvram0.precorrect.bin"
#define TARGET_PATH "/var/hr54-transfer/nvram0.corrected.bin"
#define SOURCE_SUM 0x0a2d
#define TARGET_SUM 0x09c0
static const unsigned offsets[] = {0x3b3e, 0x3b19};
static const unsigned lengths[] = {13, 4};
#else
#define SOURCE_PATH "/var/hr54-transfer/nvram0.bin"
#define TARGET_PATH "/var/hr54-transfer/nvram0.patched.bin"
#define SOURCE_SUM 0x0851
#define TARGET_SUM 0x0a2d
static const unsigned offsets[] = {0x3b1d, 0x3b3f, 0x3b19};
static const unsigned lengths[] = {10, 12, 4};
#endif
#define RANGES_COUNT (sizeof(offsets)/sizeof(offsets[0]))

static void say(const char *s) {
    const char *p=s;
    while (*p) p++;
    write(1, s, (unsigned long)(p-s));
}
static int same(const unsigned char *a, const unsigned char *b, unsigned n) {
    unsigned i;
    for (i=0; i<n; i++) if (a[i]!=b[i]) return 0;
    return 1;
}
static int load(const char *path, unsigned char *buf) {
    int fd=open(path,O_RDONLY), ok=1;
    unsigned done=0;
    if (fd<0) return 0;
    while (done<SIZE) {
        long n=read(fd,buf+done,SIZE-done);
        if (n<=0) { ok=0; break; }
        done+=(unsigned)n;
    }
    close(fd);
    return ok && done==SIZE;
}
static int sample(void) {
    unsigned char check[128];
    if (!load("/dev/nds/nvram0",live)) return 0;
    if (NVReadBytesFromEEPROM(0x3b19,4,check)!=1 ||
        !same(check,live+0x3b19,4)) return 0;
    if (NVReadBytesFromEEPROM(0x3b1d,128,check)!=1 ||
        !same(check,live+0x3b1d,128)) return 0;
    return 1;
}
static int put(const unsigned char *source) {
    unsigned i;
    for (i=0;i<RANGES_COUNT;i++)
        if (NVWriteBytesToEEPROM(offsets[i],lengths[i],source+offsets[i])!=1)
            return 0;
    return 1;
}
static int checksum(unsigned expected) {
    unsigned calculated=0, stored=0;
    return CalcChecksumEEPROM(84,&calculated)==1 &&
           GetChecksumEEPROM(84,&stored)==1 &&
           calculated==expected && stored==expected;
}
/* The staged image must carry a well-formed credential pair: a non-empty
 * NUL-terminated SSID inside the 32-byte field, a NUL-terminated passphrase
 * inside the 65-byte field, and no bytes past either terminator.
 * This asserts structure only -- no SSID or passphrase value is embedded in
 * this source. patch-nvram.py on the host is what substitutes the operator's
 * own credentials; the build gate fails if the result is malformed. */
static int cred_ok(const unsigned char *img, unsigned soff, unsigned slen,
                   unsigned poff, unsigned plen) {
    unsigned i, n = 0;
    for (i=0;i<slen;i++) { if (img[soff+i]==0) { n=i; break; } }
    if (n==0) return 0;                      /* empty SSID */
    for (i=n+1;i<slen;i++) if (img[soff+i]!=0) return 0;   /* not NUL-padded */
    for (i=0;i<plen;i++) { if (img[poff+i]==0) { n=i; break; } }
    if (n==0) return 0;                      /* empty passphrase */
    for (i=n+1;i<plen;i++) if (img[poff+i]!=0) return 0;
    return 1;
}
#define ssid_field_ok(img) cred_ok((img),0x3b1d,32,0x3b3e,65)

static int intended_patch(void) {
    unsigned i, count=0, sum=0;
    for (i=0;i<SIZE;i++) if (original[i]!=patched[i]) {
#ifdef CORRECTION
        if (!(i>=0x3b19 && i<0x3b1d) &&
            !(i>=0x3b3e && i<0x3b4b)) return 0;
#else
        if (!(i>=0x3b1b && i<0x3b1d) &&
            !(i>=0x3b1d && i<0x3b27) &&
            !(i>=0x3b3f && i<0x3b4b)) return 0;
#endif
        count++;
    }
    for (i=0x3b1d;i<0x3b1d+128;i++) sum+=patched[i];
#ifdef CORRECTION
    if (count!=14 || patched[0x3b4a]!=0)
        return 0;
    for (i=0;i<12;i++)
        if (patched[0x3b3e + i]!=original[0x3b3f + i]) return 0;
    return original[0x3b19]==0 && original[0x3b1a]==0 &&
           original[0x3b1b]==0x0a && original[0x3b1c]==0x2d &&
           patched[0x3b19]==0 && patched[0x3b1a]==0 &&
           patched[0x3b1b]==0x09 && patched[0x3b1c]==0xc0 &&
           original[0x3b7f]==0xfe && patched[0x3b7f]==0xfe &&
           ssid_field_ok(patched) &&
           sum==TARGET_SUM;
#else
    return count==24 && original[0x3b19]==0 && original[0x3b1a]==0 &&
           original[0x3b1b]==8 && original[0x3b1c]==0x51 &&
           patched[0x3b19]==0 && patched[0x3b1a]==0 &&
           patched[0x3b1b]==0x0a && patched[0x3b1c]==0x2d &&
           sum==0x0a2d && patched[0x3b7f]==0xfe &&
           ssid_field_ok(patched) &&
           patched[0x3b4b]==0;
#endif
}
static int allowed_restore(void) {
    unsigned i;
    for (i=0;i<SIZE;i++) if (live[i]!=original[i] && live[i]!=patched[i]) return 0;
    return 1;
}
__attribute__((constructor)) static void run(void) {
    int restore=0, check=0, ok=0;
    char *action=getenv("NVRAM_ACTION");
    if (action && same((unsigned char *)action,(unsigned char *)"restore",8))
        restore=1;
    else if (action && same((unsigned char *)action,(unsigned char *)"check",6))
        check=1;
    else if (!action || !same((unsigned char *)action,(unsigned char *)"apply",6)) {
        say("nvwrite: explicit NVRAM_ACTION required; no write\n"); return;
    }
    if (!load(SOURCE_PATH,original) ||
        !load(TARGET_PATH,patched) ||
        !intended_patch()) { say("nvwrite: staged input invalid; no write\n"); return; }
    if (NVInit()!=1) { say("nvwrite: NVInit failed; no write\n"); return; }
    if (!sample()) { say("nvwrite: vendor read failed; no write\n"); return; }
    if (check) {
        say(same(live,original,SIZE) && checksum(SOURCE_SUM) ?
            "nvwrite: original image and vendor checksum verified; no write\n" :
            "nvwrite: live image differs; no write\n");
        return;
    }
    if (restore) {
        if (!allowed_restore()) { say("nvwrite: live divergence; restore refused\n"); return; }
        say("nvwrite: restoring original fields\n");
        ok=put(original) && sample() && same(live,original,SIZE) && checksum(SOURCE_SUM);
    } else {
        if (!same(live,original,SIZE) || !checksum(SOURCE_SUM)) {
            say("nvwrite: live image or checksum differs; no write\n"); return;
        }
        say("nvwrite: writing staged WiFi fields\n");
        ok=put(patched) && sample() && same(live,patched,SIZE) && checksum(TARGET_SUM);
        if (!ok) {
            say("nvwrite: verification failed; restoring before reboot\n");
            ok=put(original) && sample() && same(live,original,SIZE) && checksum(SOURCE_SUM);
            say(ok ? "nvwrite: original restored\n" : "nvwrite: RESTORE FAILED\n");
            return;
        }
    }
    say(ok ? "nvwrite: verified complete image and vendor checksum\n" : "nvwrite: verification FAILED\n");
}
