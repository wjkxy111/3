#include "../include/autosar_secure_image.h"

#include <string.h>

typedef struct st_sha256
{
    uint32_t state[8];
    uint64_t bits;
    uint8_t block[64];
    uint8_t used;
} sha256_t;

static uint32_t sha_rotr(uint32_t value, uint32_t amount)
{
    return (value >> amount) | (value << (32U - amount));
}

static uint32_t sha_be32(uint8_t const * p)
{
    return ((uint32_t) p[0] << 24U) | ((uint32_t) p[1] << 16U) |
           ((uint32_t) p[2] << 8U) | (uint32_t) p[3];
}

static void sha_store_be32(uint8_t * p, uint32_t value)
{
    p[0] = (uint8_t) (value >> 24U);
    p[1] = (uint8_t) (value >> 16U);
    p[2] = (uint8_t) (value >> 8U);
    p[3] = (uint8_t) value;
}

static void sha256_transform(sha256_t * sha, uint8_t const block[64])
{
    static uint32_t const k[64] =
    {
        0x428A2F98UL, 0x71374491UL, 0xB5C0FBCFUL, 0xE9B5DBA5UL,
        0x3956C25BUL, 0x59F111F1UL, 0x923F82A4UL, 0xAB1C5ED5UL,
        0xD807AA98UL, 0x12835B01UL, 0x243185BEUL, 0x550C7DC3UL,
        0x72BE5D74UL, 0x80DEB1FEUL, 0x9BDC06A7UL, 0xC19BF174UL,
        0xE49B69C1UL, 0xEFBE4786UL, 0x0FC19DC6UL, 0x240CA1CCUL,
        0x2DE92C6FUL, 0x4A7484AAUL, 0x5CB0A9DCUL, 0x76F988DAUL,
        0x983E5152UL, 0xA831C66DUL, 0xB00327C8UL, 0xBF597FC7UL,
        0xC6E00BF3UL, 0xD5A79147UL, 0x06CA6351UL, 0x14292967UL,
        0x27B70A85UL, 0x2E1B2138UL, 0x4D2C6DFCUL, 0x53380D13UL,
        0x650A7354UL, 0x766A0ABBUL, 0x81C2C92EUL, 0x92722C85UL,
        0xA2BFE8A1UL, 0xA81A664BUL, 0xC24B8B70UL, 0xC76C51A3UL,
        0xD192E819UL, 0xD6990624UL, 0xF40E3585UL, 0x106AA070UL,
        0x19A4C116UL, 0x1E376C08UL, 0x2748774CUL, 0x34B0BCB5UL,
        0x391C0CB3UL, 0x4ED8AA4AUL, 0x5B9CCA4FUL, 0x682E6FF3UL,
        0x748F82EEUL, 0x78A5636FUL, 0x84C87814UL, 0x8CC70208UL,
        0x90BEFFFAUL, 0xA4506CEBUL, 0xBEF9A3F7UL, 0xC67178F2UL
    };
    uint32_t w[64];
    uint32_t a = sha->state[0];
    uint32_t b = sha->state[1];
    uint32_t c = sha->state[2];
    uint32_t d = sha->state[3];
    uint32_t e = sha->state[4];
    uint32_t f = sha->state[5];
    uint32_t g = sha->state[6];
    uint32_t h = sha->state[7];
    for (uint32_t i = 0U; i < 16U; i++)
    {
        w[i] = sha_be32(&block[i * 4U]);
    }
    for (uint32_t i = 16U; i < 64U; i++)
    {
        uint32_t const s0 = sha_rotr(w[i - 15U], 7U) ^ sha_rotr(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
        uint32_t const s1 = sha_rotr(w[i - 2U], 17U) ^ sha_rotr(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
        w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }
    for (uint32_t i = 0U; i < 64U; i++)
    {
        uint32_t const s1 = sha_rotr(e, 6U) ^ sha_rotr(e, 11U) ^ sha_rotr(e, 25U);
        uint32_t const ch = (e & f) ^ ((~e) & g);
        uint32_t const temp1 = h + s1 + ch + k[i] + w[i];
        uint32_t const s0 = sha_rotr(a, 2U) ^ sha_rotr(a, 13U) ^ sha_rotr(a, 22U);
        uint32_t const maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t const temp2 = s0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    sha->state[0] += a; sha->state[1] += b; sha->state[2] += c; sha->state[3] += d;
    sha->state[4] += e; sha->state[5] += f; sha->state[6] += g; sha->state[7] += h;
}

static void sha256_init(sha256_t * sha)
{
    static uint32_t const initial[8] =
    {
        0x6A09E667UL, 0xBB67AE85UL, 0x3C6EF372UL, 0xA54FF53AUL,
        0x510E527FUL, 0x9B05688CUL, 0x1F83D9ABUL, 0x5BE0CD19UL
    };
    memcpy(sha->state, initial, sizeof(initial));
    sha->bits = 0U;
    sha->used = 0U;
}

static void sha256_update(sha256_t * sha, uint8_t const * data, size_t length)
{
    while (length > 0U)
    {
        size_t const space = sizeof(sha->block) - sha->used;
        size_t const chunk = (length < space) ? length : space;
        memcpy(&sha->block[sha->used], data, chunk);
        sha->used = (uint8_t) (sha->used + chunk);
        sha->bits += (uint64_t) chunk * 8ULL;
        data += chunk;
        length -= chunk;
        if (sha->used == sizeof(sha->block))
        {
            sha256_transform(sha, sha->block);
            sha->used = 0U;
        }
    }
}

static void sha256_final(sha256_t * sha, uint8_t digest[32])
{
    uint64_t const bits = sha->bits;
    sha->block[sha->used++] = 0x80U;
    while (sha->used != 56U)
    {
        if (sha->used == sizeof(sha->block))
        {
            sha256_transform(sha, sha->block);
            sha->used = 0U;
        }
        sha->block[sha->used++] = 0U;
    }
    for (uint8_t i = 0U; i < 8U; i++)
    {
        sha->block[56U + i] = (uint8_t) (bits >> (56U - (8U * i)));
    }
    sha256_transform(sha, sha->block);
    for (uint8_t i = 0U; i < 8U; i++)
    {
        sha_store_be32(&digest[i * 4U], sha->state[i]);
    }
}

void autosar_sha256(uint8_t const * data, size_t length, uint8_t digest[32])
{
    sha256_t sha;
    if ((NULL == digest) || ((NULL == data) && (length != 0U)))
    {
        return;
    }
    sha256_init(&sha);
    sha256_update(&sha, data, length);
    sha256_final(&sha, digest);
}

void autosar_secure_signing_digest(autosar_secure_image_header_t const * header, uint8_t digest[32])
{
    /* The signed domain is all immutable metadata through image_hash.  The
     * signature and reserved bytes are excluded, so signing is deterministic. */
    if (NULL == header)
    {
        return;
    }
    autosar_sha256((uint8_t const *) header, offsetof(autosar_secure_image_header_t, signature), digest);
}

static bool autosar_secure_vector_valid(autosar_secure_image_header_t const * header,
                                        uint32_t slot_address,
                                        uint32_t slot_size)
{
    uint32_t const image_start = slot_address + AUTOSAR_SECURE_IMAGE_HEADER_SIZE;
    uint32_t const slot_end = slot_address + slot_size;
    bool const stack_ok = (header->vector_sp >= 0x22000000UL) &&
                          (header->vector_sp < 0x220E0000UL) &&
                          ((header->vector_sp & 0x7U) == 0U);
    bool const reset_ok = ((header->vector_reset & 1U) != 0U) &&
                          (header->vector_reset >= (image_start | 1U)) &&
                          (header->vector_reset < slot_end);
    return stack_ok && reset_ok;
}

bool autosar_secure_header_valid(autosar_secure_image_header_t const * header,
                                 uint32_t slot_address,
                                 uint32_t slot_size)
{
    (void) slot_address;
    if ((NULL == header) || (slot_size < AUTOSAR_SECURE_IMAGE_HEADER_SIZE) ||
        (header->magic != AUTOSAR_SECURE_IMAGE_MAGIC) ||
        (header->header_size != AUTOSAR_SECURE_IMAGE_HEADER_SIZE) ||
        (header->format_version != AUTOSAR_SECURE_IMAGE_FORMAT_VERSION) ||
        (header->image_size == 0U) ||
        (header->image_size > (slot_size - AUTOSAR_SECURE_IMAGE_HEADER_SIZE)) ||
        (header->key_id == 0U))
    {
        return false;
    }
    return true;
}

autosar_secure_validation_t autosar_secure_validate(
    autosar_secure_image_header_t const * header,
    uint32_t slot_address,
    uint32_t slot_size,
    uint32_t minimum_security_counter,
    autosar_secure_hash_fn hash_fn,
    autosar_secure_verify_fn verify_fn,
    void * context)
{
    uint8_t actual_hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE];
    uint8_t signing_hash[AUTOSAR_SECURE_IMAGE_HASH_SIZE];
    if ((NULL == header) || (NULL == hash_fn) || (NULL == verify_fn))
    {
        return AUTOSAR_SECURE_ERR_ARGUMENT;
    }
    if ((slot_size < AUTOSAR_SECURE_IMAGE_HEADER_SIZE) ||
        (header->image_size > (slot_size - AUTOSAR_SECURE_IMAGE_HEADER_SIZE)))
    {
        return AUTOSAR_SECURE_ERR_RANGE;
    }
    if (!autosar_secure_header_valid(header, slot_address, slot_size))
    {
        return AUTOSAR_SECURE_ERR_HEADER;
    }
    if (!autosar_secure_vector_valid(header, slot_address, slot_size))
    {
        return AUTOSAR_SECURE_ERR_VECTOR;
    }
    if (header->security_counter < minimum_security_counter)
    {
        return AUTOSAR_SECURE_ERR_ROLLBACK;
    }
    if (!hash_fn(slot_address + AUTOSAR_SECURE_IMAGE_HEADER_SIZE,
                 header->image_size, actual_hash, context) ||
        (memcmp(actual_hash, header->image_hash, sizeof(actual_hash)) != 0))
    {
        return AUTOSAR_SECURE_ERR_HASH;
    }
    autosar_secure_signing_digest(header, signing_hash);
    if (!verify_fn(signing_hash, header->signature, header->key_id, context))
    {
        return AUTOSAR_SECURE_ERR_SIGNATURE;
    }
    return AUTOSAR_SECURE_VALID;
}

char const * autosar_secure_validation_name(autosar_secure_validation_t status)
{
    switch (status)
    {
        case AUTOSAR_SECURE_VALID:          return "VALID";
        case AUTOSAR_SECURE_ERR_ARGUMENT:   return "ARGUMENT";
        case AUTOSAR_SECURE_ERR_HEADER:     return "HEADER";
        case AUTOSAR_SECURE_ERR_RANGE:      return "RANGE";
        case AUTOSAR_SECURE_ERR_VECTOR:     return "VECTOR";
        case AUTOSAR_SECURE_ERR_ROLLBACK:   return "ROLLBACK";
        case AUTOSAR_SECURE_ERR_HASH:       return "HASH";
        case AUTOSAR_SECURE_ERR_SIGNATURE:  return "SIGNATURE";
        default:                            return "UNKNOWN";
    }
}
