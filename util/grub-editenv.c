/* grub-editenv.c - tool to edit environment block.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008,2009,2010 Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <grub/types.h>
#include <grub/emu/misc.h>
#include <grub/util/misc.h>
#include <grub/lib/envblk.h>
#include <grub/i18n.h>
#include <grub/emu/hostfile.h>
#include <grub/emu/hostdisk.h>
#include <grub/util/install.h>
#include <grub/util/libzfs.h>
#include <grub/emu/getroot.h>
#include <grub/fs.h>
#include <grub/crypto.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#include <argp.h>
#pragma GCC diagnostic error "-Wmissing-prototypes"
#pragma GCC diagnostic error "-Wmissing-declarations"
#include <assert.h>

#include "progname.h"

#define DEFAULT_ENVBLK_PATH DEFAULT_DIRECTORY "/" GRUB_ENVBLK_DEFCFG

static struct argp_option options[] = {
  {0,        0, 0, OPTION_DOC, N_("Commands:"), 1},
  {"create", 0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("Create a blank environment block file."), 0},
  {"list",   0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("List the current variables."), 0},
  /* TRANSLATORS: "set" is a keyword. It's a summary of "set" subcommand.  */
  {N_("set [NAME=VALUE ...]"), 0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("Set variables."), 0},
  /* TRANSLATORS: "unset" is a keyword. It's a summary of "unset" subcommand.  */
  {N_("unset [NAME ...]"),    0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("Delete variables."), 0},

  {0,         0, 0, OPTION_DOC, N_("Options:"), -1},
  {"verbose", 'v', 0, 0, N_("print verbose messages."), 0},

  { 0, 0, 0, 0, 0, 0 }
};

/* Print the version information.  */
static void
print_version (FILE *stream, struct argp_state *state)
{
  fprintf (stream, "%s (%s) %s\n", program_name, PACKAGE_NAME, PACKAGE_VERSION);
}
void (*argp_program_version_hook) (FILE *, struct argp_state *) = print_version;

/* Set the bug report address */
const char *argp_program_bug_address = "<"PACKAGE_BUGREPORT">";

