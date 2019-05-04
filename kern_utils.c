#include <sched.h>
#include <sys/param.h>
#include <sys/stat.h>

#include "common.h"
#include "kern_utils.h"
#include "kexecute.h"
#include "kmem.h"
#include "offsetof.h"
#include "osobject.h"
#include "sandbox.h"

mach_port_t tfp0;
uint64_t kernel_base;
uint64_t kernel_slide;

uint64_t offset_kernel_task;
uint64_t offset_zonemap;
uint64_t offset_add_ret_gadget;
uint64_t offset_osboolean_true;
uint64_t offset_osboolean_false;
uint64_t offset_osunserializexml;
uint64_t offset_smalloc;
uint64_t offset_paciza_pointer__l2tp_domain_module_start;
uint64_t offset_paciza_pointer__l2tp_domain_module_stop;
uint64_t offset_l2tp_domain_inited;
uint64_t offset_sysctl__net_ppp_l2tp;
uint64_t offset_sysctl_unregister_oid;
uint64_t offset_mov_x0_x4__br_x5;
uint64_t offset_mov_x9_x0__br_x1;
uint64_t offset_mov_x10_x3__br_x6;
uint64_t offset_kernel_forge_pacia_gadget;
uint64_t offset_kernel_forge_pacda_gadget;
uint64_t offset_IOUserClient__vtable;
uint64_t offset_IORegistryEntry__getRegistryEntryID;
uint64_t offset_vfs_context_current;
uint64_t offset_vnode_lookup;
uint64_t offset_vnode_put;
uint64_t offset_proc_find;
uint64_t offset_proc_rele;
uint64_t offset_sstrdup;
uint64_t offset_extension_create_file;
uint64_t offset_extension_add;
uint64_t offset_extension_release;
uint64_t offset_strlen;
uint64_t offset_sfree;

static const char* abs_path_exceptions[] = {
    "/Library",
    "/private/var/mobile/Library",
    "/System/Library/Caches",
    "/private/var/mnt",
    NULL
};

static const char *file_exc_key = "com.apple.security.exception.files.absolute-path.read-only";

pthread_mutex_t fixup_lock;

static void modify_csflags(uint64_t proc, void (^function)(uint32_t *flags)) {
    if (function == NULL) return;
    uint32_t csflags = rk32(proc + offsetof_p_csflags);
    function(&csflags);
    wk32(proc + offsetof_p_csflags, csflags);
}

static void modify_t_flags(uint64_t proc, void (^function)(uint32_t *flags)) {
    if (function == NULL) return;
    uint64_t task = rk64(proc + offsetof_task);
    uint32_t t_flags = rk32(task + offsetof_t_flags);
    function(&t_flags);
    wk32(task + offsetof_t_flags, t_flags);
}

uint64_t proc_find(pid_t pid) {
    if (initialized) {
        uint64_t proc = kexecute(offset_proc_find, pid, 0, 0, 0, 0, 0 ,0);
        if (proc > 0) {
            proc = zm_fix_addr(proc);
        }
        return proc;
    }
    
    static uint64_t kernproc = 0;
    if (kernproc == 0) {
        kernproc = rk64(rk64(offset_kernel_task) + offsetof_bsd_info);
        if (kernproc == 0) {
            CROAK("Unable to get kernproc");
            return 0;
        }
    }
    
    uint64_t proc = kernproc;
    
    if (pid == 0) {
        return proc;
    }
    
    while (proc != 0) {
        uint32_t found_pid = rk32(proc + offsetof_p_pid);
        
        if (found_pid == pid) {
            return proc;
        }
        
        proc = rk64(proc + offsetof_p_p_list + sizeof(void *));
    }
    
    return 0;
}

void proc_rele(uint64_t proc) {
    kexecute(offset_proc_rele, proc, 0, 0, 0, 0, 0, 0);
}

CACHED_FIND(uint64_t, our_task_addr) {
    uint64_t proc = proc_find(getpid());
    if (proc == 0) {
        CROAK("Unable to find proc");
        return 0;
    }
    uint64_t task_addr = rk64(proc + offsetof_task);
    return task_addr;
}

