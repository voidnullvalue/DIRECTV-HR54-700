/* Receiver-native Jellyfin service for the DIRECTV HR54.
 *
 * Everything the TV frontend needs runs on the receiver itself: Quick Connect
 * auth state, browse/search, artwork, PlaybackInfo, the opaque MPEG-TS relay,
 * transport control, and receiver UI presentation.  The only external service
 * required is the Jellyfin server.  The old host backend (server.py) is the
 * behavioral specification, not a dependency.
 *
 * Build: zig cc -target mips-linux-musleabi -mcpu=mips32 -static -O2 \
 *          -o hr54-jf hr54_jf.c
 * Usage: hr54-jf DOCROOT JELLYFIN_IPV4 JELLYFIN_PORT LISTEN_PORT [--no-launcher]
 *
 * Layout under /var/hr54-persist/jellyfin (or $JF_PERSIST_ROOT for tests):
 *   config/config.json, config/token   0600, directories 0700
 *   state/tv-state.json                saved TV browse checkpoint
 *   cache/<itemId> + cache/<itemId>.ct artwork cache
 *   log/jf.log
 */

#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Tunables mirrored from the reference server.py                     */
/* ------------------------------------------------------------------ */

#define JF_PERSIST_DEFAULT "/var/hr54-persist/jellyfin"
#define JF_PLAY_CMD_DEFAULT "/var/opt/hr54/bin/hr54-play-url"
#define JF_SHEF_PORT 8080

#define LIVE_TV_SCREEN 2320
#define MENU_SCREEN_A 10306
#define MENU_SCREEN_B 2100
#define MENU_IDLE_SECONDS 8.0
#define REMOTE_EXIT_IDLE 2.0
#define REMOTE_EXIT_DISMISS 1.5
#define WALK_SETTLE 1.5
#define APP_EXIT_GRACE 15.0
#define LAUNCH_COOLDOWN 30.0
#define POST_STOP_DISMISS 8.0
#define RESUME_RESTART 20.0
#define QC_LIFETIME 600.0
#define CLAIM_WAIT 15.0

#define JF_STREAM_SLOTS 4
#define RELAY_CHUNK 65536
#define REQ_CAP 16384
#define BODY_CAP 8192
#define RESP_CAP (4 * 1024 * 1024)

static const char *persist_root = JF_PERSIST_DEFAULT;
static const char *play_cmd = JF_PLAY_CMD_DEFAULT;
static const char *docroot = "";

/* ------------------------------------------------------------------ */
/* Error plumbing: request children are single-threaded.              */
/* ------------------------------------------------------------------ */

static char g_err[256];

static int fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
    return -1;
}

static double mono_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void nap(double seconds) {
    struct timespec ts = {(time_t)seconds,
                          (long)((seconds - (time_t)seconds) * 1e9)};
    nanosleep(&ts, NULL);
}

static int write_all_fd(int fd, const void *data, size_t len) {
    const char *p = data;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Growable string buffer                                              */
/* ------------------------------------------------------------------ */

struct sb {
    char *p;
    size_t len, cap;
};

static int sb_grow(struct sb *b, size_t need) {
    if (b->p && b->len + need + 1 <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + need + 1) cap *= 2;
    char *p = realloc(b->p, cap);
    if (!p) return fail("out of memory");
    b->p = p;
    b->cap = cap;
    return 0;
}

static int sb_putn(struct sb *b, const char *s, size_t n) {
    if (sb_grow(b, n)) return -1;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
    return 0;
}

static int sb_puts(struct sb *b, const char *s) { return sb_putn(b, s, strlen(s)); }

static int sb_fmt(struct sb *b, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if ((size_t)n < sizeof tmp) return sb_putn(b, tmp, (size_t)n);
    char *big = malloc((size_t)n + 1);
    if (!big) return fail("out of memory");
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    int rc = sb_putn(b, big, (size_t)n);
    free(big);
    return rc;
}

/* JSON string literal with escaping (quote, backslash, C0 controls). */
static int sb_json_str(struct sb *b, const char *s) {
    if (sb_puts(b, "\"")) return -1;
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"': if (sb_puts(b, "\\\"")) return -1; break;
        case '\\': if (sb_puts(b, "\\\\")) return -1; break;
        case '\b': if (sb_puts(b, "\\b")) return -1; break;
        case '\f': if (sb_puts(b, "\\f")) return -1; break;
        case '\n': if (sb_puts(b, "\\n")) return -1; break;
        case '\r': if (sb_puts(b, "\\r")) return -1; break;
        case '\t': if (sb_puts(b, "\\t")) return -1; break;
        default:
            if (c < 0x20) { if (sb_fmt(b, "\\u%04x", c)) return -1; }
            else if (sb_putn(b, (const char *)&c, 1)) return -1;
        }
    }
    return sb_puts(b, "\"");
}

/* ------------------------------------------------------------------ */
/* URL encoding helpers                                                */
/* ------------------------------------------------------------------ */

static int url_encode(struct sb *out, const char *s) {
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            if (sb_putn(out, (const char *)&c, 1)) return -1;
        } else if (sb_fmt(out, "%%%02X", c)) {
            return -1;
        }
    }
    return 0;
}

static size_t url_decode(char *dst, size_t dstsz, const char *src, size_t n) {
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < dstsz; i++) {
        char c = src[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%' && i + 2 < n && isxdigit((unsigned char)src[i + 1]) &&
                   isxdigit((unsigned char)src[i + 2])) {
            char hex[3] = {src[i + 1], src[i + 2], 0};
            c = (char)strtol(hex, NULL, 16);
            i += 2;
        }
        dst[o++] = c;
    }
    dst[o] = 0;
    return o;
}

