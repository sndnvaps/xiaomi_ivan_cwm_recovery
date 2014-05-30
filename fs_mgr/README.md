# fs_mgr 修改说明


	为了适配小米手机，需要对fs_mgr进行修改才可以。
	为了区分开$(TOP_SOURCE_TREE)/system/core/fs_mgr 与 $(cwm_recovery_path)/fs_mgr 生成的 ‘LOCAL_MODULE' 
	所以修改 本目录下面的生成规则为如下

‘
LOCAL_MODULE:= libfs_mgr_xiaomi #原来的为LOCAL_MODULE:= libfs_mgr
'


## 修改 fs_mgr.h文件，添加对双系统的功能支持与参数

- 往 `struct fstab` 结构体中添加对象 `bool dualboot`
变成如下：

```c
struct fstab {
    int num_entries;
    struct fstab_rec *recs;
    char *fstab_filename;
    //for dualboot featual 
    bool dualboot;  //这是添加的内容，用于添加分区表中是否存在 /system1分区，以开启双系统支持
};
``` 

- 往 `struct fstab_rec` 添加对象 `struct stat st` 
变成如下 ：
```c 
struct fstab_rec {
    char *blk_device;
    char *mount_point;
    char *fs_type;
    ... 

    //for xiaomi dualboot feature 
    struct stat st; //这是添加的内容，用于保存分区的状态信息，用于切换分区
};
```

## 修改 fs_mgr.c ，实现双系统分区支持

- 修改函数 `struct fstab *fs_mgr_read_fstab(const char *fstab_path)` 
定位到 


```c 
    ... 
    ... 省略 
if (!(p = strtok_r(NULL, delim, &save_ptr))) {
            ERROR("Error parsing mount_point\n");
            goto err;
        }
        fstab->recs[cnt].mount_point = strdup(p);
``` 

我们要在这一行代码的下面，添加代码
	fstab->recs[cnt].mount_point = strdup(p);

添加的代码如下：
```c
	//add by sndnvaps for xiaomi dualboot feature
	int code = 0;
	if (code = stat(fstab->recs[cnt].blk_device, &fstab->recs[cnt].st) != 0) //此行用于保存分区状态
		ERROR("stat: Error %d on file %s\n", code, fstab->recs[cnt].blk_device);

	if (strcmp(fstab->recs[cnt].mount_point, "/system1") == 0)    //判断是否是/system1分区，如果正确，就将fstab->dualboot 设置为true 
		fstab->dualboot = true; 
	//end 
``` 



	



