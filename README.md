android_bootable_recovery
=========================
- 小米手机１／１Ｓ　双系统共存

- 小米手机 2 / 2 S 双系统共存

--------------------------
功能介绍：

  - `备份/恢复`             -> 这个功能主要是用于备份 4.0, 4.1, 4.2等没有包含SELinux 功能的系统
  - `高级备份/恢复功能`     ->  这个主要用于备份 4.3, 4.4 等包含了SELinux功能的系统

-----------------------------------
加入了备份`selinux context` 功能

*感谢TWRP提供的源代码*

2014-03-23 更新
下列代码都是提取自最新版本的TWRP源代码。
  - libtar 
  - openaes 
  - twrpTarMain 
  - 添加twrpTar调用方法， 增加`高级备份 / 恢复功能`
 
2014-03-24 更新
  - 去掉一些warning 
  - 修改twrpTar单个文件最大的体积为500MB,当备份文件体积大于500MB会自动分卷


2014-04-01 更新
    修改分区表格式为最新版本
    使用fs_mgr读取分区表
    增加OTG USB功能
    能从otg usb挂载的U盘中安装刷机包(/storage/usbdisk)
    去除部分没有用的代码

bug: 无法使用大容量模式

2014-05-30  change 
    添加新方法用来格式化 '/system1'分区
    添加 Ignore 分区
    重点部分方法
    重写分区表 

*为了能备份android 4.3 以上系统的SELinux selable*
- 请在BoardConfig.mk中加入如下定义
  `TWHAVE_SELINUX := true`

**twrpTar使用说明书**
[twrpTar说明书](twrpTarMain/README.md)

**fs_mgr修改说明**
[fs_mgr修改简介](fs_mgr/README.md)

------------------------------------------------------------------
**函数声明**

	minui/minui.h:37:int gr_text(int x, int y, const char *s);
	这个gr_text()函数参数是三个，如果你在Device/xiaomi/aries/recovery/graphics.c中定义的 gr_text()函数是四个参数的。
	请修改gr_text()函数在minui.h中的声明为
	int gr_text(int x ,int y, const char *, int bold);

---------------------------------------------------------------