uint64_t find_port(mach_port_name_t port) {
    static uint64_t is_table = 0;
    if (is_table == 0) {
        uint64_t task_addr = our_task_addr();
        if (!task_addr) {
            CROAK("Unable to get task_addr");
            return 0;
        }
        uint64_t itk_space = rk64(task_addr + offsetof_itk_space);
        if (!itk_space) {
            CROAK("Unable to get itk_space");
            return 0;
        }
        is_table = rk64(itk_space + offsetof_ipc_space_is_table);
        if (!is_table) {
            CROAK("Unable to get is_table");
            return 0;
        }
    }
  
    uint32_t port_index = port >> 8;
    const int sizeof_ipc_entry_t = 0x18;
    uint64_t port_addr = rk64(is_table + (port_index * sizeof_ipc_entry_t));
    return port_addr;
}

uint64_t get_exception_osarray(const char **exceptions) {
    uint64_t exception_osarray = 0;
    DEBUGLOG("Generating exception_osarray");
    size_t xmlsize = 0x1000;
    size_t len=0;
    ssize_t written=0;
    char *ents = malloc(xmlsize);
    size_t xmlused = sprintf(ents, "<array>");
    for (const char **exception = exceptions; *exception; exception++) {
        len = strlen(*exception);
        len += strlen("<string></string>");
        while (xmlused + len >= xmlsize) {
            xmlsize += 0x1000;
            ents = reallocf(ents, xmlsize);
            if (!ents) {
                CROAK("Unable to reallocate memory");
                return 0;
            }
        }
        written = sprintf(ents + xmlused, "<string>%s/</string>", *exception);
        if (written < 0) {
            CROAK("Couldn't write string");
            free(ents);
            return 0;
        }
        xmlused += written;
    }
    len = strlen("</array>");
    if (xmlused + len >= xmlsize) {
        xmlsize += len;
        ents = reallocf(ents, xmlsize);
        if (!ents) {
            CROAK("Unable to reallocate memory");
            return 0;
        }
    }
    written = sprintf(ents + xmlused, "</array>");
    
    exception_osarray = OSUnserializeXML(ents);
    DEBUGLOG("Generated exception_osarray: 0x%llx (entitlements %s)", exception_osarray, ents);
    free(ents);
    return exception_osarray;
}

void release_exception_osarray(uint64_t *exception_osarray) {
    if (*exception_osarray != 0) {
        OSObject_Release(*exception_osarray);
        *exception_osarray = 0;
    }
}

void set_sandbox_extensions(uint64_t proc, uint64_t proc_ucred, uint64_t sandbox) {
    if (sandbox == 0) {
        DEBUGLOG("No sandbox, skipping (proc: 0x%llx)", proc);
        return;
    }

    uint64_t ext = 0;
    for (const char **exception = abs_path_exceptions; *exception; exception++) {
        if (has_file_extension(sandbox, *exception)) {
            DEBUGLOG("Already has '%s', skipping", *exception);
            continue;
        }
        ext = extension_create_file(*exception, ext);
        if (ext == 0) {
            DEBUGLOG("extension_create_file(%s) failed, panic!", *exception);
        }
    }
    
    if (ext != 0) {
        extension_add(ext, sandbox, file_exc_key);
    }
}

char **copy_amfi_entitlements(uint64_t present) {
    unsigned int itemCount = OSArray_ItemCount(present);
    uint64_t itemBuffer = OSArray_ItemBuffer(present);
    size_t bufferSize = 0x1000;
    size_t bufferUsed = 0;
    size_t arraySize = (itemCount+1) * sizeof(char*);
    char **entitlements = malloc(arraySize + bufferSize);
    entitlements[itemCount] = NULL;

    for (int i=0; i<itemCount; i++) {
        uint64_t item = rk64(itemBuffer + (i * sizeof(void *)));
        char *entitlementString = OSString_CopyString(item);
        size_t len = strlen(entitlementString) + 1;
        while (bufferUsed + len > bufferSize) {
            bufferSize += 0x1000;
            entitlements = realloc(entitlements, arraySize + bufferSize);
            if (!entitlements) {
                CROAK("Unable to reallocate memory");
                return NULL;
            }
        }
        entitlements[i] = (char*)entitlements + arraySize + bufferUsed;
        strcpy(entitlements[i], entitlementString);
        bufferUsed += len;
    }
    return entitlements;
}

