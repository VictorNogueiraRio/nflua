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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <net/netfilter/nf_conntrack.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <handle.h>
#include <data.h>
#include <luadata.h>

#include "luautil.h"
#include "xt_lua.h"
#include "netlink.h"
#include "states.h"
#include "nf_util.h"

static int nflua_reply(lua_State *L)
{
	size_t len;
	unsigned char *type;
	unsigned char *msg;
	struct nflua_ctx *ctx;

	luaU_getregval(L, NFLUA_CTXENTRY, &ctx);
	if (ctx == NULL)
		goto error;

	type = (unsigned char *)luaL_checkstring(L, 1);
	msg = (unsigned char *)luaL_checklstring(L, 2, &len);

	switch (type[0]) {
	case 't':
		if (tcp_reply(ctx->skb, ctx->par, msg, len) != 0)
			goto error;
		break;
	default:
		goto error;
	}

	return 0;
error:
	return luaL_error(L, "couldn't reply a packet");
}

static void *checkldata(lua_State *L, int idx, size_t *size)
{
	data_t *ld = (data_t *)luaL_checkudata(L, idx, DATA_USERDATA);
	single_t *single;

	if (ld->handle->type != HANDLE_TYPE_SINGLE)
		luaL_argerror(L, idx, "invalid ldata handle type");

	single = &ld->handle->bucket.single;
	if (single->ptr == NULL)
		luaL_argerror(L, idx, "invalid ldata pointer");

	*size = ld->length - ld->offset;
	return (char *)single->ptr + ld->offset;
}

static int nflua_netlink(lua_State *L)
{
	struct nflua_state *s = luaU_getenv(L, struct nflua_state);
	int pid = luaL_checkinteger(L, 1);
	int group = luaL_optinteger(L, 2, 0);
	const void *payload;
	size_t size;
	int err;

	if (s == NULL)
		return luaL_error(L, "invalid nflua_state");

	switch (lua_type(L, 3)) {
	case LUA_TSTRING:
		payload = luaL_checklstring(L, 3, &size);
		break;
	case LUA_TUSERDATA:
		payload = checkldata(L, 3, &size);
		break;
	default:
		return luaL_argerror(L, 3, "must be string or ldata");
	}

	if ((err = nflua_nl_send_data(s, pid, group, payload, size)) < 0)
		return luaL_error(L, "failed to send message. Return code %d", err);

	lua_pushinteger(L, (lua_Integer)size);
	return 1;
}

#define NFLUA_SKBUFF  "lskb"
#define tolskbuff(L) ((struct sk_buff **) luaL_checkudata(L, 1, NFLUA_SKBUFF))
#define lnewskbuff(L) \
	((struct sk_buff **) lua_newuserdata(L, sizeof(struct sk_buff *)))

static int nflua_skb_send(lua_State *L)
{
	struct sk_buff *nskb, **lskb = tolskbuff(L);
	unsigned char *payload;
	size_t len;

	if (*lskb == NULL)
		return luaL_error(L, "closed packet");

	payload = (unsigned char *)lua_tolstring(L, 2, &len);
	if (payload != NULL) {
		nskb = tcp_payload(*lskb, payload, len);
		if (nskb == NULL)
			return luaL_error(L, "unable to set tcp payload");

		/* Original packet is not needed anymore */
		kfree_skb(*lskb);
		*lskb = NULL;
	} else {
		if (unlikely(skb_shared(*lskb)))
			return luaL_error(L, "cannot send a shared skb");
		nskb = *lskb;
	}

	if (route_me_harder(nskb)) {
		pr_err("unable to route packet");
		goto error;
	}

	if (tcp_send(nskb) != 0) {
		pr_err("unable to send packet");
		goto error;
	}

	*lskb = NULL;
	return 0;
error:
	if (*lskb == NULL && nskb != NULL)
		kfree_skb(nskb);
	return luaL_error(L, "send packet error");
}

