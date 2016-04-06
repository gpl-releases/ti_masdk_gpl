/*
 * File name: tiuhal.c
 *
 * Description: Telephony Interface Daughtercard HAL. HAL to TID interface driver
 * functions. This interface relies upon the SPI interface provided by a OS.
 *
 * Copyright (C) 2011 Texas Instruments, Incorporated
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifdef GG_LINUX
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/spi/spi.h>

#include <asm-arm/arch-avalanche/generic/pal.h>
#include <linux/spi/spi.h>
#include <asm-arm/arch-avalanche/generic/pal_sys.h>


#if defined(CONFIG_MACH_PUMA6)
#include <asm-arm/arch-avalanche/puma6/puma6.h>
/* **TODO - we need to Remove/replace below headers with common files for Puma after getting VSDK 4.0 */
#include <asm-arm/arch-avalanche/puma6/puma6_spi.h>
#include <asm-arm/arch-avalanche/puma6/puma6_bootcfg_ctrl.h>
#elif defined(CONFIG_MACH_PUMA5)
/* **TODO - we need to Remove/replace below headers with common files for Puma after getting VSDK 4.0 */
#include <asm-arm/arch-avalanche/puma5/puma5.h>
#include <asm-arm/arch-avalanche/puma5/puma5_spi.h>
#else
#error No Architecture specified
#endif
#include <asm-arm/arch-avalanche/generic/soc.h>
#include <asm-arm/arch-avalanche/generic/avalanche_intc.h>
#else
#error Not a supported OS
#endif

#include "tiuhalcfg.h"
#include "rtos_hal.h"
#include "tiuhal.h"

#ifndef PRIVATE
#define PRIVATE static
#endif

#if defined(CONFIG_MACH_PUMA6)

#define pVOIC_PCMCR1_TDM0_REG                ((volatile UINT32 *)(AVALANCHE_TDM_BASE+0x00C0))
#define VOIC_PCMCR1_TDM0_REG                 (*pVOIC_PCMCR1_TDM0_REG)
#define pVOIC_PCMCR2_TDM0_REG                ((volatile UINT32 *)(AVALANCHE_TDM_BASE+0x00C4))
#define VOIC_PCMCR2_TDM0_REG                 (*pVOIC_PCMCR2_TDM0_REG)
#define pVOIC_PCMCR1_TDM1_REG                ((volatile UINT32 *)(AVALANCHE_TDM_1_BASE+0x00C0))
#define VOIC_PCMCR1_TDM1_REG                 (*pVOIC_PCMCR1_TDM1_REG)
#define pVOIC_PCMCR2_TDM1_REG                ((volatile UINT32 *)(AVALANCHE_TDM_1_BASE+0x00C4))
#define VOIC_PCMCR2_TDM1_REG                 (*pVOIC_PCMCR2_TDM1_REG)

#elif defined(CONFIG_MACH_PUMA5)

#define pVOIC_PCMCR1_TDM0_REG                ((volatile UINT32 *)(AVALANCHE_TDM_BASE+0x00C0))
#define VOIC_PCMCR1_TDM0_REG                 (*pVOIC_PCMCR1_TDM0_REG)
#define pVOIC_PCMCR2_TDM0_REG                ((volatile UINT32 *)(AVALANCHE_TDM_BASE+0x00C4))
#define VOIC_PCMCR2_TDM0_REG                 (*pVOIC_PCMCR2_TDM0_REG)

#else
#error No Architecture specified
#endif

#define     VOIC_PCMCR1_PCM_CLKDIV(x)   (((x) & 0x0fff) << 0)
#define     VOIC_PCMCR1_PCM_FS_PER(x)   (((x) & 0x3fff) << 16)

#define     VOIC_PCMCR2_CODEC_RESET     (1 << 3)
#define     VOIC_PCMCR2_FDIR            (1 << 2)
#if defined(CONFIG_MACH_PUMA5)
#define     VOIC_PCMCR2_CDIR            (1 << 1)
#elif defined(CONFIG_MACH_PUMA6)
#define     VOIC_PCMCR2_CDIR            (1 << 0)
#endif
#define     VOIC_PCMCR2_PSEL            (1 << 0)

#if defined(CONFIG_MACH_PUMA5)

