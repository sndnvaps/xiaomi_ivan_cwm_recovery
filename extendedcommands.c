#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/limits.h>
#include <dirent.h>
#include <sys/stat.h>

#include <signal.h>
#include <sys/wait.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "firmware.h"
#include "install.h"
#include "make_ext4fs.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "recovery_ui.h"

#include "extendedcommands.h"
#include "recovery_settings.h"
#include "nandroid.h"
#include "mounts.h"
#include "flashutils/flashutils.h"
#include "edify/expr.h"
#include <libgen.h>
#include "mtdutils/mtdutils.h"
#include "bmlutils/bmlutils.h"
#include "cutils/android_reboot.h"
#include "mmcutils/mmcutils.h"
#include "voldclient/voldclient.h"

#include "adb_install.h"

int signature_check_enabled = 1;
int script_assert_enabled = 1;
static const char *SDCARD_UPDATE_FILE = "/sdcard/update.zip";

int
get_filtered_menu_selection(char** headers, char** items, int menu_only, int initial_selection, int items_count) {
    int index;
    int offset = 0;
    int* translate_table = (int*)malloc(sizeof(int) * items_count);
    for (index = 0; index < items_count; index++) {
        if (items[index] == NULL)
            continue;
        char *item = items[index];
        items[index] = NULL;
        items[offset] = item;
        translate_table[offset] = index;
        offset++;
    }
    items[offset] = NULL;

    for (index = 0; index < offset; index++) {
        if(translate_table[index] == initial_selection)
            initial_selection = index;
    }

    int ret = get_menu_selection(headers, items, menu_only, initial_selection);
    if (ret < 0 || ret >= offset) {
        free(translate_table);
        return ret;
    }

    ret = translate_table[ret];
    free(translate_table);
    return ret;
}

void write_string_to_file(const char* filename, const char* string) {
    ensure_path_mounted(filename);
    char tmp[PATH_MAX];
    sprintf(tmp, "mkdir -p $(dirname %s)", filename);
    __system(tmp);
    FILE *file = fopen(filename, "w");
    if( file != NULL) {
        fprintf(file, "%s", string);
        fclose(file);
    }
}

void write_recovery_version() {
   char path[PATH_MAX];
    sprintf(path, "%s%s%s", get_primary_storage_path(), (is_data_media() ? "/0/" : "/"), RECOVERY_VERSION_FILE);
    write_string_to_file(path, EXPAND(RECOVERY_VERSION) "\n" EXPAND(TARGET_DEVICE));
    // force unmount /data for /data/media devices as we call this on recovery exit 
   ignore_data_media_workaround(1);
    ensure_path_unmounted(path);
    ignore_data_media_workaround(0);
}

static void write_last_install_path(const char* install_path) {
    char path[PATH_MAX];
    sprintf(path, "%s%s%s", get_primary_storage_path(), (is_data_media() ? "/0/" : "/"), RECOVERY_LAST_INSTALL_FILE);
    write_string_to_file(path, install_path);
}

const char* read_last_install_path() {
    static char path[PATH_MAX];
    sprintf(path, "%s%s%s", get_primary_storage_path(), (is_data_media() ? "/0/" : "/"), RECOVERY_LAST_INSTALL_FILE);

    ensure_path_mounted(path);
    FILE *f = fopen(path, "r");
    if (f != NULL) {
        fgets(path, PATH_MAX, f);
        fclose(f);

        return strdup(path);
    }
    return NULL;
}

void toggle_signature_check()
{
    signature_check_enabled = !signature_check_enabled;
    ui_print("签名验证: %s\n", signature_check_enabled ? "开启" : "关闭");
}

int install_zip(const char* packagefilepath)
{
    // updater_script will not be able to mount /data so do it now otherwise files will land on ramdisk
    if(is_dualsystem() && isTrueDualbootEnabled())
        ensure_path_mounted("/data");

    ui_print("\n-- 正在安装: %s\n", packagefilepath);
    if (device_flash_type() == MTD) {
        set_sdcard_update_bootloader_message();
    }
    int status = install_package(packagefilepath);
    ui_reset_progress();
    if (status != INSTALL_SUCCESS) {
        ui_set_background(BACKGROUND_ICON_ERROR);
        ui_print("安装出错.\n");
        return 1;
    }
#ifdef ENABLE_LOKI
    if (loki_support_enabled) {
        ui_print("Checking if loki-fying is needed\n");
        status = loki_check();
        if (status != INSTALL_SUCCESS) {
            ui_set_background(BACKGROUND_ICON_ERROR);
            return 1;
        }
    }
#endif

    ui_set_background(BACKGROUND_ICON_NONE);
    ui_print("\n安装完成.\n");
    return 0;
}

// top fixed menu items, those before extra storage volumes
#define FIXED_TOP_INSTALL_ZIP_MENUS 1
// bottom fixed menu items, those after extra storage volumes
#define FIXED_BOTTOM_INSTALL_ZIP_MENUS 3
#define FIXED_INSTALL_ZIP_MENUS (FIXED_TOP_INSTALL_ZIP_MENUS + FIXED_BOTTOM_INSTALL_ZIP_MENUS)


void show_install_update_menu()
{
    char buf[100];
    int i = 0, chosen_item = 0;
    static char* install_menu_items[MAX_NUM_MANAGED_VOLUMES + FIXED_INSTALL_ZIP_MENUS + 1];

    char* primary_path = get_primary_storage_path();
    char** extra_paths = get_extra_storage_paths();
    int num_extra_volumes = get_num_extra_volumes();

    memset(install_menu_items, 0, MAX_NUM_MANAGED_VOLUMES + FIXED_INSTALL_ZIP_MENUS + 1);

    static char* headers[] = {  "安装zip",
                                "",
                                NULL
    };

// FIXED_TOP_INSTALL_ZIP_MENUS
    sprintf(buf, "从%s选择zip", primary_path);
    install_menu_items[0] = strdup(buf);

    // extra storage volumes (vold managed)
    for (i = 0; i < num_extra_volumes; i++) {
        sprintf(buf, "从 %s选择zip", extra_paths[i]);
        install_menu_items[FIXED_TOP_INSTALL_ZIP_MENUS + i] = strdup(buf);
    }

    // FIXED_BOTTOM_INSTALL_ZIP_MENUS
    install_menu_items[FIXED_TOP_INSTALL_ZIP_MENUS + num_extra_volumes] = "从上一次的位置中选择zip";
    install_menu_items[FIXED_TOP_INSTALL_ZIP_MENUS + num_extra_volumes + 1] = "从sideload安装zip";
    install_menu_items[FIXED_TOP_INSTALL_ZIP_MENUS + num_extra_volumes + 2] = "签名验证选项";

    // extra NULL for GO_BACK
    install_menu_items[FIXED_TOP_INSTALL_ZIP_MENUS + num_extra_volumes + 3] = NULL;
    
    for (;;)
    {
        int chosen_item = get_menu_selection(headers, install_menu_items, 0, 0);
        if (chosen_item == 0) {
            show_choose_zip_menu(primary_path);
        } else if (chosen_item >= FIXED_TOP_INSTALL_ZIP_MENUS && chosen_item < FIXED_TOP_INSTALL_ZIP_MENUS + num_extra_volumes) {
            show_choose_zip_menu(extra_paths[chosen_item - FIXED_TOP_INSTALL_ZIP_MENUS]);
        } else if (chosen_item == FIXED_TOP_INSTALL_ZIP_MENUS + num_extra_volumes) {
            const char *last_path_used = read_last_install_path();
            if (last_path_used == NULL)
                show_choose_zip_menu(primary_path);
            else
	    {
                show_choose_zip_menu(last_path_used);
	    }
        } else if (chosen_item == FIXED_TOP_INSTALL_ZIP_MENUS + num_extra_volumes + 1) {
		if (is_dualsystem()) {
			int system = select_system("选择系统安装:");
			if (system >= 0) {
				if (set_active_system(system) != 0)
					ui_print("设置系统失败，请重启!\n");
				else apply_from_adb();
			}
		}   else {
		       	apply_from_adb();
		}

        } else if (chosen_item == FIXED_TOP_INSTALL_ZIP_MENUS + num_extra_volumes + 2) {
            toggle_signature_check();
	} else {
		// GO_BACK or REFRESH (chosen_item < 0)
            goto out;
		
	}
    }
out:
    // free all the dynamic items
    free(install_menu_items[0]);
    if (extra_paths != NULL) {
        for (i = 0; i < num_extra_volumes; i++)
            free(install_menu_items[FIXED_TOP_INSTALL_ZIP_MENUS + i]);
    }
    return chosen_item;
            
           
}

void free_string_array(char** array)
{
    if (array == NULL)
        return;
    char* cursor = array[0];
    int i = 0;
    while (cursor != NULL)
    {
        free(cursor);
        cursor = array[++i];
    }
    free(array);
}

