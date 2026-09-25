#include "core.h"
#ifndef PROBE_HOST
/* Clang can lower fixed-size copies/initialisation to memcpy/memset even in
 * freestanding mode; the EFI image links no C library. Host tests use libc. */
void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d=dst; const uint8_t *s=src;
    for (size_t i=0;i<n;++i) d[i]=s[i];
    return dst;
}
void *memset(void *dst, int value, size_t n) {
    uint8_t *d=dst;
    for (size_t i=0;i<n;++i) d[i]=(uint8_t)value;
    return dst;
}
#endif
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p)<<32 | be32(p+4); }
/* True when [start, start + length) has a representable exclusive end. A
 * region ending exactly at 2^64 is rejected (conservative; no AArch64
 * physical address space reaches it). */
int range_ok(uint64_t start, uint64_t length) { return length <= UINT64_MAX - start; }

static MapResult map_state(State s, const char *detail) {
    MapResult r={.state=s,.detail=detail}; return r;
}
MapResult map_next_capacity(size_t capacity, size_t required, size_t stride, size_t *next) {
    *next=0;
    if (stride < sizeof(EFI_MEMORY_DESCRIPTOR) || stride > MAP_LIMIT) return map_state(MALFORMED,"too_small_stride");
    if (required <= capacity) return map_state(MALFORMED,"too_small_inconsistent");
    /* Two descriptors of slack: the next AllocatePool may split a region. */
    if (required > MAP_LIMIT || MAP_LIMIT - required < 2*stride) return map_state(UNSUPPORTED,"size_limit");
    *next=required+2*stride;
    return map_state(OBSERVED,0);
}
MapResult parse_map(const void *data, size_t capacity, size_t length, size_t stride, uint32_t version) {
    if (!data || !capacity || capacity > MAP_LIMIT) return map_state(MALFORMED,"buffer");
    /* Checked before any descriptor byte is read. */
    if (length > capacity) return map_state(MALFORMED,"size_exceeds_buffer");
    if (version != EFI_MEMORY_DESCRIPTOR_VERSION) return map_state(UNSUPPORTED,"descriptor_version");
    if (stride < sizeof(EFI_MEMORY_DESCRIPTOR) || stride > capacity) return map_state(MALFORMED,"stride");
    if (!length) return map_state(MALFORMED,"empty");
    if (length % stride) return map_state(MALFORMED,"partial_descriptor");
    MapResult r={.state=OBSERVED,.stride=(uint32_t)stride,.version=version};
    const uint8_t *p=data;
    for (size_t i=0;i<length/stride;++i) {
        EFI_MEMORY_DESCRIPTOR d;
        const uint8_t *s=p+i*stride;   /* i*stride + sizeof d <= length <= capacity */
        uint8_t *t=(uint8_t *)&d;
        for (size_t j=0;j<sizeof d;++j) t[j]=s[j];
        if (d.pages > UINT64_MAX/EFI_PAGE_BYTES) return map_state(MALFORMED,"pages_overflow");
        uint64_t bytes=d.pages*EFI_PAGE_BYTES;
        if (!range_ok(d.physical,bytes) || !range_ok(d.virtual_address,bytes))
            return map_state(MALFORMED,"range_overflow");
        if (d.physical % EFI_PAGE_BYTES) return map_state(MALFORMED,"unaligned_physical");
        if (r.count<REPORTED_DESCRIPTORS) {
            r.entries[r.count].type=d.type;
            r.entries[r.count].physical=d.physical;
            r.entries[r.count].pages=d.pages;
            r.entries[r.count].attributes=d.attributes;
        }
        ++r.count;
    }
    return r;
}

/* Supported format: FDT header version >= 17 with last_comp_version <= 17
 * (readable by a v17 reader). Earlier versions are `unsupported` because
 * they lack size_dt_struct. Blocks must be in bounds, aligned and disjoint;
 * the reservation map must be terminated inside totalsize; the structure
 * block must hold exactly one unnamed root node followed by FDT_END. Only
 * the root `model` property value is extracted. */
