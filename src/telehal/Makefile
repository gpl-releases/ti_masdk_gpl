#
# File Name: Makefile
#
# Description: Makefile to build the telephony HAL kernel module.
#
# Copyright (C) 2008 Texas Instruments, Incorporated
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

ifeq ($(MASDK_PFORM),puma)
TI_SOC=puma
endif

obj-m := telehal.o
telehal-y := board/$(MASDK_PFORM)/tiuhal_pform.o arch/$(TI_SOC)/tiuhal.o tiuhal_mod.o

CLEAN_EXTRA := clean_extra

MODULE_PATH := $(telehal_MODULE_PATH)

include $(gpl_ARCH_DEF)

