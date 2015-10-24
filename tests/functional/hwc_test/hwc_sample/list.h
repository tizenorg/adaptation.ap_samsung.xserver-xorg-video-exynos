#ifndef _LIST_H
#define _LIST_H

#include <X11/X.h>
#include <xorg/list.h>

// just because without xorg_ prefix macros look more pretty
// Note: this isn't bridge between linux kernel list and xorg list.
#define list_head           xorg_list
#define list_entry          xorg_list_entry
#define list_add            xorg_list_add
#define list_append         xorg_list_append
#define list_del            xorg_list_del
#define list_for_each_entry xorg_list_for_each_entry

// be carefully, this macros checked only in gdb
#define list_for_each_prev_entry(pos, head, member) \
    for (pos = __container_of((head)->prev, pos, member);\
     &pos->member != (head);\
     pos = __container_of(pos->member.prev, pos, member))

#define list_init xorg_list_init

#endif
