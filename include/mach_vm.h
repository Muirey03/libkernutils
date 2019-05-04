/***** mach_vm.h *****/
kern_return_t mach_vm_read(
                           vm_map_t target_task,
                           mach_vm_address_t address,
                           mach_vm_size_t size,
                           vm_offset_t *data,
                           mach_msg_type_number_t *dataCnt);

kern_return_t mach_vm_write(
                            vm_map_t target_task,
                            mach_vm_address_t address,
                            vm_offset_t data,
                            mach_msg_type_number_t dataCnt);

kern_return_t mach_vm_read_overwrite(
                                     vm_map_t target_task,
                                     mach_vm_address_t address,
                                     mach_vm_size_t size,
                                     mach_vm_address_t data,
                                     mach_vm_size_t *outsize);

kern_return_t mach_vm_allocate(
                               vm_map_t target,
                               mach_vm_address_t *address,
                               mach_vm_size_t size,
                               int flags);

kern_return_t mach_vm_deallocate (
                                  vm_map_t target,
                                  mach_vm_address_t address,
                                  mach_vm_size_t size);

kern_return_t mach_vm_protect (
                               vm_map_t target_task,
                               mach_vm_address_t address,
                               mach_vm_size_t size,
                               boolean_t set_maximum,
                               vm_prot_t new_protection);