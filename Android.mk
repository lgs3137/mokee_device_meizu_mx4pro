LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),mx4pro)

include $(call all-makefiles-under,$(LOCAL_PATH))

endif