enum { FDT_HEADER=40, FDT_BEGIN_NODE=1, FDT_END_NODE=2, FDT_PROP=3, FDT_NOP=4, FDT_END=9 };
static DtResult dt_state(DtResult r, State s, const char *detail) {
    r.state=s; r.detail=detail; r.model_state=NOT_PROBED; r.model_length=0; r.model[0]=0; return r;
}
static int disjoint(uint64_t a, uint64_t al, uint64_t b, uint64_t bl) { return a+al <= b || b+bl <= a; }
/* Skip a NUL-terminated name in [pos,end); returns the 4-aligned next token or 0. */
static size_t skip_name(const uint8_t *p, size_t pos, size_t end, size_t *name_length) {
    size_t start=pos;
    while (pos<end && p[pos]) ++pos;
    if (pos>=end) return 0;
    *name_length=pos-start;
    pos=(pos+4)&~(size_t)3;
    return pos<=end ? pos : 0;
}
/* A valid string longer than DT_MODEL_LIMIT is `truncated`, not malformed. */
static void take_model(DtResult *r, const uint8_t *v, uint32_t len) {
    if (r->model_state!=UNAVAILABLE) { r->model_state=MALFORMED; r->model_length=0; r->model[0]=0; return; }
    if (!len || v[len-1]) { r->model_state=MALFORMED; return; }
    for (uint32_t i=0;i+1<len;++i)
        if (!v[i] || v[i]>=0x80) { r->model_state=UNSUPPORTED; return; }   /* string list or non-ASCII */
    uint32_t n=len-1 > DT_MODEL_LIMIT ? DT_MODEL_LIMIT : len-1;
    for (uint32_t i=0;i<n;++i) r->model[i]=(char)v[i];
    r->model[n]=0;
    r->model_length=len-1;
    r->model_state=len-1 > DT_MODEL_LIMIT ? TRUNCATED : OBSERVED;
}
DtResult parse_dt(const uint8_t *p, size_t available) {
    DtResult r={.state=MALFORMED,.model_state=NOT_PROBED};
    if (!p) return dt_state(r,MALFORMED,"null");
    if (available < FDT_HEADER) return dt_state(r,MALFORMED,"header_truncated");
    if (be32(p) != 0xd00dfeed) return dt_state(r,MALFORMED,"magic");
    uint32_t total=be32(p+4), off_struct=be32(p+8), off_strings=be32(p+12), off_rsv=be32(p+16);
    r.version=be32(p+20); r.last_comp_version=be32(p+24);
    /* Checked before the v17-only size_dt_struct field is interpreted. */
    if (r.version < 17 || r.last_comp_version > 17 || r.last_comp_version > r.version)
        return dt_state(r,UNSUPPORTED,"version");
    if (total > DT_LIMIT) return dt_state(r,UNSUPPORTED,"size_limit");
    if (total < FDT_HEADER) return dt_state(r,MALFORMED,"totalsize");
    if (total > available) return dt_state(r,MALFORMED,"totalsize_exceeds_span");
    r.size=total;
    uint32_t size_strings=be32(p+32), size_struct=be32(p+36);
    if (off_rsv % 8 || off_struct % 4 || size_struct % 4) return dt_state(r,MALFORMED,"alignment");
    if (off_rsv < FDT_HEADER || off_struct < FDT_HEADER || off_strings < FDT_HEADER ||
        off_rsv > total || off_struct > total || size_struct > total-off_struct ||
        off_strings > total || size_strings > total-off_strings) return dt_state(r,MALFORMED,"block_bounds");
    /* The reservation map (16-byte entries ending with an all-zero entry)
     * must start outside the other blocks and terminate before the next
     * non-empty block that follows it (or totalsize). */
    if (!disjoint(off_rsv,1,off_struct,size_struct) || !disjoint(off_rsv,1,off_strings,size_strings) ||
        !disjoint(off_struct,size_struct,off_strings,size_strings)) return dt_state(r,MALFORMED,"block_overlap");
    size_t limit=total;
    if (size_struct && off_struct > off_rsv && off_struct < limit) limit=off_struct;
    if (size_strings && off_strings > off_rsv && off_strings < limit) limit=off_strings;
    size_t rsv=off_rsv;
    for (;;) {
        if (limit-rsv < 16) return dt_state(r,MALFORMED,"rsvmap_unterminated");
        uint64_t address=be64(p+rsv), size=be64(p+rsv+8);
        rsv+=16;
        if (!address && !size) break;
        if (!range_ok(address,size)) return dt_state(r,MALFORMED,"rsvmap_range");
    }
    const uint8_t *strings=p+off_strings;
    size_t pos=off_struct, end=(size_t)off_struct+size_struct;
    unsigned depth=0, roots=0;
    uint8_t has_child[DT_DEPTH_LIMIT]={0};
    r.model_state=UNAVAILABLE;
    for (;;) {
        if (end-pos < 4) return dt_state(r,MALFORMED,"missing_end");
        uint32_t token=be32(p+pos); pos+=4;
        if (token==FDT_NOP) continue;
        if (token==FDT_BEGIN_NODE) {
            size_t name_length=0;
            if (!depth && roots) return dt_state(r,MALFORMED,"multiple_roots");
            if (!(pos=skip_name(p,pos,end,&name_length))) return dt_state(r,MALFORMED,"name_truncated");
            if (!depth) { if (name_length) return dt_state(r,MALFORMED,"root_name"); ++roots; }
            if (depth==DT_DEPTH_LIMIT) return dt_state(r,UNSUPPORTED,"depth_limit");
            if (depth) has_child[depth-1]=1;
            has_child[depth++]=0;
        } else if (token==FDT_END_NODE) {
            if (!depth) return dt_state(r,MALFORMED,"unbalanced_end_node");
            has_child[--depth]=0;
        } else if (token==FDT_PROP) {
            if (!depth) return dt_state(r,MALFORMED,"property_outside_root");
            if (end-pos < 8) return dt_state(r,MALFORMED,"property_truncated");
            uint32_t len=be32(p+pos), name=be32(p+pos+4); pos+=8;
            if (len > end-pos) return dt_state(r,MALFORMED,"property_truncated");
            if (name >= size_strings) return dt_state(r,MALFORMED,"property_name");
            size_t n=name;
            while (n<size_strings && strings[n]) ++n;
            if (n==size_strings) return dt_state(r,MALFORMED,"property_name");
            /* Properties precede subnodes in every node, not only the root. */
            if (has_child[depth-1]) return dt_state(r,MALFORMED,"property_after_subnode");
            if (depth==1 && n-name==5 && strings[name]=='m' && strings[name+1]=='o' &&
                strings[name+2]=='d' && strings[name+3]=='e' && strings[name+4]=='l')
                take_model(&r,p+pos,len);
            pos=(pos+len+3)&~(size_t)3;   /* end is 4-aligned, so pos <= end */
        } else if (token==FDT_END) {
            if (depth || !roots) return dt_state(r,MALFORMED,"unterminated_node");
            if (pos!=end) return dt_state(r,MALFORMED,"trailing_structure");
            break;
        } else return dt_state(r,MALFORMED,"token");
    }
    r.state=OBSERVED; r.detail=0;
    return r;
}