static error_t argp_parser (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
      case 'v':
        verbosity++;
        break;

      case ARGP_KEY_NO_ARGS:
        fprintf (stderr, "%s",
		 _("You need to specify at least one command.\n"));
        argp_usage (state);
        break;

      default:
        return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

#pragma GCC diagnostic ignored "-Wformat-nonliteral"

static char *
help_filter (int key, const char *text, void *input __attribute__ ((unused)))
{
  switch (key)
    {
      case ARGP_KEY_HELP_POST_DOC:
        return xasprintf (text, DEFAULT_ENVBLK_PATH, DEFAULT_ENVBLK_PATH);

      default:
        return (char *) text;
    }
}

#pragma GCC diagnostic error "-Wformat-nonliteral"

struct argp argp = {
  options, argp_parser, N_("FILENAME COMMAND"),
  "\n"N_("\
Tool to edit environment block.")
"\v"N_("\
If FILENAME is `-', the default value %s is used.\n\n\
There is no `delete' command; if you want to delete the whole environment\n\
block, use `rm %s'."),
  NULL, help_filter, NULL
};

struct fs_envblk_spec {
  const char *fs_name;
  int (*fs_read) (void *, char **, size_t *);
  int (*fs_write) (void *, const grub_envblk_t);
  int (*fs_init) (grub_device_t, void **);
  void (*fs_fini) (void *);
};
typedef struct fs_envblk_spec fs_envblk_spec_t;

struct fs_envblk {
  struct fs_envblk_spec *spec;
  grub_fs_t fs;
  void *data;
};
typedef struct fs_envblk fs_envblk_t;

fs_envblk_t *fs_envblk = NULL;

#if defined(HAVE_LIBZFS) && defined(HAVE_LIBNVPAIR) && defined(HAVE_LIBZFS_BOOTENV)
static int
grub_zfs_init (grub_device_t dev, void **out)
{
  libzfs_handle_t *g_zfs = libzfs_init();
  int err;
  char *name;
  zpool_handle_t *zhp;
  nvlist_t *nvl;

  if (g_zfs == NULL)
    return -1;

  err = fs_envblk->fs->label(dev, &name);
  if (err != GRUB_ERR_NONE) {
    libzfs_fini(g_zfs);
    return -1;
  }
  zhp = zpool_open(g_zfs, name);
  if (zhp == NULL)
    {
      libzfs_fini(g_zfs);
      return -1;
    }
  /* If bootenv support isn't present on this system, don't try to use the envblk. */
  err = zpool_get_bootenv (zhp, &nvl);
  if (err == -1 && libzfs_errno(zpool_get_handle(zhp)) == EZFS_IOC_NOTSUPPORTED)
    {
      zpool_close(zhp);
      libzfs_fini(g_zfs);
      return -1;
    }
  nvlist_free(nvl);

  *out = zhp;
  return 0;
}

static void
grub_zfs_fini (void *arg)
{
  zpool_handle_t *zhp = arg;
  libzfs_handle_t *g_zfs = zpool_get_handle(zhp);
  zpool_close(zhp);
  libzfs_fini(g_zfs);
}

typedef enum vbe_vers {
        VB_RAW = 0,
        VB_NVLIST = 1
} vbe_vers_t;

/*
 * We need to convert ZFS's nvlist to a buffer we can manipulate in a
 * platform-agnostic fashion.
 */
static int
grub_zfs_get_bootenv (void *arg, char **buf, size_t *size)
{
  zpool_handle_t *zhp = arg;
  nvlist_t *nvl;
  int error = zpool_get_bootenv (zhp, &nvl);
  nvpair_t *nvp = NULL;
  size_t count;
  u_int64_t version;
  if (error != 0)
    {
      return error;
    }
  nvlist_size (nvl, size, NV_ENCODE_NATIVE);
  *size *= 2;
  if (*size < GRUB_ENVBLK_DEFAULT_SIZE)
    *size = GRUB_ENVBLK_DEFAULT_SIZE;
  *buf = malloc (*size);
  if (nvlist_lookup_uint64 (nvl, "version", &version) != 0)
    {
      free (*buf);
      nvlist_free (nvl);
      return -1;
    }
  if (version == VB_RAW)
    {
      char *raw;
      if (nvlist_lookup_string (nvl, "grub:envmap", &raw) != 0)
        {
          free (*buf);
          nvlist_free (nvl);
          return -1;
        }
      strncpy (*buf, raw, *size);
      *size = strlen (raw);
      nvlist_free (nvl);
      return 0;
    }
  grub_util_create_envblk_buffer (*buf, *size);
  count = (strchr (*buf, '\n') - *buf) + 1;
  /*
   * NVLIST format envblocks contain a series of STRING values. No other types
   * are allowed, except for the UINT64 version field. Each string value
   * represents one entry in the envblock.
   */
  while ((nvp = nvlist_next_nvpair (nvl, nvp)) != NULL)
    {
      char *name, *value;
      int rc;
      name = nvpair_name (nvp);
      if (nvpair_type (nvp) == DATA_TYPE_UINT64)
        {
          assert (strcmp(name, "version") == 0);
          continue;
        }
      assert (nvpair_type (nvp) == DATA_TYPE_STRING);
      nvpair_value_string (nvp, &value);
      rc = snprintf (*buf + count, *size - count, "%s=%s\n", name, value);
      if (rc < 0)
        grub_util_error (_("cannot write nvlist entry %s to buffer"), name);
      else if (rc < grub_strlen (name) + grub_strlen (value) + 2)
        grub_util_error (_("envblock contents too large for buffer"));
      count += rc;
      assert (count < *size);
    }
  if (count < GRUB_ENVBLK_DEFAULT_SIZE)
    {
      memset (*buf + count, '#', GRUB_ENVBLK_DEFAULT_SIZE - count - 1);
      count = GRUB_ENVBLK_DEFAULT_SIZE - 1;
      (*buf)[count] = '\0';
      count++;
    }
  nvlist_free (nvl);
  *size = strlen (*buf);
  return 0;
}

static int
nvlist_add_cb (const char *name, const char *value, void *hook_data)
{
  nvlist_t *nvl = hook_data;
  return nvlist_add_string (nvl, name, value);
}

static int
grub_zfs_set_bootenv (void *arg, const grub_envblk_t envblk)
{
  zpool_handle_t *zhp = arg;
  nvlist_t *nvl;
  int rc;
  if (nvlist_alloc (&nvl, NV_UNIQUE_NAME, 0) != 0)
      grub_util_error (_("cannot allocate nvlist"));
  if (nvlist_add_uint64(nvl, "version", VB_NVLIST) != 0)
      grub_util_error (_("cannot write to nvlist"));
  grub_envblk_iterate (envblk, nvl, nvlist_add_cb);
  rc = zpool_set_bootenv (zhp, nvl);
  nvlist_free (nvl);
  if (rc != 0)
    rc = libzfs_errno (zpool_get_handle (zhp));
  return rc;
}

#endif

fs_envblk_spec_t fs_envblk_table[] = {
#if defined(HAVE_LIBZFS) && defined(HAVE_LIBNVPAIR) && defined(HAVE_LIBZFS_BOOTENV)
  { "zfs", grub_zfs_get_bootenv, grub_zfs_set_bootenv, grub_zfs_init, grub_zfs_fini},
#endif
  { NULL, NULL, NULL, NULL, NULL }
};

static grub_envblk_t
open_envblk_file (const char *name)
{
  FILE *fp;
  char *buf;
  size_t size;
  grub_envblk_t envblk;

  fp = grub_util_fopen (name, "rb");
  if (! fp)
    {
      /* Create the file implicitly.  */
      grub_util_create_envblk_file (name);
      fp = grub_util_fopen (name, "rb");
      if (! fp)
	grub_util_error (_("cannot open `%s': %s"), name,
			 strerror (errno));
    }

  if (fseek (fp, 0, SEEK_END) < 0)
    grub_util_error (_("cannot seek `%s': %s"), name,
		     strerror (errno));

  size = (size_t) ftell (fp);

  if (fseek (fp, 0, SEEK_SET) < 0)
    grub_util_error (_("cannot seek `%s': %s"), name,
		     strerror (errno));

  buf = xmalloc (size);

  if (fread (buf, 1, size, fp) != size)
    grub_util_error (_("cannot read `%s': %s"), name,
		     strerror (errno));

  fclose (fp);

  envblk = grub_envblk_open (buf, size);
  if (! envblk)
    grub_util_error ("%s", _("invalid environment block"));

  return envblk;
}

static grub_envblk_t
open_envblk (const char *name)
{
  char *buf = NULL;
  size_t size;
  grub_envblk_t envblk;
  int rc;

  if (fs_envblk == NULL)
    return open_envblk_file(name);

  rc = fs_envblk->spec->fs_read(fs_envblk->data, &buf, &size);
  if (rc != 0)
    {
      grub_util_error (_("cannot read envblock: %s"), strerror (errno));
      return NULL;
    }
  envblk = grub_envblk_open (buf, size);
  if (! envblk)
    grub_util_error ("%s", _("invalid environment block"));
  return envblk;
}

static void
close_envblk (grub_envblk_t envblk)
{
  grub_envblk_close (envblk);
  if (fs_envblk != NULL)
    {
      fs_envblk->spec->fs_fini (fs_envblk->data);
      fs_envblk->data = NULL;
    }
}

static int
print_var (const char *varname, const char *value,
           void *hook_data __attribute__ ((unused)))
{
  printf ("%s=%s\n", varname, value);
  return 0;
}

static void
list_variables (const char *name)
{
  grub_envblk_t envblk;

  envblk = open_envblk (name);
  grub_envblk_iterate (envblk, NULL, print_var);
  close_envblk (envblk);
}

static void
write_envblk_file (const char *name, grub_envblk_t envblk)
{
  FILE *fp;

  fp = grub_util_fopen (name, "wb");
  if (! fp)
    grub_util_error (_("cannot open `%s': %s"), name,
		     strerror (errno));

  if (fwrite (grub_envblk_buffer (envblk), 1, grub_envblk_size (envblk), fp)
      != grub_envblk_size (envblk))
    grub_util_error (_("cannot write to `%s': %s"), name,
		     strerror (errno));

  grub_util_file_sync (fp);
  fclose (fp);
}

static void
write_envblk (const char *name, const grub_envblk_t envblk)
{
  int err = 0;
  if (fs_envblk == NULL)
    return write_envblk_file(name, envblk);

  if ((err = fs_envblk->spec->fs_write (fs_envblk->data, envblk)) != 0)
    grub_util_error (_("cannot write to envblock: %s"), strerror (err));
}

static void
create_envblk (const char *name)
{
  char *buf;
  grub_envblk_t envblk;

  if (fs_envblk == NULL)
    grub_util_create_envblk_file (name);
  buf = xmalloc (GRUB_ENVBLK_DEFAULT_SIZE);
  grub_util_create_envblk_buffer(buf, GRUB_ENVBLK_DEFAULT_SIZE);

  envblk = grub_envblk_open (buf, GRUB_ENVBLK_DEFAULT_SIZE);
  write_envblk (name, envblk);
  close_envblk (envblk);
}

static void
set_variables (const char *name, int argc, char *argv[])
{
  grub_envblk_t envblk;

  envblk = open_envblk (name);
  while (argc)
    {
      char *p;

      p = strchr (argv[0], '=');
      if (! p)
        grub_util_error (_("invalid parameter %s"), argv[0]);

      *(p++) = 0;

      if (! grub_envblk_set (envblk, argv[0], p))
        grub_util_error ("%s", _("environment block too small"));

      argc--;
      argv++;
    }

  write_envblk (name, envblk);
  close_envblk (envblk);
}

static void
unset_variables (const char *name, int argc, char *argv[])
{
  grub_envblk_t envblk;

  envblk = open_envblk (name);
  while (argc)
    {
      grub_envblk_delete (envblk, argv[0]);

      argc--;
      argv++;
    }

  write_envblk (name, envblk);
  close_envblk (envblk);
}

static int
probe_abstraction (grub_disk_t disk)
{
  if (disk->partition == NULL)
    grub_util_info ("no partition map found for %s", disk->name);

  if (disk->dev->id == GRUB_DISK_DEVICE_DISKFILTER_ID ||
      disk->dev->id == GRUB_DISK_DEVICE_CRYPTODISK_ID)
    return 1;
  return 0;
}

static fs_envblk_t *
probe_fs_envblk (fs_envblk_spec_t *spec)
{
  char **grub_devices;
  char **curdev, **curdrive;
  size_t ndev = 0;
  char **grub_drives;
  grub_device_t grub_dev = NULL;
  grub_fs_t grub_fs;
  int have_abstraction = 0, err;
  fs_envblk_spec_t *p;

  /* Initialize the emulated biosdisk driver.  */
  grub_util_biosdisk_init (DEFAULT_DEVICE_MAP);


  /* Initialize all modules. */
  grub_init_all ();
  grub_gcry_init_all ();

  grub_lvm_fini ();
  grub_mdraid09_fini ();
  grub_mdraid1x_fini ();
  grub_diskfilter_fini ();
  grub_diskfilter_init ();
  grub_mdraid09_init ();
  grub_mdraid1x_init ();
  grub_lvm_init ();

  grub_devices = grub_guess_root_devices (DEFAULT_DIRECTORY);

  if (!grub_devices || !grub_devices[0])
    grub_util_error (_("cannot find a device for %s (is /dev mounted?)"), DEFAULT_DIRECTORY);

  for (curdev = grub_devices; *curdev; curdev++)
    {
      grub_util_pull_device (*curdev);
      ndev++;
    }

  grub_drives = xmalloc (sizeof (grub_drives[0]) * (ndev + 1));

  for (curdev = grub_devices, curdrive = grub_drives; *curdev; curdev++,
       curdrive++)
    {
      *curdrive = grub_util_get_grub_dev (*curdev);
      if (! *curdrive)
	grub_util_error (_("cannot find a GRUB drive for %s.  Check your device.map"),
			 *curdev);
    }
  *curdrive = NULL;

  grub_dev = grub_device_open (grub_drives[0]);

  grub_fs = grub_fs_probe (grub_dev);

  if (grub_dev->disk)
    have_abstraction = probe_abstraction (grub_dev->disk);
  for (curdrive = grub_drives + 1; *curdrive; curdrive++)
    {
      grub_device_t dev = grub_device_open (*curdrive);
      if (!dev)
        continue;
      if (dev->disk)
        have_abstraction |= probe_abstraction (dev->disk);
      grub_device_close (dev);
    }

  for (p = spec; p->fs_name; p++)
    {
      if (strcmp (grub_fs->name, p->fs_name) == 0 && !have_abstraction)
        {
          grub_util_info ("Detected envblock support in %s, leveraging", grub_fs->name);
          fs_envblk = xmalloc (sizeof (fs_envblk_t));
          fs_envblk->spec = p;
          fs_envblk->fs = grub_fs;
          err = p->fs_init (grub_dev, &fs_envblk->data);

          if (err != 0)
            {
              grub_util_info ("Envblock init failed, continuing %d", err);
              free(fs_envblk);
              continue;
            }

          grub_device_close (grub_dev);
          free (grub_drives);
          grub_gcry_fini_all ();
          grub_fini_all ();
          grub_util_biosdisk_fini ();

          return fs_envblk;
        }
    }

  free (grub_drives);
  grub_gcry_fini_all ();
  grub_fini_all ();
  grub_util_biosdisk_fini ();

  grub_device_close (grub_dev);
  return NULL;
}

int
main (int argc, char *argv[])
{
  const char *filename;
  char *command;
  int curindex, arg_count;

  grub_util_host_init (&argc, &argv);

  /* Parse our arguments */
  if (argp_parse (&argp, argc, argv, 0, &curindex, 0) != 0)
    {
      fprintf (stderr, "%s", _("Error in parsing command line arguments\n"));
      exit(1);
    }

  arg_count = argc - curindex;

  if (arg_count == 1)
    {
      filename = DEFAULT_ENVBLK_PATH;
      command  = argv[curindex++];
    }
  else
    {
      filename = argv[curindex++];
      if (strcmp (filename, "-") == 0)
        filename = DEFAULT_ENVBLK_PATH;
      command  = argv[curindex++];
    }

  if (strcmp (filename, DEFAULT_ENVBLK_PATH) == 0)
    fs_envblk = probe_fs_envblk (fs_envblk_table);

  if (strcmp (command, "create") == 0)
    create_envblk (filename);
  else if (strcmp (command, "list") == 0)
    list_variables (filename);
  else if (strcmp (command, "set") == 0)
    set_variables (filename, argc - curindex, argv + curindex);
  else if (strcmp (command, "unset") == 0)
    unset_variables (filename, argc - curindex, argv + curindex);
  else
    {
      char *program = xstrdup(program_name);
      fprintf (stderr, _("Unknown command `%s'.\n"), command);
      argp_help (&argp, stderr, ARGP_HELP_STD_USAGE, program);
      free(program);
      exit(1);
    }

  return 0;
}
