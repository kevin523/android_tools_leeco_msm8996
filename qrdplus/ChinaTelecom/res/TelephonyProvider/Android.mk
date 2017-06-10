LOCAL_PATH := $(call my-dir)

res_dir := res
include $(CLEAR_VARS)

LOCAL_RESOURCE_DIR := $(addprefix $(LOCAL_PATH)/, $(res_dir))
LOCAL_MODULE_TAGS := optional eng
LOCAL_SRC_FILES := $(call all-subdir-java-files)
LOCAL_SDK_VERSION := current
LOCAL_PACKAGE_NAME := CtTelephonyProviderRes
LOCAL_MODULE_STEM := TelephonyProvider-overlay

# This will install the file in /system/vendor/ChinaMobile
LOCAL_MODULE_PATH := $(TARGET_OUT)/vendor/ChinaTelecom/system/vendor/overlay/system/app
LOCAL_CERTIFICATE := shared

include $(BUILD_PACKAGE)