char** gather_files(const char* directory, const char* fileExtensionOrDirectory, int* numFiles)
{
    char path[PATH_MAX] = "";
    DIR *dir;
    struct dirent *de;
    int total = 0;
    int i;
    char** files = NULL;
    int pass;
    *numFiles = 0;
    int dirLen = strlen(directory);

    dir = opendir(directory);
    if (dir == NULL) {
        ui_print("不能打开文件夹.\n");
        return NULL;
    }

   unsigned int extension_length = 0;
    if (fileExtensionOrDirectory != NULL)
        extension_length = strlen(fileExtensionOrDirectory);

    int isCounting = 1;
    i = 0;
    for (pass = 0; pass < 2; pass++) {
        while ((de=readdir(dir)) != NULL) {
            // skip hidden files
            if (de->d_name[0] == '.')
                continue;

            // NULL means that we are gathering directories, so skip this
            if (fileExtensionOrDirectory != NULL)
            {
                // make sure that we can have the desired extension (prevent seg fault)
                if (strlen(de->d_name) < extension_length)
                    continue;
                // compare the extension
                if (strcmp(de->d_name + strlen(de->d_name) - extension_length, fileExtensionOrDirectory) != 0)
                    continue;
            }
            else
            {
                struct stat info;
                char fullFileName[PATH_MAX];
                strcpy(fullFileName, directory);
                strcat(fullFileName, de->d_name);
                lstat(fullFileName, &info);
                // make sure it is a directory
                if (!(S_ISDIR(info.st_mode)))
                    continue;
            }

            if (pass == 0)
            {
                total++;
                continue;
            }

            files[i] = (char*) malloc(dirLen + strlen(de->d_name) + 2);
            strcpy(files[i], directory);
            strcat(files[i], de->d_name);
            if (fileExtensionOrDirectory == NULL)
                strcat(files[i], "/");
            i++;
        }
        if (pass == 1)
            break;
        if (total == 0)
            break;
        rewinddir(dir);
        *numFiles = total;
        files = (char**) malloc((total+1)*sizeof(char*));
        files[total]=NULL;
    }

    if(closedir(dir) < 0) {
        LOGE("Failed to close directory.\n");
    }

    if (total==0) {
        return NULL;
    }

    // sort the result
    if (files != NULL) {
        for (i = 0; i < total; i++) {
            int curMax = -1;
            int j;
            for (j = 0; j < total - i; j++) {
                if (curMax == -1 || strcmp(files[curMax], files[j]) < 0)
                    curMax = j;
            }
            char* temp = files[curMax];
            files[curMax] = files[total - i - 1];
            files[total - i - 1] = temp;
        }
    }

    return files;
}

// pass in NULL for fileExtensionOrDirectory and you will get a directory chooser
char* choose_file_menu(const char* basedir, const char* fileExtensionOrDirectory, const char* headers[])
{
    char path[PATH_MAX] = "";
    DIR *dir;
    struct dirent *de;
    int numFiles = 0;
    int numDirs = 0;
    int i;
    char* return_value = NULL;
    char directory[PATH_MAX];
    int dir_len = strlen(basedir);

    strcpy(directory, basedir);

    // Append a trailing slash if necessary
    if (directory[dir_len - 1] != '/') {
        strcat(directory, "/");
        dir_len++;
    }

    i = 0;
    while (headers[i]) {
        i++;
    }
    const char** fixed_headers = (const char*)malloc((i + 3) * sizeof(char*));
    i = 0;
    while (headers[i]) {
        fixed_headers[i] = headers[i];
        i++;
    }
    fixed_headers[i] = directory;
    fixed_headers[i + 1] = "";
    fixed_headers[i + 2 ] = NULL;

    char** files = gather_files(directory, fileExtensionOrDirectory, &numFiles);
    char** dirs = NULL;
    if (fileExtensionOrDirectory != NULL)
        dirs = gather_files(directory, NULL, &numDirs);
    int total = numDirs + numFiles;
    if (total == 0)
    {
        ui_print("无文件.\n");
    }
    else
    {
        char** list = (char**) malloc((total + 1) * sizeof(char*));
        list[total] = NULL;


        for (i = 0 ; i < numDirs; i++)
        {
            list[i] = strdup(dirs[i] + dir_len);
        }

        for (i = 0 ; i < numFiles; i++)
        {
            list[numDirs + i] = strdup(files[i] + dir_len);
        }

        for (;;)
        {
            int chosen_item = get_menu_selection(fixed_headers, list, 0, 0);
            if (chosen_item == GO_BACK)
                break;
            static char ret[PATH_MAX];
            if (chosen_item < numDirs)
            {
                char* subret = choose_file_menu(dirs[chosen_item], fileExtensionOrDirectory, headers);
                if (subret != NULL)
                {
                    strcpy(ret, subret);
                    return_value = ret;
                    break;
                }
                continue;
            }
            strcpy(ret, files[chosen_item - numDirs]);
            return_value = ret;
            break;
        }
        free_string_array(list);
    }

    free_string_array(files);
    free_string_array(dirs);
    free(fixed_headers);
    return return_value;
}

void show_choose_zip_menu(const char *mount_point)
{
    if (ensure_path_mounted(mount_point) != 0) {
        LOGE ("Can't mount %s\n", mount_point);
        return;
    }

    static char* headers[] = {  "选择安装zip",
                                "",
                                NULL
    };

    char* file = choose_file_menu(mount_point, ".zip", headers);
    if (file == NULL)
        return;
    static char* confirm_install  = "确认安装?";
    static char confirm[PATH_MAX];
    
    if(is_dualsystem()) {
        int system = select_system("选择系统安装:");
        if (system>=0) {
            if(set_active_system(system)!=0) {
                ui_print("设置系统失败，请重启!\n");
            }
            else {
                char confirm[PATH_MAX];
                sprintf(confirm, "是的 - 安装 %s 到 系统%d", basename(file), system);
                if (confirm_selection("确认安装?", confirm))
                    install_zip(file);
		    write_last_install_path(dirname(file));
            }
        }
    }
    else {
        sprintf(confirm, "是的 - 安装 %s", basename(file));
        if (confirm_selection(confirm_install, confirm))
            install_zip(file);
	    write_last_install_path(dirname(file));
    }
}

void show_nandroid_restore_menu(const char* path)
{
    if (ensure_path_mounted(path) != 0) {
        LOGE("Can't mount %s\n", path);
        return;
    }

    static char* headers[] = {  "选择镜像恢复",
                                "",
                                NULL
    };

    char tmp[PATH_MAX];
    sprintf(tmp, "%s/clockworkmod/backup/", path);
    char* file = choose_file_menu(tmp, NULL, headers);
    if (file == NULL)
        return;
    if(is_dualsystem()) {
        int system = select_dualboot_restoremode("选择恢复模式:", file);
        int result;
        switch(system) {
            case DUALBOOT_ITEM_RESTORE_SYSTEM0:
                result = set_active_system(DUALBOOT_ITEM_SYSTEM0);
                if (result==0 && confirm_selection("确认恢复?", "是的 - 恢复"))
                    nandroid_twrpTar_restore(file, 1, 1, 1, 1, 1, 0, 0, 0, 0);
                break;
            case DUALBOOT_ITEM_RESTORE_SYSTEM1:
                result = set_active_system(DUALBOOT_ITEM_SYSTEM1);
                if (result==0 && confirm_selection("确认恢复?", "是的 - 恢复"))
                    nandroid_twrpTar_restore(file, 0, 0, 0, 1, 1, 0, 1, 1, 1);
                break;
            case DUALBOOT_ITEM_RESTORE_BOTH:
                result = set_active_system(DUALBOOT_ITEM_BOTH);
                if (result==0 && confirm_selection("确认恢复?", "是的 - 恢复"))
                    nandroid_restore(file, 1, 1, 1, 1, 1, 0, 1, 1, 1);
                break;
            case DUALBOOT_ITEM_RESTORE_ONE_TO_TWO:
                result = set_active_system(DUALBOOT_ITEM_SYSTEM1);
                if (result==0 && confirm_selection("确认恢复?", "是的 - 恢复"))
                    nandroid_twrpTar_restore(file, 1, 1, 1, 1, 1, 0, 0, 0, 0);
                break;
            case DUALBOOT_ITEM_RESTORE_TWO_TO_ONE:
                result = set_active_system(DUALBOOT_ITEM_SYSTEM0);
                if (result==0 && confirm_selection("确认恢复?", "是的 - 恢复"))
                    nandroid_twrpTar_restore(file, 0, 0, 0, 1, 1, 0, 1, 1, 1);
                break;
            case DUALBOOT_ITEM_RESTORE_BOTH_INTERCHANGED:
                result = set_active_system(DUALBOOT_ITEM_INTERCHANGED);
                if (result==0 && confirm_selection("确认恢复?", "是的 - 恢复"))
                    nandroid_twrpTar_restore(file, 1, 1, 1, 1, 1, 0, 1, 1, 1);
                break;

            default:
                return;
        }
        if(result!=0)
            ui_print("设置系统失败，请重启!\n");
        return;
    }
    if (confirm_selection("确认恢复?", "是的 - 恢复"))
        nandroid_restore(file, 1, 1, 1, 1, 1, 0, 0, 0, 0);
}

void show_nandroid_delete_menu(const char* path, int twrpTar)
{
    if (ensure_path_mounted(path) != 0) {
        LOGE("Can't mount %s\n", path);
        return;
    }

    static char* headers[] = {  "选择删除镜像",
                                "",
                                NULL
    };

    char tmp[PATH_MAX];
    if (twrpTar == 0) {
            snprintf(tmp, PATH_MAX, "%s/clockworkmod/backup/", path);
    } else if (twrpTar == 1 ) {
	    snprintf(tmp, PATH_MAX, "%s/clockworkmod/twrpTar/backup", path);
    }

	
    char* file = choose_file_menu(tmp, NULL, headers);
    if (file == NULL)
        return;

    if (confirm_selection("确认删除?", "是的 - 删除")) {
        // nandroid_restore(file, 1, 1, 1, 1, 1, 0, 0, 0);
        sprintf(tmp, "rm -rf %s", file);
        __system(tmp);
    }
}