/* Firmware-owned blob with no caller-supplied length. Reads 8 bytes to
 * check magic and totalsize, then only [p, p + totalsize) when totalsize
 * is within DT_LIMIT. totalsize is a declared length, not proof that the
 * memory is readable; a bad firmware pointer cannot be detected here. */
DtResult parse_firmware_dt(const uint8_t *p) {
    DtResult r={.state=MALFORMED,.model_state=NOT_PROBED};
    if (!p) { r.detail="null"; return r; }
    if (be32(p) != 0xd00dfeed) { r.detail="magic"; return r; }
    uint32_t total=be32(p+4);
    if (total > DT_LIMIT) { r.state=UNSUPPORTED; r.detail="size_limit"; return r; }
    if (total < FDT_HEADER) { r.detail="totalsize"; return r; }
    return parse_dt(p,total);
}

/* GetVariable policy (buffer is exactly one byte):
 *   SUCCESS, size 1, value 0/1 -> observed
 *   SUCCESS with size != 1 or value > 1 -> malformed
 *   NOT_FOUND -> unavailable; BUFFER_TOO_SMALL -> malformed (not 1 byte)
 *   UNSUPPORTED -> unsupported; other errors -> unavailable;
 *   non-zero warning -> unsupported. The raw status is always reported. */
