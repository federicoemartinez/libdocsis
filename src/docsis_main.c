/*
 * docsis - command-line harness on top of libdocsis.
 *
 * This binary is NOT the product: it is not installed. It exists because
 * the 142 fixtures under tests/ and the encode-vs-decode comparison under
 * examples/ are driven through it. The product is libdocsis (see
 * src/libdocsis.h).
 *
 * Split out of docsis.c so the shared library does not export a main().
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H  */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/config_api.h>
#include <net-snmp/output_api.h>
#include <net-snmp/mib_api.h>

#include "docsis.h"
#include "docsis_globals.h"
#include "ethermac.h"

#ifdef __GNUC__
static void usage () __attribute__((__noreturn__));
#endif

static void
usage ()
{
  fprintf(stderr, "DOCSIS Configuration File creator, version %s\n", VERSION);
  fprintf(stderr, "Copyright (c) 1999,2000,2001 Cornel Ciocirlan, ctrl@users.sourceforge.net\n");
  fprintf(stderr, "Copyright (c) 2002,2003,2004,2005 Evvolve Media SRL, docsis@evvolve.com\n");
  fprintf(stderr, "Copyright (c) 2014 - 2015 Adrian Simionov, daniel.simionov@gmail.com\n\n");

  fprintf(stderr, "To encode a cable modem configuration file: \n\tdocsis [modifiers] -e <modem_cfg_file> <key_file> <output_file>\n");
  fprintf(stderr, "To encode multiple cable modem configuration files: \n\tdocsis [modifiers] -m <modem_cfg_file1> ... <key_file> <new_extension>\n");
  fprintf(stderr, "To encode a MTA configuration file: \n\tdocsis [modifiers] -p <mta_cfg_file> <output_file>\n");
  fprintf(stderr, "To encode multiple MTA configuration files: \n\tdocsis [modifiers] -m -p <mta_file1> ... <new_extension>\n");
  fprintf(stderr, "To decode a CM or MTA config file: \n\tdocsis [modifiers] -d <binary_file>\n\n");

  fprintf(stderr, "Where:\n<cfg_file>\t\t= name of text (human readable) cable modem or MTA \n"
		  "\t\t\t  configuration file;\n"
		  "<key_file>\t\t= text file containing the authentication key\n"
		  "\t\t\t  (shared secret) to be used for the CMTS MIC;\n"
		  "<output_file> \t\t= name of output file where"
		  " the binary data will\n\t\t\t  be written to (if it does not exist it is created);\n"
		  "<binary_file>\t\t= name of binary file to be decoded;\n"
		  "<new_extension>\t\t= new extension to be used when encoding multiple files.\n\n");

  fprintf(stderr, "The following command-line modifiers are available:\n"
	"	-o\n"
	"		Decode OIDs numerically.\n\n"
	"	-M \"PATH1:PATH2\"\n"
	"		Specify the SNMP MIB directory when encoding or decoding configuration\n"
	"		files.\n\n"
	"	-na | -eu\n"
	"		Adds CableLabs PacketCable or Excentis EuroPacketCable SHA1 hash\n"
	"		when encoding an MTA config file.\n\n"
	"	-dialplan\n"
	"		Adds a PC20 dialplan from an external file called \"dialplan.txt\" in\n"
	"		the current directory.\n\n"
	"	-nohash\n"
	"		Removes the PacketCable SHA1 hash from the MTA config file when\n"
	"		decoding.\n"
	);
  fprintf(stderr, "\nSee examples/*.cfg for sample configuration files.\n");
  fprintf(stderr, "\nPlease report bugs or feature requests on GitHub.");
  fprintf(stderr, "\nProject repository is https://github.com/rlaager/docsis\n\n");
  exit (-10);
}


