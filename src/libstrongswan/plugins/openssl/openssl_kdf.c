/*
 * Copyright (C) 2022 Tobias Brunner, codelabs GmbH
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <openssl/opensslv.h>
#include <openssl/opensslconf.h>

#if !defined(OPENSSL_NO_HMAC) && OPENSSL_VERSION_NUMBER >= 0x10101000L

#include <openssl/evp.h>
#include <openssl/kdf.h>

#include "openssl_kdf.h"

typedef struct private_kdf_t private_kdf_t;

/**
 * Private data.
 */
struct private_kdf_t {

	/**
	 * Public interface.
	 */
	kdf_t public;

	/**
	 * Hasher to use for underlying PRF.
	 */
	const EVP_MD *hasher;

	/**
	 * Key for KDF. Stored here because OpenSSL's HKDF API does not provide a
	 * way to clear the "info" field in the context, new data is always
	 * appended (up to 1024 bytes).
	 */
	chunk_t key;

	/**
	 * Salt for prf+ (see above).
	 */
	chunk_t salt;
};

METHOD(kdf_t, get_type, key_derivation_function_t,
	private_kdf_t *this)
{
	return KDF_PRF_PLUS;
}

METHOD(kdf_t, get_length, size_t,
	private_kdf_t *this)
{
	return SIZE_MAX;
}

METHOD(kdf_t, get_bytes, bool,
	private_kdf_t *this, size_t out_len, uint8_t *buffer)
{
	EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);

	if (!ctx ||
		EVP_PKEY_derive_init(ctx) <= 0 ||
		EVP_PKEY_CTX_set_hkdf_md(ctx, this->hasher) <= 0 ||
		EVP_PKEY_CTX_hkdf_mode(ctx, EVP_PKEY_HKDEF_MODE_EXPAND_ONLY) <= 0 ||
		EVP_PKEY_CTX_set1_hkdf_key(ctx, this->key.ptr, this->key.len) <= 0 ||
		EVP_PKEY_CTX_add1_hkdf_info(ctx, this->salt.ptr, this->salt.len) <= 0 ||
		EVP_PKEY_derive(ctx, buffer, &out_len) <= 0)
	{
		EVP_PKEY_CTX_free(ctx);
		return FALSE;
	}
	EVP_PKEY_CTX_free(ctx);
	return TRUE;
}

METHOD(kdf_t, allocate_bytes, bool,
	private_kdf_t *this, size_t out_len, chunk_t *chunk)
{
	*chunk = chunk_alloc(out_len);

	if (!get_bytes(this, out_len, chunk->ptr))
	{
		chunk_free(chunk);
		return FALSE;
	}
	return TRUE;
}

METHOD(kdf_t, set_param, bool,
	private_kdf_t *this, kdf_param_t param, ...)
{
	chunk_t chunk;

	switch (param)
	{
		case KDF_PARAM_KEY:
			VA_ARGS_GET(param, chunk);
			chunk_clear(&this->key);
			this->key = chunk_clone(chunk);
			break;
		case KDF_PARAM_SALT:
			VA_ARGS_GET(param, chunk);
			chunk_clear(&this->salt);
			this->salt = chunk_clone(chunk);
			break;
	}
	return TRUE;
}

METHOD(kdf_t, destroy, void,
	private_kdf_t *this)
{
	chunk_clear(&this->salt);
	chunk_clear(&this->key);
	free(this);
}

/*
 * Described in header
 */
kdf_t *openssl_kdf_create(key_derivation_function_t algo, va_list args)
{
	private_kdf_t *this;
	pseudo_random_function_t prf_alg;
	char *name, buf[8];

	if (algo != KDF_PRF_PLUS)
	{
		return NULL;
	}

	VA_ARGS_VGET(args, prf_alg);
	name = enum_to_name(hash_algorithm_short_names,
						hasher_algorithm_from_prf(prf_alg));
	if (!name)
	{
		return NULL;
	}

	INIT(this,
		.public = {
			.get_type = _get_type,
			.get_length = _get_length,
			.get_bytes = _get_bytes,
			.allocate_bytes = _allocate_bytes,
			.set_param = _set_param,
			.destroy = _destroy,
		},
		.hasher = EVP_get_digestbyname(name),
		/* use a lengthy key to test the implementation below to make sure the
		 * algorithms are usable, see openssl_hmac.c for details */
		.key = chunk_clone(chunk_from_str("00000000000000000000000000000000")),
	);

	if (!this->hasher || !get_bytes(this, sizeof(buf), buf))
	{
		destroy(this);
		return NULL;
	}
	return &this->public;
}

#endif /* OPENSSL_NO_HMAC && OPENSSL_VERSION_NUMBER */
