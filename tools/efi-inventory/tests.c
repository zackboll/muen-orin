#include "core.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
extern EFI_STATUS efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE *);
static uint64_t simulated_el=4;
uint64_t probe_current_el(void) { return simulated_el; }
static EFI_STATUS alloc(uint32_t type,size_t n,void **p) { assert(type==EFI_LOADER_DATA); *p=malloc(n); return *p ? 0 : (UINT64_C(1)<<63|9); }
static unsigned frees,calls,forbidden;
static EFI_STATUS release(void *p) { ++frees; free(p); return 0; }
static EFI_STATUS map(size_t *n,EFI_MEMORY_DESCRIPTOR *p,size_t *key,size_t *stride,uint32_t *version) {
    ++calls; *key=7; *stride=sizeof *p; *version=1;
    if (*n<sizeof *p) { *n=sizeof *p; return EFI_BUFFER_TOO_SMALL; }
    *n=sizeof *p; *p=(EFI_MEMORY_DESCRIPTOR){.type=77,.physical=0x1000,.pages=1,.attributes=UINT64_C(1)<<63};return 0;
}
static EFI_STATUS too_small(size_t *n,EFI_MEMORY_DESCRIPTOR *p,size_t *k,size_t *s,uint32_t *v) {
    (void)p;(void)k;(void)v; ++calls; *s=48; *n=5000+calls*5000;return EFI_BUFFER_TOO_SMALL;
}
static EFI_STATUS denied(uint32_t t,size_t n,void **p) { (void)t;(void)n;*p=0;return UINT64_C(1)<<63|9; }
static EFI_STATUS bad_call(void) { ++forbidden; return 0; }
static char console[4096]; static size_t used;
static EFI_STATUS output(EFI_SIMPLE_TEXT_OUTPUT *self,const CHAR16 *p) {
    (void)self; while (*p) { assert(used+1<sizeof console); console[used++]=(char)*p++; }
    console[used]=0;return 0;
}
static EFI_STATUS no_image(EFI_HANDLE h,EFI_GUID *g,void **p) {
    (void)h;(void)g;*p=0;return EFI_NOT_FOUND;
}
static void big(uint8_t *p,unsigned v) { p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v; }
int main(void) {
    uint8_t buf[96]={0};EFI_MEMORY_DESCRIPTOR d={.type=0x1234,.physical=0x1000,.pages=2,.attributes=UINT64_C(1)<<63};
    memcpy(buf,&d,sizeof d);
    assert(parse_map(buf,sizeof d,sizeof d,1).state==OBSERVED);
    assert(parse_map(buf,sizeof buf,sizeof buf,1).count==1);
    assert(parse_map(buf,sizeof buf,sizeof buf,1).entries[0].type==0x1234);
    assert(parse_map(buf,sizeof buf,sizeof buf,1).entries[0].attributes==(UINT64_C(1)<<63));
    assert(parse_map(buf,47,48,1).state==MALFORMED);
    assert(parse_map(buf,48,39,1).state==MALFORMED);
    assert(parse_map(buf,48,48,2).state==MALFORMED);
    d.physical=UINT64_MAX-100;memcpy(buf,&d,sizeof d);
    assert(parse_map(buf,48,48,1).state==MALFORMED);
    assert(!range_ok(UINT64_MAX,1));assert(range_ok(UINT64_MAX,0));
    assert(parse_dt(0,0).state==MALFORMED);
    uint8_t dt[80]={0};big(dt,0xd00dfeed);big(dt+4,80);big(dt+8,40);
    big(dt+12,64);big(dt+16,72);big(dt+20,17);big(dt+32,6);big(dt+36,24);
    big(dt+40,1);big(dt+48,3);big(dt+52,4);big(dt+56,0);
    memcpy(dt+60,"abc",4);memcpy(dt+64,"model",6);
    /* A valid root is BEGIN_NODE, property, END_NODE, END. */
    big(dt+48,3);big(dt+52,4);big(dt+56,0);
    big(dt+64,0); /* build a separate minimal root without property */
    big(dt+4,64);big(dt+12,56);big(dt+16,48);big(dt+32,0);big(dt+36,16);
    big(dt+40,1);big(dt+44,0);big(dt+48,2);big(dt+52,9);
    assert(parse_dt(dt,64).state==OBSERVED);
    big(dt+4,1000);assert(parse_dt(dt,64).state==MALFORMED);
    big(dt+4,64);big(dt+52,3);assert(parse_dt(dt,64).state==MALFORMED);
    big(dt+52,9);
    big(dt,0x12345678);assert(parse_dt(dt,64).state==UNSUPPORTED);
    assert(parse_var(EFI_NOT_FOUND,1,0).state==UNAVAILABLE);
    assert(parse_var(0,2,1).state==MALFORMED);
    assert(parse_var(0,1,3).state==MALFORMED);
    assert(decode_el(4).level==1 && decode_el(8).level==2);
    assert(decode_el(0).state==MALFORMED);
    char small[8];Writer w={small,0,sizeof small,0};
    escaped(&w,"\"\\\n");assert(w.truncated && small[w.length]==0);
    char large[32]; Writer full={large,0,sizeof large,0};
    escaped(&full,"\"\\\n");assert(!strcmp(large,"\\\"\\\\\\u000a"));
    EFI_SIMPLE_TEXT_OUTPUT out={.output_string=output};
    EFI_BOOT_SERVICES bs={.allocate_pool=alloc,.free_pool=release,.get_memory_map=map,
        .handle_protocol=no_image,.exit_boot_services=(void *)bad_call,
        .set_watchdog_timer=(void *)bad_call};
    EFI_SYSTEM_TABLE st={.boot_services=&bs,.con_out=&out};
    efi_main(0,&st);assert(calls==1 && frees==1 && forbidden==0);
    assert(strstr(console,"\"schema\":1") && strstr(console,"\"reported_level\":1"));
    assert(strstr(console,"\"firmware_dt_status\":\"unavailable\""));
    FILE *capture=fopen("/tmp/muen-efi-inventory-record.json","w");assert(capture);
    char *json=strchr(console,'{');assert(json);
    assert(fwrite(json,1,strcspn(json,"\r"),capture)==strcspn(json,"\r"));
    assert(!fclose(capture));
    simulated_el=8;used=0;console[0]=0;efi_main(0,&st);
    assert(strstr(console,"\"reported_level\":2") &&
           strstr(console,"\"execution_context\":\"not_established\""));
    bs.get_memory_map=too_small;calls=frees=0;efi_main(0,&st);
    assert(calls<=MAP_ATTEMPTS && calls==frees && forbidden==0);
    bs.allocate_pool=denied;calls=frees=0;efi_main(0,&st);
    assert(!calls && !frees && !forbidden);
    puts("probe host doubles: OK");return 0;
}
