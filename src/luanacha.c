// Copyright (c) 2017  Phil Leblanc  -- see LICENSE file
// ---------------------------------------------------------------------

/*

luanacha

This is a Lua library wrapping the Monocypher library by Loup Vaillant.
http://loup-vaillant.fr/projects/monocypher/

The functions keep as much as possible the same name as in  Monocypher 
(without the "crypto_" prefix)

luanachaAPI:

randombytes(n)
	return a string containing n random bytes
	
--- Authenticated encryption

ae_lock
	authenticated encryption
	with an optional prefix prepended to the encrypted text

ae_unlock
	authenticated decryption
	with an optional offset for the start of the encrypted text

--- Curve25519-based key exchange

x25519_keypair
	generates a pair of curve25519 keys (secret key, public key)

x25519_public_key
	return the public key associated to a secret key

lock_key
	DH key exchange. Return a session key

--- Blake2b cryptographic hash

blake2b_init
	initialize and return a blake2b context object

blake2b_update
	update the hash with a new text fragment

blake2b_final
	return the final value of the hash

blake2b
	compute the hash of a string (convenience function)



--- Ed25519 signature

ed25519_keypair
	generates a pair of ed25519 signature keys (secret key, public key)

ed25519_public_key
	return the public key associated to a secret key

ed25519_sign
	sign a text with a secret key

ed25519_check
	check a text signature with a public key

*/

#define LUANACHA_VERSION "luanacha-0.1"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "monocypher.h"

//----------------------------------------------------------------------
// compatibility with Lua 5.2  --and lua 5.3, added 150621
// (from roberto's lpeg 0.10.1 dated 101203)
//
#if (LUA_VERSION_NUM >= 502)

#undef lua_equal
#define lua_equal(L,idx1,idx2)  lua_compare(L,(idx1),(idx2),LUA_OPEQ)

#undef lua_getfenv
#define lua_getfenv	lua_getuservalue
#undef lua_setfenv
#define lua_setfenv	lua_setuservalue

#undef lua_objlen
#define lua_objlen	lua_rawlen

#undef luaL_register
#define luaL_register(L,n,f) \
	{ if ((n) == NULL) luaL_setfuncs(L,f,0); else luaL_newlib(L,f); }

#endif

//----------------------------------------------------------------------
// lua binding   (all LuaNacha exposed functions are prefixed with "ln_")

# define LERR(msg) return luaL_error(L, msg)



extern void randombytes(unsigned char *x,unsigned long long xlen); 

static int ln_randombytes(lua_State *L) {
	
    size_t bufln; 
	lua_Integer li = luaL_checkinteger(L, 1);  // 1st arg
	bufln = (size_t) li;
    unsigned char *buf = malloc(bufln); 
	randombytes(buf, li);
    lua_pushlstring (L, buf, bufln); 
    free(buf);
	return 1;
}//randombytes()

//----------------------------------------------------------------------
// authenticated encryption

static int ln_ae_lock(lua_State *L) {
	// Lua API: ae_lock(k, n, m [, pfx])
	//  k: key string (32 bytes)
	//  n: nonce string (24 bytes)
	//	m: message (plain text) string 
	//  pfx: optional prefix string - prepended to the encrypted text
	//     - pfx length should be a multiple of 8 for alignment
	//  return encrypted text string
	int r;
	size_t mln, nln, kln, pfxln, bufln;
	const char *k = luaL_checklstring(L,1,&kln);
	const char *n = luaL_checklstring(L,2,&nln);	
	const char *m = luaL_checklstring(L,3,&mln);	
	const char *pfx = luaL_optlstring(L,4,"",&pfxln);
	if (nln != 24) LERR("bad nonce size");
	if (kln != 32) LERR("bad key size");
	if ((pfxln % 8) != 0) LERR("bad prefix size");
	bufln = mln + 16 + pfxln;
	unsigned char * buf = malloc(bufln);
	crypto_ae_lock(buf+pfxln, k, n, m, mln);
	if (pfxln > 0) {
		memcpy(buf, pfx, pfxln);
	}
	lua_pushlstring (L, buf, bufln); 
	free(buf);
	return 1;
} // ae_lock()

