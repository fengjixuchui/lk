LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += $(LOCAL_DIR)/ahci.cpp
MODULE_SRCS += $(LOCAL_DIR)/ata.cpp
MODULE_SRCS += $(LOCAL_DIR)/disk.cpp
MODULE_SRCS += $(LOCAL_DIR)/port.cpp

MODULE_DEPS += dev/bus/pci
MODULE_DEPS += lib/bio

MODULE_CPPFLAGS += -Wno-invalid-offsetof

include make/module.mk
