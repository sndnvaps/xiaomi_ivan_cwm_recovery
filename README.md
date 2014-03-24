android_bootable_recovery
=========================
- 小米手机１／１Ｓ　双系统共存

- 小米手机 2 / 2 S 双系统共存

--------------------------
功能介绍：
     备份/恢复             -> 这个功能主要是用于备份 4.0, 4.1, 4.2等没有包含SELinux 功能的系统
     高级备份/恢复功能     ->  这个主要用于备份 4.3, 4.4 等包含了SELinux功能的系统

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



*为了能备份android 4.3 以上系统的SELinux selable*
- 请在BoardConfig.mk中加入如下定义
  `TWHAVE_SELINUX := true`

**twrpTar使用说明书**
[twrpTar说明书](twrpTarMain/README.md)