void set_amfi_entitlements(uint64_t proc, uint64_t proc_ucred, uint64_t amfi_entitlements, uint64_t sandbox) {
    bool rv = false;

    uint64_t key = 0;

    key = OSDictionary_GetItem(amfi_entitlements, "com.apple.private.skip-library-validation");
    if (key != offset_osboolean_true) {
        rv = OSDictionary_SetItem(amfi_entitlements, "com.apple.private.skip-library-validation", offset_osboolean_true);
        if (rv != true) {
            DEBUGLOG("Failed to set com.apple.private.skip-library-validation!");
        }
    }
    
    if (OPT(GET_TASK_ALLOW)) {
        key = OSDictionary_GetItem(amfi_entitlements, "get-task-allow");
        if (key != offset_osboolean_true) {
            rv = OSDictionary_SetItem(amfi_entitlements, "get-task-allow", offset_osboolean_true);
            if (rv != true) {
                DEBUGLOG("Failed to set get-task-allow!");
            }
        }
    }

    if (!sandbox) {
        DEBUGLOG("Skipping exceptions because no sandbox");
        return;
    }

    uint64_t present = OSDictionary_GetItem(amfi_entitlements, file_exc_key);

    if (present == 0) {
        uint64_t exception_osarray = get_exception_osarray(abs_path_exceptions);
        DEBUGLOG("present=NULL; setting to 0x%llx", exception_osarray);
        rv = OSDictionary_SetItem(amfi_entitlements, file_exc_key, exception_osarray);
        release_exception_osarray(&exception_osarray);
        if (rv != true) {
            DEBUGLOG("Failed to set %s", file_exc_key);
        }
        return;
    }

    char **currentExceptions = copy_amfi_entitlements(present);

    for (const char **exception = abs_path_exceptions; *exception; exception++) {
        DEBUGLOG("Looking for %s", *exception);
        Boolean foundException = false;
        for (char **entitlementString = currentExceptions; *entitlementString && !foundException; entitlementString++) {
            char *ent = strdup(*entitlementString);
            int lastchar = strlen(ent) - 1;
            if (ent[lastchar] == '/') ent[lastchar] = '\0';

            if (strcasecmp(ent, *exception) == 0) {
                DEBUGLOG("Found existing exception: %s", *entitlementString);
                foundException = true;
            }
            free(ent);
        }
        if (!foundException) {
            DEBUGLOG("Adding exception: %s", *exception);
            const char **exception_array = malloc(((1 + 1) * sizeof(char*)) + MAXPATHLEN);
            exception_array[0] = *exception;
            exception_array[1] = NULL;
            uint64_t exception_osarray = get_exception_osarray(exception_array);
            free(exception_array);
            rv = OSArray_Merge(present, exception_osarray);
            release_exception_osarray(&exception_osarray);
            
            if (rv != true) {
                DEBUGLOG("Failed to add exception: %s", *exception);
            }
        }
    }
    free(currentExceptions);
}

size_t kstrlen(uint64_t ptr) {
    size_t kstrlen = (size_t)kexecute(offset_strlen, ptr, 0, 0, 0, 0, 0, 0);
    return kstrlen;
}

uint64_t kstralloc(const char *str) {
    size_t str_kptr_size = strlen(str) + 1;
    uint64_t str_kptr = kalloc(str_kptr_size);
    if (str_kptr != 0) {
        if (kwrite(str_kptr, str, str_kptr_size) != str_kptr_size) {
            CROAK("Unable to write string to kernel memory");
        }
        kwrite(str_kptr, str, str_kptr_size);
    } else {
        CROAK("Unable to allocate kernel memory for string");
    }
    return str_kptr;
}