#define pCODEC_SPI_CORSTCR_REG			((volatile UINT32 *)(IO_ADDRESS(0x08611B5CUL)))
#define CODEC_SPI_CORSTCR_REG			(*pCODEC_SPI_CORSTCR_REG)
#define 	CODEC_SPI_CODEC_OUT_OF_RESET        (0xAUL)
#define 	CODEC_SPI_CODEC_IN_RESET            (0x0UL)
#endif

struct spi_device* tiuhal_spi_dev;

struct ti_tid {
	struct spi_device *spi;
	spinlock_t lock;
};

/*Changed from tid_template to tid_template[] for maintaining template per TID*/ 

struct ti_tid 			tid_template[TIUHW_MAX_TIDS];


/**
 * spi_tid_probe - probe function for client driver
 * @this_dev: structure spi_device of SPI Master Controller
 *
 * This function will be called from kernel when match will be successful
 * for SPI controller device and client driver.
 */
static int __devinit spi_tid_probe(struct spi_device *this_dev)
{
	/*Assign the pointer of SPI device so that whenevr in future during 
	open call of this driver it will be assigned to file->privare_data*/
	if(this_dev)
	{
		if(this_dev->chip_select < TIUHW_MAX_TIDS)
		{
			tid_template[this_dev->chip_select].spi = this_dev;
		}
	      else 
              {
		     return -EINVAL;
	       }
	}
	else {
		return -EINVAL;
	}
	/* Initialize bits per word */
	this_dev->bits_per_word = 8;

	return 0;
}

                                                                                                                 
                                                                                                                 
/**
 * spiee_remove - remove function for client driver
 * @this_dev: structure spi_device of SPI Master Controller
 *
 * This function will be called from kernel when client driver will be
 * released from SPI Maser controller.
 */
static int __devexit spi_tid_remove(struct spi_device *this_dev)
{    
    return 0;
}

static struct spi_driver tid_driver = {

	.driver = {
		.name = "tid_0",
	},
	.probe = spi_tid_probe,
	.remove = __devexit_p(spi_tid_remove),
};              

static struct spi_driver tid_driver1 = {

	.driver = {
		.name = "tid_1",
	},
	.probe = spi_tid_probe,
	.remove = __devexit_p(spi_tid_remove),
};              

/*******************************************************************************
* FUNCTION:     tiuhal_spi_drv
*
********************************************************************************
* DESCRIPTION:    Called from tiuhal_spi_write or tiuhal_spi_read
*
*******************************************************************************/
PRIVATE struct spi_device* tiuhal_spi_drv(TCID id)
{
    UINT8 ret_val;

    ret_val = tiuhw_select_tid(id); /*This tiuhw_select_tid(id) returns 0 for id=0,1; returns 1 for id=2,3 */
    if(ret_val < TIUHAL_SPI_MAX_CS)
	{
            if(ret_val ==0 || ret_val ==1) 
           {
	         return tid_template[ret_val].spi;
           }
	    else 
	    {
		printk("tiuhal_spi_drv returning NULL\n");
		return NULL;
	    }	
	}
    return NULL;
}

