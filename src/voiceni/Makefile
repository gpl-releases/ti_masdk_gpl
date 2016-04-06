#
# File Name: Makefile
#
# Description: Makefile to build kernel voice network interface module.
#
# Copyright (C) 2011 Texas Instruments, Incorporated
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation version 2.
#
# This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
# whether express or implied; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#


obj-m += voiceni.o
voiceni-y := voice_ni.o

MODULE_EXTRA_INC := -I$(voiceni_MODULE_PATH)/$(MASDK_PFORM) \
	-I$(TARGET_HOME)/ti/docsis/src/include \
	-I$(TARGET_HOME)/kernel/ti/linux-2.6.18/src/include/linux \
	-I$(TARGET_HOME)/kernel/ti/linux-2.6.18/src/include/asm \
	-I$(TARGET_HOME)/kernel/ti/linux-2.6.18/src/include/asm/arch \
	-I$(TARGET_HOME)/kernel/ti/linux-2.6.18/src/include/asm/arch/generic \
	-I$(TARGET_HOME)/kernel/ti/linux-2.6.18/src/include/asm-arm/arch-avalanche/puma5 

MODULE_PATH      := $(voiceni_MODULE_PATH)

include $(gpl_ARCH_DEF)
