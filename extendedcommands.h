extern int signature_check_enabled;
extern int script_assert_enabled;

void write_recovery_version();

void toggle_signature_check();

void show_choose_zip_menu();

int
do_nandroid_backup(const char* backup_name);

int do_nandroid_restore();

void show_nandroid_restore_menu(const char* path);

void show_nandroid_advanced_restore_menu(const char* path);

void show_nandroid_menu();

int  show_partition_menu();

void show_choose_zip_menu();

int install_zip(const char* packagefilepath);

int __system(const char *command);

void show_advanced_menu();

int format_unknown_device(const char *device, const char* path, const char *fs_type);

void wipe_battery_stats();

void create_fstab();

int has_datadata();

void handle_failure(int ret);

void process_volumes();

int extendedcommand_file_exists();

void show_install_update_menu();

int confirm_selection(const char* title, const char* confirm);

void remove_extendedcommand();

int run_and_remove_extendedcommand();

int verify_root_and_recovery(int system_number);

int getDualsystemMode();

int select_system(const char* title);

int select_dualboot_backupmode(const char* title);

int select_dualboot_restoremode(const char* title, const char* file);

int setBootmode(char* bootmode);

int getBootmode(char* bootmode);

int isTrueDualbootEnabled();

int enableTrueDualboot(int enable);

#ifdef RECOVERY_EXTEND_NANDROID_MENU
void extend_nandroid_menu(char** items, int item_count, int max_items);
void handle_nandroid_menu(int item_count, int selected);
#endif

#define DUALBOOT_ITEM_RESTORE_SYSTEM0            0
#define DUALBOOT_ITEM_RESTORE_SYSTEM1            1
#define DUALBOOT_ITEM_RESTORE_BOTH               2
#define DUALBOOT_ITEM_RESTORE_ONE_TO_TWO         3
#define DUALBOOT_ITEM_RESTORE_TWO_TO_ONE         4
#define DUALBOOT_ITEM_RESTORE_BOTH_INTERCHANGED  5

#define DUALBOOT_FILE_TRUEDUALBOOT DUALBOOT_PATH_DATAROOT "/.truedualboot"
#define DUALBOOT_PATH_DATAROOT "/data_root"
#define DUALBOOT_PATH_USERDATA0 DUALBOOT_PATH_DATAROOT "/system0"
#define DUALBOOT_PATH_USERDATA1 DUALBOOT_PATH_DATAROOT "/system1"