/*******************************************************************************
* FUNCTION:     tiuhal_spi_init
*
********************************************************************************
* DESCRIPTION:    Initializes the voice interface
*
*******************************************************************************/
PRIVATE TIUHAL_INIT_ERROR_T tiuhal_spi_init(TIUHAL_CAP_T *capabilities, void *options)
{
	static BOOL first_time = TRUE;
	int ret=0;
	int ret1=0;

	if(capabilities)
	{
		memset(capabilities,0,sizeof(TIUHAL_CAP_T));
		capabilities->version = 3;
	}

	if(first_time == TRUE)
	{

#if defined(CONFIG_MACH_PUMA6)
#ifndef CONFIG_INTEL_KERNEL_DOCSIS_SUPPORT
        PAL_sysResetCtrl(CRU_NUM_DOCSIS_MAC0, OUT_OF_RESET);
        PAL_sysResetCtrl(CRU_NUM_DOCSIS_MAC1, OUT_OF_RESET);
#endif
#elif defined(CONFIG_MACH_PUMA5)
#ifndef CONFIG_INTEL_KERNEL_DOCSIS_SUPPORT
		REG32_WRITE(0x08611B68,0x500);		/* docsis mac phycr */
		REG32_WRITE(0x08621a2C,0x103);	    /* DOCSIS_MAC */


		/* Below is configuration of docsis clock */
		REG32_WRITE(0x08611B44,0x0000000A);   
		REG32_WRITE(0x08611B50,0x1);		  
		REG32_WRITE(0x08611B48,0x80000000);

		REG32_WRITE(0x08621120, 0x1);		/* to take effect */
		
		mdelay(1);

		REG32_WRITE(0x09004104,0x22F3D939);	
#endif
		REG32_WRITE(0x0900410C, 0x5);	
#ifndef CONFIG_INTEL_KERNEL_DOCSIS_SUPPORT
		REG32_WRITE(0x09004108,0x00000008);	
#endif
#endif

		/* Clock dividers, and FS settings */

#if defined(CONFIG_MACH_PUMA6)
		/* **TODO - Need to define use cases for codec if configurations */
		VOIC_PCMCR2_TDM0_REG = VOIC_PCMCR2_CDIR;
		VOIC_PCMCR1_TDM0_REG =
			((VOIC_PCMCR1_PCM_FS_PER(TIUHAL_TDM_FS_PER-1)) |
		    (VOIC_PCMCR1_PCM_CLKDIV((TIUHAL_TDM_CLKIN/TIUHAL_TDM_CLKOUT)-1)));
    	VOIC_PCMCR2_TDM1_REG = VOIC_PCMCR2_CDIR;
    	VOIC_PCMCR1_TDM1_REG =
			((VOIC_PCMCR1_PCM_FS_PER(TIUHAL_TDM_FS_PER-1)) |
		    (VOIC_PCMCR1_PCM_CLKDIV((TIUHAL_TDM_CLKIN/TIUHAL_TDM_CLKOUT)-1)));

#elif defined(CONFIG_MACH_PUMA5)
		VOIC_PCMCR2_TDM0_REG = VOIC_PCMCR2_CDIR;
		VOIC_PCMCR1_TDM0_REG =
			((VOIC_PCMCR1_PCM_FS_PER(TIUHAL_TDM_FS_PER-1)) |
            (VOIC_PCMCR1_PCM_CLKDIV((TIUHAL_TDM_CLKIN/TIUHAL_TDM_CLKOUT)-1)));

		REG32_WRITE(0x0861090c, REG32_DATA(0x0861090c)& 0xFC03FFFF );
#endif

        printk(KERN_ERR "TIUHAL: Finished TDM configuration\n");
	
		/* Codec Reset Control Register - set direction to OUT and bring them out of reset */
#if defined(CONFIG_MACH_PUMA6)
        PAL_sysBootCfgCtrl_ReadModifyWriteReg(BOOTCFG_GPCR, BOOTCFG_SCC_RESET_MASK, BOOTCFG_SCC_OUT_OF_RESET_VAL);
        PAL_sysBootCfgCtrl_DocsisIo_SCC_RESET(BOOTCFG_IO_ENABLE);
        /* **FIXME - Do we need to reset other TID device when LPC enabled */
        //PAL_sysBootCfgCtrl_ReadModifyWriteReg(BOOTCFG_GPCR, BOOTCFG_ZDS_RESET_MASK, BOOTCFG_ZDS_OUT_OF_RESET_VAL);
        //PAL_sysBootCfgCtrl_DocsisIo_ZDS(BOOTCFG_IO_ENABLE);
#elif defined(CONFIG_MACH_PUMA5)
        CODEC_SPI_CORSTCR_REG = CODEC_SPI_CODEC_OUT_OF_RESET;
#endif
             ret = spi_register_driver(&tid_driver);
	      if (ret < 0) 
	     {
	          printk(KERN_ERR "TIUHAL: Error [ret=%d]  registering SPI\n", ret);
		   return TIUHAL_INIT_RESOURCE;
             }
		
	     /*Added register for tid_driver1*/ 
	     if (TIUHW_MAX_TIDS > 1)
	    {
                ret1 = spi_register_driver(&tid_driver1);
                if (ret1 <0)
	        {
		      printk(KERN_ERR "TIUHAL: Error [ret1=%d] registering SPI\n", ret1);
		      return TIUHAL_INIT_RESOURCE;
	        }
	    }
           first_time = FALSE;
	}
#ifdef CONFIG_INTEL_KERNEL_DOCSIS_SUPPORT
	printk("TIUHAL using DOCSIS clock source\n");
#else
	printk("TIUHAL using Internal clock source\n");
#endif
    return(TIUHAL_INIT_OK);
}

/*******************************************************************************
 * FUNCTION:    tiuhal_spi_read
 *******************************************************************************
 * DESCRIPTION: Reads a bytestream from a specified register.
 *
 *
 ******************************************************************************/
