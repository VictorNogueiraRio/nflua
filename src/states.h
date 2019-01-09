/*
 * Copyright (C) 2017-2019 CUJO LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef NFLUA_STATES_H
#define NFLUA_STATES_H

#include <linux/refcount.h>

#include <lua.h>

#define NFLUA_NAME_MAXSIZE 64

struct xt_lua_net;

struct nflua_state {
	struct hlist_node node;
	lua_State *L;
	struct sock *sock;
	spinlock_t lock;
	refcount_t users;
	int id;
	u32 dseqnum;
	unsigned int maxalloc; /* Max alloc bytes  */
	unsigned char name[NFLUA_NAME_MAXSIZE];
};

typedef int (*nflua_state_cb)(struct nflua_state *s, unsigned short total,
        void *data);

struct nflua_state *nflua_state_create(struct xt_lua_net *xt_lua,
        unsigned int maxalloc, const char *name);

int nflua_state_destroy(struct xt_lua_net *xt_lua, const char *name);

struct nflua_state *nflua_state_lookup(struct xt_lua_net *xt_lua,
        const char *name);

int nflua_state_list(struct xt_lua_net *xt_lua, nflua_state_cb cb,
        void *data);

void nflua_state_destroy_all(struct xt_lua_net *xt_lua);

static inline struct nflua_state *nflua_state_get(struct nflua_state *s)
{
	refcount_inc(&s->users);
	return s;
}

static inline void nflua_state_put(struct nflua_state *s)
{
	refcount_dec(&s->users);
}

void nflua_states_init(struct xt_lua_net *xt_lua);
void nflua_states_exit(struct xt_lua_net *xt_lua);

#endif /* NFLUA_STATES_H */