/* Host-only firmware doubles. They exercise the application through
 * efi_main() and the parsers directly; they do not validate any firmware or
 * the AArch64 calling convention. Each case writes the captured JSON record
 * to <records>/<case>.json for check_records.py. */
#include "core.h"
#include "fixtures.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern EFI_STATUS efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE *);

static const char *current, *record_dir;
static unsigned case_failed;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %s: %s:%d: %s\n",current,__FILE__,__LINE__,#c); case_failed=1; } } while (0)

/* ---- firmware double state (reset before every case) ---- */
typedef struct { EFI_STATUS status; size_t size; size_t stride; uint32_t version; unsigned descriptors; } MapStep;
#define MAX_ALLOCS 8
static struct {
    uint64_t el; unsigned el_reads;
    MapStep steps[MAX_ALLOCS]; unsigned nsteps, map_calls;
    EFI_MEMORY_DESCRIPTOR custom; int use_custom;
    void *live[MAX_ALLOCS]; size_t sizes[MAX_ALLOCS]; unsigned allocs, frees, bad_frees, alloc_calls;
    unsigned alloc_fail_at; int alloc_null; EFI_STATUS free_status;
    EFI_STATUS var_status[2]; size_t var_size[2]; uint8_t var_value[2]; unsigned var_calls[2], var_bad;
    unsigned out_calls, out_fail_at, out_overlong; EFI_STATUS out_fail_status, out_status;
    char console[32768]; size_t used;
    unsigned forbidden, protocol_calls;
    EFI_STATUS image_status;
} fw;
static EFI_BOOT_SERVICES bs; static EFI_RUNTIME_SERVICES rt; static EFI_SIMPLE_TEXT_OUTPUT out;
static EFI_SYSTEM_TABLE st; static EFI_LOADED_IMAGE image; static EFI_STATUS returned;
static EFI_CONFIGURATION_TABLE tables[300];
static CHAR16 vendor[80];

uint64_t probe_current_el(void) { ++fw.el_reads; return fw.el; }
static EFI_STATUS forbidden_call(void) { ++fw.forbidden; return EFI_UNSUPPORTED; }
/* Every function slot after the table header starts as a sentinel. */
static void poison(void *table, size_t header, size_t size) {
    for (size_t o=header;o<size;o+=sizeof(void *)) { void *f=(void *)forbidden_call; memcpy((char *)table+o,&f,sizeof f); }
}

static EFI_STATUS alloc(uint32_t type,size_t n,void **p) {
    ++fw.alloc_calls;
    CHECK(type==EFI_LOADER_DATA && n>0 && n<=MAP_LIMIT && p);
    if (fw.alloc_fail_at==fw.alloc_calls) { *p=0; return EFI_OUT_OF_RESOURCES; }
    if (fw.alloc_null) { *p=0; return EFI_SUCCESS; }
    CHECK(fw.allocs<MAX_ALLOCS);
    if (fw.allocs>=MAX_ALLOCS) return EFI_OUT_OF_RESOURCES;
    *p=malloc(n);   /* exact size: any read past n is an ASan finding */
    fw.live[fw.allocs]=*p; fw.sizes[fw.allocs]=n; ++fw.allocs;
    return EFI_SUCCESS;
}
static EFI_STATUS release(void *p) {
    for (unsigned i=0;i<fw.allocs;++i) if (fw.live[i]==p && p) {
        fw.live[i]=0; ++fw.frees; free(p); return fw.free_status;
    }
    ++fw.bad_frees; return EFI_INVALID_PARAMETER;
}
static size_t alloc_size(const void *p) {
    for (unsigned i=0;i<fw.allocs;++i) if (fw.live[i]==p) return fw.sizes[i];
    return 0;
}
/* Scripted GetMemoryMap. Never writes outside the buffer it was given. */
static EFI_STATUS get_map(size_t *n,EFI_MEMORY_DESCRIPTOR *map,size_t *key,size_t *stride,uint32_t *version) {
    CHECK(n && map && key && stride && version);
    CHECK(*n==alloc_size(map));
    size_t capacity=*n;
    MapStep s=fw.steps[fw.map_calls<fw.nsteps ? fw.map_calls : fw.nsteps-1];
    ++fw.map_calls;
    *key=0x77; *stride=s.stride; *version=s.version; *n=s.size;
    uint8_t *base=(uint8_t *)map;
    for (unsigned i=0;i<s.descriptors && (size_t)(i+1)*s.stride<=capacity;++i) {
        memset(base+i*s.stride,0xa5,s.stride);
        EFI_MEMORY_DESCRIPTOR d={.type=i+1,.physical=UINT64_C(0x80000000)+UINT64_C(0x10000)*i,
            .virtual_address=0,.pages=i+1,.attributes=UINT64_C(0x8000000000000000)|i};
        if (fw.use_custom && i==0) d=fw.custom;
        memcpy(base+i*s.stride,&d,sizeof d);
    }
    return s.status;
}
static int name_is(const CHAR16 *a,const char *b) {
    while (*a && *b && *a==(unsigned char)*b) { ++a; ++b; }
    return !*a && !*b;
}
static EFI_STATUS get_var(CHAR16 *name,EFI_GUID *guid,uint32_t *attributes,size_t *size,void *data) {
    int i=name_is(name,"SecureBoot") ? 0 : name_is(name,"SetupMode") ? 1 : -1;
    if (i<0 || memcmp(guid,&global_guid,sizeof *guid) || !size || !data || *size!=1) { ++fw.var_bad; return EFI_INVALID_PARAMETER; }
    ++fw.var_calls[i];
    EFI_STATUS s=fw.var_status[i];
    if (s==EFI_BUFFER_TOO_SMALL) { *size=fw.var_size[i]; return s; }
    if (EFI_ERROR(s)) return s;
    if (attributes) *attributes=6;
    *(uint8_t *)data=fw.var_value[i];   /* writes one byte only */
    *size=fw.var_size[i];               /* may claim a different size */
    return s;
}
static EFI_STATUS output(EFI_SIMPLE_TEXT_OUTPUT *self,const CHAR16 *p) {
    CHECK(self==&out && p);
    ++fw.out_calls;
    size_t n=0; while (p[n]) ++n;
    if (n>126) ++fw.out_overlong;
    if (fw.out_fail_at && fw.out_calls>=fw.out_fail_at) return fw.out_fail_status;
    for (size_t i=0;i<n;++i) { CHECK(p[i]<128); if (fw.used+1<sizeof fw.console) fw.console[fw.used++]=(char)p[i]; }
    fw.console[fw.used]=0;
    return fw.out_status;
}
static EFI_STATUS handle_protocol(EFI_HANDLE h,EFI_GUID *g,void **p) {
    ++fw.protocol_calls;
    CHECK(h==(EFI_HANDLE)&image && !memcmp(g,&loaded_image_guid,sizeof *g) && p);
    if (EFI_ERROR(fw.image_status)) { *p=0; return fw.image_status; }
    *p=&image; return EFI_SUCCESS;
}
static void set_vendor(const char *s) {
    size_t i=0; for (;s[i] && i+1<sizeof vendor/sizeof *vendor;++i) vendor[i]=(unsigned char)s[i];
    vendor[i]=0;
}
static void standard_map(void) {
    fw.nsteps=1; fw.steps[0]=(MapStep){EFI_SUCCESS,3*sizeof(EFI_MEMORY_DESCRIPTOR),sizeof(EFI_MEMORY_DESCRIPTOR),1,3};
}
static void reset(void) {
    memset(&fw,0,sizeof fw); memset(tables,0,sizeof tables);
    fw.el=8; fw.var_status[0]=fw.var_status[1]=EFI_NOT_FOUND; fw.var_size[0]=fw.var_size[1]=1;
    standard_map();
    poison(&bs,sizeof(EFI_TABLE_HEADER),sizeof bs); poison(&rt,sizeof(EFI_TABLE_HEADER),sizeof rt);
    poison(&out,0,sizeof out);
    bs.allocate_pool=alloc; bs.free_pool=release; bs.get_memory_map=get_map; bs.handle_protocol=handle_protocol;
    rt.get_variable=get_var; out.output_string=output;
    memset(&image,0,sizeof image);
    image.image_base=(void *)(uintptr_t)0x40000000; image.image_size=0x3000; image.file_path=&image;
    set_vendor("EDK II");
    memset(&st,0,sizeof st);
    st.hdr.revision=(2u<<16)|70; st.firmware_vendor=vendor; st.firmware_revision=0x10000;
    st.con_out=&out; st.boot_services=&bs; st.runtime_services=&rt;
}
static void add_table(size_t index,const EFI_GUID *g,const void *p) {
    tables[index].guid=*g; tables[index].table=(void *)p;
    if (st.number_of_table_entries<=index) st.number_of_table_entries=index+1;
    st.configuration_table=tables;
}
static const EFI_GUID other_guid={0x12345678,0x1234,0x5678,{1,2,3,4,5,6,7,8}};
/* Copy into an exact-size heap buffer so any read past it is an ASan finding. */
static uint8_t *heap_copy(const uint8_t *p,size_t n) { uint8_t *c=malloc(n ? n : 1); memcpy(c,p,n); return c; }
static const char *record(void) {
    const char *j=strchr(fw.console,'{');
    return j ? j : "";
}
static void run(void) {
    returned=efi_main((EFI_HANDLE)&image,&st);
    char path[512]; snprintf(path,sizeof path,"%s/%s.json",record_dir,current);
    FILE *f=fopen(path,"w"); CHECK(f);
    if (f) { const char *j=record(); fwrite(j,1,strcspn(j,"\r"),f); fclose(f); }
}
static int has(const char *fragment) { return strstr(fw.console,fragment)!=0; }

