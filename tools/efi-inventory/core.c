#include "core.h"
/* Clang can lower fixed descriptor copies to memcpy even in freestanding mode. */
void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d=dst; const uint8_t *s=src;
    for (size_t i=0;i<n;++i) d[i]=s[i];
    return dst;
}
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
int range_ok(uint64_t start, uint64_t length) { return length <= UINT64_MAX - start; }
MapResult parse_map(const void *data, size_t length, size_t stride, uint32_t version) {
    MapResult r = { .state = MALFORMED };
    if (!data || !length || !stride || stride < sizeof(EFI_MEMORY_DESCRIPTOR) || stride > MAP_LIMIT ||
        length > MAP_LIMIT || length % stride || version != 1) return r;
    const uint8_t *p = data;
    r.stride = (uint32_t)stride; r.version = version;
    for (size_t i = 0; i < length / stride; ++i) {
        EFI_MEMORY_DESCRIPTOR d;
        const uint8_t *s = p + i*stride;
        uint8_t *t = (uint8_t *)&d;
        for (size_t j=0;j<sizeof d;++j) t[j]=s[j];
        if (d.pages > UINT64_MAX / 4096 || !range_ok(d.physical, d.pages*4096) ||
            !range_ok(d.virtual_address, d.pages*4096)) return (MapResult){.state=MALFORMED};
        if (r.count<REPORTED_DESCRIPTORS) {
            r.entries[r.count].type=d.type;
            r.entries[r.count].physical=d.physical;
            r.entries[r.count].pages=d.pages;
            r.entries[r.count].attributes=d.attributes;
        }
        ++r.count;
    }
    r.state = OBSERVED; return r;
}
DtResult parse_dt(const uint8_t *p, size_t available) {
    DtResult r = {.state=MALFORMED};
    if (!p || available < 40) return r;
    if (be32(p) != 0xd00dfeed) { r.state=UNSUPPORTED; return r; }
    uint32_t total=be32(p+4), structure=be32(p+8), strings=be32(p+12);
    uint32_t strings_size=be32(p+32), structure_size=be32(p+36);
    if (total < 40 || total > DT_LIMIT || total > available ||
        structure < 40 || structure > total || structure_size > total-structure ||
        strings < 40 || strings > total || strings_size > total-strings ||
        be32(p+16) < 40 || be32(p+16) > total-16) return r;
    r.version=be32(p+20); r.size=total;
    if (r.version < 16 || r.version > 17) { r.state=UNSUPPORTED; return r; }
    /* Only the first root node and its direct model property are inspected. */
    size_t pos=structure, end=(size_t)structure+structure_size;
    if (end-pos < 8 || be32(p+pos)!=1) return r;
    pos+=4;
    while (pos < end && p[pos]) ++pos;
    if (pos >= end) return r;
    pos=(pos+4)&~(size_t)3;
    if (pos>end) return r;
    /* Validate the full structure token stream and every property name. */
    unsigned depth=1, finished=0;
    while (end-pos>=4) {
        uint32_t token=be32(p+pos); pos+=4;
        if (token==1) {
            while (pos<end && p[pos]) ++pos;
            if (pos==end) return r;
            pos=(pos+4)&~(size_t)3;
            if (pos>end || ++depth>64) return r;
        } else if (token==2) {
            if (!depth) return r;
            --depth;
        } else if (token==3) {
            if (end-pos<8) return r;
            uint32_t len=be32(p+pos),name=be32(p+pos+4); pos+=8;
            if (len>end-pos || name>=strings_size) return r;
            size_t n=name;
            while (n<strings_size && p[strings+n]) ++n;
            if (n==strings_size) return r;
            if (depth==1 && n-name==5 && p[strings+name]=='m' && p[strings+name+1]=='o' &&
                p[strings+name+2]=='d' && p[strings+name+3]=='e' && p[strings+name+4]=='l') {
                if (!len || len>64 || p[pos+len-1]!=0) return r;
                for (uint32_t i=0;i<len-1;++i) {
                    if (!p[pos+i]) return r;
                    r.model[i]=(p[pos+i]>=32 && p[pos+i]<127) ? (char)p[pos+i] : '?';
                }
            }
            pos+=(len+3u)&~(size_t)3;
            if (pos>end) return r;
        } else if (token==4) { /* NOP */ }
        else if (token==9 && !depth) { finished=1; break; }
        else return r;
    }
    if (!finished || pos!=end) return r;
    r.state=OBSERVED; return r;
}
VarResult parse_var(EFI_STATUS s, size_t size, uint8_t v) {
    if (s==EFI_NOT_FOUND) return (VarResult){.state=UNAVAILABLE};
    if (EFI_ERROR(s)) return (VarResult){.state=UNSUPPORTED};
    if (size!=1 || v>1) return (VarResult){.state=MALFORMED};
    return (VarResult){.state=OBSERVED,.value=v};
}
ElResult decode_el(uint64_t raw) {
    ElResult r={.state=MALFORMED,.raw=raw};
    if (!(raw & ~UINT64_C(0xc)) && ((raw>>2)&3)) {
        r.state=OBSERVED; r.level=(unsigned)((raw>>2)&3);
    }
    return r;
}
const char *state_name(State s) {
    static const char *names[]={"observed","unavailable","unsupported","malformed","not_probed"};
    return (unsigned)s<5 ? names[s] : "malformed";
}
void put(Writer *w, const char *s) {
    while (*s) {
        if (w->length+1<w->capacity) w->data[w->length++]=*s;
        else w->truncated=1;
        ++s;
    }
    if (w->capacity) w->data[w->length]=0;
}
void escaped(Writer *w, const char *s) {
    static const char hex[]="0123456789abcdef";
    for (;*s;++s) {
        unsigned char c=(unsigned char)*s;
        if (c=='"' || c=='\\') { char b[3]={'\\',(char)c,0}; put(w,b); }
        else if (c<32 || c>=127) {
            char b[7]={'\\','u','0','0',hex[c>>4],hex[c&15],0}; put(w,b);
        } else { char b[2]={(char)c,0}; put(w,b); }
    }
}