int control_usb_storage(bool on) {
    int i = 0;
    int num = 0;

    for (i = 0; i < get_num_volumes(); i++) {
        Volume *v = get_device_volumes() + i;
        if (fs_mgr_is_voldmanaged(v) && vold_is_volume_available(v->mount_point)) {
            if (on) {
                vold_share_volume(v->mount_point);
            } else {
                vold_unshare_volume(v->mount_point, 1);
            }
            property_set("sys.storage.ums_enabled", on ? "1" : "0");
            num++;
        }
    }
    return num;
}

void show_mount_usb_storage_menu()
{
	// Enable USB storage using vold
    if (!control_usb_storage(true))
        return;


    static char* headers[] = {  "USB大容量存储",
                                "离开本菜单将",
                                "断开SD卡连接.",
                                "",
                                NULL
    };

    static char* list[] = { "卸载", NULL };

    for (;;)
    {
        int chosen_item = get_menu_selection(headers, list, 0, 0);
        if (chosen_item == GO_BACK || chosen_item == 0)
            break;
    }

    // Disable USB storage
    control_usb_storage(false);
}

int confirm_selection(const char* title, const char* confirm)
{
    struct stat info;
    int ret = 0;
    char path[PATH_MAX];
    sprintf(path, "%s%s%s", get_primary_storage_path(), (is_data_media() ? "/0/" : "/"), RECOVERY_NO_CONFIRM_FILE);
    ensure_path_mounted(path);
    if (0 == stat(path, &info))
        return 1;

 
    int many_confirm;
    char* confirm_str = strdup(confirm);
    const char* confirm_headers[] = { title, "  这将不能...", "", NULL };

    sprintf(path, "%s%s%s", get_primary_storage_path(), (is_data_media() ? "/0/" : "/"), RECOVERY_MANY_CONFIRM_FILE);
    ensure_path_mounted(path);
    many_confirm = 0 == stat(path, &info);

if (many_confirm) {
        char* items[] = { "No",
                          "No",
                          "No",
                          "No",
                          "No",
                          "No",
                          "No",
                          confirm_str, // Yes, [7]
                          "No",
                          "No",
                          "No",
                          NULL };
        int chosen_item = get_menu_selection(confirm_headers, items, 0, 0);
        ret = (chosen_item == 7);
    } else {
        char* items[] = { "No",
                          confirm_str, // Yes, [1]
                          NULL };
        int chosen_item = get_menu_selection(confirm_headers, items, 0, 0);
        ret = (chosen_item == 1);
    }
    free(confirm_str);
    return ret;


    }

#define MKE2FS_BIN      "/sbin/mke2fs"
#define TUNE2FS_BIN     "/sbin/tune2fs"
#define E2FSCK_BIN      "/sbin/e2fsck"

extern struct selabel_handle *sehandle;
int format_device(const char *device, const char *path, const char *fs_type) {
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        // silent failure for sd-ext
        if (strcmp(path, "/sd-ext") == 0)
            return -1;
        LOGE("unknown volume \"%s\"\n", path);
        return -1;
    }
    if (is_data_media_volume_path(path)) {
        return format_unknown_device(NULL, path, NULL);
    }
    if (strstr(path, "/data") == path && is_data_media()) {
        return format_unknown_device(NULL, path, NULL);
    }
    if (strstr(path, "/data") == path && is_dualsystem() && isTrueDualbootEnabled()) {
        return format_unknown_device(NULL, path, NULL);
    }
    if (strcmp(fs_type, "ramdisk") == 0) {
        // you can't format the ramdisk.
        LOGE("can't format_volume \"%s\"", path);
        return -1;
    }

    if (strcmp(fs_type, "rfs") == 0) {
        if (ensure_path_unmounted(path) != 0) {
            LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
            return -1;
        }
        if (0 != format_rfs_device(device, path)) {
            LOGE("format_volume: format_rfs_device failed on %s\n", device);
            return -1;
        }
        return 0;
    }
 
    if (strcmp(v->mount_point, path) != 0) {
        return format_unknown_device(v->blk_device, path, NULL);
    }

    if (ensure_path_unmounted(path) != 0) {
        LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
        return -1;
    }

    if (strcmp(fs_type, "yaffs2") == 0 || strcmp(fs_type, "mtd") == 0) {
        mtd_scan_partitions();
        const MtdPartition* partition = mtd_find_partition_by_name(device);
        if (partition == NULL) {
            LOGE("format_volume: no MTD partition \"%s\"\n", device);
            return -1;
        }

        MtdWriteContext *write = mtd_write_partition(partition);
        if (write == NULL) {
            LOGW("format_volume: can't open MTD \"%s\"\n", device);
            return -1;
        } else if (mtd_erase_blocks(write, -1) == (off_t) -1) {
            LOGW("format_volume: can't erase MTD \"%s\"\n", device);
            mtd_write_close(write);
            return -1;
        } else if (mtd_write_close(write)) {
            LOGW("format_volume: can't close MTD \"%s\"\n",device);
            return -1;
        }
        return 0;
    }

    if (strcmp(fs_type, "ext4") == 0) {
        int length = 0;
        if (strcmp(v->fs_type, "ext4") == 0) {
            // Our desired filesystem matches the one in fstab, respect v->length
            length = v->length;
        }
        reset_ext4fs_info();
        int result = make_ext4fs(device, length, v->mount_point, sehandle);
        if (result != 0) {
            LOGE("format_volume: make_ext4fs failed on %s\n", device);
            return -1;
        }
        return 0;
    }
#ifdef USE_F2FS
    if (strcmp(fs_type, "f2fs") == 0) {
        int result = make_f2fs_main(device, v->mount_point);
        if (result != 0) {
            LOGE("format_volume: mkfs.f2f2 failed on %s\n", device);
            return -1;
        }
        return 0;
    }
#endif
    return format_unknown_device(device, path, fs_type);
}

int format_unknown_device(const char *device, const char* path, const char *fs_type)
{
    LOGI("Formatting unknown device.\n");

    if (fs_type != NULL && get_flash_type(fs_type) != UNSUPPORTED)
        return erase_raw_partition(fs_type, device);

    // if this is SDEXT:, don't worry about it if it does not exist.
    if (0 == strcmp(path, "/sd-ext"))
    {
        struct stat st;
        Volume *vol = volume_for_path("/sd-ext");
        if (vol == NULL || 0 != stat(vol->blk_device, &st))
        {
            ui_print("没有app2sd分区，跳过格式化 /sd-ext.\n");
            return 0;
        }
    }

    if (NULL != fs_type) {
        if (strcmp("ext3", fs_type) == 0) {
            LOGI("Formatting ext3 device.\n");
            if (0 != ensure_path_unmounted(path)) {
                LOGE("Error while unmounting %s.\n", path);
                return -12;
            }
            return format_ext3_device(device);
        }

        if (strcmp("ext2", fs_type) == 0) {
            LOGI("Formatting ext2 device.\n");
            if (0 != ensure_path_unmounted(path)) {
                LOGE("Error while unmounting %s.\n", path);
                return -12;
            }
            return format_ext2_device(device);
        }
    }

    if (0 != ensure_path_mounted(path))
    {
        ui_print("挂载错误 %s!\n", path);
        ui_print("跳过格式化...\n");
        return 0;
    }

    char tmp[PATH_MAX];
    if (strcmp(path, "/data") == 0) {
        sprintf(tmp, "cd /data ; for f in $(ls -a | grep -v ^media$); do rm -rf $f; done");
        __system(tmp);
        // if the /data/media sdcard has already been migrated for android 4.2,
        // prevent the migration from happening again by writing the .layout_version
        struct stat st;
        if (0 == lstat("/data/media/0", &st)) {
            char* layout_version = "2";
            FILE* f = fopen("/data/.layout_version", "wb");
            if (NULL != f) {
                fwrite(layout_version, 1, 2, f);
                fclose(f);
            }
            else {
                LOGI("error opening /data/.layout_version for write.\n");
            }
        }
        else {
            LOGI("/data/media/0 not found. migration may occur.\n");
        }
    }
    else {
        sprintf(tmp, "rm -rf %s/*", path);
        __system(tmp);
        sprintf(tmp, "rm -rf %s/.*", path);
        __system(tmp);
    }

    ensure_path_unmounted(path);
    return 0;
}

//#define MOUNTABLE_COUNT 5
//#define DEVICE_COUNT 4
//#define MMC_COUNT 2

typedef struct {
    char mount[255];
    char unmount[255];
    char path[PATH_MAX];
} MountMenuEntry;

typedef struct {
    char txt[255];
    char path[PATH_MAX];
    char type[255];
} FormatMenuEntry;

typedef struct {
    char *name;
    int can_mount;
    int can_format;
} MFMatrix;