/* ---------- GetVariable wrapper (through efi_main) ---------- */
static void var_case(EFI_STATUS s0,size_t z0,uint8_t v0,EFI_STATUS s1,size_t z1,uint8_t v1) {
    fw.var_status[0]=s0; fw.var_size[0]=z0; fw.var_value[0]=v0;
    fw.var_status[1]=s1; fw.var_size[1]=z1; fw.var_value[1]=v1;
    run();
    CHECK(fw.var_calls[0]==1 && fw.var_calls[1]==1 && !fw.var_bad);
}
/* Reviewed GCC 14.2 defect: firmware wrote 1, report said 0. */
static void t_var_secure1_setup0(void) {
    var_case(EFI_SUCCESS,1,1,EFI_SUCCESS,1,0);
    CHECK(has("\"SecureBoot\":{\"status\":\"observed\",\"detail\":null,\"efi_status\":\"0x0000000000000000\",\"value\":1}"));
    CHECK(has("\"SetupMode\":{\"status\":\"observed\",\"detail\":null,\"efi_status\":\"0x0000000000000000\",\"value\":0}"));
}
static void t_var_secure0_setup1(void) {
    var_case(EFI_SUCCESS,1,0,EFI_SUCCESS,1,1);
    CHECK(has("\"SecureBoot\":{\"status\":\"observed\",\"detail\":null,\"efi_status\":\"0x0000000000000000\",\"value\":0}"));
    CHECK(has("\"SetupMode\":{\"status\":\"observed\",\"detail\":null,\"efi_status\":\"0x0000000000000000\",\"value\":1}"));
}
static void t_var_malformed_size(void) {
    var_case(EFI_SUCCESS,2,1,EFI_SUCCESS,1,1);
    CHECK(has("\"SecureBoot\":{\"status\":\"malformed\",\"detail\":\"variable_size\",\"efi_status\":\"0x0000000000000000\",\"value\":null}"));
    CHECK(has("\"SetupMode\":{\"status\":\"observed\",\"detail\":null,\"efi_status\":\"0x0000000000000000\",\"value\":1}"));
}
static void t_var_invalid_value(void) {
    var_case(EFI_SUCCESS,1,0,EFI_SUCCESS,1,2);
    CHECK(has("\"SecureBoot\":{\"status\":\"observed\",\"detail\":null,\"efi_status\":\"0x0000000000000000\",\"value\":0}"));
    CHECK(has("\"SetupMode\":{\"status\":\"malformed\",\"detail\":\"value_range\",\"efi_status\":\"0x0000000000000000\",\"value\":null}"));
}
static void t_var_not_found(void) {
    var_case(EFI_NOT_FOUND,1,0,EFI_SUCCESS,1,1);
    CHECK(has("\"SecureBoot\":{\"status\":\"unavailable\",\"detail\":\"not_found\",\"efi_status\":\"0x800000000000000e\",\"value\":null}"));
    CHECK(has("\"SetupMode\":{\"status\":\"observed\",\"detail\":null,\"efi_status\":\"0x0000000000000000\",\"value\":1}"));
}
static void t_var_buffer_too_small(void) {
    var_case(EFI_SUCCESS,1,1,EFI_BUFFER_TOO_SMALL,4,0);
    CHECK(has("\"SecureBoot\":{\"status\":\"observed\",\"detail\":null,\"efi_status\":\"0x0000000000000000\",\"value\":1}"));
    CHECK(has("\"SetupMode\":{\"status\":\"malformed\",\"detail\":\"variable_size\",\"efi_status\":\"0x8000000000000005\",\"value\":null}"));
}
static void t_var_other_errors(void) {
    var_case(EFI_DEVICE_ERROR,1,1,EFI_UNSUPPORTED,1,1);
    CHECK(has("\"SecureBoot\":{\"status\":\"unavailable\",\"detail\":\"efi_error\",\"efi_status\":\"0x8000000000000007\",\"value\":null}"));
    CHECK(has("\"SetupMode\":{\"status\":\"unsupported\",\"detail\":\"service_unsupported\",\"efi_status\":\"0x8000000000000003\",\"value\":null}"));
}
static void t_var_security_and_warning(void) {
    var_case(EFI_SECURITY_VIOLATION,1,1,UINT64_C(1),1,1);
    CHECK(has("\"SecureBoot\":{\"status\":\"unavailable\",\"detail\":\"efi_error\",\"efi_status\":\"0x800000000000001a\",\"value\":null}"));
    CHECK(has("\"SetupMode\":{\"status\":\"unsupported\",\"detail\":\"warning_status\",\"efi_status\":\"0x0000000000000001\",\"value\":null}"));
}
static void t_var_service_missing(void) {
    st.runtime_services=0; run();
    CHECK(fw.var_calls[0]==0 && fw.var_calls[1]==0);
    CHECK(has("\"SecureBoot\":{\"status\":\"unavailable\",\"detail\":\"service_missing\""));
    CHECK(has("\"SetupMode\":{\"status\":\"unavailable\",\"detail\":\"service_missing\""));
}