int
main (int argc, char *argv[])
{
  unsigned char key[65];
  FILE *kf;
  char *config_file=NULL, *key_file=NULL, *output_file=NULL, *extension_string=NULL, *custom_mibs=NULL;
  unsigned int keylen = 0;
  unsigned int encode_docsis = FALSE, decode_bin = FALSE, hash = 0;
  int i;
  int resolve_oids = 1;

  while (argc > 0) {
    argc--; argv++;

    if (!argc) {
      usage();
    }

    /* the initial command-line parameters are flags / modifiers */
    if (!strcmp (argv[0], "-nohash")) {
      nohash = 1;

      continue;
    }

    if (!strcmp (argv[0], "-o")) {
      resolve_oids = 0;
      continue;
    }

    if (!strcmp (argv[0], "-M")) {
      if (argc < 2 ) {
        usage();
      }

      custom_mibs=argv[1];

      argc--; argv++;
      continue;
    }

    if (!strcmp (argv[0], "-na")) {
      if (hash) {
        usage();
      }
      hash = 1;
      continue;
    }

    if (!strcmp (argv[0], "-eu")) {
      if (hash) {
        usage();
      }
      hash = 2;
      continue;
    }

    if (!strcmp (argv[0], "-dialplan")) {
      dialplan = 1;
      continue;
    }

    /* the following command-line parameters are actions */

    if (!strcmp (argv[0], "-d")) {
      if (argc < 2 ) {
        usage();
      }

      decode_bin = TRUE;
      config_file = argv[1];

      break;
    }

    if (!strcmp (argv[0], "-e")) {
      if (argc < 4 ) {
        usage();
      }

      encode_docsis = TRUE;
      config_file = argv[1];
      key_file = argv[2];
      output_file = argv[3];

      break;
    }

    if (!strcmp (argv[0], "-m")) {
      extension_string = argv[argc-1];
      key_file = argv[argc-2];
      encode_docsis = TRUE;

      continue;
    }

    if (!strcmp (argv[0], "-p")) {
      /* encode_docsis may already have been set via the "-m" option */
      encode_docsis = 0;

      argc--; argv++;

      if (argc < 2 ) {
        usage();
      }

      /* -p might be followed by -dialplan.  This is allowed for backwards
       * compatibility */
      if (!strcmp (argv[0], "-dialplan")) {
        dialplan = 1;
        argc--; argv++;
      }

      if (argc < 2 ) {
        usage();
      }

      /* if -m has not already been specified, then we expect "<mta_cfg_file> <output_file>" */
      if (extension_string == NULL) {
        config_file = argv[0];
        output_file = argv[1];
      }

      break;
    }

    /* no more recognisable options means that we've either finished parsing
     * all arguments or else that the remaining arguments refer to a list of
     * config files */
    if ((argc && encode_docsis) || (argc && decode_bin)) {
      break;
    }
  }

  if (encode_docsis)
    {
      if ((kf = fopen (key_file, "r")) == NULL)
        {
          fprintf (stderr, "docsis: error: can't open keyfile %s\n", key_file);
          exit (-5);
        }
      keylen = fread (key, sizeof (unsigned char), 64, kf);
      while (keylen > 0 && (key[keylen - 1] == 10 || key[keylen - 1] == 13))
        {
          keylen--;		/* eliminate trailing \n or \r */
        }
      fclose(kf);
    }

  init_global_symtable ();
  setup_mib_flags(resolve_oids,custom_mibs);

  if (decode_bin)
  {
      decode_file (config_file);
      exit(0); // TODO: clean shutdown
  }

  if (extension_string) { /* encoding multiple files */
	if (encode_docsis) {
		/* encode argv[argc-3] to argv[0] */
		for (i=0; i<argc-2; i++) {
			if ( (output_file = get_output_name (argv[i], extension_string)) == NULL ) {
				fprintf(stderr, "Cannot process input file %s, extension too short ?\n",argv[i] );
				continue;
			}

			fprintf(stderr, "Processing input file %s: output to  %s\n",argv[i], output_file);
			if (encode_one_file (argv[i], output_file, key, keylen, encode_docsis, hash)) {
				exit(2);
			}
			free (output_file);
			output_file = NULL;
		}
	} else {
		/* encode argv[argc-2] to argv[0] */
		for (i=0; i<argc-1; i++) {
			if ( (output_file = get_output_name (argv[i], extension_string)) == NULL ) {
				fprintf(stderr, "Cannot process input file %s, extension too short ?\n",argv[i] );
				continue;
			}
			fprintf (stderr, "Processing input file %s: output to  %s\n",argv[i], output_file);
			if (encode_one_file (argv[i], output_file, key, keylen, encode_docsis, hash)) {
				exit(2);
			}
			free (output_file);
			output_file = NULL;
		}
	}
  } else {
	if (encode_one_file (config_file, output_file, key, keylen, encode_docsis, hash)) {
		exit(2);
	}
	/* encode argv[1] */
  }
  //free(global_symtable);
  shutdown_mib();
  return 0;
}