void kstrfree(uint64_t ptr) {
    if (ptr != 0) {
        size_t size = kstrlen(ptr);
        kfree(ptr, size);
    }
}

void fixup_cs_flags(uint64_t proc) {
    modify_csflags(proc, ^(uint32_t *flags) {
        if (!(*flags & CS_VALID)) {
            DEBUGLOG("Adding CS_VALID (0x%x)", CS_VALID);
            *flags |= CS_VALID;
        }
        if (!(*flags & CS_PLATFORM_BINARY)) {
            DEBUGLOG("Adding CS_PLATFORM_BINARY (0x%x)", CS_PLATFORM_BINARY);
            *flags |= CS_PLATFORM_BINARY;
        }
        if ((*flags & CS_REQUIRE_LV)) {
            DEBUGLOG("Removing CS_REQUIRE_LV (0x%x)", CS_REQUIRE_LV);
            *flags &= ~CS_REQUIRE_LV;
        }
        if ((*flags & CS_CHECK_EXPIRATION)) {
            DEBUGLOG("Removing CS_CHECK_EXPIRATION (0x%x)", CS_CHECK_EXPIRATION);
            *flags &= ~CS_CHECK_EXPIRATION;
        }
        if (!(*flags & CS_DYLD_PLATFORM)) {
            DEBUGLOG("Adding CS_DYLD_PLATFORM (0x%x)", CS_DYLD_PLATFORM);
            *flags |= CS_DYLD_PLATFORM;
        }
        if (OPT(GET_TASK_ALLOW)) {
            if (!(*flags & CS_GET_TASK_ALLOW)) {
                DEBUGLOG("Adding CS_GET_TASK_ALLOW (0x%x)", CS_GET_TASK_ALLOW);
                *flags |= CS_GET_TASK_ALLOW;
            }
            if (!(*flags & CS_INSTALLER)) {
                DEBUGLOG("Adding CS_INSTALLER (0x%x)", CS_INSTALLER);
                *flags |= CS_INSTALLER;
            }
            if ((*flags & CS_RESTRICT)) {
                DEBUGLOG("Removing CS_RESTRICT (0x%x)", CS_RESTRICT);
                *flags &= ~CS_RESTRICT;
            }
        }
        if (OPT(CS_DEBUGGED)) {
            if (!(*flags & CS_DEBUGGED)) {
                DEBUGLOG("Adding CS_DEBUGGED (0x%x)", CS_DEBUGGED);
                *flags |= CS_DEBUGGED;
            }
            if ((*flags & CS_HARD)) {
                DEBUGLOG("Removing CS_HARD (0x%x)", CS_HARD);
                *flags &= ~CS_HARD;
            }
            if ((*flags & CS_KILL)) {
                DEBUGLOG("Removing CS_KILL (0x%x)", CS_KILL);
                *flags &= ~CS_KILL;
            }
        }
    });
}

void fixup_t_flags(uint64_t proc) {
    modify_t_flags(proc, ^(uint32_t *flags) {
        if (!(*flags & TF_PLATFORM)) {
            DEBUGLOG("Adding TF_PLATFORM (0x%x)", TF_PLATFORM);
            *flags |= TF_PLATFORM;
        }
    });
}

