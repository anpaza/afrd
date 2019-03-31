# Android makefile for building an bionic-linked binary of the afrd

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := afrd
LOCAL_SRC_FILES := $(addprefix ../,main.c afrd.c sysfs.c cfg_parse/cfg_parse.c \
	cfg.c modes.c mstime.c uevent_filter.c colorspace.c strfun.c shmem.c \
	apisock.c crc32.c)
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../cfg_parse
LOCAL_CFLAGS := -DBDATE="\"$(shell date +"%Y-%m-%d %H:%M:%S")\""

include $(BUILD_EXECUTABLE)
