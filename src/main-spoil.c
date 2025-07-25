/**
 * \file main-spoil.c
 * \brief Support spoiler generation from the command line
 *
 * Copyright (c) 2021 Eric Branlund
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "angband.h"

#ifdef USE_SPOIL

#include "datafile.h"
#include "game-world.h"
#include "init.h"
#include "main.h"
#include "obj-init.h"
#include "obj-randart.h"
#include "obj-util.h"
#include "player-birth.h"
#include "savefile.h"
#include "ui-game.h"
#include "wizard.h"

static struct {
	char letter;
	void (*func)(const char*);
	bool enabled;
	const char *path;
} opts[] = {
	{ 'a', spoil_artifact, false, NULL },
	{ 'm', spoil_mon_desc, false, NULL },
	{ 'M', spoil_mon_info, false, NULL },
	{ 'o', spoil_obj_desc, false, NULL },
};

const char help_spoil[] =
	"Spoiler generation mode, subopts\n"
	"              -a fname    Write artifact spoilers to fname;\n"
	"                          if neither -p, -r, nor -s are used, uses\n"
	"                          the standard artifacts\n"
	"              -m fname    Write brief monster spoilers to fname\n"
	"              -M fname    Write extended monster spoilers to fname\n"
	"              -o fname    Write object spoilers to fname\n"
	"              -p          Use the artifacts associated with the\n"
	"                          savefile set by main.c\n"
	"              -r fname    Use the randart file, fname, as the source\n"
	"                          of the artifacts; overrides -p or -s\n"
	"              -s seed     Use the given randart seed (hexadecimal\n"
	"                          value; no leading 0x) to generate the\n"
        "                          artifacts; causes the randart file and log\n"
        "                          to be generated as well; overrides -p";

static bool copy_file(const char *src, const char *dest, file_type ft)
{
	ang_file *fin = file_open(src, MODE_READ, -1);
	ang_file *fout;
	char buf[1024];
	bool result;

	if (!fin) {
		return false;
	}

	fout = file_open(dest, MODE_WRITE, ft);
	if (!fout) {
		(void) file_close(fin);
		return false;
	}

	result = true;
	while (1) {
		int nin = file_read(fin, buf, sizeof(buf));

		if (nin > 0) {
			if (!file_write(fout, buf, nin)) {
				result = false;
				break;
			}
		} else {
			if (nin < 0) {
				result = false;
			}
			break;
		}
	}

	if (!file_close(fout)) {
		result = false;
	}
	if (!file_close(fin)) {
		result = false;
	}

	return result;
}

/* Make an effort to get the seed from the supplied randart file. */
static uint32_t parse_seed(const char *src)
{
	uint32_t result = 0;
	ang_file *fin = file_open(src, MODE_READ, -1);

	if (fin) {
		char buf[256];
		char *s;

		if (file_getl(fin, buf, sizeof(buf)) &&
				(s = my_stristr(buf, "seed "))) {
			char *pe;
			unsigned long ulv;

			errno = 0;
			ulv = strtoul(s + 5, &pe, 16);
			if (pe != s + 5 && (ulv < ULONG_MAX || errno == 0)) {
#if ULONG_MAX > 4294967295
				if (ulv <= 4294967295) {
					result = (uint32_t)ulv;
				}
#else
				result = (uint32_t)ulv;
#endif
			}
		}
		(void) file_close(fin);
	}
	return result;
}

/**
 * Usage:
 *
 * angband -mspoil -- [-a fname] [-m fname] [-M fname] [-o fname] \
 *     [-p] [-r fname] [-s seed]
 *
 *   -a fname  Write artifact spoilers to a file named fname.  If neither -p,
 *             -r, nor -s are used, the artifacts will be the standard set.
 *   -m fname  Write brief monster spoilers to a file named fname.
 *   -M fname  Write extended monster spoilers to a file named fname.
 *   -o fname  Write object spoilers to a file named fname.
 *   -p        Use the artifacts associated with savefile set by main.c.
 *   -r fname  Use the randart file, fname, as the source of the artifacts.
 *             Overrides -p or -s.
 *   -s seed   Use the given randart seed, a hexadecimal value without the
 *             leading 0x, to generate the artifacts.  Causes the randart file
 *             and log to be generated as well.  Overrides -p.
 *
 * Bugs:
 * Would be nice to accept "-" as the file name and write the spoilers to
 * standard output in that case.  Given the current implementation in
 * wiz-spoil.c, that would require writing to a temporary file and then
 * copying the contents of that file to standard output.  Don't have temporary
 * file support in z-file.c so punting on that for now.
 *
 * The functions in wiz-spoil.c don't provide any feedback about whether the
 * operation failed so the exit code will always indicate success even if there
 * was a problem.
 */