MFMatrix get_mnt_fmt_capabilities(char *fs_type, char *mount_point) {
    MFMatrix mfm = { mount_point, 1, 1 };

    const int NUM_FS_TYPES = 5;
    MFMatrix *fs_matrix = malloc(NUM_FS_TYPES * sizeof(MFMatrix));
    // Defined capabilities:   fs_type     mnt fmt
    fs_matrix[0] = (MFMatrix){ "bml",       0,  1 };
    fs_matrix[1] = (MFMatrix){ "datamedia", 0,  1 };
    fs_matrix[2] = (MFMatrix){ "emmc",      0,  1 };
    fs_matrix[3] = (MFMatrix){ "mtd",       0,  0 };
    fs_matrix[4] = (MFMatrix){ "ramdisk",   0,  0 };

    const int NUM_MNT_PNTS = 14;
    MFMatrix *mp_matrix = malloc(NUM_MNT_PNTS * sizeof(MFMatrix));
    // Defined capabilities:   mount_point   mnt fmt
    mp_matrix[0] = (MFMatrix){ "/misc",       0,  0 };
    mp_matrix[1] = (MFMatrix){ "/radio",      0,  0 };
    mp_matrix[2] = (MFMatrix){ "/bootloader", 0,  0 };
    mp_matrix[3] = (MFMatrix){ "/recovery",   0,  0 };
    mp_matrix[4] = (MFMatrix){ "/efs",        0,  0 };
    mp_matrix[5] = (MFMatrix){ "/wimax",      0,  0 };
    //for xiaomi device 
    mp_matrix[6] = (MFMatrix){ "/sbl1",       0,  0 };
    mp_matrix[7] = (MFMatrix){ "/sbl2",       0,  0 };
    mp_matrix[8] = (MFMatrix){ "/sbl3",       0,  0 };
    mp_matrix[9] = (MFMatrix){ "/aboot",      0,  0 };
    mp_matrix[10] = (MFMatrix){ "/rpm",       0,  0 };
    mp_matrix[11] = (MFMatrix){ "/tz",        0,  0 };
    mp_matrix[12] = (MFMatrix){ "/firmware",  0,  0 };
    mp_matrix[13] = (MFMatrix){ "/persist",   0,  0 };
    //end for xiaomi device 



    int i;
    for (i = 0; i < NUM_FS_TYPES; i++) {
        if (strcmp(fs_type, fs_matrix[i].name) == 0) {
            mfm.can_mount = fs_matrix[i].can_mount;
            mfm.can_format = fs_matrix[i].can_format;
        }
    }
    for (i = 0; i < NUM_MNT_PNTS; i++) {
        if (strcmp(mount_point, mp_matrix[i].name) == 0) {
            mfm.can_mount = mp_matrix[i].can_mount;
            mfm.can_format = mp_matrix[i].can_format;
        }
    }

    free(fs_matrix);
    free(mp_matrix);

    // User-defined capabilities
    char *custom_mp;
    char custom_forbidden_mount[PROPERTY_VALUE_MAX];
    char custom_forbidden_format[PROPERTY_VALUE_MAX];
    property_get("ro.cwm.forbid_mount", custom_forbidden_mount, "");
    property_get("ro.cwm.forbid_format", custom_forbidden_format, "");

    custom_mp = strtok(custom_forbidden_mount, ",");
    while (custom_mp != NULL) {
        if (strcmp(mount_point, custom_mp) == 0) {
            mfm.can_mount = 0;
        }
        custom_mp = strtok(NULL, ",");
    }

    custom_mp = strtok(custom_forbidden_format, ",");
    while (custom_mp != NULL) {
        if (strcmp(mount_point, custom_mp) == 0) {
            mfm.can_format = 0;
        }
        custom_mp = strtok(NULL, ",");
    }

    return mfm;
}
void format_sdcard(const char* volume) {
    if (is_data_media_volume_path(volume))
        return;

    Volume *v = volume_for_path(volume);
    if (v == NULL || strcmp(v->fs_type, "auto") != 0)
        return;
    if (!fs_mgr_is_voldmanaged(v) && !can_partition(volume))
        return;

    const char* headers[] = { "Format device:", volume, "", NULL };

    static char* list[] = { "default",
                            "vfat",
                            "exfat",
                            "ntfs",
                            "ext4",
                            "ext3",
                            "ext2",
                            NULL };

    int ret = -1;
    char cmd[PATH_MAX];
    int chosen_item = get_menu_selection(headers, list, 0, 0);
    if (chosen_item < 0) // REFRESH or GO_BACK
        return;
    if (!confirm_selection("Confirm formatting?", "Yes - Format device"))
        return;

    if (ensure_path_unmounted(v->mount_point) != 0)
        return;

    switch (chosen_item) {
        case 0:
            ret = format_volume(v->mount_point);
            break;
        case 1:
        case 2:
        case 3:
        case 4: {
            if (fs_mgr_is_voldmanaged(v)) {
                ret = vold_custom_format_volume(v->mount_point, list[chosen_item], 1) == CommandOkay ? 0 : -1;
            } else if (strcmp(list[chosen_item], "vfat") == 0) {
                sprintf(cmd, "/sbin/newfs_msdos -F 32 -O android -c 8 %s", v->blk_device);
                ret = __system(cmd);
            } else if (strcmp(list[chosen_item], "exfat") == 0) {
                sprintf(cmd, "/sbin/mkfs.exfat %s", v->blk_device);
                ret = __system(cmd);
            } else if (strcmp(list[chosen_item], "ntfs") == 0) {
                sprintf(cmd, "/sbin/mkntfs -f %s", v->blk_device);
                ret = __system(cmd);
            } else if (strcmp(list[chosen_item], "ext4") == 0) {
                ret = make_ext4fs(v->blk_device, v->length, volume, sehandle);
            }
            break;
        }
        case 5:
        case 6: {
            // workaround for new vold managed volumes that cannot be recognized by prebuilt ext2/ext3 bins
            const char *device = v->blk_device2;
            if (device == NULL)
                device = v->blk_device;
            ret = format_unknown_device(device, v->mount_point, list[chosen_item]);
            break;
        }
    }

    if (ret)
        ui_print("不能格式化 %s (%s)\n", volume, list[chosen_item]);
    else
        ui_print("格式化 %s 分区(%s) 完成\n", volume, list[chosen_item]);
}


int  show_partition_menu()
{
    static char* headers[] = {  "挂载/存储",
                                "",
                                NULL
    };

    static char* confirm_format = "是否要格式化?";
    static char* confirm = "是的 - 格式化";
    char confirm_string[255];



    static MountMenuEntry* mount_menu = NULL;
    static FormatMenuEntry* format_menu = NULL;
    static char *list[256];



    int i, mountable_volumes, formatable_volumes;
    int num_volumes;
    int chosen_item = 0;


    num_volumes = get_num_volumes();



    if(!num_volumes)
        return 0;

    mountable_volumes = 0;
    formatable_volumes = 0;

    mount_menu = malloc(num_volumes * sizeof(MountMenuEntry));
    format_menu = malloc(num_volumes * sizeof(FormatMenuEntry));

for (i = 0; i < num_volumes; i++) {
        Volume* v = get_device_volumes() + i;

        if (fs_mgr_is_voldmanaged(v) && !vold_is_volume_available(v->mount_point)) {
            continue;
        }

        MFMatrix mfm = get_mnt_fmt_capabilities(v->fs_type, v->mount_point);

        if (mfm.can_mount) {
            sprintf(mount_menu[mountable_volumes].mount, "挂载 %s", v->mount_point);
            sprintf(mount_menu[mountable_volumes].unmount, "卸载 %s", v->mount_point);
            sprintf(mount_menu[mountable_volumes].path, "%s", v->mount_point);
            ++mountable_volumes;
        }
        if (mfm.can_format) {
            sprintf(format_menu[formatable_volumes].txt, "格式化 %s", v->mount_point);
            sprintf(format_menu[formatable_volumes].path, "%s", v->mount_point);
            sprintf(format_menu[formatable_volumes].type, "%s", v->fs_type);
            ++formatable_volumes;
        }
    }


 for (;;) {
        for (i = 0; i < mountable_volumes; i++) {
            MountMenuEntry* e = &mount_menu[i];
            if (is_path_mounted(e->path))
                list[i] = e->unmount;
            else
                list[i] = e->mount;
        }

        for (i = 0; i < formatable_volumes; i++) {
            FormatMenuEntry* e = &format_menu[i];
            list[mountable_volumes + i] = e->txt;
        }

        if (!is_data_media()) {
            list[mountable_volumes + formatable_volumes] = "开启大容量模式";
            list[mountable_volumes + formatable_volumes + 1] = '\0';
        } else {
            list[mountable_volumes + formatable_volumes] = "格式化 /data 和 /data/media (/sdcard)";
            list[mountable_volumes + formatable_volumes + 1] = "开启大容量模式";
            list[mountable_volumes + formatable_volumes + 2] = '\0';
        }

        chosen_item = get_menu_selection(headers, list, 0, 0);
        if (chosen_item == GO_BACK || chosen_item == REFRESH)
            break;
        if (chosen_item == (mountable_volumes + formatable_volumes)) {
            if (!is_data_media()) {
                show_mount_usb_storage_menu();
            } else {
                if (!confirm_selection("格式化 /data 和 /data/media (/sdcard)", confirm))
                    continue;
                ignore_data_media_workaround(1);
                ui_print("格式化 /data...\n");
                if (0 != format_volume("/data"))
                    ui_print("格式化 /data 失败!\n");
                else
                    ui_print("格式化完成.\n");
                ignore_data_media_workaround(0);
            }
        } else if (is_data_media() && chosen_item == (mountable_volumes + formatable_volumes + 1)) {
            show_mount_usb_storage_menu();
        } else if (chosen_item < mountable_volumes) {
            MountMenuEntry* e = &mount_menu[chosen_item];

            if (is_path_mounted(e->path)) {
                ignore_data_media_workaround(1);
                if (0 != ensure_path_unmounted(e->path))
                    ui_print("卸载 %s 失败!\n", e->path);
                ignore_data_media_workaround(0);
            } else {
                if (0 != ensure_path_mounted(e->path))
                    ui_print("挂载 %s 失败!\n", e->path);
            }
        } else if (chosen_item < (mountable_volumes + formatable_volumes)) {
            chosen_item = chosen_item - mountable_volumes;
            FormatMenuEntry* e = &format_menu[chosen_item];

            sprintf(confirm_string, "%s - %s", e->path, confirm_format);

            // support user choice fstype when formatting external storage
            // ensure fstype==auto because most devices with internal vfat storage cannot be formatted to other types
            if (strcmp(e->type, "auto") == 0) {
                format_sdcard(e->path);
                continue;
            }

            if (!confirm_selection(confirm_string, confirm))
                continue;
            ui_print("正在格式化 %s...\n", e->path);
            if (0 != format_volume(e->path))
                ui_print("格式化 %s 错误!\n", e->path);
            else
                ui_print("格式化完成.\n");
        }
    }

    free(mount_menu);
    free(format_menu);
    return chosen_item;   
}

