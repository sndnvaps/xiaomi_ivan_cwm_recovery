# Copyright 2011 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= fs_mgr.c fs_mgr_verity.c

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include

LOCAL_MODULE:= libfs_mgr_xiaomi
LOCAL_STATIC_LIBRARIES := liblogwrap libmincrypt libext4_utils_static
LOCAL_C_INCLUDES += system/extras/ext4_utils
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include

include $(BUILD_STATIC_LIBRARY)