/* Extract one query parameter (percent-decoded) from "a=b&c=d". */
static int query_param(const char *query, const char *name,
                       char *out, size_t outsz) {
    if (!query) return 0;
    size_t nlen = strlen(name);
    const char *p = query;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        size_t seg = amp ? (size_t)(amp - p) : strlen(p);
        const char *eq = memchr(p, '=', seg);
        if (eq && (size_t)(eq - p) == nlen && !memcmp(p, name, nlen)) {
            url_decode(out, outsz, eq + 1, seg - nlen - 1);
            return 1;
        }
        if (!amp) break;
        p = amp + 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Minimal JSON DOM parser                                             */
/* ------------------------------------------------------------------ */

enum jtype { J_NULL, J_FALSE, J_TRUE, J_NUM, J_STR, J_ARR, J_OBJ };

struct jval {
    enum jtype t;
    double num;
    char *str;
    struct jval **items;
    char **keys;
    size_t n, cap;
};

static void jfree(struct jval *v) {
    if (!v) return;
    free(v->str);
    for (size_t i = 0; i < v->n; i++) {
        jfree(v->items[i]);
        if (v->keys) free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
    free(v);
}

static int jpush(struct jval *parent, char *key, struct jval *child) {
    if (parent->n == parent->cap) {
        size_t cap = parent->cap ? parent->cap * 2 : 8;
        struct jval **items = realloc(parent->items, cap * sizeof *items);
        if (!items) return -1;
        parent->items = items;
        if (parent->t == J_OBJ) {
            char **keys = realloc(parent->keys, cap * sizeof *keys);
            if (!keys) return -1;
            parent->keys = keys;
        }
        parent->cap = cap;
    }
    parent->items[parent->n] = child;
    if (parent->keys) parent->keys[parent->n] = key;
    parent->n++;
    return 0;
}

static void jskip_ws(const char **p, const char *end) {
    while (*p < end && (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r'))
        (*p)++;
}

static struct jval *jparse_value(const char **p, const char *end);

static int jparse_hex4(const char **p, const char *end, unsigned *out) {
    if (end - *p < 4) return -1;
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = (*p)[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return -1;
    }
    *p += 4;
    *out = v;
    return 0;
}

static int utf8_emit(struct sb *b, unsigned cp) {
    char t[4];
    int n;
    if (cp < 0x80) { t[0] = (char)cp; n = 1; }
    else if (cp < 0x800) {
        t[0] = (char)(0xC0 | (cp >> 6));
        t[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        t[0] = (char)(0xE0 | (cp >> 12));
        t[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        t[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        t[0] = (char)(0xF0 | (cp >> 18));
        t[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        t[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        t[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    return sb_putn(b, t, (size_t)n);
}

/* Parse a JSON string body (opening quote at **p) into a raw buffer. */
static int jparse_string(struct sb *out, const char **p, const char *end) {
    if (*p >= end || **p != '"') return -1;
    (*p)++;
    while (*p < end) {
        char c = **p;
        if (c == '"') { (*p)++; return 0; }
        if ((unsigned char)c < 0x20) return -1;
        if (c == '\\') {
            (*p)++;
            if (*p >= end) return -1;
            char e = **p;
            (*p)++;
            switch (e) {
            case '"': if (sb_putn(out, "\"", 1)) return -1; break;
            case '\\': if (sb_putn(out, "\\", 1)) return -1; break;
            case '/': if (sb_putn(out, "/", 1)) return -1; break;
            case 'b': if (sb_putn(out, "\b", 1)) return -1; break;
            case 'f': if (sb_putn(out, "\f", 1)) return -1; break;
            case 'n': if (sb_putn(out, "\n", 1)) return -1; break;
            case 'r': if (sb_putn(out, "\r", 1)) return -1; break;
            case 't': if (sb_putn(out, "\t", 1)) return -1; break;
            case 'u': {
                unsigned cp;
                if (jparse_hex4(p, end, &cp)) return -1;
                if (cp >= 0xD800 && cp <= 0xDBFF && end - *p >= 6 &&
                    (*p)[0] == '\\' && (*p)[1] == 'u') {
                    const char *save = *p;
                    unsigned lo;
                    *p += 2;
                    if (!jparse_hex4(p, end, &lo) && lo >= 0xDC00 && lo <= 0xDFFF)
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    else
                        *p = save;
                }
                if (utf8_emit(out, cp)) return -1;
                break;
            }
            default:
                return -1;
            }
        } else {
            if (sb_putn(out, &c, 1)) return -1;
            (*p)++;
        }
    }
    return -1;
}

static struct jval *jnew(enum jtype t) {
    struct jval *v = calloc(1, sizeof *v);
    if (v) v->t = t;
    return v;
}

static struct jval *jparse_value(const char **p, const char *end) {
    jskip_ws(p, end);
    if (*p >= end) return NULL;
    char c = **p;
    if (c == '{') {
        (*p)++;
        struct jval *o = jnew(J_OBJ);
        if (!o) return NULL;
        jskip_ws(p, end);
        if (*p < end && **p == '}') { (*p)++; return o; }
        for (;;) {
            jskip_ws(p, end);
            if (*p >= end || **p != '"') { jfree(o); return NULL; }
            struct sb key = {0};
            if (jparse_string(&key, p, end)) { free(key.p); jfree(o); return NULL; }
            jskip_ws(p, end);
            if (*p >= end || **p != ':') { free(key.p); jfree(o); return NULL; }
            (*p)++;
            struct jval *child = jparse_value(p, end);
            if (!child || jpush(o, key.p, child)) {
                free(key.p);
                jfree(child);
                jfree(o);
                return NULL;
            }
            jskip_ws(p, end);
            if (*p < end && **p == ',') { (*p)++; continue; }
            if (*p < end && **p == '}') { (*p)++; return o; }
            jfree(o);
            return NULL;
        }
    }
    if (c == '[') {
        (*p)++;
        struct jval *a = jnew(J_ARR);
        if (!a) return NULL;
        jskip_ws(p, end);
        if (*p < end && **p == ']') { (*p)++; return a; }
        for (;;) {
            struct jval *child = jparse_value(p, end);
            if (!child || jpush(a, NULL, child)) { jfree(child); jfree(a); return NULL; }
            jskip_ws(p, end);
            if (*p < end && **p == ',') { (*p)++; continue; }
            if (*p < end && **p == ']') { (*p)++; return a; }
            jfree(a);
            return NULL;
        }
    }
    if (c == '"') {
        struct sb s = {0};
        if (jparse_string(&s, p, end)) { free(s.p); return NULL; }
        struct jval *v = jnew(J_STR);
        if (!v) { free(s.p); return NULL; }
        v->str = s.p ? s.p : calloc(1, 1);
        if (!v->str) { jfree(v); return NULL; }
        return v;
    }
    if (end - *p >= 4 && !strncmp(*p, "true", 4)) { *p += 4; return jnew(J_TRUE); }
    if (end - *p >= 5 && !strncmp(*p, "false", 5)) { *p += 5; return jnew(J_FALSE); }
    if (end - *p >= 4 && !strncmp(*p, "null", 4)) { *p += 4; return jnew(J_NULL); }
    if (c == '-' || isdigit((unsigned char)c)) {
        char *stop = NULL;
        char *copy = strndup(*p, (size_t)(end - *p));
        if (!copy) return NULL;
        double d = strtod(copy, &stop);
        size_t used = (size_t)(stop - copy);
        free(copy);
        if (!used) return NULL;
        struct jval *v = jnew(J_NUM);
        if (!v) return NULL;
        v->num = d;
        *p += used;
        return v;
    }
    return NULL;
}

static struct jval *json_parse(const char *s, size_t n) {
    const char *p = s, *end = s + n;
    struct jval *v = jparse_value(&p, end);
    if (!v) return NULL;
    jskip_ws(&p, end);
    if (p != end) { jfree(v); return NULL; }
    return v;
}

static struct jval *jget(struct jval *obj, const char *key) {
    if (!obj || obj->t != J_OBJ) return NULL;
    for (size_t i = 0; i < obj->n; i++)
        if (!strcmp(obj->keys[i], key)) return obj->items[i];
    return NULL;
}

static struct jval *jnth(struct jval *arr, size_t i) {
    if (!arr || arr->t != J_ARR || i >= arr->n) return NULL;
    return arr->items[i];
}

static const char *jstr(struct jval *v) { return v && v->t == J_STR ? v->str : NULL; }
static double jnum(struct jval *v, double dflt) {
    return v && v->t == J_NUM ? v->num : dflt;
}
static int jbool(struct jval *v, int dflt) {
    if (!v) return dflt;
    if (v->t == J_TRUE) return 1;
    if (v->t == J_FALSE || v->t == J_NULL) return 0;
    return dflt;
}

/* ------------------------------------------------------------------ */
/* HTTP client                                                         */
/* ------------------------------------------------------------------ */

struct jf_resp {
    int status;
    char ctype[128];
    char *body; /* single allocation; header text precedes body in it */
    size_t body_len;
};

static int tcp_connect(const char *host, int port, int timeout_s) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct timeval tv = {timeout_s, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr)) {
        close(fd);
        return -1;
    }
    return fd;
}

static void resp_free(struct jf_resp *r) {
    free(r->body);
    r->body = NULL;
    r->body_len = 0;
}

/* Locate "\r\n\r\n"; fill status and Content-Type; return header length. */
static int http_split(char *buf, size_t len, struct jf_resp *out, size_t *hdr_len) {
    buf[len] = 0;
    char *split = strstr(buf, "\r\n\r\n");
    if (!split) return -1;
    *hdr_len = (size_t)(split - buf) + 4;
    out->status = 0;
    if (!strncmp(buf, "HTTP/", 5)) {
        char *sp = strchr(buf, ' ');
        if (sp) out->status = atoi(sp + 1);
    }
    out->ctype[0] = 0;
    char *line = buf;
    while (line < split) {
        char *eol = strstr(line, "\r\n");
        if (!eol || eol > split) eol = split;
        if (!strncasecmp(line, "Content-Type:", 13)) {
            char *v = line + 13;
            while (v < eol && *v == ' ') v++;
            size_t n = (size_t)(eol - v);
            if (n >= sizeof out->ctype) n = sizeof out->ctype - 1;
            memcpy(out->ctype, v, n);
            out->ctype[n] = 0;
        }
        line = eol + 2;
    }
    return 0;
}

/* Blocking request; the whole response is buffered up to cap.  0 on reply. */
static int jf_http(const char *host, int port, const char *method,
                   const char *pathq, const char *token, const char *authz,
                   const char *body, struct jf_resp *out, size_t cap) {
    memset(out, 0, sizeof *out);
    int fd = tcp_connect(host, port, 30);
    if (fd < 0) return fail("Jellyfin connection failed");
    struct sb req = {0};
    sb_fmt(&req, "%s %s%s HTTP/1.0\r\nHost: %s:%d\r\nAccept: application/json\r\n",
           method, *pathq == '/' ? "" : "/", pathq, host, port);
    if (token && *token) sb_fmt(&req, "X-Emby-Token: %s\r\n", token);
    if (authz && *authz) sb_fmt(&req, "Authorization: %s\r\n", authz);
    if (body) sb_fmt(&req, "Content-Type: application/json\r\nContent-Length: %zu\r\n",
                     strlen(body));
    sb_puts(&req, "Connection: close\r\n\r\n");
    if (body) sb_puts(&req, body);
    int rc = -1;
    if (!req.p) goto done;
    if (write_all_fd(fd, req.p, req.len)) goto done;
    struct sb resp = {0};
    for (;;) {
        char chunk[16384];
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        if (resp.len + (size_t)n > cap) { free(resp.p); goto done; }
        if (sb_putn(&resp, chunk, (size_t)n)) { free(resp.p); goto done; }
    }
    if (!resp.p) goto done;
    {
        size_t hdr = 0;
        if (http_split(resp.p, resp.len, out, &hdr)) { free(resp.p); goto done; }
        out->body = resp.p; /* owns the single allocation */
        out->body_len = resp.len - hdr;
        memmove(out->body, resp.p + hdr, out->body_len);
        out->body[out->body_len] = 0;
    }
    rc = 0;
done:
    free(req.p);
    close(fd);
    return rc;
}

/* Streaming open for the /play relay: returns fd with headers consumed and
 * any body bytes already read available in pre[]. */
struct jf_stream {
    int fd;
    char pre[RELAY_CHUNK];
    size_t pre_len;
    int status;
};

static int jf_stream_open(const char *host, int port, const char *pathq,
                          const char *token, struct jf_stream *out) {
    memset(out, 0, sizeof *out);
    int fd = tcp_connect(host, port, 120);
    if (fd < 0) return -1;
    struct sb req = {0};
    sb_fmt(&req, "GET %s%s HTTP/1.0\r\nHost: %s:%d\r\nAccept: */*\r\n",
           *pathq == '/' ? "" : "/", pathq, host, port);
    if (token && *token) sb_fmt(&req, "X-Emby-Token: %s\r\n", token);
    sb_puts(&req, "Connection: close\r\n\r\n");
    if (!req.p || write_all_fd(fd, req.p, req.len)) {
        free(req.p);
        close(fd);
        return -1;
    }
    free(req.p);
    size_t have = 0;
    for (;;) {
        ssize_t n = read(fd, out->pre + have, sizeof out->pre - have);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        have += (size_t)n;
        out->pre[have] = 0;
        if (strstr(out->pre, "\r\n\r\n")) break;
        if (have >= sizeof out->pre - 1) break;
    }
    if (have < 4) { close(fd); return -1; }
    out->pre[have] = 0;
    char *split = strstr(out->pre, "\r\n\r\n");
    if (!split) { close(fd); return -1; }
    size_t hdr = (size_t)(split - out->pre) + 4;
    out->status = 0;
    if (!strncmp(out->pre, "HTTP/", 5)) {
        char *sp = strchr(out->pre, ' ');
        if (sp) out->status = atoi(sp + 1);
    }
    memmove(out->pre, out->pre + hdr, have - hdr);
    out->pre_len = have - hdr;
    out->fd = fd;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Shared state: MAP_SHARED anonymous, serialized with flock           */
/* ------------------------------------------------------------------ */

#define JF_NAME_MAX 256
#define JF_ID_MAX 80
#define JF_URL_MAX 2048

struct stream_slot {
    int in_use, claimed, closed, paused;
    char token[64];
    char upath[JF_URL_MAX]; /* path?query on the Jellyfin server */
    double paused_since, paused_total;
};

struct shared {
    char jf_host[64];
    int jf_port;
    char device_id[33];
    char token[512];
    char user_id[JF_ID_MAX];
    char user_name[JF_NAME_MAX];
    int auth_valid;
    char qc_secret[512];
    char qc_code[16];
    double qc_started;
    int playing;
    char play_name[JF_NAME_MAX];
    char play_item[JF_ID_MAX];
    char play_token[64];
    int return_to_tv;
    double play_started;
    int play_base;
    struct stream_slot streams[JF_STREAM_SLOTS];
    int loopback_failed;
    char lan_addr[64];
    int listen_port;
};

static struct shared *S;
static int lock_fd_local = -1; /* per-process: separate OFDs make flock exclude */

static void state_lock(void) {
    if (lock_fd_local < 0) {
        char path[512];
        snprintf(path, sizeof path, "%s/state.lock", persist_root);
        lock_fd_local = open(path, O_RDWR | O_CREAT, 0600);
        if (lock_fd_local < 0) lock_fd_local = open("/dev/null", O_RDONLY);
    }
    if (lock_fd_local >= 0)
        while (flock(lock_fd_local, LOCK_EX) && errno == EINTR) {}
}

static void state_unlock(void) {
    if (lock_fd_local >= 0)
        while (flock(lock_fd_local, LOCK_UN) && errno == EINTR) {}
}

static void jf_log(const char *fmt, ...) {
    char path[512], line[512];
    snprintf(path, sizeof path, "%s/log/jf.log", persist_root);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    char stamp[40];
    strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", &tm);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0600);
    if (fd >= 0) {
        char out[640];
        int n = snprintf(out, sizeof out, "%s %s\n", stamp, line);
        if (n > 0) write_all_fd(fd, out, (size_t)n);
        close(fd);
    }
}

static int random_hex(char *out, size_t hexchars) {
    unsigned char raw[32];
    size_t need = (hexchars + 1) / 2;
    if (need > sizeof raw) return -1;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    size_t got = 0;
    while (got < need) {
        ssize_t n = read(fd, raw + got, need - got);
        if (n <= 0) { close(fd); return -1; }
        got += (size_t)n;
    }
    close(fd);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < hexchars; i++)
        out[i] = hex[(raw[i / 2] >> (i % 2 ? 0 : 4)) & 0xF];
    out[hexchars] = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Persistence: config, token, TV state                                */
/* ------------------------------------------------------------------ */

static int atomic_write(const char *path, const char *data, size_t len, mode_t mode) {
    char tmp[640];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return -1;
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); unlink(tmp); return -1; }
        off += (size_t)n;
    }
    fsync(fd);
    close(fd);
    if (chmod(tmp, mode) || rename(tmp, path)) { unlink(tmp); return -1; }
    return 0;
}

static char *read_file(const char *path, size_t *len_out) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) || st.st_size > (off_t)RESP_CAP) { close(fd); return NULL; }
    size_t sz = (size_t)st.st_size;
    char *buf = malloc(sz + 1);
    if (!buf) { close(fd); return NULL; }
    size_t got = 0;
    while (got < sz) {
        ssize_t n = read(fd, buf + got, sz - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
    buf[got] = 0;
    if (len_out) *len_out = got;
    return buf;
}

static void config_paths(char *dir, size_t dsz, char *cfg, size_t csz,
                         char *tok, size_t tsz) {
    snprintf(dir, dsz, "%s/config", persist_root);
    snprintf(cfg, csz, "%s/config.json", dir);
    snprintf(tok, tsz, "%s/token", dir);
}

/* Caller holds the state lock. */
static int save_config_locked(void) {
    char dir[512], cfg[576], tok[640];
    config_paths(dir, sizeof dir, cfg, sizeof cfg, tok, sizeof tok);
    struct sb b = {0};
    sb_puts(&b, "{\"server\":\"http://");
    sb_puts(&b, S->jf_host);
    sb_fmt(&b, ":%d\",\"device_id\":\"%s\"", S->jf_port, S->device_id);
    if (S->user_id[0]) sb_fmt(&b, ",\"user_id\":\"%s\"", S->user_id);
    if (S->user_name[0]) {
        sb_puts(&b, ",\"user_name\":");
        sb_json_str(&b, S->user_name);
    }
    sb_puts(&b, "}\n");
    int rc = b.p ? atomic_write(cfg, b.p, b.len, 0600) : -1;
    free(b.p);
    return rc;
}

/* Caller holds the state lock. */
static int save_token_locked(const char *token) {
    char dir[512], cfg[576], tok[640];
    config_paths(dir, sizeof dir, cfg, sizeof cfg, tok, sizeof tok);
    if (!token || !*token) { unlink(tok); return 0; }
    char data[600];
    int n = snprintf(data, sizeof data, "%s\n", token);
    if (n < 0 || (size_t)n >= sizeof data) return -1;
    return atomic_write(tok, data, (size_t)n, 0600);
}

/* Run once in the parent before forking; no lock needed. */
static void load_persist(void) {
    char dir[512], cfg[576], tok[640];
    config_paths(dir, sizeof dir, cfg, sizeof cfg, tok, sizeof tok);
    size_t n = 0;
    char *raw = read_file(cfg, &n);
    if (raw) {
        struct jval *v = json_parse(raw, n);
        if (v) {
            const char *did = jstr(jget(v, "device_id"));
            if (did && strlen(did) < sizeof S->device_id)
                snprintf(S->device_id, sizeof S->device_id, "%s", did);
            jfree(v);
        }
        free(raw);
    }
    if (!S->device_id[0]) {
        if (!random_hex(S->device_id, 32)) save_config_locked();
        else jf_log("cannot read /dev/urandom for device id");
    }
    raw = read_file(tok, &n);
    if (raw) {
        while (n && (raw[n - 1] == '\n' || raw[n - 1] == '\r' || raw[n - 1] == ' '))
            raw[--n] = 0;
        if (n && n < sizeof S->token) {
            memcpy(S->token, raw, n);
            S->token[n] = 0;
        }
        free(raw);
    }
}

/* TV browse checkpoint: one shared slot keyed "hr54", as in server.py. */
static void tv_state_path(char *path, size_t sz) {
    snprintf(path, sz, "%s/state/tv-state.json", persist_root);
}

/* Caller holds the state lock. */
static int tv_state_save_locked(struct jval *payload) {
    char path[512];
    tv_state_path(path, sizeof path);
    char lib[80] = "", submitted[80] = "", query[80] = "", zone[24] = "";
    int page = 0, index = 0, searching = 0;
    const char *s;
    if ((s = jstr(jget(payload, "lib")))) snprintf(lib, sizeof lib, "%.64s", s);
    if ((s = jstr(jget(payload, "submitted"))))
        snprintf(submitted, sizeof submitted, "%.64s", s);
    if ((s = jstr(jget(payload, "query")))) snprintf(query, sizeof query, "%.64s", s);
    if ((s = jstr(jget(payload, "zone")))) snprintf(zone, sizeof zone, "%.16s", s);
    if (!zone[0]) snprintf(zone, sizeof zone, "card");
    page = (int)jnum(jget(payload, "page"), 0);
    if (page < 0) page = 0;
    if (page > 100000) page = 100000;
    index = (int)jnum(jget(payload, "index"), 0);
    if (index < 0) index = 0;
    if (index > 5) index = 5;
    searching = jbool(jget(payload, "searching"), 0);
    struct sb b = {0};
    sb_puts(&b, "{\"hr54\":{\"lib\":");
    sb_json_str(&b, lib);
    sb_fmt(&b, ",\"page\":%d,\"searching\":%s,\"submitted\":", page,
           searching ? "true" : "false");
    sb_json_str(&b, submitted);
    sb_puts(&b, ",\"query\":");
    sb_json_str(&b, query);
    sb_puts(&b, ",\"zone\":");
    sb_json_str(&b, zone);
    sb_fmt(&b, ",\"index\":%d}}\n", index);
    int rc = b.p ? atomic_write(path, b.p, b.len, 0600) : -1;
    free(b.p);
    return rc;
}

static int tv_state_load(struct sb *out) {
    char path[512];
    tv_state_path(path, sizeof path);
    size_t n = 0;
    char *raw = read_file(path, &n);
    if (!raw) { sb_puts(out, "{}"); return 0; }
    struct jval *v = json_parse(raw, n);
    free(raw);
    struct jval *slot = v ? jget(v, "hr54") : NULL;
    if (!slot || slot->t != J_OBJ) {
        jfree(v);
        sb_puts(out, "{}");
        return 0;
    }
    static const char *fields[] = {"lib", "page", "searching", "submitted",
                                   "query", "zone", "index"};
    sb_puts(out, "{");
    for (size_t i = 0; i < sizeof fields / sizeof *fields; i++) {
        struct jval *f = jget(slot, fields[i]);
        if (i) sb_puts(out, ",");
        sb_json_str(out, fields[i]);
        sb_puts(out, ":");
        if (!f) { sb_puts(out, "null"); continue; }
        switch (f->t) {
        case J_STR: sb_json_str(out, f->str); break;
        case J_NUM: sb_fmt(out, "%.0f", f->num); break;
        case J_TRUE: sb_puts(out, "true"); break;
        case J_FALSE: sb_puts(out, "false"); break;
        default: sb_puts(out, "null"); break;
        }
    }
    sb_puts(out, "}");
    jfree(v);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Jellyfin API calls                                                  */
/* ------------------------------------------------------------------ */

static int require_token(void) {
    if (!S->token[0]) return fail("sign in to Jellyfin first");
    return 0;
}

static void media_browser_auth(struct sb *out) {
    sb_fmt(out, "MediaBrowser Client=\"HR54 Jellyfin\", Device=\"DIRECTV HR54\", "
                "DeviceId=\"%s\", Version=\"0.1\"", S->device_id);
    if (S->token[0]) sb_fmt(out, ", Token=\"%s\"", S->token);
}

static int jf_get(const char *pathq, struct jf_resp *r, size_t cap) {
    return jf_http(S->jf_host, S->jf_port, "GET", pathq, S->token, NULL, NULL, r, cap);
}

static int jf_post(const char *pathq, const char *body, struct jf_resp *r, size_t cap) {
    return jf_http(S->jf_host, S->jf_port, "POST", pathq, S->token, NULL, body, r, cap);
}

/* Validate the token via Users/Me; on failure clear in-memory auth. */
static int jf_validate_token(void) {
    struct jf_resp r;
    if (jf_get("Users/Me", &r, 64 * 1024)) {
        state_lock();
        S->token[0] = 0;
        S->user_id[0] = 0;
        S->user_name[0] = 0;
        S->auth_valid = 0;
        state_unlock();
        return -1;
    }
    int ok = 0;
    if (r.status == 200 && r.body) {
        struct jval *v = json_parse(r.body, r.body_len);
        if (v) {
            const char *id = jstr(jget(v, "Id"));
            const char *name = jstr(jget(v, "Name"));
            if (id && strlen(id) < sizeof S->user_id) {
                state_lock();
                snprintf(S->user_id, sizeof S->user_id, "%s", id);
                if (name) snprintf(S->user_name, sizeof S->user_name, "%.200s", name);
                S->auth_valid = 1;
                state_unlock();
                ok = 1;
            }
            jfree(v);
        }
    }
    resp_free(&r);
    if (!ok) {
        state_lock();
        S->token[0] = 0;
        S->user_id[0] = 0;
        S->user_name[0] = 0;
        S->auth_valid = 0;
        state_unlock();
    }
    return ok ? 0 : -1;
}

static void qc_status_json(struct sb *out, int authenticated, int pending,
                           const char *user, const char *code, int expired) {
    sb_puts(out, "{\"authenticated\":");
    sb_puts(out, authenticated ? "true" : "false");
    sb_puts(out, ",\"user\":");
    sb_json_str(out, user ? user : "");
    sb_puts(out, ",\"pending\":");
    sb_puts(out, pending ? "true" : "false");
    sb_puts(out, ",\"code\":");
    if (code && *code) sb_json_str(out, code);
    else sb_puts(out, "null");
    if (expired) sb_puts(out, ",\"expired\":true");
    sb_puts(out, "}");
}

/* Caller holds the state lock. */
static void auth_status_locked(struct sb *out) {
    qc_status_json(out, S->auth_valid, !!S->qc_secret[0], S->user_name, S->qc_code, 0);
}

static void qc_clear_locked(void) {
    S->qc_secret[0] = 0;
    S->qc_code[0] = 0;
    S->qc_started = 0;
}

/* POST /api/auth/start */
static int qc_start(struct sb *out) {
    char authz[640];
    struct sb ab = {0};
    media_browser_auth(&ab);
    snprintf(authz, sizeof authz, "%s", ab.p ? ab.p : "");
    free(ab.p);
    struct jf_resp r;
    if (jf_http(S->jf_host, S->jf_port, "GET", "QuickConnect/Enabled", NULL, authz,
                NULL, &r, 4096))
        return fail("Quick Connect availability check failed");
    int enabled = 0;
    if (r.status == 200 && r.body) {
        struct jval *v = json_parse(r.body, r.body_len);
        enabled = v && v->t == J_TRUE;
        jfree(v);
    }
    resp_free(&r);
    if (!enabled) return fail("Jellyfin Quick Connect is disabled");
    if (jf_http(S->jf_host, S->jf_port, "POST", "QuickConnect/Initiate", NULL, authz,
                "{}", &r, 64 * 1024))
        return fail("Quick Connect initiate failed");
    char secret[512] = "", code[16] = "";
    if (r.status == 200 && r.body) {
        struct jval *v = json_parse(r.body, r.body_len);
        if (v) {
            const char *sec = jstr(jget(v, "Secret"));
            const char *c = jstr(jget(v, "Code"));
            if (sec && *sec && c && *c) {
                snprintf(secret, sizeof secret, "%.500s", sec);
                snprintf(code, sizeof code, "%.15s", c);
            }
            jfree(v);
        }
    }
    resp_free(&r);
    if (!secret[0] || !code[0])
        return fail("Quick Connect response lacks secret or code");
    state_lock();
    snprintf(S->qc_secret, sizeof S->qc_secret, "%s", secret);
    snprintf(S->qc_code, sizeof S->qc_code, "%s", code);
    S->qc_started = mono_now();
    state_unlock();
    sb_puts(out, "{\"code\":");
    sb_json_str(out, code);
    sb_puts(out, ",\"pending\":true}");
    return 0;
}

/* GET /api/auth/poll */
static int qc_poll(struct sb *out) {
    char secret[512];
    double started;
    state_lock();
    int pending = !!S->qc_secret[0];
    snprintf(secret, sizeof secret, "%s", S->qc_secret);
    started = S->qc_started;
    if (!pending) auth_status_locked(out);
    state_unlock();
    if (!pending) return 0;
    if (mono_now() - started > QC_LIFETIME) {
        state_lock();
        qc_clear_locked();
        state_unlock();
        qc_status_json(out, 0, 0, "", NULL, 1);
        return 0;
    }
    char authz[640];
    struct sb ab = {0};
    media_browser_auth(&ab);
    snprintf(authz, sizeof authz, "%s", ab.p ? ab.p : "");
    free(ab.p);
    struct sb path = {0};
    sb_puts(&path, "QuickConnect/Connect?Secret=");
    url_encode(&path, secret);
    struct jf_resp r;
    if (jf_http(S->jf_host, S->jf_port, "GET", path.p ? path.p : "", NULL, authz,
                NULL, &r, 64 * 1024)) {
        free(path.p);
        return fail("Quick Connect status check failed");
    }
    free(path.p);
    int authenticated = 0;
    if (r.status == 200 && r.body) {
        struct jval *v = json_parse(r.body, r.body_len);
        if (v) authenticated = jbool(jget(v, "Authenticated"), 0);
        jfree(v);
    }
    resp_free(&r);
    if (!authenticated) {
        state_lock();
        auth_status_locked(out);
        state_unlock();
        return 0;
    }
    struct sb body = {0};
    sb_puts(&body, "{\"Secret\":");
    sb_json_str(&body, secret);
    sb_puts(&body, "}");
    if (jf_http(S->jf_host, S->jf_port, "POST", "Users/AuthenticateWithQuickConnect",
                NULL, authz, body.p, &r, 256 * 1024)) {
        free(body.p);
        return fail("Quick Connect login failed");
    }
    free(body.p);
    char token[512] = "", user_id[JF_ID_MAX] = "", user_name[JF_NAME_MAX] = "";
    if (r.status == 200 && r.body) {
        struct jval *v = json_parse(r.body, r.body_len);
        if (v) {
            const char *t = jstr(jget(v, "AccessToken"));
            struct jval *u = jget(v, "User");
            const char *uid = u ? jstr(jget(u, "Id")) : NULL;
            const char *un = u ? jstr(jget(u, "Name")) : NULL;
            if (t && *t && uid && *uid) {
                snprintf(token, sizeof token, "%.500s", t);
                snprintf(user_id, sizeof user_id, "%.70s", uid);
                if (un) snprintf(user_name, sizeof user_name, "%.200s", un);
            }
            jfree(v);
        }
    }
    resp_free(&r);
    if (!token[0] || !user_id[0])
        return fail("Quick Connect did not return a user token");
    state_lock();
    snprintf(S->token, sizeof S->token, "%s", token);
    snprintf(S->user_id, sizeof S->user_id, "%s", user_id);
    snprintf(S->user_name, sizeof S->user_name, "%s", user_name);
    qc_clear_locked();
    save_token_locked(S->token);
    save_config_locked();
    state_unlock();
    if (jf_validate_token()) {
        jf_log("Quick Connect token did not validate");
        return fail("Quick Connect did not return a user token");
    }
    jf_log("Quick Connect sign-in complete");
    state_lock();
    auth_status_locked(out);
    state_unlock();
    return 0;
}

/* POST /api/auth/logout */
static int auth_logout(struct sb *out) {
    state_lock();
    S->token[0] = 0;
    S->user_id[0] = 0;
    S->user_name[0] = 0;
    S->auth_valid = 0;
    qc_clear_locked();
    save_token_locked(NULL);
    save_config_locked();
    auth_status_locked(out);
    state_unlock();
    jf_log("signed out");
    return 0;
}

/* GET /api/libraries */
static int jf_libraries(struct sb *out) {
    if (require_token()) return -1;
    struct sb path = {0};
    sb_fmt(&path, "Users/%s/Views", S->user_id);
    struct jf_resp r;
    if (jf_get(path.p, &r, 512 * 1024)) { free(path.p); return -1; }
    free(path.p);
    int rc = -1;
    struct jval *v = r.body ? json_parse(r.body, r.body_len) : NULL;
    if (v && r.status == 200) {
        struct jval *items = jget(v, "Items");
        sb_puts(out, "{\"libraries\":[");
        for (size_t i = 0; jnth(items, i); i++) {
            struct jval *it = jnth(items, i);
            if (i) sb_puts(out, ",");
            sb_puts(out, "{\"id\":");
            const char *id = jstr(jget(it, "Id"));
            if (id) sb_json_str(out, id); else sb_puts(out, "null");
            sb_puts(out, ",\"name\":");
            const char *name = jstr(jget(it, "Name"));
            sb_json_str(out, name ? name : "");
            sb_puts(out, ",\"kind\":");
            const char *kind = jstr(jget(it, "CollectionType"));
            sb_json_str(out, kind ? kind : "");
            sb_puts(out, "}");
        }
        sb_puts(out, "]}");
        rc = 0;
    } else {
        fail("Jellyfin returned HTTP %d", r.status);
    }
    jfree(v);
    resp_free(&r);
    return rc;
}

/* GET /api/items */
static int jf_items(const char *query, struct sb *out) {
    if (require_token()) return -1;
    char parent[JF_ID_MAX] = "", search[128] = "";
    char limit_s[16] = "", offset_s[16] = "", video_only[8] = "";
    query_param(query, "parent", parent, sizeof parent);
    query_param(query, "search", search, sizeof search);
    query_param(query, "limit", limit_s, sizeof limit_s);
    query_param(query, "offset", offset_s, sizeof offset_s);
    query_param(query, "videoOnly", video_only, sizeof video_only);
    int limit = atoi(limit_s);
    if (limit < 1) limit = 60;
    if (limit > 200) limit = 200;
    int offset = atoi(offset_s);
    if (offset < 0) offset = 0;
    struct sb path = {0};
    sb_fmt(&path, "Users/%s/Items?Recursive=true&Limit=%d&StartIndex=%d"
                  "&SortBy=SortName&SortOrder=Ascending"
                  "&ExcludeLocationTypes=Virtual"
                  "&Fields=Overview,RunTimeTicks,ProductionYear,MediaType,Type",
           S->user_id, limit, offset);
    if (parent[0]) {
        sb_puts(&path, "&ParentId=");
        url_encode(&path, parent);
    }
    if (!strcmp(video_only, "1"))
        sb_puts(&path, "&IncludeItemTypes=Movie,Episode,Video,MusicVideo");
    if (search[0]) {
        sb_puts(&path, "&SearchTerm=");
        url_encode(&path, search);
    }
    struct jf_resp r;
    if (jf_get(path.p, &r, RESP_CAP)) { free(path.p); return -1; }
    free(path.p);
    int rc = -1;
    struct jval *v = r.body ? json_parse(r.body, r.body_len) : NULL;
    if (v && r.status == 200) {
        struct jval *items = jget(v, "Items");
        double total = jnum(jget(v, "TotalRecordCount"), 0);
        sb_fmt(out, "{\"total\":%.0f,\"items\":[", total);
        for (size_t i = 0; jnth(items, i); i++) {
            struct jval *it = jnth(items, i);
            if (i) sb_puts(out, ",");
            sb_puts(out, "{\"id\":");
            const char *id = jstr(jget(it, "Id"));
            if (id) sb_json_str(out, id); else sb_puts(out, "null");
            sb_puts(out, ",\"name\":");
            const char *name = jstr(jget(it, "Name"));
            sb_json_str(out, name ? name : "");
            sb_puts(out, ",\"type\":");
            const char *type = jstr(jget(it, "Type"));
            sb_json_str(out, type ? type : "");
            sb_puts(out, ",\"year\":");
            struct jval *year = jget(it, "ProductionYear");
            if (year && year->t == J_NUM) sb_fmt(out, "%.0f", year->num);
            else sb_puts(out, "null");
            sb_puts(out, ",\"runtime\":");
            struct jval *rt = jget(it, "RunTimeTicks");
            if (rt && rt->t == J_NUM) sb_fmt(out, "%.0f", rt->num);
            else sb_puts(out, "null");
            sb_puts(out, ",\"overview\":");
            const char *ov = jstr(jget(it, "Overview"));
            if (ov) {
                char trunc[404];
                snprintf(trunc, sizeof trunc, "%.400s", ov);
                sb_json_str(out, trunc);
            } else {
                sb_puts(out, "\"\"");
            }
            const char *mt = jstr(jget(it, "MediaType"));
            const char *lt = jstr(jget(it, "LocationType"));
            int playable = mt && !strcmp(mt, "Video") && (!lt || strcmp(lt, "Virtual"));
            sb_fmt(out, ",\"playable\":%s}", playable ? "true" : "false");
        }
        sb_puts(out, "]}");
        rc = 0;
    } else {
        fail("Jellyfin returned HTTP %d", r.status);
    }
    jfree(v);
    resp_free(&r);
    return rc;
}

/* The proven PlaybackInfo request profile (MPEG-TS, H.264, AC3). */
static void playbackinfo_body(struct sb *b, const char *item_id) {
    sb_puts(b, "{\"UserId\":\"");
    sb_puts(b, S->user_id);
    sb_puts(b, "\",\"MediaSourceId\":\"");
    sb_puts(b, item_id);
    sb_puts(b, "\",\"DeviceProfile\":{\"Name\":\"DIRECTV HR54 MPEG-TS H264 AC3\","
                "\"MaxStreamingBitrate\":8000000,\"DirectPlayProfiles\":[],"
                "\"TranscodingProfiles\":[{\"Container\":\"ts\",\"Type\":\"Video\","
                "\"Protocol\":\"http\",\"VideoCodec\":\"h264\",\"AudioCodec\":\"ac3\","
                "\"MaxAudioChannels\":\"2\"}]},"
                "\"EnableDirectPlay\":false,\"EnableDirectStream\":false,"
                "\"EnableTranscoding\":true,\"AllowVideoStreamCopy\":true,"
                "\"AllowAudioStreamCopy\":false,\"MaxAudioChannels\":2,"
                "\"MaxStreamingBitrate\":8000000}");
}

/* Items/<id> sanity checks mirroring server.py's transcode_url prelude. */
static int check_item_video(const char *item_id, char *name, size_t namesz) {
    struct sb path = {0};
    sb_fmt(&path, "Items/%s", item_id);
    struct jf_resp r;
    if (jf_get(path.p, &r, 512 * 1024)) { free(path.p); return -1; }
    free(path.p);
    int rc = -1;
    struct jval *v = r.body ? json_parse(r.body, r.body_len) : NULL;
    if (v && r.status == 200) {
        const char *mt = jstr(jget(v, "MediaType"));
        const char *lt = jstr(jget(v, "LocationType"));
        if (!mt || strcmp(mt, "Video")) fail("item is not video");
        else if (lt && !strcmp(lt, "Virtual"))
            fail("this is a Jellyfin virtual episode with no media file");
        else {
            struct jval *sources = jget(v, "MediaSources");
            int probed = 0;
            for (size_t i = 0; jnth(sources, i); i++) {
                struct jval *ms = jget(jnth(sources, i), "MediaStreams");
                if (ms && ms->n) probed = 1;
            }
            if (sources && sources->n && !probed)
                fail("Jellyfin has no probed audio/video streams for this title; "
                     "refresh its media information in Jellyfin");
            else {
                const char *nm = jstr(jget(v, "Name"));
                snprintf(name, namesz, "%.200s", nm ? nm : "");
                rc = 0;
            }
        }
    } else {
        fail("Jellyfin returned HTTP %d", r.status);
    }
    jfree(v);
    resp_free(&r);
    return rc;
}

static int transcode_url(const char *item_id, double start_seconds,
                         char *name, size_t namesz, char *upath, size_t upathsz) {
    if (check_item_video(item_id, name, namesz)) return -1;
    struct sb body = {0};
    playbackinfo_body(&body, item_id);
    struct sb path = {0};
    sb_fmt(&path, "Items/%s/PlaybackInfo", item_id);
    for (int attempt = 0; attempt < 3; attempt++) {
        struct jf_resp r;
        if (jf_post(path.p, body.p, &r, 512 * 1024)) {
            free(path.p);
            free(body.p);
            return -1;
        }
        struct jval *v = r.body ? json_parse(r.body, r.body_len) : NULL;
        int found = 0, no_compatible = 0, bad_status = 0;
        if (v && r.status == 200) {
            struct jval *sources = jget(v, "MediaSources");
            for (size_t i = 0; jnth(sources, i) && !found; i++) {
                struct jval *src = jnth(sources, i);
                const char *tu = jstr(jget(src, "TranscodingUrl"));
                const char *tc = jstr(jget(src, "TranscodingContainer"));
                if (tu && *tu && tc && !strcmp(tc, "ts")) {
                    if (strlen(tu) >= upathsz - 64) fail("transcode URL too long");
                    else { snprintf(upath, upathsz, "%s", tu); found = 1; }
                }
            }
            const char *ec = jstr(jget(v, "ErrorCode"));
            if (!found && ec && !strcmp(ec, "NoCompatibleStream")) no_compatible = 1;
        } else if (!found) {
            fail("Jellyfin returned HTTP %d", r.status);
            bad_status = 1;
        }
        jfree(v);
        resp_free(&r);
        if (found) {
            if (start_seconds > 0) {
                long long ticks = (long long)(start_seconds * 10.0e6);
                const char *joiner = strchr(upath, '?') ? "&" : "?";
                size_t len = strlen(upath);
                snprintf(upath + len, upathsz - len, "%sStartTimeTicks=%lld",
                         joiner, ticks);
            }
            free(path.p);
            free(body.p);
            return 0;
        }
        if (no_compatible) {
            free(path.p);
            free(body.p);
            return fail("Jellyfin reports no compatible stream for this title");
        }
        if (bad_status) {
            free(path.p);
            free(body.p);
            return -1;
        }
        if (attempt + 1 < 3) sleep(2);
    }
    free(path.p);
    free(body.p);
    return fail("Jellyfin offered no MPEG-TS transcode (tried 3 times)");
}

/* ------------------------------------------------------------------ */
/* Playback state machine                                              */
/* ------------------------------------------------------------------ */

static struct stream_slot *slot_by_token(const char *token) {
    for (int i = 0; i < JF_STREAM_SLOTS; i++)
        if (S->streams[i].in_use && !strcmp(S->streams[i].token, token))
            return &S->streams[i];
    return NULL;
}

static struct stream_slot *active_slot_locked(void) {
    return S->playing && S->play_token[0] ? slot_by_token(S->play_token) : NULL;
}

static void playback_end_locked(void) {
    for (int i = 0; i < JF_STREAM_SLOTS; i++)
        if (S->streams[i].in_use) {
            S->streams[i].closed = 1;
            S->streams[i].in_use = 0;
        }
    S->playing = 0;
    S->play_name[0] = 0;
    S->play_item[0] = 0;
    S->play_token[0] = 0;
    S->return_to_tv = 0;
    S->play_started = 0;
    S->play_base = 0;
}

static void playback_begin_locked(const char *token, const char *name,
                                  const char *item_id, int base, int return_to_tv) {
    for (int i = 0; i < JF_STREAM_SLOTS; i++) {
        struct stream_slot *s = &S->streams[i];
        if (s->in_use && strcmp(s->token, token)) {
            s->closed = 1;
            s->in_use = 0;
        }
    }
    snprintf(S->play_token, sizeof S->play_token, "%s", token);
    snprintf(S->play_name, sizeof S->play_name, "%.200s", name);
    snprintf(S->play_item, sizeof S->play_item, "%.70s", item_id);
    S->playing = 1;
    S->play_started = mono_now();
    S->play_base = base;
    S->return_to_tv = return_to_tv;
}

static double playback_elapsed_locked(void) {
    struct stream_slot *s = active_slot_locked();
    if (!s) return (double)S->play_base;
    double now = mono_now();
    double total = now - S->play_started - s->paused_total;
    if (s->paused) total -= now - s->paused_since;
    if (total < 0) total = 0;
    return (double)S->play_base + total;
}

static void status_common(struct sb *out) {
    sb_puts(out, ",\"receiver\":true,\"receiverHost\":");
    sb_json_str(out, S->lan_addr);
    sb_puts(out, ",\"user\":");
    sb_json_str(out, S->user_name);
}

/* GET /api/status */
static int playback_snapshot(struct sb *out) {
    state_lock();
    if (!S->playing) {
        sb_puts(out, "{\"playing\":false");
        status_common(out);
        sb_puts(out, "}");
        state_unlock();
        return 0;
    }
    struct stream_slot *s = active_slot_locked();
    char name[JF_NAME_MAX], item[JF_ID_MAX];
    snprintf(name, sizeof name, "%s", S->play_name);
    snprintf(item, sizeof item, "%s", S->play_item);
    int return_to_tv = S->return_to_tv;
    double elapsed = playback_elapsed_locked();
    int paused = s ? s->paused : 0;
    state_unlock();
    sb_puts(out, "{\"playing\":true,\"name\":");
    sb_json_str(out, name);
    sb_puts(out, ",\"itemId\":");
    sb_json_str(out, item);
    sb_fmt(out, ",\"returnToTv\":%s,\"elapsed\":%d,\"paused\":%s",
           return_to_tv ? "true" : "false", (int)elapsed, paused ? "true" : "false");
    status_common(out);
    sb_puts(out, "}");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Receiver control (on-box: uconntest, dt, SHEF, hr54-play-url)       */
/* ------------------------------------------------------------------ */

static int run_shell(char *out, size_t outsz, int timeout_s, const char *fmt, ...) {
    char cmd[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof cmd) return -1;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);
    int fds[2];
    if (pipe(fds)) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
        signal(SIGCHLD, SIG_DFL);
        close(fds[0]);
        if (fds[1] != STDOUT_FILENO) dup2(fds[1], STDOUT_FILENO);
        if (fds[1] != STDERR_FILENO) dup2(fds[1], STDERR_FILENO);
        if (fds[1] != STDOUT_FILENO && fds[1] != STDERR_FILENO) close(fds[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    size_t got = 0;
    int status = -1, timed_out = 0;
    struct pollfd pf = {fds[0], POLLIN, 0};
    for (;;) {
        int pr = poll(&pf, 1, timeout_s * 1000);
        if (pr < 0) { if (errno == EINTR) continue; break; }
        if (pr == 0) { timed_out = 1; break; }
        char buf[4096];
        ssize_t n2 = read(fds[0], buf, sizeof buf);
        if (n2 < 0) { if (errno == EINTR) continue; break; }
        if (n2 == 0) break;
        if (out && got + 1 < outsz) {
            size_t take = (size_t)n2;
            if (got + take + 1 > outsz) take = outsz - got - 1;
            memcpy(out + got, buf, take);
            got += take;
        }
    }
    if (timed_out) kill(pid, SIGKILL);
    close(fds[0]);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (out && outsz) out[got] = 0;
    if (timed_out || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

static int rc_key(const char *name) {
    char pathq[256];
    snprintf(pathq, sizeof pathq, "/remote/processKey?key=%s&hold=keyPress", name);
    struct jf_resp r;
    if (jf_http("127.0.0.1", JF_SHEF_PORT, "GET", pathq, NULL, NULL, NULL, &r, 4096))
        return fail("receiver SHEF key request failed");
    int ok = r.status == 200;
    resp_free(&r);
    return ok ? 0 : -1;
}

static const char *DRUID_PROBE_XML =
    "<com.directv.druid.dt.DruidTester command=\"getCurrentScreenId\" "
    "session=\"local\"/>";

/* Screen id plus boot-OSD flag in one shell round trip. */
static int rc_probe(int *screen, int *boot_osd) {
    char out[4096];
    int rc = run_shell(out, sizeof out, 12,
        "osd=/var/mw_registry/Registry/Device/Server/OSD/Current; "
        "echo \"OSDNUM=$(cat $osd/Number 2>/dev/null)\"; "
        "/opt/middleware_core/system/tv/uconntest '%s' 2>&1", DRUID_PROBE_XML);
    *screen = -1;
    *boot_osd = 0;
    char *line = out;
    while (line && *line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = 0;
        if (!strncmp(line, "OSDNUM=", 7)) {
            char *v = line + 7;
            while (*v == ' ' || *v == '"') v++;
            *boot_osd = !strncmp(v, "36", 2) && (v[2] == 0 || v[2] == '"');
        }
        const char *marker = "Current screenId for session 0 is ";
        char *m = strstr(line, marker);
        if (m) *screen = atoi(m + strlen(marker));
        line = eol ? eol + 1 : NULL;
    }
    if (*screen < 0) {
        jf_log("screen probe lacked a screen id (helper rc %d): %.200s", rc, out);
        return -1;
    }
    return 0;
}

static int rc_clear_boot_osd(void) {
    return run_shell(NULL, 0, 15,
        "osd=/var/mw_registry/Registry/Device/Server/OSD/Current; "
        "if [ \"$(cat $osd/Number 2>/dev/null)\" = '\"36\"' ]; then "
        "/usr/bin/dt removeOsd -osd 36 -session 0 >/dev/null 2>&1; fi") ? -1 : 0;
}

static int rc_walk_to_live_tv(int screen) {
    for (int i = 0; i < 5; i++) {
        int boot_osd;
        if (screen < 0 && rc_probe(&screen, &boot_osd)) return -1;
        if (screen == LIVE_TV_SCREEN) return 0;
        if (rc_key("exit")) return -1;
        nap(WALK_SETTLE);
        screen = -1;
    }
    int screen2, boot_osd2;
    return rc_probe(&screen2, &boot_osd2) == 0 && screen2 == LIVE_TV_SCREEN ? 0 : -1;
}

static int rc_prepare_itv(void) {
    return run_shell(NULL, 0, 20,
        "if ! pgrep -f '/opt/itv/itvpack/root/bin/[i]tv -i 0' >/dev/null; then "
        "/opt/itv/itvpack/root/bin/start.sh -i 0 >/tmp/jellyfin-itv.log 2>&1 & "
        "sleep 12; fi") ? -1 : 0;
}

static int rc_launch_itv(const char *url) {
    char out[4096];
    int rc = run_shell(out, sizeof out, 15,
        "/opt/middleware_core/system/tv/uconntest "
        "'<com.ucentric.pvruconnect.DirectTest command=\"itvStartApp\" "
        "sessionId=\"0\" url=\"%s\"/>' 2>&1", url);
    /* uconntest uses vendor-specific nonzero success statuses.  Its response
     * body, not the process status, is authoritative. */
    if (strstr(out, "<Error>") || (!out[0] && rc)) return -1;
    return 0;
}

static int rc_itv_running(void) {
    char out[4096];
    int rc = run_shell(out, sizeof out, 12,
        "/opt/middleware_core/system/tv/uconntest "
        "'<com.ucentric.pvruconnect.DirectTest command=\"itvGetAppStatus\" "
        "sessionId=\"0\"/>' 2>&1");
    if (strstr(out, "Running appId")) return 1;
    if (strstr(out, "No app running")) return 0;
    jf_log("ITV status response unrecognized (helper rc %d): %.200s", rc, out);
    return -1;
}

static void tv_url(char *out, size_t sz) {
    snprintf(out, sz, "http://%s:%d/", S->lan_addr, S->listen_port);
}

/* Clear boot OSD, walk Druid to LiveTV, then start the app: all three are
 * required for a clean full-screen view. */
static void rc_present_tv_ui(int screen) {
    rc_clear_boot_osd();
    rc_walk_to_live_tv(screen);
    char url[128];
    tv_url(url, sizeof url);
    if (rc_launch_itv(url)) jf_log("ITV launch failed");
}

/* Post-stop UI return, ported from server.py's schedule_tv_return. */
static void schedule_tv_return(int return_to_tv, double delay) {
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        signal(SIGCHLD, SIG_DFL);
        signal(SIGPIPE, SIG_IGN);
        alarm(0);
        struct timespec ts = {(time_t)delay, 0};
        nanosleep(&ts, NULL);
        int playing = 0;
        state_lock();
        playing = S->playing;
        state_unlock();
        if (playing) _exit(0);
        if (return_to_tv) rc_present_tv_ui(-1);
        else rc_key("menu");
        _exit(0);
    }
}

/* ------------------------------------------------------------------ */
/* /api/play: PlaybackInfo -> opaque local URL -> on-box playURL       */
/* ------------------------------------------------------------------ */

static void play_url_for(const char *token, char *out, size_t sz) {
    const char *host = S->loopback_failed ? S->lan_addr : "127.0.0.1";
    snprintf(out, sz, "http://%s:%d/play/%s.ts", host, S->listen_port, token);
}

static int invoke_play_url(const char *url) {
    int rc = run_shell(NULL, 0, 60, "%s '%s'", play_cmd, url);
    if (rc) {
        jf_log("receiver refused playURL");
        return fail("receiver refused playURL");
    }
    return 0;
}

static int wait_for_claim(const char *token, double seconds) {
    for (double t = 0; t < seconds; t += 0.2) {
        int claimed = 0, closed = 0;
        state_lock();
        struct stream_slot *s = slot_by_token(token);
        claimed = s && s->claimed;
        closed = !s || s->closed;
        state_unlock();
        if (claimed || closed) return 1;
        nap(0.2);
    }
    return 0;
}

/* Emits {"playing":true,"name":X,"itemId":Y[,"startSeconds":N] without the
 * closing brace; the caller closes the object (and may add extras). */
static int start_play(const char *item_id, double start_seconds, int return_to_tv,
                      struct sb *out) {
    char name[JF_NAME_MAX] = "", upath[JF_URL_MAX], token[64], url[512];
    if (transcode_url(item_id, start_seconds, name, sizeof name, upath, sizeof upath))
        return -1;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (random_hex(token, 24)) return fail("cannot read /dev/urandom");
        state_lock();
        struct stream_slot *slot = NULL;
        for (int i = 0; i < JF_STREAM_SLOTS; i++)
            if (!S->streams[i].in_use) { slot = &S->streams[i]; break; }
        if (!slot) {
            state_unlock();
            return fail("too many pending streams");
        }
        memset(slot, 0, sizeof *slot);
        slot->in_use = 1;
        snprintf(slot->token, sizeof slot->token, "%s", token);
        snprintf(slot->upath, sizeof slot->upath, "%s", upath);
        state_unlock();
        play_url_for(token, url, sizeof url);
        if (invoke_play_url(url)) {
            state_lock();
            struct stream_slot *s = slot_by_token(token);
            if (s) { s->closed = 1; s->in_use = 0; }
            state_unlock();
            return -1;
        }
        if (wait_for_claim(token, CLAIM_WAIT)) {
            state_lock();
            playback_begin_locked(token, name, item_id, (int)start_seconds, return_to_tv);
            state_unlock();
            jf_log("now playing: %s", name);
            sb_puts(out, "{\"playing\":true,\"name\":");
            sb_json_str(out, name);
            sb_puts(out, ",\"itemId\":");
            sb_json_str(out, item_id);
            if (start_seconds > 0)
                sb_fmt(out, ",\"startSeconds\":%d", (int)start_seconds);
            return 0;
        }
        /* The decoder never fetched the stream: if the URL was loopback,
         * fall back to the receiver's LAN address exactly once. */
        state_lock();
        struct stream_slot *s = slot_by_token(token);
        if (s) { s->closed = 1; s->in_use = 0; }
        int was_loopback = !S->loopback_failed;
        S->loopback_failed = 1;
        state_unlock();
        if (!was_loopback) break;
        jf_log("playURL did not open the loopback stream; retrying on LAN address");
    }
    return fail("receiver did not open the stream");
}

/* ------------------------------------------------------------------ */
/* Transport                                                           */
/* ------------------------------------------------------------------ */

static int transport_action(const char *action, struct sb *out) {
    state_lock();
    struct stream_slot *s = active_slot_locked();
    if (!s) {
        state_unlock();
        return fail("nothing is playing");
    }
    if (!strcmp(action, "pause")) {
        int was = s->paused;
        if (!was) {
            s->paused = 1;
            s->paused_since = mono_now();
        }
        state_unlock();
        sb_puts(out, "{\"paused\":");
        sb_puts(out, was ? "false" : "true");
        sb_puts(out, "}");
        jf_log("pause");
        return 0;
    }
    if (!strcmp(action, "resume")) {
        if (s->paused && mono_now() - s->paused_since > RESUME_RESTART) {
            char item[JF_ID_MAX];
            snprintf(item, sizeof item, "%s", S->play_item);
            double elapsed = playback_elapsed_locked();
            int return_to_tv = S->return_to_tv;
            playback_end_locked();
            state_unlock();
            jf_log("long pause: restarting stream");
            if (start_play(item, elapsed, return_to_tv, out)) return -1;
            sb_puts(out, ",\"resumed\":true}");
            return 0;
        }
        int was = s->paused;
        if (was) {
            s->paused_total += mono_now() - s->paused_since;
            s->paused = 0;
        }
        state_unlock();
        /* server.py: {"paused": not gate.resume()} */
        sb_puts(out, "{\"paused\":");
        sb_puts(out, was ? "false" : "true");
        sb_puts(out, "}");
        jf_log("resume");
        return 0;
    }
    if (!strcmp(action, "stop")) {
        int return_to_tv = S->return_to_tv;
        playback_end_locked();
        state_unlock();
        jf_log("stop");
        schedule_tv_return(return_to_tv, POST_STOP_DISMISS);
        sb_puts(out, "{\"stopped\":true}");
        return 0;
    }
    state_unlock();
    return fail("unsupported transport action");
}

/* ------------------------------------------------------------------ */
/* HTTP responses                                                     */
/* ------------------------------------------------------------------ */

static void send_body(int fd, int code, const char *reason, const char *ctype,
                      const char *cache, const char *body, size_t len,
                      const char *method) {
    char head[640];
    int n = snprintf(head, sizeof head,
        "HTTP/1.0 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n%s%s"
        "Connection: close\r\n\r\n",
        code, reason, ctype, len,
        cache ? "Cache-Control: " : "", cache ? cache : "");
    if (n < 0 || (size_t)n >= sizeof head) return;
    if (write_all_fd(fd, head, (size_t)n)) return;
    if (len && strcmp(method, "HEAD")) write_all_fd(fd, body, len);
}

static const char *http_reason(int code) {
    switch (code) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Body Too Large";
    case 431: return "Headers Too Large";
    case 500: return "Internal Server Error";
    default: return "Bad Gateway";
    }
}

static void send_json_sb(int fd, int code, const char *reason, struct sb *b,
                         const char *method) {
    send_body(fd, code, reason, "application/json", NULL,
              b->p ? b->p : "{}", b->p ? b->len : 2, method);
}

static void send_json_error(int fd, int code, const char *msg, const char *method) {
    struct sb b = {0};
    sb_puts(&b, "{\"error\":");
    sb_json_str(&b, msg);
    sb_puts(&b, "}");
    send_json_sb(fd, code, http_reason(code), &b, method);
    free(b.p);
}

/* ------------------------------------------------------------------ */
/* Artwork                                                            */
/* ------------------------------------------------------------------ */

static int is_alnum_str(const char *s) {
    if (!s || !*s) return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s)) return 0;
    return 1;
}

static void serve_art(int fd, const char *method, const char *raw) {
    char item_id[JF_ID_MAX];
    snprintf(item_id, sizeof item_id, "%s", raw);
    char *dot = strchr(item_id, '.');
    if (dot) *dot = 0;
    if (!is_alnum_str(item_id)) {
        send_json_error(fd, 404, "not found", method);
        return;
    }
    char data_path[600], ct_path[640];
    snprintf(data_path, sizeof data_path, "%s/cache/%s", persist_root, item_id);
    snprintf(ct_path, sizeof ct_path, "%s/cache/%s.ct", persist_root, item_id);
    size_t n = 0;
    char *body = read_file(data_path, &n);
    if (body) {
        char ct[128] = "image/jpeg";
        char *ctraw = read_file(ct_path, NULL);
        if (ctraw) {
            snprintf(ct, sizeof ct, "%.120s", ctraw);
            char *nl = strpbrk(ct, "\r\n");
            if (nl) *nl = 0;
            free(ctraw);
        }
        send_body(fd, 200, "OK", ct, "max-age=3600", body, n, method);
        free(body);
        return;
    }
    if (require_token()) {
        send_json_error(fd, 404, "no artwork", method);
        return;
    }
    struct sb path = {0};
    sb_fmt(&path, "Items/%s/Images/Primary", item_id);
    struct jf_resp r;
    if (jf_get(path.p, &r, RESP_CAP)) {
        free(path.p);
        send_json_error(fd, 404, "no artwork", method);
        return;
    }
    free(path.p);
    if (r.status != 200 || !r.body || !r.body_len) {
        resp_free(&r);
        send_json_error(fd, 404, "no artwork", method);
        return;
    }
    const char *ct = r.ctype[0] ? r.ctype : "image/jpeg";
    atomic_write(data_path, r.body, r.body_len, 0600);
    atomic_write(ct_path, ct, strlen(ct), 0600);
    send_body(fd, 200, "OK", ct, "max-age=3600", r.body, r.body_len, method);
    resp_free(&r);
}

/* ------------------------------------------------------------------ */
/* /play/<opaque>.ts relay                                            */
/* ------------------------------------------------------------------ */

static void serve_play(int fd, const char *token_raw) {
    char token[64];
    snprintf(token, sizeof token, "%.60s", token_raw);
    char *dot = strrchr(token, '.');
    if (dot && !strcmp(dot, ".ts")) *dot = 0;
    if (!is_alnum_str(token)) {
        send_json_error(fd, 404, "unknown stream", "GET");
        return;
    }
    alarm(0); /* a movie relay must not die to the per-request alarm */
    state_lock();
    struct stream_slot *s = slot_by_token(token);
    if (!s || s->claimed || s->closed) {
        state_unlock();
        send_json_error(fd, 404, "unknown stream", "GET");
        return;
    }
    s->claimed = 1;
    char upath[JF_URL_MAX];
    snprintf(upath, sizeof upath, "%s", s->upath);
    state_unlock();
    jf_log("receiver opened stream");
    struct jf_stream st;
    if (jf_stream_open(S->jf_host, S->jf_port, upath, S->token, &st)) {
        jf_log("upstream stream unavailable");
        send_json_error(fd, 404, "stream unavailable", "GET");
        return;
    }
    char head[256];
    int hn = snprintf(head, sizeof head,
        "HTTP/1.0 200 OK\r\nContent-Type: video/mp2t\r\nConnection: close\r\n\r\n");
    if (hn > 0) write_all_fd(fd, head, (size_t)hn);
    if (st.pre_len) write_all_fd(fd, st.pre, st.pre_len);
    char *buf = malloc(RELAY_CHUNK);
    if (!buf) { close(st.fd); return; }
    for (;;) {
        int closed = 0, paused = 0;
        state_lock();
        struct stream_slot *gate = slot_by_token(token);
        closed = !gate || gate->closed;
        paused = gate && gate->paused && !closed;
        state_unlock();
        if (closed) { jf_log("stream closed by control"); break; }
        if (paused) { nap(0.2); continue; }
        ssize_t n = read(st.fd, buf, RELAY_CHUNK);
        if (n < 0) {
            if (errno == EINTR) continue;
            jf_log("upstream read error");
            break;
        }
        if (n == 0) break;
        if (write_all_fd(fd, buf, (size_t)n)) {
            jf_log("receiver closed stream");
            break;
        }
    }
    free(buf);
    close(st.fd);
}

/* ------------------------------------------------------------------ */
/* Static TV files                                                    */
/* ------------------------------------------------------------------ */

static void serve_static(int fd, const char *method, const char *path) {
    const char *name = NULL, *type = NULL;
    if (!strcmp(path, "/") || !strcmp(path, "/index.html") ||
        !strcmp(path, "/tv") || !strcmp(path, "/tv/") ||
        !strcmp(path, "/tv/index.html")) {
        name = "tv/index.html"; type = "text/html; charset=utf-8";
    } else if (!strcmp(path, "/tv/app.js")) {
        name = "tv/app.js"; type = "application/javascript";
    } else if (!strcmp(path, "/tv/app.css")) {
        name = "tv/app.css"; type = "text/css";
    } else {
        send_json_error(fd, 404, "not found", method);
        return;
    }
    char target[1024];
    if (snprintf(target, sizeof target, "%s/%s", docroot, name) >= (int)sizeof target) {
        send_json_error(fd, 500, "path too long", method);
        return;
    }
    int source = open(target, O_RDONLY | O_NOFOLLOW);
    struct stat stt;
    if (source < 0 || fstat(source, &stt) || !S_ISREG(stt.st_mode)) {
        if (source >= 0) close(source);
        send_json_error(fd, 404, "not found", method);
        return;
    }
    char head[512];
    int n = snprintf(head, sizeof head,
        "HTTP/1.0 200 OK\r\nContent-Type: %s\r\nContent-Length: %lld\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        type, (long long)stt.st_size);
    if (n < 0 || (size_t)n >= sizeof head) { close(source); return; }
    if (write_all_fd(fd, head, (size_t)n)) { close(source); return; }
    if (strcmp(method, "HEAD")) {
        char buffer[8192];
        ssize_t r;
        while ((r = read(source, buffer, sizeof buffer)) > 0)
            if (write_all_fd(fd, buffer, (size_t)r)) break;
    }
    close(source);
}

/* ------------------------------------------------------------------ */
/* API routing                                                        */
/* ------------------------------------------------------------------ */

static void handle_api(int fd, const char *method, const char *route,
                       const char *query, const char *body) {
    struct jval *payload = json_parse(body && *body ? body : "{}",
                                      body && *body ? strlen(body) : 2);
    if (!payload) {
        send_json_error(fd, 400, "bad json", method);
        return;
    }
    struct sb out = {0};
    int rc = 0;

    if (!strcmp(method, "GET") || !strcmp(method, "HEAD")) {
        if (!strcmp(route, "/api/status")) {
            rc = playback_snapshot(&out);
        } else if (!strcmp(route, "/api/auth/status")) {
            state_lock();
            auth_status_locked(&out);
            state_unlock();
        } else if (!strcmp(route, "/api/auth/poll")) {
            rc = qc_poll(&out);
        } else if (!strcmp(route, "/api/libraries")) {
            rc = jf_libraries(&out);
        } else if (!strcmp(route, "/api/items")) {
            rc = jf_items(query, &out);
        } else if (!strcmp(route, "/api/tv/state")) {
            tv_state_load(&out);
        } else {
            send_json_error(fd, 404, "not found", method);
            jfree(payload);
            return;
        }
    } else if (!strcmp(method, "POST")) {
        if (!strcmp(route, "/api/auth/start")) {
            rc = qc_start(&out);
        } else if (!strcmp(route, "/api/auth/logout")) {
            auth_logout(&out);
        } else if (!strcmp(route, "/api/play")) {
            const char *item_id = jstr(jget(payload, "itemId"));
            double start = jnum(jget(payload, "startSeconds"), 0);
            int return_to_tv = jbool(jget(payload, "returnToTv"), 0);
            if (!item_id || !*item_id) {
                send_json_error(fd, 502, "itemId required", method);
                jfree(payload);
                return;
            }
            if (require_token()) {
                rc = -1;
            } else if (start_play(item_id, start, return_to_tv, &out)) {
                rc = -1;
            } else {
                sb_puts(&out, "}");
                rc = 0;
            }
        } else if (!strcmp(route, "/api/transport")) {
            const char *action = jstr(jget(payload, "action"));
            rc = transport_action(action ? action : "", &out);
        } else if (!strcmp(route, "/api/seek")) {
            state_lock();
            if (!S->playing) {
                state_unlock();
                send_json_error(fd, 502, "nothing is playing", method);
                jfree(payload);
                return;
            }
            char item[JF_ID_MAX];
            snprintf(item, sizeof item, "%s", S->play_item);
            double target = playback_elapsed_locked();
            int return_to_tv = S->return_to_tv;
            playback_end_locked();
            state_unlock();
            struct jval *seconds = jget(payload, "seconds");
            if (seconds && seconds->t == J_NUM) target = seconds->num;
            target += jnum(jget(payload, "delta"), 0);
            if (target < 0) target = 0;
            jf_log("seek to %d", (int)target);
            if (require_token()) {
                rc = -1;
            } else if (start_play(item, target, return_to_tv, &out)) {
                rc = -1;
            } else {
                sb_puts(&out, "}");
                rc = 0;
            }
        } else if (!strcmp(route, "/api/tv/state")) {
            state_lock();
            tv_state_save_locked(payload);
            state_unlock();
            sb_puts(&out, "{\"ok\":true}");
        } else if (!strcmp(route, "/api/tv/event")) {
            sb_puts(&out, "{\"ok\":true}");
        } else {
            send_json_error(fd, 404, "not found", method);
            jfree(payload);
            return;
        }
    } else {
        send_json_error(fd, 405, "Method Not Allowed", method);
        jfree(payload);
        return;
    }

    if (rc) send_json_error(fd, 502, g_err, method);
    else send_json_sb(fd, 200, "OK", &out, method);
    free(out.p);
    jfree(payload);
}

/* ------------------------------------------------------------------ */
/* Request dispatch                                                  */
/* ------------------------------------------------------------------ */

static void handle(int client) {
    char request[REQ_CAP];
    size_t used = 0;
    char *end = NULL;
    while (used < sizeof request - 1) {
        ssize_t n = read(client, request + used, sizeof request - 1 - used);
        if (n <= 0) return;
        used += (size_t)n;
        request[used] = 0;
        end = strstr(request, "\r\n\r\n");
        if (end) break;
    }
    if (!end) { send_json_error(client, 431, "Headers Too Large", "GET"); return; }
    char method[8], path[2048];
    if (sscanf(request, "%7s %2047s", method, path) != 2 ||
        (strcmp(method, "GET") && strcmp(method, "HEAD") && strcmp(method, "POST"))) {
        send_json_error(client, 400, "Bad Request", "GET");
        return;
    }
    size_t length = 0;
    char *line = strstr(request, "\r\n");
    while (line && line < end) {
        line += 2;
        if (!strncasecmp(line, "Content-Length:", 15))
            length = (size_t)strtoul(line + 15, NULL, 10);
        line = strstr(line, "\r\n");
    }
    if (length > BODY_CAP) { send_json_error(client, 413, "Body Too Large", method); return; }
    char *body = end + 4;
    size_t have = used - (size_t)(body - request);
    while (have < length) {
        if (used >= sizeof request - 1) break;
        ssize_t n = read(client, request + used, sizeof request - 1 - used);
        if (n <= 0) break;
        used += (size_t)n;
        request[used] = 0;
        have = used - (size_t)(body - request);
    }
    if (have > length) have = length;
    body[have] = 0;

    char path_only[2048];
    snprintf(path_only, sizeof path_only, "%s", path);
    char *query = strchr(path_only, '?');
    if (query) *query++ = 0;

    if (!strncmp(path_only, "/play/", 6)) {
        serve_play(client, path_only + 6);
        return;
    }
    if (!strncmp(path_only, "/api/", 5)) {
        if (!strcmp(path_only, "/api/play") || !strcmp(path_only, "/api/transport") ||
            !strcmp(path_only, "/api/seek"))
            alarm(240);
        handle_api(client, method, path_only, query, body);
        return;
    }
    if (!strncmp(path_only, "/art/", 5)) {
        serve_art(client, method, path_only + 5);
        return;
    }
    if (strcmp(method, "GET") && strcmp(method, "HEAD")) {
        send_json_error(client, 405, "Method Not Allowed", method);
        return;
    }
    serve_static(client, method, path_only);
}

/* ------------------------------------------------------------------ */
/* Menu watcher, ported from server.py's watch_tv_launcher            */
/* ------------------------------------------------------------------ */

static int hijack_paused(void) {
    char path[512];
    snprintf(path, sizeof path, "%s/ui/hijack-pause", persist_root);
    struct stat st;
    return stat(path, &st) == 0;
}

static void watcher_run(void) {
    signal(SIGCHLD, SIG_DFL);
    signal(SIGPIPE, SIG_IGN);
    alarm(0);
    /* Token validation can open this before fork().  Give the watcher its
     * own open-file description so flock actually excludes sibling workers. */
    if (lock_fd_local >= 0) close(lock_fd_local);
    lock_fd_local = -1;
    double stable_since = 0, app_seen_at = 0, launched_at = 0;
    int stable_screen = -2;
    for (;;) {
        double now = mono_now();
        int screen = -1, boot_osd = 0;
        if (rc_probe(&screen, &boot_osd)) {
            if (stable_screen != -1) jf_log("MENU watcher screen probe failed");
            screen = -1;
        }
        if (screen != stable_screen) {
            stable_screen = screen;
            stable_since = now;
            jf_log("MENU watcher screen %d", screen);
        }
        state_lock();
        int playing = S->playing;
        state_unlock();
        int in_menu = screen == MENU_SCREEN_A || screen == MENU_SCREEN_B;
        if (playing && !in_menu) { nap(2.0); continue; }
        int idle = now - stable_since >=
                   (playing ? REMOTE_EXIT_IDLE : MENU_IDLE_SECONDS);
        int app_running = rc_itv_running();
        if (app_running < 0) {
            jf_log("MENU watcher ITV status probe failed");
            nap(2.0);
            continue;
        }
        if (!playing && app_running) app_seen_at = now;
        if (playing && in_menu && idle) {
            state_lock();
            int return_to_tv = S->return_to_tv;
            playback_end_locked();
            state_unlock();
            jf_log("receiver left playback for a menu; ending stream");
            /* The Broadcom decoder can surrender the video plane several
             * seconds after the menu transition.  Relaunch only after the
             * normal decoder-dismiss interval so Druid cannot cover ITV
             * again with the still-draining menu plane. */
            schedule_tv_return(return_to_tv, POST_STOP_DISMISS);
            launched_at = now;
        } else if (!playing && in_menu && idle && !hijack_paused() &&
                   now - app_seen_at >= APP_EXIT_GRACE &&
                   now - launched_at >= LAUNCH_COOLDOWN && !app_running) {
            jf_log("MENU watcher presenting Jellyfin");
            if (rc_prepare_itv()) jf_log("ITV preparation failed");
            rc_present_tv_ui(screen);
            launched_at = mono_now();
        }
        nap(2.0);
    }
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

static int mkdir_p_mode(const char *path, mode_t mode) {
    char copy[512];
    if (snprintf(copy, sizeof copy, "%s", path) >= (int)sizeof copy) return -1;
    for (char *p = copy + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(copy, mode) && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(copy, mode) && errno != EEXIST) return -1;
    return chmod(path, mode);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s DOCROOT JELLYFIN_IPV4 JELLYFIN_PORT "
                        "LISTEN_PORT [--no-launcher]\n", argv[0]);
        return 2;
    }
    docroot = argv[1];
    const char *jf_host = argv[2];
    int jf_port = atoi(argv[3]);
    int listen_port = atoi(argv[4]);
    int run_launcher = 1;
    for (int i = 5; i < argc; i++)
        if (!strcmp(argv[i], "--no-launcher")) run_launcher = 0;
    struct in_addr probe;
    if (inet_pton(AF_INET, jf_host, &probe) != 1 || jf_port <= 0 || listen_port <= 0) {
        fprintf(stderr, "invalid Jellyfin host/port or listen port\n");
        return 2;
    }
    const char *env;
    if ((env = getenv("JF_PERSIST_ROOT")) && *env) persist_root = env;
    if ((env = getenv("JF_PLAY_CMD")) && *env) play_cmd = env;

    S = mmap(NULL, sizeof *S, PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (S == MAP_FAILED) { perror("mmap"); return 1; }
    memset(S, 0, sizeof *S);
    snprintf(S->jf_host, sizeof S->jf_host, "%s", jf_host);
    S->jf_port = jf_port;
    S->listen_port = listen_port;

    char sub[512];
    snprintf(sub, sizeof sub, "%s/config", persist_root);
    if (mkdir_p_mode(sub, 0700)) { perror(sub); return 1; }
    snprintf(sub, sizeof sub, "%s/state", persist_root);
    if (mkdir_p_mode(sub, 0700)) { perror(sub); return 1; }
    snprintf(sub, sizeof sub, "%s/cache", persist_root);
    if (mkdir_p_mode(sub, 0700)) { perror(sub); return 1; }
    snprintf(sub, sizeof sub, "%s/log", persist_root);
    if (mkdir_p_mode(sub, 0700)) { perror(sub); return 1; }

    /* Receiver LAN address, discovered the way the reference backend does. */
    {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0) {
            struct sockaddr_in to;
            memset(&to, 0, sizeof to);
            to.sin_family = AF_INET;
            to.sin_port = htons((unsigned short)jf_port);
            inet_pton(AF_INET, jf_host, &to.sin_addr);
            if (!connect(fd, (struct sockaddr *)&to, sizeof to)) {
                struct sockaddr_in me;
                socklen_t mel = sizeof me;
                if (!getsockname(fd, (struct sockaddr *)&me, &mel))
                    inet_ntop(AF_INET, &me.sin_addr, S->lan_addr, sizeof S->lan_addr);
            }
            close(fd);
        }
    }
    if (!S->lan_addr[0]) snprintf(S->lan_addr, sizeof S->lan_addr, "127.0.0.1");

    load_persist();
    jf_log("service starting: jellyfin %s:%d listen %d lan %s",
           S->jf_host, S->jf_port, listen_port, S->lan_addr);
    if (S->token[0]) jf_validate_token();

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { perror("socket"); return 1; }
    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in local;
    memset(&local, 0, sizeof local);
    local.sin_family = AF_INET;
    local.sin_port = htons((unsigned short)listen_port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listener, (struct sockaddr *)&local, sizeof local) ||
        listen(listener, 12)) {
        perror("listen");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);

    if (run_launcher) {
        pid_t w = fork();
        if (w == 0) {
            close(listener);
            watcher_run();
            _exit(0);
        }
    }
    signal(SIGCHLD, SIG_IGN);

    for (;;) {
        int client = accept(listener, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            continue;
        }
        pid_t child = fork();
        if (child == 0) {
            signal(SIGCHLD, SIG_DFL);
            close(listener);
            alarm(90);
            handle(client);
            close(client);
            _exit(0);
        }
        close(client);
    }
}
