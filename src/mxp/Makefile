#
# File Name: Makefile
#
# Description: Makefile to build kernel MXP modules.
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

obj-m := mxpmod.o
mxpmod-y := mmxpcore.o

obj-m += mxpmem.o
mxpmem-y := mmxpmem.o

MODULE_EXTRA_INC := -I$(mxp_MODULE_PATH)/$(MASDK_PFORM)
MODULE_PATH      := $(mxp_MODULE_PATH)

include $(gpl_ARCH_DEF)

