#include "core.h"
/* Ownership: every firmware pointer (system table, services, protocols,
 * configuration tables, FirmwareVendor, FDT blob) is borrowed and never
 * freed. The only owned memory is a successful AllocatePool buffer, released
 * exactly once with FreePool on every path. A failed AllocatePool transfers
 * no ownership, so nothing is freed. A failed FreePool is recorded and not
 * retried; no further allocation is attempted after it.
 *
 * Trust boundary: C code cannot check that firmware pointers are readable.
 * The probe reads at most VENDOR_LIMIT+1 CHAR16 units of FirmwareVendor and,
 * for the FDT, the 8-byte magic/totalsize prefix and then at most the
 * declared totalsize (capped at DT_LIMIT). */
extern uint64_t probe_current_el(void);
static const CHAR16 secure_boot[]={ 'S','e','c','u','r','e','B','o','o','t',0 };
static const CHAR16 setup_mode[]={ 'S','e','t','u','p','M','o','d','e',0 };
static int guid_equal(const EFI_GUID *a,const EFI_GUID *b) {
    const uint8_t *x=(const uint8_t *)a,*y=(const uint8_t *)b;
    for (size_t i=0;i<sizeof *a;++i) if (x[i]!=y[i]) return 0;
    return 1;
}
static void number(Writer *w,uint64_t v) {
    char b[21]; unsigned i=20; b[i]=0;
    do { b[--i]=(char)('0'+v%10); v/=10; } while (v);
    put(w,b+i);
}
static void hex(Writer *w,uint64_t v) {
    static const char h[]="0123456789abcdef";
    char b[21]; b[0]='"';b[1]='0';b[2]='x';b[19]='"';b[20]=0;
    for (unsigned i=0;i<16;++i) { b[18-i]=h[v&15]; v>>=4; }
    put(w,b);
}
static void key(Writer *w,const char *k) { put(w,",\""); put(w,k); put(w,"\":"); }
static void status_field(Writer *w,const char *k,State s,const char *d) {
    key(w,k); put(w,"{\"status\":\""); put(w,state_name(s)); put(w,"\",\"detail\":");
    if (d) { put(w,"\""); put(w,d); put(w,"\""); } else put(w,"null");
}

/* Output policy: text is emitted in chunks of at most 126 CHAR16 units.
 * Missing ConOut/OutputString -> EFI_UNSUPPORTED. The first error status
 * stops all further output and is returned. Warnings (e.g.
 * EFI_WARN_UNKNOWN_GLYPH) mean text was displayed and are tolerated. */
static EFI_STATUS print(EFI_SYSTEM_TABLE *st,const char *s) {
    if (!st->con_out || !st->con_out->output_string) return EFI_UNSUPPORTED;
    CHAR16 buf[128];
    while (*s) {
        unsigned n=0;
        while (n<126 && *s) buf[n++]=(unsigned char)*s++;
        buf[n]=0;
        EFI_STATUS status=st->con_out->output_string(st->con_out,buf);
        if (EFI_ERROR(status)) return status;
    }
    return EFI_SUCCESS;
}

typedef struct { MapResult map; EFI_STATUS efi_status, release_status; unsigned attempts, releases; } MapAcquisition;
static int release(EFI_BOOT_SERVICES *bs,void *buffer,MapAcquisition *a) {
    EFI_STATUS s=bs->free_pool(buffer);
    ++a->releases;
    if (EFI_ERROR(s) && !EFI_ERROR(a->release_status)) a->release_status=s;
    return !EFI_ERROR(s);
}
static MapResult map_fail(State s,const char *d) { MapResult r={.state=s,.detail=d}; return r; }
/* At most MAP_ATTEMPTS allocations of at most MAP_LIMIT bytes. The returned
 * size is checked against the capacity actually allocated and passed to
 * GetMemoryMap before any descriptor is read (in parse_map). */
static MapAcquisition acquire_map(EFI_BOOT_SERVICES *bs) {
    MapAcquisition a={.map={.state=UNAVAILABLE,.detail="service_missing"}};
    if (!bs || !bs->allocate_pool || !bs->free_pool || !bs->get_memory_map) return a;
    size_t cap=MAP_INITIAL;
    for (;;) {
        if (a.attempts==MAP_ATTEMPTS) { a.map=map_fail(UNSUPPORTED,"retry_limit"); return a; }
        ++a.attempts;
        void *buffer=0;
        EFI_STATUS allocation=bs->allocate_pool(EFI_LOADER_DATA,cap,&buffer);
        if (EFI_ERROR(allocation)) { a.efi_status=allocation; a.map=map_fail(UNAVAILABLE,"allocation_failed"); return a; }
        if (!buffer) { a.map=map_fail(MALFORMED,"allocation_null"); return a; }
        size_t size=cap,map_key=0,stride=0; uint32_t version=0;
        EFI_STATUS s=bs->get_memory_map(&size,buffer,&map_key,&stride,&version);
        a.efi_status=s;
        if (s==EFI_BUFFER_TOO_SMALL) {
            size_t next=0;
            MapResult grow=map_next_capacity(cap,size,stride,&next);
            if (!release(bs,buffer,&a)) { a.map=map_fail(NOT_PROBED,"release_failed"); return a; }
            if (grow.state!=OBSERVED) { a.map=grow; return a; }
            cap=next; continue;
        }
        if (EFI_ERROR(s)) a.map=map_fail(UNAVAILABLE,"efi_error");
        else if (s!=EFI_SUCCESS) a.map=map_fail(UNSUPPORTED,"warning_status");
        else a.map=parse_map(buffer,cap,size,stride,version);
        release(bs,buffer,&a);
        return a;
    }
}
/* GetVariable runs in its own full expression; its outputs are read only
 * after it returns, independent of argument evaluation order. */
