/*
 * libdocsis - API pública.
 *
 * Uso: initialize() una vez, después encode_file() cuantas veces haga falta,
 * liberando cada buffer con free_encoded_file().
 *
 * MIBs: initialize() carga los MIBs vía net-snmp y NO setea MIBDIRS. Si los
 * MIBs no están en la ruta por defecto de net-snmp, exportá MIBDIRS antes de
 * llamar a initialize(), apuntando a los directorios mibs/, mibs/iana y
 * mibs/ietf de esta distribución.
 *
 * Concurrencia: encode_file() serializa internamente con un mutex. Es seguro
 * llamarla desde varios hilos, pero no corre en paralelo. Ver el comentario
 * ponytail: sobre _my_tlvtree_head en docsis_yy.y.
 *
 * Plataforma: POSIX. parse_config_string() usa fmemopen(), que no existe en
 * MSVC.
 */
#ifndef LIBDOCSIS_H
#define LIBDOCSIS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Carga los MIBs y la tabla de símbolos. Llamar una vez antes de todo.
   Devuelve 1 en éxito, 0 si los MIBs no cargaron.
   (0 significa fallo en toda esta API, sin excepciones.) */
int initialize (void);

/* Codifica `content` (largo `content_size`) en un buffer nuevo.
   En éxito devuelve el largo en bytes (> 0) y deja el buffer en *output.
   En error devuelve 0 y deja *output en NULL.
   El buffer se libera con free_encoded_file(). */
int encode_file (unsigned char *content, unsigned content_size,
                 unsigned char **output,
                 unsigned char *key, unsigned int keylen,
                 int encode_docsis, unsigned int hash);

/* Libera un buffer devuelto por encode_file(). Acepta NULL. */
void free_encoded_file (unsigned char *buffer);

#ifdef __cplusplus
}
#endif

#endif /* LIBDOCSIS_H */