void show_nandroid_advanced_restore_menu(const char* path)
{
    if (ensure_path_mounted(path) != 0) {
        LOGE ("Can't mount sdcard\n");
        return;
    }

    static char* advancedheaders[] = {  "选择恢复镜像",
                                "",
                                "先选择恢复镜像",
                                "下个菜单将",
                                "提供更多选项.",
                                "",
                                NULL
    };

    char tmp[PATH_MAX];
    int system;
    sprintf(tmp, "%s/clockworkmod/backup/", path);
    char* file = choose_file_menu(tmp, NULL, advancedheaders);
    if (file == NULL)
        return;

    static char* headers[] = {  "高级恢复",
                                "",
                                NULL
    };

    char* list[] = { "恢复 boot",
                            "恢复 boot1",
                            "恢复 system",
                            "恢复 system1",
                            "恢复 data",
                            "恢复 data1",
                            "恢复 cache",
                            "恢复 sd-ext",
                            "恢复 wimax",
                            NULL
    };
    
    if (0 != get_partition_device("wimax", tmp)) {
        // disable wimax restore option
        list[8] = NULL;
    }

    sprintf(tmp, "%s%s", file, "boot.img");
    if(!fileExists(tmp))
        list[0]=NULL;

    sprintf(tmp, "%s%s", file, "boot1.img");
    if(!fileExists(tmp))
        list[1]=NULL;

    sprintf(tmp, "%s%s", file, "system.ext4.tar");
    if(!fileExists(tmp))
        list[2]=NULL;

    sprintf(tmp, "%s%s", file, "system1.ext4.tar");
    if(!fileExists(tmp))
        list[3]=NULL;

    sprintf(tmp, "%s%s", file, "data.ext4.tar");
    if(!fileExists(tmp))
        list[4]=NULL;

    sprintf(tmp, "%s%s", file, "data1.ext4.tar");
    if(!fileExists(tmp))
        list[5]=NULL;

    sprintf(tmp, "%s%s", file, "cache.ext4.tar");
    if(!fileExists(tmp))
        list[6]=NULL;


    static char* confirm_restore  = "确认恢复?";

    int chosen_item = get_filtered_menu_selection(headers, list, 0, 0, sizeof(list) / sizeof(char*));

    if(is_dualsystem() && chosen_item>=0 && chosen_item<=5) {
        if(!((chosen_item==4 || chosen_item==5) && !isTrueDualbootEnabled())) {
            system = select_system("选择系统恢复:");
            if (system>=0) {
                if(set_active_system(system)!=0) {
                    LOGE("Failed setting system. Please REBOOT.\n");
                    return;
                }
            }
            else return;
        }
    }

    switch (chosen_item)
    {
        case 0:
            if (confirm_selection(confirm_restore, "是的 - 恢复 boot"))
                nandroid_restore(file, 1, 0, 0, 0, 0, 0, 0, 0, 0);
            break;
        case 1:
            if (confirm_selection(confirm_restore, "是的 - 恢复 boot1"))
                nandroid_restore(file, 0, 0, 0, 0, 0, 0, 0, 0, 1);
            break;
        case 2:
            if (confirm_selection(confirm_restore, "是的 - 恢复 system"))
                nandroid_restore(file, 0, 1, 0, 0, 0, 0, 0, 0, 0);
            break;
        case 3:
            if (confirm_selection(confirm_restore, "是的 - 恢复 system1"))
                nandroid_restore(file, 0, 0, 0, 0, 0, 0, 1, 0, 0);
            break;
        case 4:
            if (confirm_selection(confirm_restore, "是的 - 恢复 data"))
                nandroid_restore(file, 0, 0, 1, 0, 0, 0, 0, 0, 0);
            break;
        case 5:
            if (confirm_selection(confirm_restore, "是的 - 恢复 data1"))
                nandroid_restore(file, 0, 0, 0, 0, 0, 0, 0, 1, 0);
            break;
        case 6:
            if (confirm_selection(confirm_restore, "是的 - 恢复 cache"))
                nandroid_restore(file, 0, 0, 0, 1, 0, 0, 0, 0, 0);
            break;
        case 7:
            if (confirm_selection(confirm_restore, "是的 - 恢复 sd-ext"))
                nandroid_restore(file, 0, 0, 0, 0, 1, 0, 0, 0, 0);
            break;
        case 8:
            if (confirm_selection(confirm_restore, "是的 - 恢复 wimax"))
                nandroid_restore(file, 0, 0, 0, 0, 0, 1, 0, 0, 0);
            break;
        
        
    }
}


//for twrpTar backup method

void show_advanced_twrpTar_menu() {

    char* primary_path = get_primary_storage_path();
    char** extra_paths = get_extra_storage_paths();
    int num_extra_volumes = get_num_extra_volumes();
    static char* headers[] = {  "twrpTar备份/恢复",
                                "",
                                NULL
    };

    char* list[] = {        "备份",
                            "恢复",
                            "删除",
                            "高级恢复",
                            NULL,
    };
 for (;;) {
        int chosen_item = get_filtered_menu_selection(headers, list, 0, 0, sizeof(list) / sizeof(char*));
        if (chosen_item == GO_BACK)
            break;
        switch (chosen_item)
        {
            case 0:
                {
                    char backup_path[PATH_MAX];
                    static time_t timep;
                    static  struct tm *time_tm;
		    time(&timep);
                    time_tm = gmtime(&timep);

                   
                        snprintf(backup_path, PATH_MAX, "%s/clockworkmod/twrpTar/backup/%03d%02d%02d-%02d%02d"
					,primary_path,time_tm->tm_year + 1900,
                        time_tm->tm_mon + 1, time_tm->tm_mday, time_tm->tm_hour + 8, time_tm->tm_min);
                   
                    nandroid_twrpTar_backup(backup_path);
                    write_recovery_version();
                }
                break;
            case 1:
                show_nandroid_twrpTar_restore_menu(primary_path);
                write_recovery_version();
                break;
            case 2:
                show_nandroid_delete_menu(primary_path, 1);
                write_recovery_version();
                break;
            case 3:
                show_nandroid_twrpTar_advanced_restore_menu(primary_path);
                write_recovery_version();
                break;
                               
            default:
                break;
        }
 }
}


void show_nandroid_twrpTar_restore_menu(const char* path)
{
    if (ensure_path_mounted(path) != 0) {
        LOGE("Can't mount %s\n", path);
        return;
    }

    static char* headers[] = {  "选择镜像恢复",
                                "",
                                NULL
    };

    char tmp[PATH_MAX];
    sprintf(tmp, "%s/clockworkmod/twrpTar/backup/", path);
    char* file = choose_file_menu(tmp, NULL, headers);
    if (file == NULL)
        return;
    if(is_dualsystem()) {
        int system = select_dualboot_restoremode("选择恢复模式:", file);
        int result;
        switch(system) {
            case DUALBOOT_ITEM_RESTORE_SYSTEM0:
                result = set_active_system(DUALBOOT_ITEM_SYSTEM0);
                if (result==0 && confirm_selection("确认恢复?", "是的 - 恢复"))
                    nandroid_twrpTar_restore(file, 1, 1, 1, 1, 1, 0, 0, 0, 0);
                break;
            case DUALBOOT_ITEM_RESTORE_SYSTEM1:
                result = set_active_system(DUALBOOT_ITEM_SYSTEM1);
                if (result==0 && confirm_selection("确认恢复?", "是的 - 恢复"))
                    nandroid_twrpTar_restore(file, 0, 0, 0, 1, 1, 0, 1, 1, 1);
                break;
            case DUALBOOT_ITEM_RESTORE_BOTH:
                result = set_active_system(DUALBOOT_ITEM_BOTH);
                if (result==0 && confirm_selection("确认恢复?", "是的 - 恢复"))
                    nandroid_twrpTar_restore(file, 1, 1, 1, 1, 1, 0, 1, 1, 1);
                break;
            case DUALBOOT_ITEM_RESTORE_ONE_TO_TWO:
                result = set_active_system(DUALBOOT_ITEM_SYSTEM1);
                if (result==0 && confirm_selection("确认恢复?", "是的 - 恢复"))
                    nandroid_twrpTar_restore(file, 1, 1, 1, 1, 1, 0, 0, 0, 0);
                break;
            case DUALBOOT_ITEM_RESTORE_TWO_TO_ONE:
                result = set_active_system(DUALBOOT_ITEM_SYSTEM0);
                if (result==0 && confirm_selection("确认恢复?", "是的 - 恢复"))
                    nandroid_twrpTar_restore(file, 0, 0, 0, 1, 1, 0, 1, 1, 1);
                break;
            case DUALBOOT_ITEM_RESTORE_BOTH_INTERCHANGED:
                result = set_active_system(DUALBOOT_ITEM_INTERCHANGED);
                if (result==0 && confirm_selection("确认恢复?", "是的 - 恢复"))
                    nandroid_twrpTar_restore(file, 1, 1, 1, 1, 1, 0, 1, 1, 1);
                break;

            default:
                return;
        }
        if(result!=0)
            ui_print("设置系统失败，请重启!\n");
        return;
    }
    if (confirm_selection("确认恢复?", "是的 - 恢复"))
        nandroid_twrpTar_restore(file, 1, 1, 1, 1, 1, 0, 0, 0, 0);
}