/* ---------- memory map acquisition (through efi_main) ---------- */
static void map_expect(const char *status_detail) { CHECK(has(status_detail)); }
static void t_map_ordinary(void) {
    run();
    CHECK(fw.allocs==1 && fw.frees==1 && fw.map_calls==1 && fw.sizes[0]==MAP_INITIAL);
    map_expect("\"boot_services_map\":{\"status\":\"observed\",\"detail\":null,\"efi_status\":\"0x0000000000000000\",\"attempts\":1,\"release_status\":\"0x0000000000000000\",\"count\":3,\"descriptor_stride\":40,\"descriptor_version\":1");
    CHECK(has("{\"type\":2,\"physical\":\"0x0000000080010000\",\"pages\":2,\"attributes\":\"0x8000000000000001\"}"));
    CHECK(has("\"omitted\":0}"));
}
static void t_map_extended_stride(void) {
    fw.steps[0]=(MapStep){EFI_SUCCESS,3*64,64,1,3}; run();
    CHECK(fw.frees==1);
    map_expect("\"status\":\"observed\",\"detail\":null,\"efi_status\":\"0x0000000000000000\",\"attempts\":1,\"release_status\":\"0x0000000000000000\",\"count\":3,\"descriptor_stride\":64");
    CHECK(has("{\"type\":3,\"physical\":\"0x0000000080020000\",\"pages\":3,\"attributes\":\"0x8000000000000002\"}"));
}
static void t_map_too_small_then_success(void) {
    fw.nsteps=2; fw.steps[0]=(MapStep){EFI_BUFFER_TOO_SMALL,5000,48,1,0};
    fw.steps[1]=(MapStep){EFI_SUCCESS,4*48,48,1,4}; run();
    CHECK(fw.allocs==2 && fw.frees==2 && fw.sizes[0]==4096 && fw.sizes[1]==5000+2*48);
    map_expect("\"status\":\"observed\",\"detail\":null,\"efi_status\":\"0x0000000000000000\",\"attempts\":2,\"release_status\":\"0x0000000000000000\",\"count\":4,\"descriptor_stride\":48");
}
/* Reviewed defect: 4096-byte buffer, SUCCESS size 4120 -> read past buffer. */
static void t_map_size_exceeds_buffer(void) {
    fw.steps[0]=(MapStep){EFI_SUCCESS,4120,40,1,103}; run();
    CHECK(fw.allocs==1 && fw.frees==1 && fw.sizes[0]==4096);
    map_expect("\"boot_services_map\":{\"status\":\"malformed\",\"detail\":\"size_exceeds_buffer\",\"efi_status\":\"0x0000000000000000\",\"attempts\":1,\"release_status\":\"0x0000000000000000\",\"count\":null,\"descriptor_stride\":null,\"descriptor_version\":null,\"descriptors\":[],\"omitted\":null}");
}
static void t_map_retry_exhaustion(void) {
    fw.nsteps=5;
    for (unsigned i=0;i<5;++i) fw.steps[i]=(MapStep){EFI_BUFFER_TOO_SMALL,5000u<<i,40,1,0};
    run();
    CHECK(fw.map_calls==MAP_ATTEMPTS && fw.allocs==MAP_ATTEMPTS && fw.frees==MAP_ATTEMPTS);
    for (unsigned i=0;i<fw.allocs;++i) CHECK(fw.sizes[i]<=MAP_LIMIT);
    map_expect("\"status\":\"unsupported\",\"detail\":\"retry_limit\",\"efi_status\":\"0x8000000000000005\",\"attempts\":5");
}
static void t_map_size_limit(void) {
    fw.steps[0]=(MapStep){EFI_BUFFER_TOO_SMALL,MAP_LIMIT,40,1,0}; run();
    CHECK(fw.allocs==1 && fw.frees==1);
    map_expect("\"status\":\"unsupported\",\"detail\":\"size_limit\"");
}
static void t_map_too_small_inconsistent(void) {
    fw.steps[0]=(MapStep){EFI_BUFFER_TOO_SMALL,4000,40,1,0}; run();
    CHECK(fw.allocs==1 && fw.frees==1);
    map_expect("\"status\":\"malformed\",\"detail\":\"too_small_inconsistent\"");
}
static void t_map_too_small_bad_stride(void) {
    fw.steps[0]=(MapStep){EFI_BUFFER_TOO_SMALL,8000,8,1,0}; run();
    CHECK(fw.allocs==1 && fw.frees==1);
    map_expect("\"status\":\"malformed\",\"detail\":\"too_small_stride\"");
}
static void t_map_allocation_failure(void) {
    fw.alloc_fail_at=1; run();
    CHECK(fw.alloc_calls==1 && fw.allocs==0 && fw.frees==0 && fw.map_calls==0);
    map_expect("\"status\":\"unavailable\",\"detail\":\"allocation_failed\",\"efi_status\":\"0x8000000000000009\"");
}
static void t_map_allocation_failure_on_retry(void) {
    fw.alloc_fail_at=2; fw.steps[0]=(MapStep){EFI_BUFFER_TOO_SMALL,6000,40,1,0}; run();
    CHECK(fw.alloc_calls==2 && fw.allocs==1 && fw.frees==1);
    map_expect("\"status\":\"unavailable\",\"detail\":\"allocation_failed\",\"efi_status\":\"0x8000000000000009\",\"attempts\":2");
}
static void t_map_allocation_null(void) {
    fw.alloc_null=1; run();
    CHECK(fw.frees==0 && fw.map_calls==0);
    map_expect("\"status\":\"malformed\",\"detail\":\"allocation_null\"");
}
static void t_map_release_failure(void) {
    fw.free_status=EFI_INVALID_PARAMETER; run();
    CHECK(fw.allocs==1 && fw.frees==1);
    map_expect("\"status\":\"observed\",\"detail\":null,\"efi_status\":\"0x0000000000000000\",\"attempts\":1,\"release_status\":\"0x8000000000000002\"");
}
static void t_map_release_failure_stops_retry(void) {
    fw.free_status=EFI_INVALID_PARAMETER; fw.nsteps=2;
    fw.steps[0]=(MapStep){EFI_BUFFER_TOO_SMALL,6000,40,1,0}; fw.steps[1]=(MapStep){EFI_SUCCESS,40,40,1,1}; run();
    CHECK(fw.allocs==1 && fw.frees==1 && fw.map_calls==1);
    map_expect("\"status\":\"not_probed\",\"detail\":\"release_failed\"");
}
static void t_map_service_error(void) {
    fw.steps[0]=(MapStep){EFI_INVALID_PARAMETER,0,0,0,0}; run();
    CHECK(fw.frees==1);
    map_expect("\"status\":\"unavailable\",\"detail\":\"efi_error\",\"efi_status\":\"0x8000000000000002\"");
}
static void t_map_service_missing(void) {
    bs.get_memory_map=0; run();
    CHECK(fw.alloc_calls==0);
    map_expect("\"status\":\"unavailable\",\"detail\":\"service_missing\"");
}
static void t_map_unsupported_version(void) {
    fw.steps[0]=(MapStep){EFI_SUCCESS,80,40,2,2}; run();
    CHECK(fw.frees==1);
    map_expect("\"status\":\"unsupported\",\"detail\":\"descriptor_version\"");
}
static void t_map_partial_descriptor(void) {
    fw.steps[0]=(MapStep){EFI_SUCCESS,100,40,1,2}; run();
    CHECK(fw.frees==1);
    map_expect("\"status\":\"malformed\",\"detail\":\"partial_descriptor\"");
}
static void t_map_short_stride(void) {
    fw.steps[0]=(MapStep){EFI_SUCCESS,64,32,1,0}; run();
    map_expect("\"status\":\"malformed\",\"detail\":\"stride\"");
}
static void t_map_empty(void) {
    fw.steps[0]=(MapStep){EFI_SUCCESS,0,40,1,0}; run();
    map_expect("\"status\":\"malformed\",\"detail\":\"empty\"");
}
static void t_map_page_overflow(void) {
    fw.use_custom=1; fw.custom=(EFI_MEMORY_DESCRIPTOR){.type=7,.physical=0,.pages=UINT64_MAX/4096+1};
    fw.steps[0]=(MapStep){EFI_SUCCESS,40,40,1,1}; run();
    map_expect("\"status\":\"malformed\",\"detail\":\"pages_overflow\"");
}
static void t_map_range_overflow(void) {
    fw.use_custom=1; fw.custom=(EFI_MEMORY_DESCRIPTOR){.type=7,.physical=UINT64_MAX-0xfff,.pages=2};
    fw.steps[0]=(MapStep){EFI_SUCCESS,40,40,1,1}; run();
    map_expect("\"status\":\"malformed\",\"detail\":\"range_overflow\"");
}
static void t_map_virtual_overflow(void) {
    fw.use_custom=1; fw.custom=(EFI_MEMORY_DESCRIPTOR){.type=7,.physical=0x1000,.virtual_address=UINT64_MAX-0xfff,.pages=2};
    fw.steps[0]=(MapStep){EFI_SUCCESS,40,40,1,1}; run();
    map_expect("\"status\":\"malformed\",\"detail\":\"range_overflow\"");
}
/* Boundary: exclusive end 0xfffffffffffff000 is accepted; ending at 2^64 is not. */
static void t_map_exact_end(void) {
    fw.use_custom=1; fw.custom=(EFI_MEMORY_DESCRIPTOR){.type=7,.physical=UINT64_MAX-0x1fff,.pages=1};
    fw.steps[0]=(MapStep){EFI_SUCCESS,40,40,1,1}; run();
    CHECK(has("{\"type\":7,\"physical\":\"0xffffffffffffe000\",\"pages\":1,\"attributes\":\"0x0000000000000000\"}"));
    reset(); fw.use_custom=1; fw.custom=(EFI_MEMORY_DESCRIPTOR){.type=7,.physical=UINT64_MAX-0xfff,.pages=1};
    fw.steps[0]=(MapStep){EFI_SUCCESS,40,40,1,1}; run();
    map_expect("\"status\":\"malformed\",\"detail\":\"range_overflow\"");
}
static void t_map_unaligned(void) {
    fw.use_custom=1; fw.custom=(EFI_MEMORY_DESCRIPTOR){.type=7,.physical=0x1001,.pages=1};
    fw.steps[0]=(MapStep){EFI_SUCCESS,40,40,1,1}; run();
    map_expect("\"status\":\"malformed\",\"detail\":\"unaligned_physical\"");
}
static void t_map_omitted(void) {
    fw.steps[0]=(MapStep){EFI_SUCCESS,20*40,40,1,20}; run();
    CHECK(has("\"count\":20,") && has("\"omitted\":8}"));
    CHECK(!has("{\"type\":13,"));
}
/* Direct helper checks: arithmetic and read bounds independent of efi_main. */
static void t_map_helpers(void) {
    size_t next=1;
    CHECK(map_next_capacity(4096,5000,40,&next).state==OBSERVED && next==5080);
    CHECK(map_next_capacity(4096,MAP_LIMIT-79,40,&next).state==UNSUPPORTED && next==0);
    CHECK(map_next_capacity(4096,MAP_LIMIT-80,40,&next).state==OBSERVED && next==MAP_LIMIT);
    CHECK(map_next_capacity(4096,SIZE_MAX,40,&next).state==UNSUPPORTED);
    CHECK(map_next_capacity(4096,5000,SIZE_MAX,&next).state==MALFORMED);
    uint8_t *b=malloc(80); memset(b,0,80);
    CHECK(parse_map(b,80,120,40,1).state==MALFORMED);
    CHECK(parse_map(b,80,80,40,1).state==OBSERVED);
    CHECK(parse_map(b,80,80,120,1).state==MALFORMED);
    CHECK(parse_map(0,80,80,40,1).state==MALFORMED);
    CHECK(parse_map(b,MAP_LIMIT+1,80,40,1).state==MALFORMED);
    free(b);
    CHECK(!range_ok(UINT64_MAX,1) && range_ok(UINT64_MAX,0) && range_ok(0,UINT64_MAX));
}

