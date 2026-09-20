/*
 * libdocsis - public API.
 *
 * Usage: call initialize() once, then encode_file() as often as needed,
 * releasing every buffer with free_encoded_file().
 *
 * MIBs: initialize() loads the MIBs through net-snmp and does NOT set
 * MIBDIRS. If the MIBs are not on net-snmp's default search path, export
 * MIBDIRS before calling initialize(), pointing at this distribution's
 * mibs/, mibs/iana and mibs/ietf directories.
 *
 * Concurrency: encode_file() serializes internally with a mutex. Calling it
 * from several threads is safe, but the calls do not run in parallel. See
 * the ponytail: note on _my_tlvtree_head in docsis_yy.y.
 *
 * Platform: POSIX. parse_config_string() uses fmemopen(), which does not
 * exist on MSVC.
 */
#ifndef LIBDOCSIS_H
#define LIBDOCSIS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Loads the MIBs and the symbol table. Call once, before anything else.
   Returns 1 on success, 0 if the MIBs could not be loaded.
   (0 means failure throughout this API, with no exceptions.) */
int initialize (void);

/* Encodes `content` (of `content_size` bytes) into a fresh buffer.
   On success returns the length in bytes (> 0) and stores the buffer in
   *output. On error returns 0 and leaves *output as NULL.
   Release the buffer with free_encoded_file(). */
int encode_file (unsigned char *content, unsigned content_size,
                 unsigned char **output,
                 unsigned char *key, unsigned int keylen,
                 int encode_docsis, unsigned int hash);

/* Releases a buffer returned by encode_file(). Accepts NULL. */
void free_encoded_file (unsigned char *buffer);

#ifdef __cplusplus
}
#endif

#endif /* LIBDOCSIS_H */