static int ln_ae_unlock(lua_State *L) {
	// Lua API: ae_unlock(k, n, c [, i])
	//  k: key string (32 bytes)
	//  n: nonce string (24 bytes)
	//	c: encrypted message string 
	//  i: optional offset of the start of the encrypted text in c
	//     default value is 0 - useful if c starts with a prefix
	//  return plain text string or (nil, error msg if MAC is not valid)
	int r = 0;
	size_t cln, nln, kln, mln;
	const char *k = luaL_checklstring(L,1,&kln);
	const char *n = luaL_checklstring(L,2,&nln);	
	const char *c = luaL_checklstring(L,3,&cln);	
	int i = luaL_optinteger(L,4, 0);	
	if (nln != 24) LERR("bad nonce size");
	if (kln != 32) LERR("bad key size");
	
	unsigned char * buf = malloc(cln);
	mln = cln - i - 16;
	r = crypto_ae_unlock(buf, k, n, c+i, mln);
	if (r != 0) { 
		free(buf); 
		lua_pushnil (L);
		lua_pushliteral(L, "unlock error");
		return 2;         
	} 
	lua_pushlstring (L, buf, mln); 
	free(buf);
	return 1;
} // ln_ae_unlock()

//----------------------------------------------------------------------
// curve25519 functions

static int ln_x25519_keypair(lua_State *L) {
	// generate and return a random key pair (publickey, secretkey)
	// lua api: x25519_keypair()
	// return (sk, pk)
	unsigned char pk[32];
	unsigned char sk[32];
	// sk is a random string. Then, compute the matching public key
	randombytes(sk, 32);
	crypto_x25519_public_key(pk, sk);
	lua_pushlstring (L, pk, 32); 
	lua_pushlstring (L, sk, 32); 
	return 2;
}//ln_x25519_keypair()

static int ln_x25519_public_key(lua_State *L) {
	// return the public key associated to a secret key
	// lua api:  x25519_public_key(sk) return pk
	// sk: a secret key (can be any random value)
	// pk: the matching public key
	size_t skln;
	unsigned char pk[32];
	const char *sk = luaL_checklstring(L,1,&skln); // secret key
	if (skln != 32) LERR("bad sk size");
	crypto_x25519_public_key(pk, sk);
	lua_pushlstring (L, pk, 32); 
	return 1;
}//ln_x25519_public_key()

static int ln_lock_key(lua_State *L) {
	// DH key exchange: compute a session key
	// lua api:  lock_key(sk, pk) => k
	// !! beware, reversed order compared to nacl box_beforenm() !!
	// sk: "your" secret key
	// pk: "their" public key
	// return the session key k
	size_t pkln, skln;
	u8 k[32];
	const char *sk = luaL_checklstring(L,1,&skln); // your secret key
	const char *pk = luaL_checklstring(L,2,&pkln); // their public key
	if (pkln != 32) LERR("bad pk size");
	if (skln != 32) LERR("bad sk size");
	r = crypto_lock_key(k, sk, pk);
	lua_pushlstring(L, k, 32); 
	return 1;   
}// ln_lock_key()




//------------------------------------------------------------
// lua library declaration
//
static const struct luaL_Reg luanachalib[] = {
	{"randombytes", ln_randombytes},
	{"ae_lock", ln_ae_lock},
	{"ae_unlock", ln_ae_unlock},
	{"x25519_keypair", ln_x25519_keypair},
	{"x25519_public_key", ln_x25519_public_key},
	{"lock_key", ln_lock_key},
	
	{NULL, NULL},
};

int luaopen_luanacha(lua_State *L) {
	luaL_register (L, "luanacha", luanachalib);
    // 
    lua_pushliteral (L, "VERSION");
	lua_pushliteral (L, LUANACHA_VERSION); 
	lua_settable (L, -3);
	return 1;
}

===           TOC