/* ---------- device tree parser ---------- */
static uint32_t rd(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void wr(uint8_t *p,uint32_t v) { p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v; }
/* Parse a heap copy of exactly n bytes (reads past n are ASan findings). */
static DtResult dt_of(const uint8_t *p,size_t n) { uint8_t *c=heap_copy(p,n); DtResult r=parse_dt(c,n); free(c); return r; }
static uint8_t work[1024];
static uint8_t *mutable(const uint8_t *p,size_t n) { CHECK(n<=sizeof work); memcpy(work,p,n); return work; }
#define H_TOTAL 4
#define H_STRUCT 8
#define H_STRINGS 12
#define H_RSV 16
#define H_VERSION 20
#define H_LAST 24
#define H_SIZE_STRINGS 32
#define H_SIZE_STRUCT 36
static void dt_bad(const uint8_t *p,size_t n,State s,const char *detail) {
    DtResult r=dt_of(p,n);
    CHECK(r.state==s);
    CHECK(r.detail && !strcmp(r.detail,detail));
    if (r.state!=s || !r.detail || strcmp(r.detail,detail)) fprintf(stderr,"  got %s/%s\n",state_name(r.state),r.detail?r.detail:"null");
    CHECK(r.model_state==NOT_PROBED && !r.model[0]);
}
static void t_dt_minimal_v17(void) {
    DtResult r=dt_of(fdt_min,sizeof fdt_min);
    CHECK(r.state==OBSERVED && r.version==17 && r.last_comp_version==16 && r.size==72);
    CHECK(r.model_state==UNAVAILABLE && !r.model[0]);
}
static void t_dt_model(void) {
    DtResult r=dt_of(fdt_model,sizeof fdt_model);
    CHECK(r.state==OBSERVED && r.model_state==OBSERVED && !strcmp(r.model,"NVIDIA \"test\" board") && r.model_length==19);
}
static void t_dt_leading_nop(void) {
    DtResult r=dt_of(fdt_leading_nop,sizeof fdt_leading_nop);
    CHECK(r.state==OBSERVED && r.model_state==OBSERVED && !strcmp(r.model,"NVIDIA \"test\" board"));
}
static void t_dt_model_at_limit(void) {
    DtResult r=dt_of(fdt_model_64,sizeof fdt_model_64);
    CHECK(r.state==OBSERVED && r.model_state==OBSERVED && strlen(r.model)==64 && r.model_length==64);
}
static void t_dt_model_longer_than_limit(void) {
    DtResult r=dt_of(fdt_model_long,sizeof fdt_model_long);
    CHECK(r.state==OBSERVED);                       /* valid tree */
    CHECK(r.model_state==TRUNCATED && r.model_length==100 && strlen(r.model)==DT_MODEL_LIMIT);
    CHECK(r.model[0]=='L' && r.model[63]=='L');
}
static void t_dt_model_nonascii(void) {
    DtResult r=dt_of(fdt_model_nonascii,sizeof fdt_model_nonascii);
    CHECK(r.state==OBSERVED && r.model_state==UNSUPPORTED && !r.model[0]);
}
static void t_dt_model_string_list(void) {
    DtResult r=dt_of(fdt_model_list,sizeof fdt_model_list);
    CHECK(r.state==OBSERVED && r.model_state==UNSUPPORTED && !r.model[0]);
}
static void t_dt_depth(void) {
    CHECK(dt_of(fdt_depth_64,sizeof fdt_depth_64).state==OBSERVED);
    dt_bad(fdt_depth_65,sizeof fdt_depth_65,UNSUPPORTED,"depth_limit");
}
static void t_dt_v16_unsupported(void) {
    /* v16 lacks size_dt_struct; it must not be interpreted. */
    dt_bad(fdt_v16,sizeof fdt_v16,UNSUPPORTED,"version");
}
static void t_dt_incompatible_versions(void) {
    uint8_t *p=mutable(fdt_min,sizeof fdt_min);
    wr(p+H_LAST,18); dt_bad(p,sizeof fdt_min,UNSUPPORTED,"version");       /* requires a v18 reader */
    wr(p+H_LAST,16); wr(p+H_VERSION,15); dt_bad(p,sizeof fdt_min,UNSUPPORTED,"version");
    wr(p+H_VERSION,18); wr(p+H_LAST,17);
    CHECK(dt_of(p,sizeof fdt_min).state==OBSERVED);                    /* v18 readable as v17 */
    wr(p+H_VERSION,17); wr(p+H_LAST,17); CHECK(dt_of(p,sizeof fdt_min).state==OBSERVED);
}
/* The fixture from the reviewed PR: rsvmap at 48 overlaps struct [40,56). */
static void t_dt_overlap_reviewed_fixture(void) {
    uint8_t p[64]={0};
    wr(p,0xd00dfeed); wr(p+H_TOTAL,64); wr(p+H_STRUCT,40); wr(p+H_STRINGS,56); wr(p+H_RSV,48);
    wr(p+H_VERSION,17); wr(p+H_LAST,16); wr(p+H_SIZE_STRINGS,0); wr(p+H_SIZE_STRUCT,16);
    wr(p+40,1); wr(p+44,0); wr(p+48,2); wr(p+52,9);
    dt_bad(p,sizeof p,MALFORMED,"block_overlap");
}
static void t_dt_overlap_strings(void) {
    uint8_t *p=mutable(fdt_model,sizeof fdt_model);
    wr(p+H_STRINGS,rd(p+H_STRUCT)+8); dt_bad(p,sizeof fdt_model,MALFORMED,"block_overlap");
}
static void t_dt_bounds(void) {
    uint8_t *p=mutable(fdt_min,sizeof fdt_min);
    wr(p+H_SIZE_STRUCT,rd(p+H_SIZE_STRUCT)+4); dt_bad(p,sizeof fdt_min,MALFORMED,"block_bounds");
    p=mutable(fdt_min,sizeof fdt_min); wr(p+H_STRUCT,36); dt_bad(p,sizeof fdt_min,MALFORMED,"block_bounds");
    p=mutable(fdt_model,sizeof fdt_model); wr(p+H_SIZE_STRINGS,1000); dt_bad(p,sizeof fdt_model,MALFORMED,"block_bounds");
}
static void t_dt_alignment(void) {
    uint8_t *p=mutable(fdt_min,sizeof fdt_min);
    wr(p+H_RSV,44); dt_bad(p,sizeof fdt_min,MALFORMED,"alignment");
    p=mutable(fdt_min,sizeof fdt_min); wr(p+H_STRUCT,58); dt_bad(p,sizeof fdt_min,MALFORMED,"alignment");
    p=mutable(fdt_min,sizeof fdt_min); wr(p+H_SIZE_STRUCT,15); dt_bad(p,sizeof fdt_min,MALFORMED,"alignment");
}
static void t_dt_totalsize(void) {
    dt_bad(fdt_min,sizeof fdt_min-1,MALFORMED,"totalsize_exceeds_span");
    dt_bad(fdt_min,39,MALFORMED,"header_truncated");
    uint8_t *p=mutable(fdt_min,sizeof fdt_min);
    wr(p+H_TOTAL,DT_LIMIT+1); dt_bad(p,sizeof fdt_min,UNSUPPORTED,"size_limit");
    wr(p+H_TOTAL,32); dt_bad(p,sizeof fdt_min,MALFORMED,"totalsize");
    p=mutable(fdt_min,sizeof fdt_min); wr(p,0xedfe0dd0); dt_bad(p,sizeof fdt_min,MALFORMED,"magic");
    DtResult r=parse_dt(0,100); CHECK(r.state==MALFORMED && !strcmp(r.detail,"null"));
}
static void t_dt_rsvmap_unterminated(void) {
    /* Replace the zero terminator with an entry: the map would continue
     * into the structure block, so it is unterminated. */
    uint8_t *p=mutable(fdt_model,sizeof fdt_model);
    uint32_t rsv=rd(p+H_RSV);
    CHECK(rd(p+rsv)==0 && rd(p+rsv+4)==0x80000000);
    wr(p+rsv+16,0); wr(p+rsv+20,1); wr(p+rsv+24,0); wr(p+rsv+28,1);   /* terminator -> entry */
    dt_bad(p,sizeof fdt_model,MALFORMED,"rsvmap_unterminated");   /* would run into struct */
    /* Totalsize cut inside the reservation map. */
    uint8_t q[64]; memcpy(q,fdt_model,sizeof q);
    wr(q+H_TOTAL,64); wr(q+H_STRUCT,56); wr(q+H_SIZE_STRUCT,8); wr(q+H_STRINGS,64); wr(q+H_SIZE_STRINGS,0);
    dt_bad(q,sizeof q,MALFORMED,"rsvmap_unterminated");
}
static void t_dt_rsvmap_range(void) {
    uint8_t *p=mutable(fdt_model,sizeof fdt_model);
    uint32_t rsv=rd(p+H_RSV);
    wr(p+rsv,0xffffffff); wr(p+rsv+4,0xffffffff);
    dt_bad(p,sizeof fdt_model,MALFORMED,"rsvmap_range");
}
/* Builder for structural mutations: dtc layout (header, empty rsvmap at 40,
 * structure at 56, strings after). Validated against dtc's fdt_min below. */
static uint8_t built[512]; static size_t built_size;
static const uint8_t *build(const uint32_t *tokens,size_t ntokens,const char *strings,size_t nstrings) {
    memset(built,0,sizeof built);
    size_t off_struct=56, size_struct=ntokens*4, off_strings=off_struct+size_struct;
    built_size=off_strings+nstrings;
    CHECK(built_size<=sizeof built);
    wr(built,0xd00dfeed); wr(built+H_TOTAL,(uint32_t)built_size); wr(built+H_STRUCT,(uint32_t)off_struct);
    wr(built+H_STRINGS,(uint32_t)off_strings); wr(built+H_RSV,40); wr(built+H_VERSION,17); wr(built+H_LAST,16);
    wr(built+H_SIZE_STRINGS,(uint32_t)nstrings); wr(built+H_SIZE_STRUCT,(uint32_t)size_struct);
    for (size_t i=0;i<ntokens;++i) wr(built+off_struct+4*i,tokens[i]);
    memcpy(built+off_strings,strings,nstrings);
    return built;
}
#define NAME(a,b,c,d) ((uint32_t)(a)<<24|(uint32_t)(b)<<16|(uint32_t)(c)<<8|(uint32_t)(d))
#define BUILD(strings,...) do { static const uint32_t t_[]={__VA_ARGS__}; build(t_,sizeof t_/4,strings,sizeof strings-1); } while (0)
static void t_dt_builder_matches_dtc(void) {
    BUILD("",1,0,2,9);
    CHECK(built_size==sizeof fdt_min && !memcmp(built,fdt_min,sizeof fdt_min));
}
static void t_dt_multiple_roots(void) {
    BUILD("",1,0,2,1,0,2,9); dt_bad(built,built_size,MALFORMED,"multiple_roots");
}
static void t_dt_property_outside_root(void) {
    BUILD("model\0",3,0,0,1,0,2,9); dt_bad(built,built_size,MALFORMED,"property_outside_root");
    BUILD("model\0",1,0,2,3,0,0,9); dt_bad(built,built_size,MALFORMED,"property_outside_root");
}
static void t_dt_nesting_and_termination(void) {
    BUILD("",1,0,2,2,9);            dt_bad(built,built_size,MALFORMED,"unbalanced_end_node");
    BUILD("",1,0,9);                dt_bad(built,built_size,MALFORMED,"unterminated_node");
    BUILD("",1,0,1,NAME('c',0,0,0),2,9); dt_bad(built,built_size,MALFORMED,"unterminated_node");
    BUILD("",9);                    dt_bad(built,built_size,MALFORMED,"unterminated_node");
    BUILD("",1,0,2);                dt_bad(built,built_size,MALFORMED,"missing_end");
    BUILD("",1,0,2,9,4);            dt_bad(built,built_size,MALFORMED,"trailing_structure");
    BUILD("",1,0,5,2,9);            dt_bad(built,built_size,MALFORMED,"token");
    BUILD("",1,NAME('x',0,0,0),2,9); dt_bad(built,built_size,MALFORMED,"root_name");
    BUILD("",1,0,1,NAME('c',0,0,0),2,2,9);
    CHECK(dt_of(built,built_size).state==OBSERVED);
    BUILD("",4,1,0,4,1,NAME('c',0,0,0),4,2,4,2,4,9);
    CHECK(dt_of(built,built_size).state==OBSERVED);
    BUILD("model\0",1,0,1,NAME('c',0,0,0),2,3,0,0,2,9);
    dt_bad(built,built_size,MALFORMED,"property_after_subnode");
}
static void t_dt_truncated_data(void) {
    BUILD("",1,0,1,NAME('a','b','c','d')); dt_bad(built,built_size,MALFORMED,"name_truncated");
    BUILD("model\0",1,0,3,100,0,2,9);      dt_bad(built,built_size,MALFORMED,"property_truncated");
    BUILD("model\0",1,0,3,0);              dt_bad(built,built_size,MALFORMED,"property_truncated");
    BUILD("model\0",1,0,3,4,6,0,2,9);      dt_bad(built,built_size,MALFORMED,"property_name");
    BUILD("model",1,0,3,4,0,0,2,9);        dt_bad(built,built_size,MALFORMED,"property_name");
}
static void t_dt_model_value_edge_cases(void) {
    BUILD("model\0",1,0,3,0,0,2,9);                      /* zero-length model */
    DtResult r=dt_of(built,built_size); CHECK(r.state==OBSERVED && r.model_state==MALFORMED);
    BUILD("model\0",1,0,3,4,0,NAME('a','b','c','d'),2,9); /* not NUL-terminated */
    r=dt_of(built,built_size); CHECK(r.state==OBSERVED && r.model_state==MALFORMED && !r.model[0]);
    BUILD("model\0",1,0,3,2,0,NAME('a',0,0,0),3,2,0,NAME('b',0,0,0),2,9); /* duplicate */
    r=dt_of(built,built_size); CHECK(r.state==OBSERVED && r.model_state==MALFORMED && !r.model[0]);
    BUILD("model\0",1,0,1,NAME('c',0,0,0),3,2,0,NAME('b',0,0,0),2,2,9); /* not at root */
    r=dt_of(built,built_size); CHECK(r.state==OBSERVED && r.model_state==UNAVAILABLE);
}
static void t_dt_firmware_entry(void) {
    /* parse_firmware_dt reads only the declared totalsize from the pointer. */
    uint8_t *c=heap_copy(fdt_model,sizeof fdt_model);
    DtResult r=parse_firmware_dt(c); CHECK(r.state==OBSERVED && r.model_state==OBSERVED);
    free(c);
    uint8_t *p=mutable(fdt_min,sizeof fdt_min); wr(p+H_TOTAL,DT_LIMIT+1);
    c=heap_copy(p,8);   /* only magic+totalsize readable */
    r=parse_firmware_dt(c); CHECK(r.state==UNSUPPORTED && !strcmp(r.detail,"size_limit")); free(c);
    p=mutable(fdt_min,sizeof fdt_min); wr(p,0);
    c=heap_copy(p,8); r=parse_firmware_dt(c); CHECK(r.state==MALFORMED && !strcmp(r.detail,"magic")); free(c);
}

/* ---------- configuration table search and DT report ---------- */
static void t_report_dt_model(void) {
    uint8_t *c=heap_copy(fdt_model,sizeof fdt_model);
    add_table(0,&other_guid,&image); add_table(1,&dt_guid,c); run(); free(c);
    CHECK(has("\"firmware_dt\":{\"status\":\"observed\",\"detail\":null,\"tables_searched\":2,\"tables_total\":2,\"size\":197,\"version\":17,\"last_comp_version\":16,\"model\":{\"status\":\"observed\",\"detail\":null,\"length\":19,\"value\":\"NVIDIA \\\"test\\\" board\"}}"));
}
static void t_report_dt_model_truncated(void) {
    uint8_t *c=heap_copy(fdt_model_long,sizeof fdt_model_long);
    add_table(0,&dt_guid,c); run(); free(c);
    CHECK(has("\"firmware_dt\":{\"status\":\"observed\","));
    CHECK(has("\"model\":{\"status\":\"truncated\",\"detail\":null,\"length\":100,\"value\":\"LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL\"}"));
}
static void t_report_dt_model_escape(void) {
    uint8_t *c=heap_copy(fdt_model_escape,sizeof fdt_model_escape);
    add_table(0,&dt_guid,c); run(); free(c);
    CHECK(has("\"value\":\"tab\\u0009here\\\\back\""));
}
static void t_report_dt_absent(void) {
    add_table(0,&other_guid,&image); run();
    CHECK(has("\"firmware_dt\":{\"status\":\"unavailable\",\"detail\":\"absent\",\"tables_searched\":1,\"tables_total\":1,\"size\":null"));
}
static void t_report_dt_no_tables(void) {
    run();
    CHECK(has("\"firmware_dt\":{\"status\":\"unavailable\",\"detail\":\"absent\",\"tables_searched\":0,\"tables_total\":0"));
}
static void t_report_dt_search_limit(void) {
    for (size_t i=0;i<300;++i) add_table(i,&other_guid,&image);
    add_table(299,&dt_guid,fdt_min);   /* beyond the searched prefix */
    run();
    CHECK(has("\"firmware_dt\":{\"status\":\"not_probed\",\"detail\":\"search_limit\",\"tables_searched\":256,\"tables_total\":300"));
    CHECK(!has("\"detail\":\"absent\""));
}
static void t_report_dt_at_search_limit(void) {
    uint8_t *c=heap_copy(fdt_min,sizeof fdt_min);
    for (size_t i=0;i<256;++i) add_table(i,&other_guid,&image);
    add_table(255,&dt_guid,c); run(); free(c);
    CHECK(has("\"firmware_dt\":{\"status\":\"observed\",\"detail\":null,\"tables_searched\":256,\"tables_total\":256"));
}
static void t_report_dt_null_pointers(void) {
    add_table(0,&dt_guid,0); run();
    CHECK(has("\"firmware_dt\":{\"status\":\"malformed\",\"detail\":\"table_null\""));
}
static void t_report_config_table_null(void) {
    st.number_of_table_entries=3; st.configuration_table=0; run();
    CHECK(has("\"firmware_dt\":{\"status\":\"malformed\",\"detail\":\"config_table_null\",\"tables_searched\":0,\"tables_total\":3"));
}
static void t_report_dt_malformed(void) {
    uint8_t *p=mutable(fdt_model,sizeof fdt_model); wr(p+H_STRINGS,rd(p+H_STRUCT)+8);
    uint8_t *c=heap_copy(p,sizeof fdt_model); add_table(0,&dt_guid,c); run(); free(c);
    CHECK(has("\"firmware_dt\":{\"status\":\"malformed\",\"detail\":\"block_overlap\",\"tables_searched\":1,\"tables_total\":1,\"size\":null,\"version\":null,\"last_comp_version\":null,\"model\":{\"status\":\"not_probed\",\"detail\":null,\"length\":null,\"value\":null}}"));
}

/* ---------- FirmwareVendor (CHAR16) ---------- */
static void t_vendor_ascii(void) {
    run();
    CHECK(has("\"firmware_vendor\":{\"status\":\"observed\",\"detail\":null,\"units\":6,\"value\":\"EDK II\"}"));
}
static void t_vendor_escapes(void) {
    static const CHAR16 v[]={'a','"','\\',0x0a,0x1f,0x7f,'z',0};
    st.firmware_vendor=(CHAR16 *)v; run();
    CHECK(has("\"firmware_vendor\":{\"status\":\"observed\",\"detail\":null,\"units\":7,\"value\":\"a\\\"\\\\\\u000a\\u001f\\u007fz\"}"));
}
static void t_vendor_non_ascii(void) {
    static const CHAR16 v[]={'N',0x00e9,0x4e2d,0xfffd,0};
    st.firmware_vendor=(CHAR16 *)v; run();
    CHECK(has("\"firmware_vendor\":{\"status\":\"observed\",\"detail\":null,\"units\":4,\"value\":\"N\\u00e9\\u4e2d\\ufffd\"}"));
}
static void t_vendor_leading_non_ascii(void) {
    static const CHAR16 v[]={0x00c9,0};
    st.firmware_vendor=(CHAR16 *)v; run();
    CHECK(has("\"units\":1,\"value\":\"\\u00c9\"}"));
    CHECK(!has("\"value\":\"\""));
}
static void t_vendor_surrogate(void) {
    static const CHAR16 v[]={'A',0xd83d,0xde00,0};
    st.firmware_vendor=(CHAR16 *)v; run();
    CHECK(has("\"firmware_vendor\":{\"status\":\"unsupported\",\"detail\":\"ucs2_surrogate\",\"units\":0,\"value\":null}"));
}
static void t_vendor_limit(void) {
    CHAR16 *v=malloc(65*sizeof *v);   /* exactly 64 units + NUL */
    for (int i=0;i<64;++i) v[i]='V';
    v[64]=0;
    st.firmware_vendor=v; run();
    CHECK(has("\"firmware_vendor\":{\"status\":\"observed\",\"detail\":null,\"units\":64,"));
    free(v);
}
static void t_vendor_truncated(void) {
    CHAR16 *v=malloc(66*sizeof *v);   /* 65 units + NUL; probe reads at most 65 */
    for (int i=0;i<65;++i) v[i]='W';
    v[65]=0;
    st.firmware_vendor=v; run();
    CHECK(has("\"firmware_vendor\":{\"status\":\"truncated\",\"detail\":null,\"units\":64,\"value\":\"WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW\"}"));
    free(v);
}
static void t_vendor_missing(void) {
    st.firmware_vendor=0; run();
    CHECK(has("\"firmware_vendor\":{\"status\":\"unavailable\",\"detail\":null,\"units\":0,\"value\":null}"));
}
static void t_vendor_empty(void) {
    static const CHAR16 v[]={0};
    st.firmware_vendor=(CHAR16 *)v; run();
    CHECK(has("\"firmware_vendor\":{\"status\":\"observed\",\"detail\":null,\"units\":0,\"value\":\"\"}"));
}

/* ---------- output, return status, other observations ---------- */
static void t_full_report(void) {
    run();
    CHECK(returned==EFI_SUCCESS && fw.out_overlong==0);
    CHECK(!strncmp(fw.console,"EFI inventory Phase A: boot-services snapshot; returning to caller\r\n{",68));
    size_t n=strlen(fw.console); CHECK(n>2 && !strcmp(fw.console+n-3,"}\r\n"));
    CHECK(has("{\"schema\":2,\"build\":\"task004-phase-a-v2\","));
    CHECK(has("\"firmware_revision\":65536,\"uefi_revision\":131142,"));
    CHECK(has("\"loaded_image\":{\"status\":\"observed\",\"detail\":null,\"efi_status\":\"0x0000000000000000\",\"base\":\"0x0000000040000000\",\"size\":12288}"));
    CHECK(has("\"device_path\":{\"status\":\"observed\",\"detail\":null,\"representation\":\"pointer_present_only\"}"));
    CHECK(has("\"CurrentEL\":{\"status\":\"observed\",\"detail\":null,\"raw\":\"0x0000000000000008\",\"reported_level\":2}"));
    CHECK(has("\"execution_context\":\"not_established\",\"record_truncated\":false}"));
    CHECK(fw.el_reads==1 && fw.protocol_calls==1);
}
static void t_image_unavailable(void) {
    fw.image_status=EFI_UNSUPPORTED; run();
    CHECK(returned==EFI_SUCCESS);
    CHECK(has("\"loaded_image\":{\"status\":\"unavailable\",\"detail\":\"handle_protocol\",\"efi_status\":\"0x8000000000000003\",\"base\":null,\"size\":null}"));
    CHECK(has("\"device_path\":{\"status\":\"not_probed\""));
}
static void t_el_values(void) {
    fw.el=4; run();
    CHECK(has("\"raw\":\"0x0000000000000004\",\"reported_level\":1}"));
    ElResult e=decode_el(0); CHECK(e.state==MALFORMED);
    e=decode_el(0x10); CHECK(e.state==MALFORMED);
    e=decode_el(0xc); CHECK(e.state==OBSERVED && e.level==3);
}
static void t_el_malformed_report(void) {
    fw.el=0x13; run();
    CHECK(has("\"CurrentEL\":{\"status\":\"malformed\",\"detail\":null,\"raw\":\"0x0000000000000013\",\"reported_level\":null}"));
}
/* A failed observation is still a successfully emitted report. */
static void t_failed_observations_still_success(void) {
    fw.alloc_fail_at=1; st.runtime_services=0; fw.image_status=EFI_NOT_FOUND; st.firmware_vendor=0; run();
    CHECK(returned==EFI_SUCCESS && has("\"record_truncated\":false}"));
}
static void t_output_missing(void) {
    st.con_out=0; run();
    CHECK(returned==EFI_UNSUPPORTED && fw.out_calls==0);
    CHECK(fw.alloc_calls==0 && fw.el_reads==0);   /* nothing observed if it cannot be reported */
}
static void t_output_function_missing(void) {
    out.output_string=0; run();
    CHECK(returned==EFI_UNSUPPORTED && fw.alloc_calls==0);
}
static void t_output_first_call_fails(void) {
    fw.out_fail_at=1; fw.out_fail_status=EFI_DEVICE_ERROR; run();
    CHECK(returned==EFI_DEVICE_ERROR && fw.out_calls==1 && fw.used==0);
    CHECK(fw.frees==fw.allocs);
}
static void t_output_fails_mid_record(void) {
    fw.out_fail_at=3; fw.out_fail_status=EFI_DEVICE_ERROR; run();
    CHECK(returned==EFI_DEVICE_ERROR && fw.out_calls==3);   /* stops at the first error */
}
static void t_output_fails_final_newline(void) {
    /* Count chunks of a successful run, then fail exactly on the CRLF. */
    run(); unsigned total=fw.out_calls;
    reset(); fw.out_fail_at=total; fw.out_fail_status=EFI_DEVICE_ERROR; run();
    CHECK(returned==EFI_DEVICE_ERROR && fw.out_calls==total);
}
static void t_output_warning_tolerated(void) {
    fw.out_status=EFI_WARN_UNKNOWN_GLYPH; run();
    CHECK(returned==EFI_SUCCESS && has("\"record_truncated\":false}"));
}
static void t_no_system_table(void) {
    CHECK(efi_main((EFI_HANDLE)&image,0)==EFI_INVALID_PARAMETER);
    CHECK(fw.out_calls==0 && fw.alloc_calls==0);
}
static void t_no_boot_services(void) {
    st.boot_services=0; run();
    CHECK(returned==EFI_SUCCESS && fw.protocol_calls==0 && fw.alloc_calls==0);
    CHECK(has("\"boot_services_map\":{\"status\":\"unavailable\",\"detail\":\"service_missing\""));
    CHECK(has("\"loaded_image\":{\"status\":\"unavailable\",\"detail\":\"handle_protocol\",\"efi_status\":\"0x8000000000000003\""));
}
/* Largest record: 12 descriptors with maximal digits, 64-unit escaped vendor,
 * 64-byte model. Must fit RECORD_CAPACITY without truncation. */
static void t_record_bound(void) {
    CHAR16 v[65]; for (int i=0;i<64;++i) v[i]=0x4e2d; v[64]=0;
    st.firmware_vendor=v; st.firmware_revision=UINT32_MAX; st.hdr.revision=UINT32_MAX;
    image.image_size=UINT64_MAX;
    fw.use_custom=1; fw.custom=(EFI_MEMORY_DESCRIPTOR){.type=UINT32_MAX,.physical=0,.pages=UINT64_MAX/4096,.attributes=UINT64_MAX};
    fw.steps[0]=(MapStep){EFI_SUCCESS,20*40,40,1,20};
    uint8_t *c=heap_copy(fdt_model_64,sizeof fdt_model_64); add_table(0,&dt_guid,c);
    fw.var_status[0]=fw.var_status[1]=EFI_SUCCESS;
    run(); free(c);
    CHECK(returned==EFI_SUCCESS && has("\"record_truncated\":false}"));
    size_t n=strlen(record());
    fprintf(stderr,"  record_bound: %zu of %d bytes\n",n,RECORD_CAPACITY);
    CHECK(n+1<RECORD_CAPACITY);
}
static void t_writer(void) {
    char small[8]; Writer w={small,0,sizeof small,0};
    escaped(&w,"\"\\\n"); CHECK(w.truncated && w.length==7 && small[7]==0);
    char large[64]; Writer f={large,0,sizeof large,0};
    escaped(&f,"\"\\\n\x7f"); CHECK(!f.truncated && !strcmp(large,"\\\"\\\\\\u000a\\u007f"));
    static const CHAR16 u[]={0x20ac,'a',0x1};
    Writer g={large,0,sizeof large,0}; escaped16(&g,u,3); CHECK(!strcmp(large,"\\u20aca\\u0001"));
    CHECK(!strcmp(state_name(TRUNCATED),"truncated") && !strcmp(state_name((State)99),"malformed"));
}
/* Built separately with -DRECORD_CAPACITY=512: the record cannot fit, so
 * only the fixed marker is emitted and EFI_BUFFER_TOO_SMALL is returned. */
#ifdef SMALL_RECORD_TEST
static void t_record_truncated(void) {
    run();
    CHECK(returned==EFI_BUFFER_TOO_SMALL);
    CHECK(!strcmp(fw.console,"EFI inventory Phase A: boot-services snapshot; returning to caller\r\n{\"schema\":2,\"record_truncated\":true}\r\n"));
    CHECK(fw.frees==fw.allocs);
}
#endif
/* The sentinel mechanism itself must be observable. */
static void t_sentinel_self_check(void) {
    EFI_STATUS (*f)(void)=(EFI_STATUS (*)(void))bs.exit_boot_services;
    CHECK(f==forbidden_call);
    CHECK((void *)rt.set_variable==(void *)forbidden_call && (void *)rt.reset_system==(void *)forbidden_call);
    CHECK((void *)rt.set_virtual_address_map==(void *)forbidden_call);
    CHECK((void *)bs.allocate_pages==(void *)forbidden_call && (void *)bs.set_watchdog_timer==(void *)forbidden_call);
    CHECK((void *)bs.open_protocol==(void *)forbidden_call && (void *)bs.create_event_ex==(void *)forbidden_call);
    CHECK((void *)out.reset==(void *)forbidden_call && (void *)out.clear_screen==(void *)forbidden_call);
    f(); CHECK(fw.forbidden==1); fw.forbidden=0;
}

typedef struct { const char *name; void (*fn)(void); } Case;
#define C(x) {#x,x}
static const Case cases[]={
#ifdef SMALL_RECORD_TEST
    C(t_record_truncated),
#endif
    C(t_sentinel_self_check),
    C(t_var_secure1_setup0),C(t_var_secure0_setup1),C(t_var_malformed_size),C(t_var_invalid_value),
    C(t_var_not_found),C(t_var_buffer_too_small),C(t_var_other_errors),C(t_var_security_and_warning),
    C(t_var_service_missing),
    C(t_map_ordinary),C(t_map_extended_stride),C(t_map_too_small_then_success),C(t_map_size_exceeds_buffer),
    C(t_map_retry_exhaustion),C(t_map_size_limit),C(t_map_too_small_inconsistent),C(t_map_too_small_bad_stride),
    C(t_map_allocation_failure),C(t_map_allocation_failure_on_retry),C(t_map_allocation_null),
    C(t_map_release_failure),C(t_map_release_failure_stops_retry),C(t_map_service_error),C(t_map_service_missing),
    C(t_map_unsupported_version),C(t_map_partial_descriptor),C(t_map_short_stride),C(t_map_empty),
    C(t_map_page_overflow),C(t_map_range_overflow),C(t_map_virtual_overflow),C(t_map_exact_end),
    C(t_map_unaligned),C(t_map_omitted),C(t_map_helpers),
    C(t_dt_minimal_v17),C(t_dt_model),C(t_dt_leading_nop),C(t_dt_model_at_limit),C(t_dt_model_longer_than_limit),
    C(t_dt_model_nonascii),C(t_dt_model_string_list),C(t_dt_depth),C(t_dt_v16_unsupported),
    C(t_dt_incompatible_versions),C(t_dt_overlap_reviewed_fixture),C(t_dt_overlap_strings),C(t_dt_bounds),
    C(t_dt_alignment),C(t_dt_totalsize),C(t_dt_rsvmap_unterminated),C(t_dt_rsvmap_range),
    C(t_dt_builder_matches_dtc),C(t_dt_multiple_roots),C(t_dt_property_outside_root),
    C(t_dt_nesting_and_termination),C(t_dt_truncated_data),C(t_dt_model_value_edge_cases),C(t_dt_firmware_entry),
    C(t_report_dt_model),C(t_report_dt_model_truncated),C(t_report_dt_model_escape),C(t_report_dt_absent),
    C(t_report_dt_no_tables),C(t_report_dt_search_limit),C(t_report_dt_at_search_limit),
    C(t_report_dt_null_pointers),C(t_report_config_table_null),C(t_report_dt_malformed),
    C(t_vendor_ascii),C(t_vendor_escapes),C(t_vendor_non_ascii),C(t_vendor_leading_non_ascii),
    C(t_vendor_surrogate),C(t_vendor_limit),C(t_vendor_truncated),C(t_vendor_missing),C(t_vendor_empty),
    C(t_full_report),C(t_image_unavailable),C(t_el_values),C(t_el_malformed_report),
    C(t_failed_observations_still_success),C(t_output_missing),C(t_output_function_missing),
    C(t_output_first_call_fails),C(t_output_fails_mid_record),C(t_output_fails_final_newline),
    C(t_output_warning_tolerated),C(t_no_system_table),C(t_no_boot_services),C(t_record_bound),C(t_writer),
};
int main(int argc,char **argv) {
    record_dir=argc>1 ? argv[1] : ".";
    const char *only=argc>2 ? argv[2] : 0;
    unsigned failed=0,ran=0;
    for (size_t i=0;i<sizeof cases/sizeof *cases;++i) {
        if (only && strcmp(only,cases[i].name)) continue;
        current=cases[i].name; case_failed=0; reset();
        cases[i].fn();
        /* Invariants for every case: no forbidden call, every successful
         * allocation released exactly once, no foreign/double free. */
        CHECK(fw.forbidden==0);
        CHECK(fw.frees==fw.allocs && fw.bad_frees==0);
        for (unsigned a=0;a<fw.allocs;++a) CHECK(fw.live[a]==0);
        ++ran;
        if (case_failed) ++failed;
        printf("%s %s\n",case_failed ? "FAIL" : "ok  ",current);
    }
    printf("%u cases, %u failed\n",ran,failed);
    return failed || !ran;
}