PRIVATE TIUHAL_IO_ERR_T tiuhal_spi_read(TCID tcid, TIUHAL_REG_T reg, TIUHAL_DATA_T *bytestream, UINT32 length)
{
	UINT8 return_code = 0;
	UINT8 i = 0;
    tiuhal_spi_dev = tiuhal_spi_drv(tcid);
    if(tiuhal_spi_dev == NULL)
    {
	 DRV_ERROR_MSG(0, "NULL SPI driver returned ERROR\n");
        return (TIUHAL_IO_INVALID_TCID);
    }

#ifdef TIUHAL_DEBUG
    DRV_DEBUG_MSG(0,"Calling spi_write_then_read tcid = %x reg=%x, read len=%x\n",tcid, reg,length);         
#endif

	return_code = spi_write(tiuhal_spi_dev, &reg, 1);

	if(return_code != 0)
	{
    	tiuhw_deselect_tid(tcid);
		return TIUHAL_IO_GEN_ERROR;
	}

	for(i = 0; i < length; i++)
	{
		/* The SPI code needs seperate transactions for the CS to be driven high/low */
		return_code = spi_read(tiuhal_spi_dev, &(bytestream[i]), 1);

		if(return_code != 0)
		{
			return TIUHAL_IO_GEN_ERROR;
		}
	}	

#ifdef TIUHAL_DEBUG
    DRV_DEBUG_MSG(0, "Back from spi_write_then_read");         
	for(i=0;i<length;i++)
    {
      DRV_DEBUG_MSG(0,"Read value for i=%d is 0x%x\n",i,bytestream[i]);
    }
#endif

    tiuhw_deselect_tid(tcid);
    return(TIUHAL_IO_OK);
}

/*******************************************************************************
 * FUNCTION:    tiuhal_spi_write
 *******************************************************************************
 * DESCRIPTION: Writes a bytestream from a specified register.
 *
 *
 ******************************************************************************/
PRIVATE TIUHAL_IO_ERR_T tiuhal_spi_write(TCID tcid, TIUHAL_REG_T reg, TIUHAL_DATA_T *bytestream, UINT32 length)
{
	UINT8 return_code = 0;
	UINT32 i;
    tiuhal_spi_dev = tiuhal_spi_drv(tcid);
    if(tiuhal_spi_dev == NULL)
    {
	   	printk(KERN_CRIT "NULL SPI driver returned ERROR\n");
             return (TIUHAL_IO_INVALID_TCID);
    }

	return_code = spi_write(tiuhal_spi_dev, &reg, 1);

	if(return_code != 0)
	{
    	tiuhw_deselect_tid(tcid);
		return TIUHAL_IO_GEN_ERROR;
	}

	for(i= 0; i < length; i++)
	{
		/* The SPI code needs seperate transactions for the CS to be driven high/low */
		return_code = spi_write(tiuhal_spi_dev, &(bytestream[i]), 1);

		if(return_code != 0)
		{
			return TIUHAL_IO_GEN_ERROR;
		}
	}

	tiuhw_deselect_tid(tcid);
	return (TIUHAL_IO_OK);
}

/*******************************************************************************
* FUNCTION:     tiuhal_spi_ioctl
*
********************************************************************************
* DESCRIPTION:    IOCTL functionality (catch all for all other capabilities)
*
*******************************************************************************/
PRIVATE UINT32 tiuhal_spi_ioctl(TCID tcid, UINT32 cmd, void *in_data, void *out_data)
{
    return(0UL); /* do nothing at this time */
}

/*******************************************************************************
* FUNCTION:     tiuhal_spi_teardown
*
********************************************************************************
* DESCRIPTION:    Deallocate any resources that the HAL allocated.
*
*******************************************************************************/
PRIVATE BOOL tiuhal_spi_teardown(void *options)
{

	spi_unregister_driver(&tid_driver);
       if (TIUHW_MAX_TIDS > 1)
      {
	    spi_unregister_driver(&tid_driver1);
      }
	return(TRUE);
}

/*******************************************************************************
 * TYPE:  TIUHAL_API_T      
 *
 *******************************************************************************
 * DESCRIPTION: Generic API's for the voice interfaces - FPGA based or other
 *
 *
 ******************************************************************************/

TIUHAL_API_T tiuhal_spi_api = 
{
	tiuhal_spi_init,	
	tiuhal_spi_read,
	tiuhal_spi_write,
	tiuhal_spi_ioctl,
	tiuhal_spi_teardown
};	