errr init_spoil(int argc, char *argv[]) {
	/* Skip over argv[0] */
	int i = 1;
	int result = 0;
	bool load_randart = false;
	const char *randart_name = NULL;
	bool have_specified_seed = false;
	uint32_t specified_seed = 0;

	/* Parse the arguments. */
	while (1) {
		bool badarg = false;
		int increment = 1;

		if (i >= argc) {
			break;
		}

		if (argv[i][0] == '-') {
			/* Try to match with a known option. */
			if (argv[i][1] == 'p' && argv[i][2] == '\0') {
				load_randart = true;
				randart_name = NULL;
			} else if (argv[i][1] == 'r' && argv[i][2] == '\0') {
				if (i < argc - 1) {
					load_randart = true;
					/* Record the name; don't parse it. */
					randart_name = argv[i + 1];
					++increment;
				} else {
					printf("init-spoil: '%s' requires an argument, the name of a randart file\n", argv[i]);
					result = 1;
				}
			} else if (argv[i][1] == 's' && argv[i][2] == '\0') {
				if (i < argc - 1) {
					char *valend;
					unsigned long val;

					val = strtoul(argv[i + 1], &valend, 16);
					++increment;
					if (argv[i + 1][0] != '\0'
							&& contains_only_spaces(valend)
							&& val <= 0xFFFFFFFFul) {
						load_randart = true;
						have_specified_seed = true;
						specified_seed = val;
					} else if (val > 0xFFFFFFFFul) {
						printf("init-spoil: seed is too large\n");
						result = 1;
					} else {
						printf("init-spoil: '%s' requires an integer argument, the randart seed\n",
							argv[i]);
						result = 1;
					}
				} else {
					printf("init-spoil: '%s' requires an argument, the randart seed\n", argv[i]);
					result = 1;
				}
			} else {
				int j = 0;

				while (1) {
					if (j >= (int)N_ELEMENTS(opts)) {
						badarg = true;
						break;
					}

					if (argv[i][1] == opts[j].letter &&
							argv[i][2] == '\0') {
						if (i < argc - 1) {
							opts[j].enabled = true;
							/*
							 * Record the filename
							 * and skip parsing
							 * of it.
							 */
							opts[j].path =
								argv[i + 1];
							++increment;
						} else {
							printf("init-spoil: '%s' requires an argument, the name of the spoiler file\n", argv[i]);
							result = 1;
						}
						break;
					}

					++j;
				}
			}
		} else {
			badarg = true;
		}

		if (badarg) {
			printf("init-spoil: bad argument '%s'\n", argv[i]);
			result = 1;
		}

		i += increment;
	}

	if (result != 0) return result;

	/* Generate the spoilers. */
	init_angband();

	if (load_randart) {
		if (randart_name || have_specified_seed) {
			if (player_make_simple(NULL, NULL, "Spoiler")) {
				char defname[1024] = "";

				deactivate_randart_file();
				option_set(option_name(OPT_birth_randarts),
					true);

				if (randart_name) {
					path_build(defname, sizeof(defname),
						ANGBAND_DIR_USER,
						"randart.txt");
					/*
					 * Copy rather than move in case the
					 * file supplied is read-only.
					 */
					if (copy_file(randart_name, defname,
							FTYPE_TEXT)) {
						seed_randart = parse_seed(randart_name);
					} else {
						printf("init-spoil: could not copy randart file to '%s'.\n", defname);
						result = 1;
					}
				} else {
					seed_randart = specified_seed;
					do_randart(seed_randart, true);
				}

				if (result == 0) {
					cleanup_parser(&artifact_parser);
					run_parser(&randart_parser);
					if (randart_name) {
						file_delete(defname);
					} else {
						deactivate_randart_file();
					}
				}
			} else {
				printf("init-spoil: could not initialize player.\n");
				result = 1;
			}
		} else {
			bool exists;

			safe_setuid_grab();
			exists = file_exists(savefile);
			safe_setuid_drop();
			if (exists) {
				bool loaded_save =
					savefile_load(savefile, false);

				deactivate_randart_file();
				if (!loaded_save) {
					printf("init-spoil: using artifacts "
						"associated with a savefile, "
						"but the savefile set by "
						"main, '%s', failed to load.\n",
						savefile);
					result = 1;
				}
			} else if (savefile[0]) {
				printf("init-spoil: using artifacts associated "
					"with a savefile, but the savefile set "
					"by main, '%s', does not exist.\n",
					savefile);
				result = 1;
			} else {
				printf("init-spoil: using artifacts associated "
					"with a savefile, but main did not set "
					"the savefile.\n");
				result = 1;
			}
		}
	} else if (!player_make_simple(NULL, NULL, "Spoiler")) {
		printf("init-spoil: could not initialize player.\n");
		result = 1;
	}

	if (result == 0) {
		flavor_set_all_aware();
		for (i = 0; i < (int)N_ELEMENTS(opts); ++i) {
			if (!opts[i].enabled) continue;
			(*(opts[i].func))(opts[i].path);
		}
	}

	cleanup_angband();

	if (result == 0) {
		exit(0);
	}

	return result;
}

#endif