static VarResult variable(EFI_SYSTEM_TABLE *st,const CHAR16 *name) {
    EFI_RUNTIME_SERVICES *rt=st->runtime_services;
    if (!rt || !rt->get_variable) return (VarResult){.state=UNAVAILABLE,.detail="service_missing"};
    uint8_t value=0; size_t size=sizeof value; uint32_t attributes=0;
    EFI_STATUS status=rt->get_variable((CHAR16 *)name,(EFI_GUID *)&global_guid,&attributes,&size,&value);
    return parse_var(status,size,value);
}
typedef struct { DtResult dt; size_t searched, total; } DtSearch;
static DtSearch find_dt(EFI_SYSTEM_TABLE *st) {
    DtSearch s={.dt={.state=UNAVAILABLE,.detail="absent",.model_state=NOT_PROBED}};
    s.total=st->number_of_table_entries;
    if (!s.total) return s;
    if (!st->configuration_table) { s.dt.state=MALFORMED; s.dt.detail="config_table_null"; return s; }
    size_t n=s.total>CONFIG_TABLE_LIMIT ? CONFIG_TABLE_LIMIT : s.total;
    for (size_t i=0;i<n;++i) {
        EFI_CONFIGURATION_TABLE *entry=&st->configuration_table[i];
        s.searched=i+1;
        if (!guid_equal(&entry->guid,&dt_guid)) continue;
        if (!entry->table) { s.dt.state=MALFORMED; s.dt.detail="table_null"; return s; }
        s.dt=parse_firmware_dt(entry->table);
        return s;
    }
    /* Entries past the limit were not inspected: absence is not confirmed. */
    if (s.total>n) { s.dt.state=NOT_PROBED; s.dt.detail="search_limit"; }
    return s;
}

static void var_field(Writer *w,const char *k,VarResult v) {
    status_field(w,k,v.state,v.detail);
    key(w,"efi_status"); hex(w,v.status);
    key(w,"value"); if (v.state==OBSERVED) number(w,v.value); else put(w,"null");
    put(w,"}");
}
/* Record schema 2. Every observation is an object with `status` and
 * `detail`; values that were not observed are JSON null. */