void show_nandroid_twrpTar_advanced_restore_menu(const char* path)
{
    if (ensure_path_mounted(path) != 0) {
        LOGE ("Can't mount sdcard\n");
        return;
    }

    static char* advancedheaders[] = {  "选择恢复镜像",
                                "",
                                "先选择恢复镜像",
                                "下个菜单将",
                                "提供更多选项.",
                                "",
                                NULL
    };

    char tmp[PATH_MAX];
    int system;
    sprintf(tmp, "%s/clockworkmod/twrpTar/backup/", path);
    char* file = choose_file_menu(tmp, NULL, advancedheaders);
    if (file == NULL)
        return;

    static char* headers[] = {  "高级恢复",
                                "",
                                NULL
    };

    char* list[] = { "恢复 boot",
                            "恢复 boot1",
                            "恢复 system",
                            "恢复 system1",
                            "恢复 data",
                            "恢复 data1",
                            "恢复 cache",
                            "恢复 sd-ext",
                            "恢复 wimax",
                            NULL
    };
    
    if (0 != get_partition_device("wimax", tmp)) {
        // disable wimax restore option
        list[8] = NULL;
    }

    sprintf(tmp, "%s%s", file, "boot.img");
    if(!fileExists(tmp))
        list[0]=NULL;

    sprintf(tmp, "%s%s", file, "boot1.img");
    if(!fileExists(tmp))
        list[1]=NULL;

    sprintf(tmp, "%s%s", file, "system.ext4.tar.gz");
    if(!fileExists(tmp))
        list[2]=NULL;

    sprintf(tmp, "%s%s", file, "system1.ext4.tar.gz");
    if(!fileExists(tmp))
        list[3]=NULL;

    sprintf(tmp, "%s%s", file, "data.ext4.tar.gz");
    if(!fileExists(tmp))
        list[4]=NULL;

    sprintf(tmp, "%s%s", file, "data1.ext4.tar.gz");
    if(!fileExists(tmp))
        list[5]=NULL;

    sprintf(tmp, "%s%s", file, "cache.ext4.tar.gz");
    if(!fileExists(tmp))
        list[6]=NULL;


    static char* confirm_restore  = "确认恢复?";

    int chosen_item = get_filtered_menu_selection(headers, list, 0, 0, sizeof(list) / sizeof(char*));

    if(is_dualsystem() && chosen_item>=0 && chosen_item<=5) {
        if(!((chosen_item==4 || chosen_item==5) && !isTrueDualbootEnabled())) {
            system = select_system("选择系统恢复:");
            if (system>=0) {
                if(set_active_system(system)!=0) {
                    LOGE("Failed setting system. Please REBOOT.\n");
                    return;
                }
            }
            else return;
        }
    }

    switch (chosen_item)
    {
        case 0:
            if (confirm_selection(confirm_restore, "是的 - 恢复 boot"))
                nandroid_restore(file, 1, 0, 0, 0, 0, 0, 0, 0, 0);
            break;
        case 1:
            if (confirm_selection(confirm_restore, "是的 - 恢复 boot1"))
                nandroid_restore(file, 0, 0, 0, 0, 0, 0, 0, 0, 1);
            break;
        case 2:
            if (confirm_selection(confirm_restore, "是的 - 恢复 system"))
                nandroid_twrpTar_restore(file, 0, 1, 0, 0, 0, 0, 0, 0, 0);
            break;
        case 3:
            if (confirm_selection(confirm_restore, "是的 - 恢复 system1"))
                nandroid_twrpTar_restore(file, 0, 0, 0, 0, 0, 0, 1, 0, 0);
            break;
        case 4:
            if (confirm_selection(confirm_restore, "是的 - 恢复 data"))
                nandroid_twrpTar_restore(file, 0, 0, 1, 0, 0, 0, 0, 0, 0);
            break;
        case 5:
            if (confirm_selection(confirm_restore, "是的 - 恢复 data1"))
                nandroid_twrpTar_restore(file, 0, 0, 0, 0, 0, 0, 0, 1, 0);
            break;
        case 6:
            if (confirm_selection(confirm_restore, "是的 - 恢复 cache"))
                nandroid_twrpTar_restore(file, 0, 0, 0, 1, 0, 0, 0, 0, 0);
            break;
        case 7:
            if (confirm_selection(confirm_restore, "是的 - 恢复 sd-ext"))
                nandroid_twrpTar_restore(file, 0, 0, 0, 0, 1, 0, 0, 0, 0);
            break;
        case 8:
            if (confirm_selection(confirm_restore, "是的 - 恢复 wimax"))
                nandroid_restore(file, 0, 0, 0, 0, 0, 1, 0, 0, 0);
            break;
        
        
    }
}

//end twrptar 



static void run_dedupe_gc(const char* other_sd) {
    ensure_path_mounted("/sdcard");
    nandroid_dedupe_gc("/sdcard/clockworkmod/blobs");
    if (other_sd) {
        ensure_path_mounted(other_sd);
        char tmp[PATH_MAX];
        sprintf(tmp, "%s/clockworkmod/blobs", other_sd);
        nandroid_dedupe_gc(tmp);
    }
}

static void choose_default_backup_format() {
    static const char* headers[] = {  "默认备份格式",
                                "",
                                NULL
    };
 int fmt = nandroid_get_default_backup_format();

    char **list;
    char* list_tar_default[] = { "tar (默认)",
                                   "dup",
                                 "tar + gzip",
                                 NULL };
    char* list_dup_default[] = { "tar",
        "dup (默认)",
        "tar + gzip",
        NULL
    };
char* list_tgz_default[] = { "tar",
                                 "dup",
                                 "tar + gzip (default)",
                                 NULL };

    if (fmt == NANDROID_BACKUP_FORMAT_DUP) {
        list = list_dup_default;
    } else if (fmt == NANDROID_BACKUP_FORMAT_TGZ) {
        list = list_tgz_default;
    } else {
        list = list_tar_default;
    }

    char path[PATH_MAX];
    snprintf(path, PATH_MAX, "%s%s", "/sdcard/", NANDROID_BACKUP_FORMAT_FILE);
    int chosen_item = get_menu_selection(headers, list, 0, 0);
    switch (chosen_item) {
        case 0:
            write_string_to_file(path, "tar");
            ui_print("默认备份格式设为 tar.\n");
            break;
        case 1:
            write_string_to_file(path, "dup");
            ui_print("默认备份格式设为 dedupe.\n");
            break;
        case 2: {
            write_string_to_file(path, "tgz");
            ui_print("Default backup format set to tar + gzip.\n");
            break;
        }
    }
}






void show_nandroid_menu()
{
    char* primary_path = get_primary_storage_path();
    char** extra_paths = get_extra_storage_paths();
    int num_extra_volumes = get_num_extra_volumes();
    static char* headers[] = {  "备份/恢复",
                                "",
                                NULL
    };

    char* list[] = { "备份",
                            "恢复",
                            "删除",
                            "高级恢复",
                            "清理无用备份",
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL
    };

    char *other_sd = NULL;
    if (volume_for_path("/emmc") != NULL) {
        other_sd = "/emmc";
        list[5] = "备份到内置SD卡";
        list[6] = "从内置SD卡恢复";
        list[7] = "(高级)从内置SD卡恢复";
        list[8] = "从内置SD卡删除";
    }
    else if (volume_for_path("/external_sd") != NULL) {
        other_sd = "/external_sd";
        list[5] = "备份到外置SD卡";
        list[6] = "从外置SD卡恢复";
        list[7] = "(高级)从外置SD卡恢复";
        list[8] = "从外置SD卡删除";
    }
#ifdef RECOVERY_EXTEND_NANDROID_MENU
    extend_nandroid_menu(list, 10, sizeof(list) / sizeof(char*));
#endif

    for (;;) {
        int chosen_item = get_filtered_menu_selection(headers, list, 0, 0, sizeof(list) / sizeof(char*));
        if (chosen_item == GO_BACK)
            break;
        switch (chosen_item)
        {
            case 0:
                {
		    char backup_path[PATH_MAX];
                    static time_t timep;
                    static  struct tm *time_tm;
		    time(&timep);
                    time_tm = gmtime(&timep);

                   
                        snprintf(backup_path, PATH_MAX, "%s/clockworkmod/backup/%03d%02d%02d-%02d%02d"
					,primary_path,time_tm->tm_year + 1900,
                        time_tm->tm_mon + 1, time_tm->tm_mday, time_tm->tm_hour + 8, time_tm->tm_min);
                    nandroid_backup(backup_path);
                    write_recovery_version();
                }
                break;
            case 1:
                show_nandroid_restore_menu(primary_path);
                write_recovery_version();
                break;
            case 2:
                show_nandroid_delete_menu(primary_path, 0);
                write_recovery_version();
                break;
            case 3:
                show_nandroid_advanced_restore_menu(primary_path);
                write_recovery_version();
                break;
            case 4:
                run_dedupe_gc(other_sd);
                break;
            case 5:
                {
                    char backup_path[PATH_MAX];
                    time_t t = time(NULL);
                    struct tm *timeptr = localtime(&t);
                    if (timeptr == NULL)
                    {
                        struct timeval tp;
                        gettimeofday(&tp, NULL);
                        if (other_sd != NULL) {
                            sprintf(backup_path, "%s/clockworkmod/backup/%d", other_sd, tp.tv_sec);
                        }
                        else {
                            break;
                        }
                    }
                    else
                    {
                        if (other_sd != NULL) {
                            char tmp[PATH_MAX];
                            strftime(tmp, sizeof(tmp), "clockworkmod/backup/%F.%H.%M.%S", timeptr);
                            // this sprintf results in:
                            // /emmc/clockworkmod/backup/%F.%H.%M.%S (time values are populated too)
                            sprintf(backup_path, "%s/%s", other_sd, tmp);
                        }
                        else {
                            break;
                        }
                    }
                    nandroid_backup(backup_path);
                }
                break;
            case 6:
                if (other_sd != NULL) {
                    show_nandroid_restore_menu(other_sd);
                }
                break;
            case 7:
                if (other_sd != NULL) {
                    show_nandroid_advanced_restore_menu(other_sd);
                }
                break;
            case 8:
                if (other_sd != NULL) {
                    show_nandroid_delete_menu(other_sd,0);
                }
                break;
            default:
#ifdef RECOVERY_EXTEND_NANDROID_MENU
                handle_nandroid_menu(10, chosen_item);
#endif
                break;
        }
    }
}