void fixup_setuid(pid_t pid, uint64_t proc, uint64_t ucred, const char *path) {
    struct stat file_st;
    if (lstat(path, &file_st) == -1) {
        CROAK("Unable to get stat for file %s", path);
        return;
    }
    
    if (!(file_st.st_mode & S_ISUID) && !(file_st.st_mode & S_ISGID)) {
        DEBUGLOG("File is not setuid or setgid: %s", path);
        return;
    }
    
    if (file_st.st_mode & S_ISUID) {
        uid_t file_uid = file_st.st_uid;
        DEBUGLOG("Applying uid 0x%x to process 0x%x", file_uid, pid);
        wk32(proc + offsetof_p_svuid, file_uid);
        wk32(ucred + offsetof_ucred_cr_svuid, file_uid);
        wk32(ucred + offsetof_ucred_cr_uid, file_uid);
    }
    
    if (file_st.st_mode & S_ISGID) {
        gid_t file_gid = file_st.st_gid;
        DEBUGLOG("Applying gid 0x%x to process 0x%x", file_gid, pid);
        wk32(proc + offsetof_p_svgid, file_gid);
        wk32(ucred + offsetof_ucred_cr_svgid, file_gid);
        wk32(ucred + offsetof_ucred_cr_groups, file_gid);
    }
}

void fixup_sandbox(uint64_t proc, uint64_t proc_ucred, uint64_t sandbox) {
    set_sandbox_extensions(proc, proc_ucred, sandbox);
}

void fixup_amfi_entitlements(uint64_t proc, uint64_t proc_ucred, uint64_t amfi_entitlements, uint64_t sandbox) {
    set_amfi_entitlements(proc, proc_ucred, amfi_entitlements, sandbox);
}

void fixup_process(const struct process_fixup *fixup, int options) {
    pthread_mutex_lock(&fixup_lock);
    
    pid_t pid = fixup->process_pid;
    const char *path = fixup->process_path;
    
    uint64_t proc = proc_find(pid);
    if (proc == 0) {
        DEBUGLOG("Failed to find proc for pid 0x%x (path %s)", pid, path);
        goto out;
    }
    
    if ((options & FIXUP_CS_FLAGS)) {
        DEBUGLOG("Fixing up codesign flags for pid 0x%x (path %s)", pid, path);
        fixup_cs_flags(proc);
    }
    
    if ((options & FIXUP_T_FLAGS)) {
        DEBUGLOG("Fixing up task flags for pid 0x%x (path %s)", pid, path);
        fixup_t_flags(proc);
    }
    
    if (!(options & FIXUP_SETUID) && !(options & FIXUP_SANDBOX) && !(options & FIXUP_AMFI_ENTITLEMENTS)) {
        goto out;
    }
    
    uint64_t proc_ucred = rk64(proc + offsetof_p_ucred);
    if (proc_ucred == 0) {
        DEBUGLOG("Failed to find proc credentials for pid 0x%x (path %s)", pid, path);
        goto out;
    }
    
    if ((options & FIXUP_SETUID)) {
        DEBUGLOG("Fixing up setuid for pid 0x%x (path %s)", pid, path);
        fixup_setuid(pid, proc, proc_ucred, path);
    }
    
    if (!(options & FIXUP_SANDBOX) && !(options & FIXUP_AMFI_ENTITLEMENTS)) {
        goto out;
    }
    
    uint64_t cr_label = rk64(proc_ucred + 0x78);
    
    if (cr_label == 0) {
        DEBUGLOG("Failed to find cr_label for pid 0x%x (path %s)", pid, path);
        goto out;
    }
    
    uint64_t amfi_entitlements = rk64(cr_label + 0x8);
    
    if (amfi_entitlements == 0) {
        DEBUGLOG("Failed to find amfi_entitlements for pid 0x%x (path %s)", pid, path);
        goto out;
    }
    
    uint64_t sandbox = rk64(cr_label + 0x8 + 0x8);
    
    if ((options & FIXUP_SANDBOX)) {
        DEBUGLOG("Fixing up sandbox for pid 0x%x (path %s)", pid, path);
        fixup_sandbox(proc, proc_ucred, sandbox);
    }
    
    if ((options & FIXUP_AMFI_ENTITLEMENTS)) {
        DEBUGLOG("Fixing up AMFI entitlements for pid 0x%x (path %s)", pid, path);
        fixup_amfi_entitlements(proc, proc_ucred, amfi_entitlements, sandbox);
    }
    
out:
    if (proc != 0) {
        proc_rele(proc);
    }
    pthread_mutex_unlock(&fixup_lock);
}

void kern_utils_cleanup() {
    return;
}