static void build_record(Writer *w,EFI_SYSTEM_TABLE *st,EFI_LOADED_IMAGE *loaded,EFI_STATUS image_status,
                         const MapAcquisition *m,const DtSearch *dt,VarResult secure,VarResult setup,ElResult el) {
    VendorResult vendor=read_vendor(st->firmware_vendor);
    put(w,"{\"schema\":2"); key(w,"build"); put(w,"\"task004-phase-a-v2\"");
    status_field(w,"firmware_vendor",vendor.state,vendor.state==UNSUPPORTED ? "ucs2_surrogate" : 0);
    key(w,"units"); number(w,vendor.units);
    key(w,"value");
    if (vendor.state==OBSERVED || vendor.state==TRUNCATED) { put(w,"\""); escaped16(w,vendor.text,vendor.units); put(w,"\""); }
    else put(w,"null");
    put(w,"}");
    key(w,"firmware_revision"); number(w,st->firmware_revision);
    key(w,"uefi_revision"); number(w,st->hdr.revision);
    status_field(w,"loaded_image",loaded ? OBSERVED : UNAVAILABLE,loaded ? 0 : "handle_protocol");
    key(w,"efi_status"); hex(w,image_status);
    key(w,"base"); if (loaded) hex(w,(uintptr_t)loaded->image_base); else put(w,"null");
    key(w,"size"); if (loaded) number(w,loaded->image_size); else put(w,"null");
    put(w,"}");
    /* FilePath is a variable-length firmware object with no supplied size;
     * only pointer presence is reported, it is never dereferenced. */
    status_field(w,"device_path",loaded && loaded->file_path ? OBSERVED : loaded ? UNAVAILABLE : NOT_PROBED,0);
    key(w,"representation"); put(w,"\"pointer_present_only\"}");
    const MapResult *map=&m->map;
    status_field(w,"boot_services_map",map->state,map->detail);
    key(w,"efi_status"); hex(w,m->efi_status);
    key(w,"attempts"); number(w,m->attempts);
    key(w,"release_status"); hex(w,m->release_status);
    key(w,"count"); if (map->state==OBSERVED) number(w,map->count); else put(w,"null");
    key(w,"descriptor_stride"); if (map->state==OBSERVED) number(w,map->stride); else put(w,"null");
    key(w,"descriptor_version"); if (map->state==OBSERVED) number(w,map->version); else put(w,"null");
    key(w,"descriptors"); put(w,"[");
    for (uint32_t i=0;map->state==OBSERVED && i<map->count && i<REPORTED_DESCRIPTORS;++i) {
        if (i) put(w,",");
        put(w,"{\"type\":"); number(w,map->entries[i].type);
        key(w,"physical"); hex(w,map->entries[i].physical);
        key(w,"pages"); number(w,map->entries[i].pages);
        key(w,"attributes"); hex(w,map->entries[i].attributes); put(w,"}");
    }
    put(w,"]");
    key(w,"omitted");
    if (map->state==OBSERVED) number(w,map->count>REPORTED_DESCRIPTORS ? map->count-REPORTED_DESCRIPTORS : 0);
    else put(w,"null");
    put(w,"}");
    const DtResult *d=&dt->dt;
    int dt_ok=d->state==OBSERVED;
    status_field(w,"firmware_dt",d->state,d->detail);
    key(w,"tables_searched"); number(w,dt->searched);
    key(w,"tables_total"); number(w,dt->total);
    key(w,"size"); if (dt_ok) number(w,d->size); else put(w,"null");
    key(w,"version"); if (dt_ok) number(w,d->version); else put(w,"null");
    key(w,"last_comp_version"); if (dt_ok) number(w,d->last_comp_version); else put(w,"null");
    status_field(w,"model",dt_ok ? d->model_state : NOT_PROBED,0);
    int has_model=dt_ok && (d->model_state==OBSERVED || d->model_state==TRUNCATED);
    key(w,"length"); if (has_model) number(w,d->model_length); else put(w,"null");
    key(w,"value"); if (has_model) { put(w,"\""); escaped(w,d->model); put(w,"\""); } else put(w,"null");
    put(w,"}}");
    var_field(w,"SecureBoot",secure);
    var_field(w,"SetupMode",setup);
    status_field(w,"CurrentEL",el.state,0);
    key(w,"raw"); hex(w,el.raw);
    key(w,"reported_level"); if (el.state==OBSERVED) number(w,el.level); else put(w,"null");
    put(w,"}");
    key(w,"execution_context"); put(w,"\"not_established\"");
    key(w,"record_truncated"); put(w,"false}");
}
/* Return policy:
 *   EFI_INVALID_PARAMETER - no system table;
 *   EFI_UNSUPPORTED       - no usable ConOut/OutputString (nothing emitted);
 *   OutputString error    - first failing status (output stopped there);
 *   EFI_BUFFER_TOO_SMALL  - record exceeded RECORD_CAPACITY; only the
 *                           fixed truncation marker was emitted;
 *   EFI_SUCCESS           - full record emitted. Observation failures are
 *                           reported inside the record, not as errors. */
EFI_STATUS efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *st) {
    if (!st) return EFI_INVALID_PARAMETER;
    if (!st->con_out || !st->con_out->output_string) return EFI_UNSUPPORTED;
    EFI_BOOT_SERVICES *bs=st->boot_services;
    EFI_LOADED_IMAGE *loaded=0;
    EFI_STATUS image_status=EFI_UNSUPPORTED;
    if (bs && bs->handle_protocol) {
        void *interface=0;
        image_status=bs->handle_protocol(image,(EFI_GUID *)&loaded_image_guid,&interface);
        if (!EFI_ERROR(image_status)) loaded=interface;
    }
    MapAcquisition map=acquire_map(bs);
    DtSearch dt=find_dt(st);
    VarResult secure=variable(st,secure_boot);
    VarResult setup=variable(st,setup_mode);
    ElResult el=decode_el(probe_current_el());
    /* Static (writable, non-executable .bss) rather than stack storage: a
     * multi-KiB stack object would require a __chkstk probe routine that
     * this image deliberately does not provide. The probe is one-shot. */
    static char record[RECORD_CAPACITY];
    Writer w={record,0,sizeof record,0};
    build_record(&w,st,loaded,image_status,&map,&dt,secure,setup,el);
    EFI_STATUS s=print(st,"EFI inventory Phase A: boot-services snapshot; returning to caller\r\n");
    if (EFI_ERROR(s)) return s;
    if (w.truncated) {
        s=print(st,"{\"schema\":2,\"record_truncated\":true}\r\n");
        return EFI_ERROR(s) ? s : EFI_BUFFER_TOO_SMALL;
    }
    s=print(st,record);
    if (EFI_ERROR(s)) return s;
    s=print(st,"\r\n");
    return EFI_ERROR(s) ? s : EFI_SUCCESS;
}
