android_bootable_recovery
=========================
- 小米手机１／１Ｓ　双系统共存

- 小米手机 2 / 2 S 双系统共存

加入了备份`selinux selabel` 功能

*感谢TWRP提供的源代码*

下列代码都是提取自最新版本的TWRP源代码。
  - libtar 
  - openaes 
  - twrpTarMain 

*为了能备份android 4.3 以上系统的SELinux selable*
- 请在BoardConfig.mk中加入如下定义
  `TWHAVE_SELINUX := true`

**twrpTar使用说明书**
[twrpTar说明书](twrpTarMain/README.md)
