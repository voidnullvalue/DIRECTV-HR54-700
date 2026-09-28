/* Read-only WifiAccessor descriptor probe; prints offsets and lengths only. */
typedef unsigned int size_t;
extern int NVInit(void);
extern long write(int, const void *, unsigned long);
typedef struct {const char *name; void *base; const char *symbol; void *address;} Dl_info;
extern int dladdr(const void *, Dl_info *);
static char out[256];
static int used;
static void str(const char *s) {while (*s) out[used++]=*s++;}
static void hex(unsigned v, int n) {
    const char *d="0123456789abcdef";
    int i;
    for(i=n-1;i>=0;i--) out[used++]=d[(v>>(i*4))&15];
}
static void flush(void) {write(1,out,used);used=0;}
__attribute__((constructor)) static void run(void) {
    Dl_info info;
    unsigned *got, *entry;
    unsigned char *desc;
    int i;
    if (!dladdr((void *)(size_t)&NVInit,&info) || !info.base || NVInit()!=1) {
        str("nvfields: unavailable\n");flush();return;
    }
    got=(unsigned *)((unsigned)(size_t)info.base+0x2c030);
    entry=(unsigned *)(size_t)(got[328]+84*32);
    if (entry[5]-(unsigned)(size_t)info.base!=0xc704) {
        str("nvfields: accessor mismatch\n");flush();return;
    }
    desc=(unsigned char *)(size_t)entry[7];
    str("nvfields: descriptors\n");flush();
    for (i=0;i<3;i++) {
        unsigned char *p=desc+i*10;
        str("  field ");hex(i,1);
        str(" offset=0x");hex(((unsigned)p[2]<<8)|p[3],4);
        str(" type=0x");hex(p[4],2);
        str(" length=0x");hex(((unsigned)p[6]<<8)|p[7],4);
        str(" flags=0x");hex(p[8],2);
        str("\n");flush();
    }
}