static void partition_sdcard(const char* volume) {
    if (!can_partition(volume)) {
        ui_print("不能分区: %s\n", volume);
        return;
    }

    static char* ext_sizes[] = { "128M",
                                 "256M",
                                 "512M",
                                 "1024M",
                                 "2048M",
                                 "4096M",
                                 NULL };

    static char* swap_sizes[] = { "0M",
                                  "32M",
                                  "64M",
                                  "128M",
                                  "256M",
                                  NULL };

    static char* ext_headers[] = { "Ext 大小", "", NULL };
    static char* swap_headers[] = { "Swap 大小", "", NULL };

    int ext_size = get_menu_selection(ext_headers, ext_sizes, 0, 0);
    if (ext_size == GO_BACK)
        return;

    int swap_size = get_menu_selection(swap_headers, swap_sizes, 0, 0);
    if (swap_size == GO_BACK)
        return;

    char sddevice[256];
    Volume *vol = volume_for_path(volume);
    strcpy(sddevice, vol->blk_device);
    // we only want the mmcblk, not the partition
    sddevice[strlen("/dev/block/mmcblkX")] = NULL;
    char cmd[PATH_MAX];
    setenv("SDPATH", sddevice, 1);
    sprintf(cmd, "sdparted -es %s -ss %s -efs ext3 -s", ext_sizes[ext_size], swap_sizes[swap_size]);
    ui_print("正在分区SD卡...请稍候...\n");
    if (0 == __system(cmd))
        ui_print("完成!\n");
    else
        ui_print("SD卡分区出错，详情请看 /tmp/recovery.log .\n");
}

int can_partition(const char* volume) {
    Volume *vol = volume_for_path(volume);
    if (vol == NULL) {
        LOGI("Can't format unknown volume: %s\n", volume);
        return 0;
    }

    int vol_len = strlen(vol->blk_device);
    // do not allow partitioning of a device that isn't mmcblkX or mmcblkXp1
    if (vol->blk_device[vol_len - 2] == 'p' && vol->blk_device[vol_len - 1] != '1') {
        LOGI("Can't partition unsafe device: %s\n", vol->blk_device);
        return 0;
    }
    
    if (strcmp(vol->fs_type, "vfat") != 0) {
        LOGI("Can't partition non-vfat: %s\n", vol->fs_type);
        return 0;
    }

    return 1;
}

void show_advanced_menu()
{
    static char* headers[] = {  "高级功能",
                                "",
                                NULL
    };
    int system;

    for (;;)
    {
        char* list[] = { "重启Recovery",
                         "重启到fastboot",
                         "清空虚拟机缓存",
                         "报告错误",
                         "按键测试",
                         "查看日志",
                         "权限修复",
                         "SD卡分区",
                         "外置SD卡分区",
                         "内置SD卡分区",
                         "当前系统: ",
                         NULL,
                         NULL
        };

        if (!can_partition("/sdcard")) {
            list[7] = NULL;
        }
        if (!can_partition("/external_sd")) {
            list[8] = NULL;
        }
        if (!can_partition("/emmc")) {
            list[9] = NULL;
        }

        if (is_dualsystem()) {
            char bootmode[13];
            getBootmode(&bootmode);
            if(strcmp(bootmode, "boot-system0")==0)
                list[10]="当前系统: 1";
            else if(strcmp(bootmode, "boot-system1")==0)
                list[10]="当前系统: 2";
            else
                list[10]=NULL;

            if(isTrueDualbootEnabled()) 
                list[11] = "关闭 双系统共存";
            else 
                list[11] = "打开 双系统共存";
        }

        int chosen_item = get_filtered_menu_selection(headers, list, 0, 0, sizeof(list) / sizeof(char*));
        if (chosen_item == GO_BACK)
            break;
        switch (chosen_item)
        {
            case 0:
                android_reboot(ANDROID_RB_RESTART2, 0, "recovery");
                break;
            case 1:
                android_reboot(ANDROID_RB_RESTART2, 0, "bootloader");
                break;
            case 2:
                if(is_dualsystem() && isTrueDualbootEnabled()) {
                    int system = select_system("选择系统清空:");
                    if (system>=0) {
                        if(set_active_system(system)!=0) {
                            ui_print("设置系统错误. 请重启!\n");
                            break;
                        }
                    }
                    else break;
                }

                if (0 != ensure_path_mounted("/data"))
                    break;
                ensure_path_mounted("/sd-ext");
                ensure_path_mounted("/cache");
                if (confirm_selection( "确认清空?", "是的 - 清空虚拟机缓存")) {
                    __system("rm -r /data/dalvik-cache");
                    __system("rm -r /cache/dalvik-cache");
                    __system("rm -r /sd-ext/dalvik-cache");
                    ui_print("虚拟机缓存已清空.\n");
                }
                ensure_path_unmounted("/data");
                break;
            case 3:
                handle_failure(1);
                break;
            case 4:
            {
                ui_print("输出键值.\n");
                ui_print("返回键终止调试.\n");
                int key;
                int action;
                do
                {
                    key = ui_wait_key();
                    action = device_handle_key(key, 1);
                    ui_print("Key: %d\n", key);
                }
                while (action != GO_BACK);
                break;
            }
            case 5:
                ui_printlogtail(12);
                break;
            case 6:
                if(is_dualsystem()) {
                    int system = select_system("选择系统修复权限:");
                    if (system>=0) {
                        if(set_active_system(system)!=0) {
                            ui_print("系统设置失败. 请重启!\n");
                            break;
                        }
                    }
                    else break;
                }
                ensure_path_mounted("/system");
                ensure_path_mounted("/data");
                ui_print("正在修复权限...\n");
                __system("fix_permissions");
                ui_print("完成!\n");
                break;
            case 7:
                partition_sdcard("/sdcard");
                break;
            case 8:
                partition_sdcard("/external_sd");
                break;
            case 9:
                partition_sdcard("/emmc");
                break;
            case 10:
                system = select_system("选择启动系统:");
                if(system==DUALBOOT_ITEM_SYSTEM0)
                    setBootmode("boot-system0");
                else if(system==DUALBOOT_ITEM_SYSTEM1)
                    setBootmode("boot-system1");
                break;
            case 11:
                enableTrueDualboot(!isTrueDualbootEnabled());
                break;
        }
    }
}

void write_fstab_root(char *path, FILE *file)
{
    Volume *vol = volume_for_path(path);
    if (vol == NULL) {
        LOGW("Unable to get recovery.fstab info for %s during fstab generation!\n", path);
        return;
    }

    char device[200];
    if (vol->blk_device[0] != '/')
        get_partition_device(vol->blk_device, device);
    else
        strcpy(device, vol->blk_device);

    fprintf(file, "%s ", device);
    fprintf(file, "%s ", path);
    // special case rfs cause auto will mount it as vfat on samsung.
    fprintf(file, "%s rw\n", vol->fs_type2 != NULL && strcmp(vol->fs_type, "rfs") != 0 ? "auto" : vol->fs_type);
}

void create_fstab()
{
    struct stat info;
    __system("touch /etc/mtab");
    FILE *file = fopen("/etc/fstab", "w");
    if (file == NULL) {
        LOGW("Unable to create /etc/fstab!\n");
        return;
    }
    Volume *vol = volume_for_path("/boot");
    if (NULL != vol && strcmp(vol->fs_type, "mtd") != 0 && strcmp(vol->fs_type, "emmc") != 0 && strcmp(vol->fs_type, "bml") != 0)
         write_fstab_root("/boot", file);
    write_fstab_root("/cache", file);
    write_fstab_root("/data", file);
    write_fstab_root("/datadata", file);
    write_fstab_root("/emmc", file);
    write_fstab_root("/system", file);
    write_fstab_root("/system1", file);
    //write_fstab_root("/sdcard", file);
   // write_fstab_root("/sd-ext", file);
    //write_fstab_root("/external_sd", file);
    fclose(file);
    LOGI("Completed outputting fstab.\n");
}

int bml_check_volume(const char *path) {
    ui_print("检查 %s...\n", path);
    ensure_path_unmounted(path);
    if (0 == ensure_path_mounted(path)) {
        ensure_path_unmounted(path);
        return 0;
    }
    
    Volume *vol = volume_for_path(path);
    if (vol == NULL) {
        LOGE("Unable process volume! Skipping...\n");
        return 0;
    }
    
    ui_print("%s 可能是 rfs. 检查...\n", path);
    char tmp[PATH_MAX];
    sprintf(tmp, "mount -t rfs %s %s", vol->blk_device, path);
    int ret = __system(tmp);
    printf("%d\n", ret);
    return ret == 0 ? 1 : 0;
}

