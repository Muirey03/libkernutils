#include <stdlib.h>

#include "kern_utils.h"
#include "kexecute.h"
#include "kmem.h"
#include "osobject.h"
#include "common.h"
#include "pac.h"
#include "kernel_call.h"

// offsets in vtable:
static uint32_t off_OSDictionary_SetObjectWithCharP = sizeof(void*) * 0x1F;
static uint32_t off_OSDictionary_GetObjectWithCharP = sizeof(void*) * 0x26;
static uint32_t off_OSDictionary_Merge              = sizeof(void*) * 0x23;

static uint32_t off_OSArray_Merge                   = sizeof(void*) * 0x1E;
static uint32_t off_OSArray_RemoveObject            = sizeof(void*) * 0x20;
static uint32_t off_OSArray_GetObject               = sizeof(void*) * 0x22;

static uint32_t off_OSObject_Release                = sizeof(void*) * 0x05;
static uint32_t off_OSObject_GetRetainCount         = sizeof(void*) * 0x03;
static uint32_t off_OSObject_Retain                 = sizeof(void*) * 0x04;

static uint32_t off_OSString_GetLength              = sizeof(void*) * 0x11;

// 1 on success, 0 on error
bool OSDictionary_SetItem(uint64_t dict, const char *key, uint64_t val) {
    size_t len = strlen(key) + 1;

    uint64_t ks = kalloc(len);
    kwrite(ks, key, len);

    uint64_t vtab = rk64(dict);
    vtab = kernel_xpacd(vtab);
    uint64_t f = rk64(vtab + off_OSDictionary_SetObjectWithCharP);
    f = kernel_xpaci(f);

    int rv = (int) kexecute(f, dict, ks, val, 0, 0, 0, 0);

    kfree(ks, len);

    return rv;
}

// XXX it can return 0 in lower 32 bits but still be valid
// fix addr of returned value and check if rk64 gives ptr
// to vtable addr saved before

// address if exists, 0 if not
uint64_t _OSDictionary_GetItem(uint64_t dict, const char *key) {
    size_t len = strlen(key) + 1;

    uint64_t ks = kalloc(len);
    kwrite(ks, key, len);

    uint64_t vtab = rk64(dict);
    vtab = kernel_xpacd(vtab);
    uint64_t f = rk64(vtab + off_OSDictionary_GetObjectWithCharP);
    f = kernel_xpaci(f);

    uint64_t rv = kexecute(f, dict, ks, 0, 0, 0, 0, 0);

    kfree(ks, len);

    return rv;
}

uint64_t OSDictionary_GetItem(uint64_t dict, const char *key) {
    uint64_t ret = _OSDictionary_GetItem(dict, key);
    
    if (ret != 0 && (ret>>32) == 0) {
        // XXX can it be not in zalloc?..
        ret = zm_fix_addr(ret);
    }

    return ret;
}

// 1 on success, 0 on error
bool OSDictionary_Merge(uint64_t dict, uint64_t aDict) {
    uint64_t vtab = rk64(dict);
    vtab = kernel_xpacd(vtab);
    uint64_t f = rk64(vtab + off_OSDictionary_Merge);
    f = kernel_xpaci(f);

    return (int) kexecute(f, dict, aDict, 0, 0, 0, 0, 0);
}

// 1 on success, 0 on error
bool OSArray_Merge(uint64_t array, uint64_t aArray) {
    uint64_t vtab = rk64(array);
    vtab = kernel_xpacd(vtab);
    uint64_t f = rk64(vtab + off_OSArray_Merge);
    f = kernel_xpaci(f);

    return (int) kexecute(f, array, aArray, 0, 0, 0, 0, 0);
}

uint64_t _OSArray_GetObject(uint64_t array, unsigned int idx){
    uint64_t vtab = rk64(array);
    vtab = kernel_xpacd(vtab);
    uint64_t f = rk64(vtab + off_OSArray_GetObject);
    f = kernel_xpaci(f);
    
    return kexecute(f, array, idx, 0, 0, 0, 0, 0);
}

uint64_t OSArray_GetObject(uint64_t array, unsigned int idx){
    uint64_t ret = _OSArray_GetObject(array, idx);
    
    if (ret != 0){
        // XXX can it be not in zalloc?..
        ret = zm_fix_addr(ret);
    }
    return ret;
}

void OSArray_RemoveObject(uint64_t array, unsigned int idx){
    uint64_t vtab = rk64(array);
    vtab = kernel_xpacd(vtab);
    uint64_t f = rk64(vtab + off_OSArray_RemoveObject);
    f = kernel_xpaci(f);
    
    (void)kexecute(f, array, idx, 0, 0, 0, 0, 0);
}

// XXX error handling just for fun? :)
uint64_t _OSUnserializeXML(const char *buffer) {
    size_t len = strlen(buffer) + 1;

    uint64_t ks = kalloc(len);
    kwrite(ks, buffer, len);

    uint64_t errorptr = 0;

    uint64_t rv = kexecute(offset_osunserializexml, ks, errorptr, 0, 0, 0, 0, 0);
    kfree(ks, len);

    return rv;
}

uint64_t OSUnserializeXML(const char *buffer) {
    uint64_t ret = _OSUnserializeXML(buffer);
    
    if (ret != 0) {
        // XXX can it be not in zalloc?..
        ret = zm_fix_addr(ret);
    }

    return ret;
}

void OSObject_Release(uint64_t osobject) {
    uint64_t vtab = rk64(osobject);
    vtab = kernel_xpacd(vtab);
    uint64_t f = rk64(vtab + off_OSObject_Release);
    f = kernel_xpaci(f);
    (void) kexecute(f, osobject, 0, 0, 0, 0, 0, 0);
}

void OSObject_Retain(uint64_t osobject) {
    uint64_t vtab = rk64(osobject);
    vtab = kernel_xpacd(vtab);
    uint64_t f = rk64(vtab + off_OSObject_Retain);
    f = kernel_xpaci(f);
    (void) kexecute(f, osobject, 0, 0, 0, 0, 0, 0);
}

uint32_t OSObject_GetRetainCount(uint64_t osobject) {
    uint64_t vtab = rk64(osobject);
    vtab = kernel_xpacd(vtab);
    uint64_t f = rk64(vtab + off_OSObject_GetRetainCount);
    f = kernel_xpaci(f);
    return (uint32_t) kexecute(f, osobject, 0, 0, 0, 0, 0, 0);
}

unsigned int OSString_GetLength(uint64_t osstring){
    uint64_t vtab = rk64(osstring);
    vtab = kernel_xpacd(vtab);
    uint64_t f = rk64(vtab + off_OSString_GetLength);
    f = kernel_xpaci(f);
    return (unsigned int)kexecute(f, osstring, 0, 0, 0, 0, 0, 0);
}

char *OSString_CopyString(uint64_t osstring){
    unsigned int length = OSString_GetLength(osstring);
    if (length == 0 || length > 0x100) DEBUGLOG("OSString_CopyString: length=%d", length);
    char *str = (char *)malloc(length + 1);
    if (!str) {
        DEBUGLOG("malloc failed OSString_CopyString: str=%p", str);
    }
    str[length] = 0;
    
    kread(OSString_CStringPtr(osstring), str, length);
    return str;
}
