ifeq ($(strip $(TARGET_USES_QTIC_HOME_LOCATION)),true)
LOCAL_PATH:= $(call my-dir)


include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional eng

LOCAL_SRC_FILES := $(call all-subdir-java-files)

LOCAL_PACKAGE_NAME := HomeLocation

LOCAL_CERTIFICATE := platform

include $(BUILD_PACKAGE)
endif