VarResult parse_var(EFI_STATUS s, size_t size, uint8_t v) {
    VarResult r={.status=s};
    if (s==EFI_NOT_FOUND) { r.state=UNAVAILABLE; r.detail="not_found"; }
    else if (s==EFI_BUFFER_TOO_SMALL) { r.state=MALFORMED; r.detail="variable_size"; }
    else if (s==EFI_UNSUPPORTED) { r.state=UNSUPPORTED; r.detail="service_unsupported"; }
    else if (EFI_ERROR(s)) { r.state=UNAVAILABLE; r.detail="efi_error"; }
    else if (s!=EFI_SUCCESS) { r.state=UNSUPPORTED; r.detail="warning_status"; }
    else if (size!=1) { r.state=MALFORMED; r.detail="variable_size"; }
    else if (v>1) { r.state=MALFORMED; r.detail="value_range"; }
    else { r.state=OBSERVED; r.value=v; }
    return r;
}
ElResult decode_el(uint64_t raw) {
    ElResult r={.state=MALFORMED,.raw=raw};
    if (!(raw & ~UINT64_C(0xc)) && ((raw>>2)&3)) {
        r.state=OBSERVED; r.level=(unsigned)((raw>>2)&3);
    }
    return r;
}
/* FirmwareVendor is a NUL-terminated UCS-2 string. Surrogate code units are
 * outside UCS-2 and reported as unsupported rather than guessed. */
VendorResult read_vendor(const CHAR16 *text) {
    VendorResult r={.state=UNAVAILABLE};
    if (!text) return r;
    unsigned i=0;
    for (;i<VENDOR_LIMIT && text[i];++i) {
        if (text[i]>=0xd800 && text[i]<=0xdfff) { r.state=UNSUPPORTED; r.units=0; r.text[0]=0; return r; }
        r.text[i]=text[i];
    }
    r.text[i]=0; r.units=i;
    r.state=(i==VENDOR_LIMIT && text[i]) ? TRUNCATED : OBSERVED;
    return r;
}
const char *state_name(State s) {
    static const char *names[]={"observed","unavailable","unsupported","malformed","not_probed","truncated"};
    return (unsigned)s<6 ? names[s] : "malformed";
}
void put(Writer *w, const char *s) {
    while (*s) {
        if (w->length+1<w->capacity) w->data[w->length++]=*s;
        else w->truncated=1;
        ++s;
    }
    if (w->capacity) w->data[w->length]=0;
}
/* Every non-printable-ASCII unit becomes a \\uXXXX escape of its code unit,
 * so the record itself is pure printable ASCII. */
static void unit(Writer *w, unsigned c) {
    static const char hex[]="0123456789abcdef";
    if (c=='"' || c=='\\') { char b[3]={'\\',(char)c,0}; put(w,b); }
    else if (c<32 || c>=127) {
        char b[7]={'\\','u',hex[(c>>12)&15],hex[(c>>8)&15],hex[(c>>4)&15],hex[c&15],0}; put(w,b);
    } else { char b[2]={(char)c,0}; put(w,b); }
}
void escaped(Writer *w, const char *s) {
    for (;*s;++s) unit(w,(unsigned char)*s);
}
void escaped16(Writer *w, const CHAR16 *s, unsigned units) {
    for (unsigned i=0;i<units;++i) unit(w,s[i]);
}