static int nflua_getpacket(lua_State *L)
{
	struct sk_buff **lskb;
	struct nflua_ctx *ctx;

	luaU_getregval(L, NFLUA_CTXENTRY, &ctx);
	if (ctx == NULL)
		return luaL_error(L, "couldn't get packet context");

	lua_getfield(L, LUA_REGISTRYINDEX, NFLUA_SKBCLONE);
	if (!lua_isuserdata(L, -1)) {
		lskb = lnewskbuff(L);
		if ((*lskb = skb_copy(ctx->skb, GFP_ATOMIC)) == NULL)
			return luaL_error(L, "couldn't copy packet");

		luaL_setmetatable(L, NFLUA_SKBUFF);
		lua_pushvalue(L, -1);
		lua_setfield(L, LUA_REGISTRYINDEX, NFLUA_SKBCLONE);
	}

	return 1;
}

static int nflua_skb_free(lua_State *L)
{
	struct sk_buff **lskb = tolskbuff(L);

	if (*lskb != NULL) {
		kfree_skb(*lskb);
		*lskb = NULL;
	}

	return 0;
}

static int nflua_skb_tostring(lua_State *L)
{
	struct sk_buff *skb = *tolskbuff(L);

	if (skb == NULL) {
		lua_pushliteral(L, "packet closed");
	} else {
		lua_pushfstring(L,
			"packet: { len:%d data_len:%d users:%d "
			"cloned:%d dataref:%d frags:%d }",
			skb->len,
			skb->data_len,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0)
			refcount_read(&skb->users),
#else
			atomic_read(&skb->users),
#endif
			skb->cloned,
			atomic_read(&skb_shinfo(skb)->dataref),
			skb_shinfo(skb)->nr_frags);
	}

	return 1;
}

static int nflua_time(lua_State *L)
{
	struct timespec ts;

	getnstimeofday(&ts);
	lua_pushinteger(L, (lua_Integer)ts.tv_sec);
	lua_pushinteger(L, (lua_Integer)(ts.tv_nsec / NSEC_PER_MSEC));

	return 2;
}

int nflua_connid(lua_State *L)
{
	struct nflua_ctx *ctx;
	enum ip_conntrack_info info;
	struct nf_conn *conn;

	luaU_getregval(L, NFLUA_CTXENTRY, &ctx);
	if (ctx == NULL)
		return luaL_error(L, "couldn't get packet context");

	conn = nf_ct_get(ctx->skb, &info);
	lua_pushlightuserdata(L, conn);

	return 1;
}

int nflua_hotdrop(lua_State *L)
{
	struct nflua_ctx *ctx;

	luaU_getregval(L, NFLUA_CTXENTRY, &ctx);
	if (ctx == NULL)
		return luaL_error(L, "couldn't get packet context");

	luaL_checktype(L, -1, LUA_TBOOLEAN);
	ctx->par->hotdrop = lua_toboolean(L, -1);
	return 0;
}

static const luaL_Reg nflua_lib[] = {
	{"reply", nflua_reply},
	{"netlink", nflua_netlink},
	{"time", nflua_time},
	{"getpacket", nflua_getpacket},
	{"connid", nflua_connid},
	{"hotdrop", nflua_hotdrop},
	{NULL, NULL}
};

static const luaL_Reg nflua_skb_ops[] = {
	{"send", nflua_skb_send},
	{"close", nflua_skb_free},
	{"__gc", nflua_skb_free},
	{"__tostring", nflua_skb_tostring},
	{NULL, NULL}
};

int luaopen_nf(lua_State *L)
{
	luaL_newmetatable(L, NFLUA_SKBUFF);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_setfuncs(L, nflua_skb_ops, 0);
	lua_pop(L, 1);

	luaL_newlib(L, nflua_lib);
	return 1;
}
EXPORT_SYMBOL(luaopen_nf);