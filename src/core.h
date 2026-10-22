/* core.h - the installer core, shared by its modules (core.c, sysfiles.c,
 * winplan.c, steps.c, files.c, setup.c, combined.c), dos.c and dossys.c
 * (DOS) or the Windows frontend with winsys.c
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#define MAXOPT   16
#define MAXLINE  600
#define MAXFIND  8
#define MAXDETECT 16

struct gdi_find {
	char name[16];
	char path[80];
	char file[13];   /* found on the installation disk: copied to %DIR% */
};
/* PatchVariant= of [Patch] (sysfiles.c) */
typedef struct {
	char group[24], label[24], path[80];
	unsigned char before[HEXPATCH_MAX_BYTES], after[HEXPATCH_MAX_BYTES];
	int length, expected, processed;
} PatchVariant;

struct gdi_detect {
	char name[16];
	char result;
};
extern char srcdir[80], dest[80], windir[80], boot[3];
extern char opt_on[MAXOPT];
extern const char *opt_label[MAXOPT];
extern char opt_value[MAXOPT][24];   /* %On%: value when option n is on */
extern int nopt;
extern int uninstall, keep_uninstaller, dosmode, bootset;
extern const char *option_override;     /* /O:0101 from the Win16 frontend */
extern struct gdi_find finds[MAXFIND];   /* [Find] */
extern int nfind;
extern struct gdi_detect detects[MAXDETECT];   /* [Detect]: external program returned zero */
extern int ndetect;
extern FILE *logf;
extern int update_mode;          /* an earlier installation is updated */
extern char old_version[24];
extern const char *gdi_self;           /* argv[0] */
extern int in_windows;                 /* the core runs in the Windows frontend */
extern char completion_file[80];       /* /W:path: Win16 completion status */
extern char registry_plan_file[80];    /* /R:path: expanded native plan */
extern int registry_family;            /* /F:31 or /F:9X */
extern char task_plan_file[80];        /* /T:path: WIN.INI and groups for the frontend */
extern int registry_deferred;          /* REGISTRY.PLN for the runner (GDIPxxxx.EXE) */
extern int completion_status;
extern int completion_registered;
extern jmp_buf extra_abort;            /* an extra installation was cancelled */
extern int in_batch;                   /* a package of [Installer] */
extern int in_extra;                   /* INSTALL.LOG: installed files */
extern char selected_language[9];

/* a text of the installer (lang.c) */
#define T gdi_text

extern char dest_top[80];
extern char *ed[MAXLINE];
extern int ned;

/* core.c */
int expand( char *out, const char *in, int max );
int named_result( const char *name, int n );
const char *cond( const char *v );
int condition_true( const char *condition );
int registry_preflight( char *message );
int registry_write_plan( char *message );
int contains( const char *s, const char *pat );
int mkdirs( const char *path );
int copy_file( const char *from, const char *to );
void fail( const char *text );
void abort_setup( void );
void write_completion( void );

/* sysfiles.c */
int ed_load( const char *path );
void ed_insert( int at, const char *s );
void backup_as( const char *path, char *saved );
int ed_save( const char *path );
void ed_free( void );
int ext_split( const char *v, char *m, char *t, char *n );
void old_entries_load( const char *ini );
int entry_new( const char *section, const char *key, const char *v );
const char *cond_new( const char *section, const char *key, const char *v );
int ini_find_key( const char *section, const char *key, int *end );
int system_set_split( const char *text, char *section, char *key, char *line );
int has_entry( const char *section, const char *key );
char *trim_field( char *text );
int patch_split( const char *value, char *path, char *old_hex, char *new_hex, int *expected );
int patch_variant_split( const char *value, PatchVariant *variant );
int hexpatch_needs_windows( void );
void patch_all( void );
void edit_file( const char *path, const char *section, int kind );

/* winplan.c */
int frontend( void );
int progman_item( const char *value, char *group, char *title, char *command, char *icon, int *index );
int has_progman( void );
void progman_all( void );
void progman_undo( void );
void task_write_plan( void );
void runner_path( char *p );
void deferred_registry( void );
void run_windows( void );
int uninstall_registry( void );

/* steps.c */
void find_windows( void );
void find_files( void );
void detect_all( void );
void find_dest( void );
void show_text( const char *title, const char *file, const char **buttons, int n, int must );
void license( void );
void welcome( void );
void read_options( void );
void ask_options( void );
void ask_dirs( void );
int confirm( void );
int find_used( const char *name );

/* files.c */
int record_read_key( const char *key, char *dir, char *bits, char *version );
void record_write( int add );
void find_update( void );
void copy_all( void );
int run_program( const char *cmdline );
void run_all( const char *key );
void edit_all( void );
void remove_all( void );

/* setup.c */
void ask_language( void );
void install_steps( void );
void use_ini( const char *dir );
void extras( void );
int core_run( void );

/* combined.c */
void batch_choices( void );
void run_installers( void );
void finish( void );

/* the platform: dossys.c (DOS) or winsys.c (the Windows frontend) */
int spawn_program( const char *cmdline );   /* exit status, -1: not run */
int windows_running( void );
void ask_reboot( const char *text );        /* does not return */
void dos_prepare( void );                   /* DOS mode: the hard disk */