void process_volumes() {
    create_fstab();

    if (is_data_media()) {
        setup_data_media();
    }

    return;

    // dead code.
    if (device_flash_type() != BML)
        return;

    ui_print("检查 for ext4 partitions...\n");
    int ret = 0;
    ret = bml_check_volume("/system");
    ret |= bml_check_volume("/data");
    if (has_datadata())
        ret |= bml_check_volume("/datadata");
    ret |= bml_check_volume("/cache");
    
    if (ret == 0) {
        ui_print("完成!\n");
        return;
    }
    
    char backup_path[PATH_MAX];
    time_t t = time(NULL);
    char backup_name[PATH_MAX];
    struct timeval tp;
    gettimeofday(&tp, NULL);
    sprintf(backup_name, "before-ext4-convert-%d", tp.tv_sec);
    sprintf(backup_path, "%s/clockworkmod/backup/%s",get_primary_storage_path(), backup_name);

    ui_set_show_text(1);
    ui_print("Filesystems need to be converted to ext4.\n");
    ui_print("A backup and restore will now take place.\n");
    ui_print("If anything goes wrong, your backup will be\n");
    ui_print("named %s. Try restoring it\n", backup_name);
    ui_print("in case of error.\n");

    nandroid_backup(backup_path);
    nandroid_restore(backup_path, 1, 1, 1, 1, 1, 0, 0, 0, 0);
    ui_set_show_text(0);
}

void handle_failure(int ret)
{
    char *primary_path = get_primary_storage_path();
    char tmp[PATH_MAX];
    struct stat st;
    if (ret == 0)
        return;
    if (0 != ensure_path_mounted(primary_path))
        return;
    
    snprintf(tmp, PATH_MAX, "%s/clockworkmod", primary_path);
    if (stat(tmp, &st) != 0)
	    mkdir("/sdcard/clockworkmod", S_IRWXU | S_IRWXG | S_IRWXO);

    snprintf(tmp, PATH_MAX, "cp /tmp/recovery.log %s/clockworkmod/recovery.log", primary_path);
    __system(tmp);
    ui_print("/tmp/recovery.log was copied to /sdcard/clockworkmod/recovery.log. Please open ROM Manager to report the issue.\n");
}

int is_path_mounted(const char* path) {
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        return 0;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // the ramdisk is always mounted.
        return 1;
    }

    int result;
    result = scan_mounted_volumes();
    if (result < 0) {
        LOGE("failed to scan mounted volumes\n");
        return 0;
    }

    const MountedVolume* mv =
        find_mounted_volume_by_mount_point(v->mount_point);
    if (mv) {
        // volume is already mounted
        return 1;
    }
    return 0;
}

int has_datadata() {
    Volume *vol = volume_for_path("/datadata");
    return vol != NULL;
}

int volume_main(int argc, char **argv) {
    load_volume_table();
    return 0;
}

int verify_root_and_recovery(int system_number) {
    if(is_dualsystem()) {
        if(set_active_system(system_number)!=0) {
            LOGE("Failed setting system. Please REBOOT.\n");
            return 0;
        }
    }

    if (ensure_path_mounted("/system") != 0)
        return 0;

    int ret = 0;
    struct stat st;
    if (0 == lstat("/system/etc/install-recovery.sh", &st)) {
        if (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
            ui_show_text(1);
            ret = 1;
            char confirm[PATH_MAX];
            if(is_dualsystem()) sprintf(confirm, "是的 - 禁止系统%d刷写Recovery", system_number);
            else sprintf(confirm, "是的 - 禁止刷写Recovery");
            if (confirm_selection("ROM可能刷入默认Recovery\n修复吗?", confirm)) {
                __system("chmod -x /system/etc/install-recovery.sh");
            }
        }
    }


    if(is_dualsystem()) {
        if(isTrueDualbootEnabled())
            __system("cp /res/dualsystem/mount_ext4_tdb.sh /system/bin/mount_ext4.sh");
        else
            __system("cp /res/dualsystem/mount_ext4_default.sh /system/bin/mount_ext4.sh");
        chmod("/system/bin/mount_ext4.sh", 0755);
    }

    ensure_path_unmounted("/system");
    return ret;
}

int select_system(const char* title)
{
    char* headers[] = { title, "", NULL };
    char* items[] = { "系统[1]",
                      "系统[2]",
                      NULL };
    int chosen_item = get_menu_selection(headers, items, 0, 0);
    return chosen_item+1;
}

int select_dualboot_backupmode(const char* title)
{
    char* headers[] = { title, "", NULL };
    char* items[] = { "备份系统[1]",
                      "备份系统[2]",
                      "同时备份双系统",
                      NULL };
    int chosen_item = get_menu_selection(headers, items, 0, 0);
    return chosen_item==2?DUALBOOT_ITEM_BOTH:chosen_item+1;
}

int select_dualboot_restoremode(const char* title, const char* file)
{
    char* headers[] = { title, "", NULL };
    char* items[] = { "仅恢复系统[1]",
                      "仅恢复系统[2]",
                      "同时恢复双系统",
                      "恢复 系统1->系统2",
                      "恢复 系统2->系统1",
                      "交换恢复",
                      NULL };
    char fileSystem0[PATH_MAX];
    char fileSystem1[PATH_MAX];

    char twrpSystem0[PATH_MAX];
    char twrpSystem1[PATH_MAX];

    sprintf(fileSystem0, "%s%s", file, "system.ext4.tar");
    sprintf(fileSystem1, "%s%s", file, "system1.ext4.tar");
    
    snprintf(twrpSystem0, PATH_MAX, "%s%s", file, "system.ext4.tar.gz");
    snprintf(twrpSystem1, PATH_MAX, "%s%s", file, "system1.ext4.tar.gz");

    int hasSystem0;
    int hasSystem1;

    if (fileExists(fileSystem0) || fileExists(twrpSystem0))
	    hasSystem0 = 1;
    if (fileExists(fileSystem1) || fileExists(twrpSystem1))
	    hasSystem1 = 1;

    if(!hasSystem0) {
        items[0] = NULL;
        items[3] = NULL;
    }
    if(!hasSystem1) {
        items[1] = NULL;
        items[4] = NULL;
    }
    if(!hasSystem0 || !hasSystem1) {
        items[2] = NULL;
        items[5] = NULL;
    }

    int chosen_item = get_filtered_menu_selection(headers, items, 0, 0, sizeof(items) / sizeof(char*));
    return chosen_item;
}

int setBootmode(char* bootmode) {
   // open misc-partition
   FILE* misc = fopen("/dev/block/platform/msm_sdcc.1/by-name/misc", "wb");
   if (misc == NULL) {
      printf("打开misc分区失败.\n");
      return -1;
   }

   // write bootmode
   fseek(misc, 0x1000, SEEK_SET);
   if(fputs(bootmode, misc)<0) {
      printf("写入启动模式失败.\n");
      return -1;
   }

   // close
   fclose(misc);
   return 0;
}

int getBootmode(char* bootmode) {
   // open misc-partition
   FILE* misc = fopen("/dev/block/platform/msm_sdcc.1/by-name/misc", "rb");
   if (misc == NULL) {
      printf("打开misc分区失败.\n");
      return -1;
   }

   // write bootmode
   fseek(misc, 0x1000, SEEK_SET);
   if(fgets(bootmode, 13, misc)==NULL) {
      printf("读取启动模式失败.\n");
      return -1;
   }

   // close
   fclose(misc);
   return 0;
}

int fileExists(const char* file) {
    struct stat st;
    return (0 == lstat(file, &st));
}

int isTrueDualbootEnabled() {
    struct stat st;
    int ret;
    if (ensure_path_mounted_at_mount_point("/data", DUALBOOT_PATH_DATAROOT)!=0) {
        LOGE("TrueDualBoot: failed mounting data\n");
        return -1;
    }
    ret = lstat(DUALBOOT_FILE_TRUEDUALBOOT, &st);
    return (ret==0);
}

int enableTrueDualboot(int enable) {
    char confirm[PATH_MAX];
    ui_setMenuTextColor(MENU_TEXT_COLOR_RED);
    sprintf(confirm, "是的 - %s 双系统共存", enable?"打开":"关闭");

    if (confirm_selection("将清空所有数据", confirm)) {
        // unmount /data
        if(ensure_path_unmounted("/data")!=0) {
            LOGE("Error unmounting /data!\n");
            return -1;
        }

        // format /data
        ui_set_background(BACKGROUND_ICON_INSTALLING);
        ui_show_indeterminate_progress();
        ui_print("正在格式化 /data...\n");
        handle_truedualsystem_format(1);
        if(format_volume("/data")!=0) {
            ui_print("格式化错误g /data!\n");
            ui_reset_progress();
            return -1;
        }
        ui_reset_progress();
        handle_truedualsystem_format(0);
        ui_print("完成.\n");

        // mount data at /data_root
        if(ensure_path_mounted_at_mount_point("/data", DUALBOOT_PATH_DATAROOT)!=0) {
            LOGE("failed mounting data at %s!\n", DUALBOOT_PATH_DATAROOT);
                return -1;
        }

        if(enable) {
            FILE * pFile = fopen(DUALBOOT_FILE_TRUEDUALBOOT,"w");
            if(pFile==NULL) {
                LOGE("TrueDualBoot: failed creating file");
            }
            fclose(pFile);
        }
        else
            remove(DUALBOOT_FILE_TRUEDUALBOOT);
    }

    ui_setMenuTextColor(MENU_TEXT_COLOR);

    return 0;
}
