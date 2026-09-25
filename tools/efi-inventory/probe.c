#include "core.h"
/* Firmware pointers are borrowed. Only AllocatePool memory is owned here. */
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
    char b[19]; b[0]='0';b[1]='x';b[18]=0;
    for (unsigned i=0;i<16;++i) { b[17-i]=h[v&15]; v>>=4; }
    put(w,b);
}
static void field(Writer *w,const char *key,State s) {
    put(w,"\"");put(w,key);put(w,"_status\":\"");put(w,state_name(s));put(w,"\",");
}
static void print(EFI_SYSTEM_TABLE *st,const char *s) {
    if (!st->con_out || !st->con_out->output_string) return;
    CHAR16 buf[128];
    while (*s) {
        unsigned n=0;
        while (n<126 && *s) buf[n++]=(unsigned char)*s++;
        buf[n]=0; st->con_out->output_string(st->con_out,buf);
    }
}
/* Bounded initial/retry allocation; no service calls after returning the map. */
static MapResult acquire_map(EFI_BOOT_SERVICES *bs) {
    MapResult r={.state=UNAVAILABLE};
    if (!bs || !bs->allocate_pool || !bs->free_pool || !bs->get_memory_map) return r;
    size_t cap=4096;
    for (unsigned attempt=0;attempt<MAP_ATTEMPTS;++attempt) {
        EFI_MEMORY_DESCRIPTOR *buffer=0;
        EFI_STATUS allocation=bs->allocate_pool(EFI_LOADER_DATA,cap,(void **)&buffer);
        if (EFI_ERROR(allocation) || !buffer) {
            if (buffer) bs->free_pool(buffer);
            return r;
        }
        size_t size=cap,key=0,stride=0; uint32_t version=0;
        EFI_STATUS s=bs->get_memory_map(&size,buffer,&key,&stride,&version);
        if (s==EFI_BUFFER_TOO_SMALL) {
            bs->free_pool(buffer);
            if (!stride || stride>MAP_LIMIT/2 || size>MAP_LIMIT-2*stride) return r;
            size_t next=size+2*stride;
            if (next<=cap) { if (cap>MAP_LIMIT/2) return r; next=cap*2; }
            if (next>MAP_LIMIT) return r;
            cap=next; continue;
        }
        if (!EFI_ERROR(s)) r=parse_map(buffer,size,stride,version);
        bs->free_pool(buffer);
        return r;
    }
    return r;
}
static VarResult variable(EFI_SYSTEM_TABLE *st,const CHAR16 *name) {
    if (!st->runtime_services) return (VarResult){.state=UNAVAILABLE};
    EFI_RUNTIME_SERVICES_PREFIX *rt=st->runtime_services;
    if (!rt->get_variable) return (VarResult){.state=UNAVAILABLE};
    uint8_t value=0; size_t size=1; uint32_t attributes=0;
    return parse_var(rt->get_variable((CHAR16 *)name,(EFI_GUID *)&global_guid,&attributes,&size,&value),size,value);
}
EFI_STATUS efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *st) {
    if (!st || !st->boot_services) return 0;
    EFI_BOOT_SERVICES *bs=st->boot_services;
    EFI_LOADED_IMAGE *loaded=0;
    if (bs->handle_protocol && EFI_ERROR(bs->handle_protocol(image,(EFI_GUID *)&loaded_image_guid,(void **)&loaded)))
        loaded=0;
    State image_state=loaded ? OBSERVED : UNAVAILABLE;
    /* FilePath is a firmware-owned variable-length object with no supplied
     * allocation size. Presence is safe to report; do not dereference it. */
    State path_state=loaded && loaded->file_path ? OBSERVED : UNAVAILABLE;
    MapResult map=acquire_map(bs);
    DtResult dt={.state=UNAVAILABLE};
    if (st->configuration_table) {
        /* A corrupted table count cannot cause an unbounded walk. */
        size_t n=st->number_of_table_entries;
        if (n>256) n=256;
        for (size_t i=0;i<n;++i) {
            EFI_CONFIGURATION_TABLE *entry=&st->configuration_table[i];
            if (guid_equal(&entry->guid,&dt_guid)) {
                /* Firmware owns the blob; total size is validated before parsing.
                 * A bad firmware pointer is outside the protection of UEFI C code. */
                if (entry->table) {
                    const uint8_t *p=entry->table;
                    size_t len=(size_t)p[4]<<24 | (size_t)p[5]<<16 | (size_t)p[6]<<8 | p[7];
                    dt=parse_dt(p,len<=DT_LIMIT ? len : 0);
                } else dt.state=MALFORMED;
                break;
            }
        }
    }
    VarResult secure=variable(st,secure_boot),setup=variable(st,setup_mode);
    ElResult el=decode_el(probe_current_el());
    char record[2048]; Writer w={record,0,sizeof record,0};
    put(&w,"{\"schema\":1,\"build\":\"task004-phase-a-v1\",");
    field(&w,"firmware",st->firmware_vendor ? OBSERVED : UNAVAILABLE);
    put(&w,"\"vendor\":");
    if (st->firmware_vendor) {
        put(&w,"\"");
        char name[65]; unsigned i=0;
        while (i<64 && st->firmware_vendor[i] && st->firmware_vendor[i]<128) {
            name[i]=(char)st->firmware_vendor[i]; ++i;
        }
        name[i]=0; escaped(&w,name);put(&w,"\"");
    } else put(&w,"null");
    put(&w,",\"firmware_revision\":");number(&w,st->firmware_revision);
    put(&w,",\"uefi_revision\":");number(&w,st->hdr.revision);
    put(&w,",");field(&w,"image",image_state);
    if (loaded) { put(&w,"\"image_base\":\"");hex(&w,(uintptr_t)loaded->image_base);
        put(&w,"\",\"image_size\":");number(&w,loaded->image_size);put(&w,","); }
    field(&w,"device_path",path_state);put(&w,"\"path_representation\":\"pointer_present_only\"");
    put(&w,",");field(&w,"boot_services_map",map.state);
    put(&w,"\"map_count\":");if (map.state==OBSERVED) number(&w,map.count);else put(&w,"null");
    put(&w,",\"descriptor_stride\":");if (map.state==OBSERVED) number(&w,map.stride);else put(&w,"null");
    put(&w,",\"descriptor_version\":");if (map.state==OBSERVED) number(&w,map.version);else put(&w,"null");
    put(&w,",\"map_descriptors\":[");
    for (uint32_t i=0;i<map.count && i<REPORTED_DESCRIPTORS;++i) {
        if (i) put(&w,",");
        put(&w,"{\"type\":");number(&w,map.entries[i].type);
        put(&w,",\"physical\":\"");hex(&w,map.entries[i].physical);
        put(&w,"\",\"pages\":");number(&w,map.entries[i].pages);
        put(&w,",\"attributes\":\"");hex(&w,map.entries[i].attributes);put(&w,"\"}");
    }
    put(&w,"],\"map_omitted\":");number(&w,map.count>REPORTED_DESCRIPTORS ? map.count-REPORTED_DESCRIPTORS : 0);
    put(&w,",");field(&w,"firmware_dt",dt.state);
    put(&w,"\"dt_size\":");if (dt.state==OBSERVED) number(&w,dt.size);else put(&w,"null");
    put(&w,",\"dt_version\":");if (dt.state==OBSERVED) number(&w,dt.version);else put(&w,"null");
    put(&w,",\"dt_model\":");if (dt.state==OBSERVED && dt.model[0]) {
        put(&w,"\"");escaped(&w,dt.model);put(&w,"\"");
    } else put(&w,"null");
    put(&w,",");field(&w,"SecureBoot",secure.state);
    if (secure.state==OBSERVED) { put(&w,"\"SecureBoot\":");number(&w,secure.value);put(&w,","); }
    field(&w,"SetupMode",setup.state);
    if (setup.state==OBSERVED) { put(&w,"\"SetupMode\":");number(&w,setup.value);put(&w,","); }
    field(&w,"CurrentEL",el.state);
    put(&w,"\"current_el_raw\":\"");hex(&w,el.raw);put(&w,"\",");
    put(&w,"\"reported_level\":");if (el.state==OBSERVED) number(&w,el.level);else put(&w,"null");
    put(&w,",\"execution_context\":\"not_established\",\"truncated\":false}");
    print(st,"EFI inventory Phase A: boot-services snapshot; returning to caller\r\n");
    if (w.truncated) print(st,"{\"schema\":1,\"truncated\":true}\r\n");
    else { print(st,record);print(st,"\r\n"); }
    return 0;
}
