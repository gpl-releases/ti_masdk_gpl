/*
 * File name: voice_ni.c
 *
 * Description: Voice network kernel module.
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

#include <linux/version.h>
#include <linux/unistd.h>
#include <asm/unistd.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/if.h>

#include <asm/irq.h>


#include "rtxerr.h"
#include "rtxuser.h"
#include "voice_ni.h"

#include <linux/miscdevice.h>
#include <asm/uaccess.h>


#include <generic/pal.h>
#include <generic/pal_cppi41.h>
#include <puma5/puma5_cppi.h>
#include <ti_hil.h>

#define VOICENI_DEBUG 

#ifdef VOICENI_DEBUG
#  define DPRINTK(fmt, args...) printk("%s: " fmt, __FUNCTION__ , ## args)
#else
#  define DPRINTK(fmt, args...)
#endif


#ifdef CONFIG_TI_DEVICE_PROTOCOL_HANDLING
extern int ti_protocol_handler (struct net_device* dev, struct sk_buff *skb);
#endif

/* Master Control Block for Voice Ni network module */
static VOICENID_PRIVATE_T voicenid_mcb;

Cppi4BufPool      bufPool = {BUF_POOL_MGR0,BMGR0_POOL09};

/* structure for voiceni params retrieval via IOCTL */
static VOICENI_PP_CONFIG_T voiceni_pp_config;

/*********************************************************************************
* FUNCTION: voiceni_init_proc
*
**********************************************************************************
*
* DESCRIPTION: Forms output for /proc/voiceni file
*********************************************************************************/
static int voiceni_init_proc(char *buf, char **start, off_t offset,
                   int count, int *eof, void *data)
{
    int len = 0;

    len += sprintf(buf + len, "VoiceNI proc entry\n");
    len += sprintf(buf + len, "===========================\n");
    len += sprintf(buf + len, "QueueMgrId:              %d\n", C55_CPPI4x_FD_QMGR);
    len += sprintf(buf + len, "QueueMgrCount:           %d\n", C55_CPPI4x_FD_Q_COUNT);
    len += sprintf(buf + len, "FreeDescriptorQueueId:   %d\n", C55_CPPI4x_FD_QNUM(0));
    len += sprintf(buf + len, "VoiceNIRXQueueId:        %d\n", C55_CPPI4x_RX_QNUM(0));
    len += sprintf(buf + len, "VoiceNITXQueueId:        %d\n", PPFW_CPPI4x_TX_EGRESS_EMB_QNUM(PAL_CPPI4x_PRTY_HIGH));

    len += sprintf(buf + len, "DSPRXQueueId:            %d\n", DSP_CCPI4X_RX_QNUM);
    len += sprintf(buf + len, "DSPTXQueueId:            %d\n", PPFW_CPPI4x_RX_INGRESS_QNUM(PAL_CPPI4x_PRTY_HIGH));
    len += sprintf(buf + len, "DSPCPPPI41SrcPortId:     %d\n", CPPI41_SRCPORT_C55);
    len += sprintf(buf + len, "BufferPoolMrgId:         %d\n", BUF_POOL_MGR0);
    len += sprintf(buf + len, "BufferPoolNumId:         %d\n", BMGR0_POOL09);
    len += sprintf(buf + len, "BufferSlotCount:         %d\n", VOICENI_CPPI4x_MAX_BUF_SLOT);
    len += sprintf(buf + len, "BufferDescriptorSize:    %d\n", VOICENI_CPPI4x_EMB_BD_SIZE);

    *eof = 1;
    
    return len;
}


/*********************************************************************************
* FUNCTION: voiceni_stats_proc
*
**********************************************************************************
*
* DESCRIPTION: Forms output for /proc/voiceni_stats file
*********************************************************************************/
static int voiceni_stats_proc(char *buf, char **start, off_t offset,
                   int count, int *eof, void *data)
{
    
    int len = 0;
    VOICENID_PRIVATE_T* priv  = (VOICENID_PRIVATE_T*)data;
    
    len += sprintf(buf + len, "Voice Network Interface Stats:\n");
    len += sprintf(buf + len, "=============================================\n");
    len += sprintf(buf + len, "Accum/RX Interrupts:                     %d\n", priv->rx_interrupt_count);
    len += sprintf(buf + len, "RX packets/BDs (packets from DSP)        %d\n", priv->rx_bd_count);
    len += sprintf(buf + len, "RX packets/BDs with null virtBD pointer  %d\n", priv->rx_bd_with_virtBD_null);
    len += sprintf(buf + len, "RX buffers freed (buffers from DSP)      %d\n", priv->rx_buffers_freed);

    len += sprintf(buf + len, "TX packets to network:                   %d\n", (int)priv->stats.tx_packets);

    len += sprintf(buf + len, "Packets from network:                    %d\n", priv->rx_skb_count);    
    len += sprintf(buf + len, "BufferPool alloc failure:                %d\n", priv->buff_pop_failure);
    len += sprintf(buf + len, "BD Queue alloc failure:                  %d\n", priv->bd_queue_pop_failure);
    len += sprintf(buf + len, "TX bytes to network:                     %d\n", (int)priv->stats.tx_bytes);
    len += sprintf(buf + len, "RX packets from network:                 %d\n", (int)priv->stats.rx_packets);
    len += sprintf(buf + len, "RX bytes fom network:                    %d\n", (int)priv->stats.rx_bytes);
    len += sprintf(buf + len, "TX BD count                              %d\n", priv->tx_bd_count);

    *eof = 1;
    return len;
}

/*********************************************************************************
* FUNCTION: voiceni_stats_proc
*
**********************************************************************************
*
* DESCRIPTION:  Tasklet routine to process packets sent by DSP via the PP. Runs in 
*               context of voiceni_rx_isr
*
*********************************************************************************/
static void voiceni_rx_processing(unsigned long data)
{
    struct net_device *dev  = (struct net_device*)data;
    VOICENID_PRIVATE_T *priv = netdev_priv(dev); 
    Cppi4EmbdDesc *virtBD, *phyBD;
    struct sk_buff *newskb;

    Cppi4BufPool pool;
    PAL_Cppi4QueueHnd free_pp_bd_queue_hnd;
    
    while (avalanche_intd_get_interrupt_count(VOICENI_INTD_HOST_NUM, C55_ACC_RX_CHNUM))
    {
        while ((phyBD = (Cppi4EmbdDesc*)((unsigned long)*priv->rxAcc_chan_list[0] & QMGR_QUEUE_N_REG_D_DESC_ADDR_MASK)))
        {
            unsigned int vBuffPtr;
            priv->rx_bd_count++;
            virtBD = (Cppi4EmbdDesc*) IO_PHY2VIRT((UINT32)phyBD);;

            if (virtBD == NULL)
            {
                 printk("voiceni_rx_processing, virtual BD is NULL, physical BD address: 0x%x\n",
                        (UINT32)phyBD);
                if (priv->recycle_queue_hnd)
                    PAL_cppi4QueuePush (priv->recycle_queue_hnd,
                              phyBD, 
                              PAL_CPPI4_DESCSIZE_2_QMGRSIZE(CPMAC_CPPI4x_RX_EMB_BD_SIZE), 0);
               
                priv->rxAcc_chan_list[0]++;
                priv->rx_bd_with_virtBD_null++;
                break;
            }
            else
            {
                PAL_CPPI4_CACHE_INVALIDATE(virtBD, VOICENI_CPPI4x_EMB_BD_SIZE); 
            }
            vBuffPtr = avalanche_no_OperSys_memory_phys_to_virt(virtBD->Buf[1].BufPtr);
            PAL_CPPI4_CACHE_INVALIDATE(vBuffPtr, (virtBD->Buf[1].BufInfo &CPPI41_EM_DESCINFO_PKTLEN_MASK));

            if(!(memcmp((void*)vBuffPtr, (void*)vBuffPtr+ETH_ALEN, ETH_ALEN)))
            {
                priv->tx_bd_count++;
                PAL_CPPI4_CACHE_WRITEBACK(virtBD, CPPI4_BD_LENGTH_FOR_CACHE);
                virtBD->EPI[1] = DSP_CCPI4X_RX_QNUM;
                PAL_cppi4QueuePush (priv->voiceni_tx_queue,
                                    phyBD,
                                    PAL_CPPI4_DESCSIZE_2_QMGRSIZE(VOICENI_CPPI4x_EMB_BD_SIZE), 
                                    virtBD->Buf[1].BufInfo &CPPI41_EM_DESCINFO_PKTLEN_MASK); 
            }
            else
            {
                newskb = dev_alloc_skb(MAX_SKB_SIZE);
                if(virtBD->EPI[1])
                {
                    memcpy(newskb->pp_packet_info.ti_epi_header, &(virtBD->EPI[0]), TI_EPI_HEADER_LEN);
                    newskb->pp_packet_info.ti_pp_flags = TI_PPM_SESSION_INGRESS_RECORDED;
                }
                memcpy(newskb->data, (void*)vBuffPtr, virtBD->Buf[1].BufInfo &CPPI41_EM_DESCINFO_PKTLEN_MASK);
              
                skb_put(newskb, virtBD->Buf[1].BufInfo &CPPI41_EM_DESCINFO_PKTLEN_MASK);
                newskb->mac.raw = newskb->data;

#ifdef CONFIG_TI_DEVICE_PROTOCOL_HANDLING
                newskb->dev = dev;;
                /* Pass the packet to the device specific protocol handler */
                if (ti_protocol_handler (newskb->dev, newskb) < 0)
                {
                    /* Device Specific Protocol handler has "captured" the packet
                    * and does not want to send it up the networking stack; so 
                    * return immediately.after freeing bd/buffer*/
                    /* Now place Buffer Back to buffer pool */
                    pool.bPool= (virtBD->Buf[1].BufInfo & CPPI41_EM_BUF_POOL_MASK) >>CPPI41_EM_BUF_POOL_SHIFT;
                    pool.bMgr = (virtBD->Buf[1].BufInfo  & CPPI41_EM_BUF_MGR_MASK) >>  CPPI41_EM_BUF_MGR_SHIFT;
               
                    PAL_cppi4BufDecRefCnt(priv->pal_hnd, pool, (Ptr)(virtBD->Buf[1].BufPtr));
                    priv->rx_buffers_freed++;

                    /* Now place BD back to Free Embedded Queue */
                    Cppi4Queue queue;
                    queue.qMgr = 0;
                    queue.qNum = virtBD->pktInfo & CPPI41_EM_PKTINFO_RETQ_MASK;
                    free_pp_bd_queue_hnd  =  PAL_cppi4QueueOpen(priv->pal_hnd, queue);
            
                    PAL_cppi4QueuePush (free_pp_bd_queue_hnd,
                              phyBD, 
                              PAL_CPPI4_DESCSIZE_2_QMGRSIZE(VOICENI_CPPI4x_EMB_BD_SIZE), 0);
                    PAL_cppi4QueueClose(priv->pal_hnd, free_pp_bd_queue_hnd);
                    return;
                }
                else
#endif
                {
                    newskb->dev = priv->ptr_etmani_device;
                    dev_queue_xmit(newskb);
                    priv->stats.tx_packets++;
                    priv->stats.tx_bytes += newskb->len;
                }
                
                /* Now place Buffer Back to buffer pool */
                pool.bPool= (virtBD->Buf[1].BufInfo & CPPI41_EM_BUF_POOL_MASK) >>CPPI41_EM_BUF_POOL_SHIFT;
                pool.bMgr = (virtBD->Buf[1].BufInfo  & CPPI41_EM_BUF_MGR_MASK) >>  CPPI41_EM_BUF_MGR_SHIFT;
               
                PAL_cppi4BufDecRefCnt(priv->pal_hnd, pool, (Ptr)(virtBD->Buf[1].BufPtr));
                priv->rx_buffers_freed++;

                /* Now place BD back to Free Embedded Queue */
                Cppi4Queue queue;
                queue.qMgr = 0;
                queue.qNum = virtBD->pktInfo & CPPI41_EM_PKTINFO_RETQ_MASK;
                free_pp_bd_queue_hnd  =  PAL_cppi4QueueOpen(priv->pal_hnd, queue);
            
                PAL_cppi4QueuePush (free_pp_bd_queue_hnd,
                              phyBD, 
                              PAL_CPPI4_DESCSIZE_2_QMGRSIZE(VOICENI_CPPI4x_EMB_BD_SIZE), 0);
                PAL_cppi4QueueClose(priv->pal_hnd, free_pp_bd_queue_hnd);
            }
            priv->rxAcc_chan_list[0]++;
        }

        /* Update the list entry for next time */        
        priv->rxAcc_chan_list[0] = PAL_cppi4AccChGetNextList(priv->acc_hnd);
        avalanche_intd_set_interrupt_count (0, C55_ACC_RX_CHNUM, 1);
    }

    avalanche_intd_write_eoi (C55_ACC_RX_INTV);

}

/*******************************************************************************
* FUNCTION:    voiceni_rx_isr
*
********************************************************************************
*
* DESCRIPTION: voiceni receive isr routine
* 
* RETURN:     0 if successful.
*
*******************************************************************************/
static irqreturn_t voiceni_rx_isr (int irq, void *context, struct pt_regs *regs)
{
    VOICENID_PRIVATE_T* priv  = ((VOICENID_PRIVATE_T*)context);
    priv->rx_interrupt_count++;
    tasklet_schedule(&(priv->tx_tasklet));

    return IRQ_RETVAL(1);
}


/*******************************************************************************
* FUNCTION:    init_voiceni_ccpi4_accum_channel
*
********************************************************************************
*
* DESCRIPTION: Initialize and open voiceni accumulator channel
* 
* RETURN:     0 if successful.
*
*******************************************************************************/
static int init_voiceni_ccpi4_accum_channel(VOICENID_PRIVATE_T *priv)
{
    Cppi4Queue                   voiceni_rx_queue = {0,C55_CPPI4x_RX_QNUM(0)};
    Cppi4AccumulatorCfg      cfg;
    
    cfg.accChanNum             = C55_ACC_RX_CHNUM;
    cfg.queue                  = voiceni_rx_queue;
    cfg.mode                   = 0;
    cfg.list.maxPageEntry      = VOICENI_ACC_PAGE_NUM_ENTRY;
    cfg.list.listEntrySize     = VOICENI_ACC_ENTRY_TYPE;  /* Only interested in register 'D' which has the desc pointer */
    cfg.list.listCountMode     = 0;                              /* Zero indicates null terminated list. */
    cfg.list.pacingMode        = 1;                              /* dont Wait for time since last interrupt, should we wait delay will be
                                                                          at most 1 ms/packet, tm: changes from 0 to 3*/
    cfg.pacingTickCnt          = 40;                             /* Wait for 1000uS == 1ms, changed from 40 to 0 */
    cfg.list.maxPageCnt        = VOICENI_ACC_NUM_PAGE;                            /* Use two pages */
    cfg.list.stallAvoidance    = 1;                              /* Use the stall avoidance feature */

    if(!(cfg.list.listBase = kzalloc(VOICENI_ACC_LIST_BYTE_SZ, GFP_KERNEL)))
    {
        DPRINTK(" init_voiceni_ccpi4_accum_channel: Unable to allocate list page\n");
        return -1;
    }
    else
         DPRINTK(" init_voiceni_ccpi4_accum_channel: Able to allocate list page\n");
    /* make sure memory allocated in kzalloc which is probably cached is actually written to 
    actual memory by doging cache writeback */
    PAL_CPPI4_CACHE_WRITEBACK((unsigned long)cfg.list.listBase, VOICENI_ACC_LIST_BYTE_SZ);

    cfg.list.listBase = (Ptr) PAL_CPPI4_VIRT_2_PHYS((Ptr)cfg.list.listBase);

    if(!(priv->acc_hnd = PAL_cppi4AccChOpen(priv->pal_hnd, &cfg)))
    {
        DPRINTK("VOICE NI module, Unable to open accumulator channel\n");
        kfree(cfg.list.listBase);
        return -1;
    }

    priv->rxAcc_chan_list_base[0] = priv->rxAcc_chan_list[0] = PAL_cppi4AccChGetNextList(priv->acc_hnd);

    if(request_irq (VOICENI_RXINT_NUM, voiceni_rx_isr, IRQF_DISABLED, VOICENI_DEV_NAME,(void*) priv))
    {
        DPRINTK("VOICE NI module, Unable to get IRQ\n");
        return -1;
    }
    
    return 0;
    
}


/*******************************************************************************
* FUNCTION:    init_voiceni_free_bd_queue
*
********************************************************************************
*
* DESCRIPTION: Initialize and open voiceni free buffer descriptor queue
* 
* RETURN:     0 if successful.
*
*******************************************************************************/
static int init_voiceni_free_bd_queue(VOICENID_PRIVATE_T* priv)
{
    Cppi4Queue          free_bd_queue = {C55_CPPI4x_FD_QMGR,C55_CPPI4x_FD_QNUM(0)};
    
    priv->free_bd_queue =  PAL_cppi4QueueOpen(priv->pal_hnd, free_bd_queue);
    if (priv->free_bd_queue == NULL)
    {
        DPRINTK("VOICE NI module, Free BD Queue Handle is NULL\n");
        return -1;
    }
    else 
    {
        DPRINTK("VOICE NI module, Free BD Queue Handle is VALID, free bd queue: %x\n",
            (int)priv->free_bd_queue);
        return 0;
    }
}

/*******************************************************************************
* FUNCTION:    init_voiceni_free_bd_pool
*
********************************************************************************
*
* DESCRIPTION:  Initialize and open voiceni free buffer descriptor pool, initialize 
*               all buffer descriptors
* 
* RETURN:     0 if successful.
*
*******************************************************************************/
static int init_voiceni_free_bd_pool(VOICENID_PRIVATE_T* priv)
{
    int i_bd;
    int b_slot;
    priv->free_bd_pool =  PAL_cppi4AllocDesc(priv->pal_hnd, 0, VOICENI_CPPI4x_EMB_BD_COUNT, VOICENI_CPPI4x_EMB_BD_SIZE);

    if ( priv->free_bd_pool == NULL)
    {
        return -1;
    }
    else
    {
        DPRINTK("VOICE NI module, Free BD Pool Handle is VALID\n");
        DPRINTK("VOICE NI Fill free discriptor queue\n");
        DPRINTK("****init_voiceni_free_bd_pool, buffer descriptor pool base address: %x\n",
                PAL_CPPI4_VIRT_2_PHYS(priv->free_bd_pool)); 
        for (i_bd = 0; i_bd < VOICENI_CPPI4x_EMB_BD_COUNT; i_bd++) 
        {
            Cppi4EmbdDesc *bd = (Cppi4EmbdDesc *)GET_VOICENI_BD_PTR(priv->free_bd_pool, i_bd);
            
        
            PAL_osMemSet(bd, 0, VOICENI_CPPI4x_EMB_BD_SIZE);
            /* descInfo correlates to seciotn 2.2.3.1, page 17 */
            bd->descInfo =
            CPPI41_EM_DESCINFO_DTYPE_EMBEDDED |
            CPPI41_EM_DESCINFO_SLOTCNT_MYCNT;
            bd->tagInfo      =  (CPPI41_SRCPORT_C55<< CPPI41_EM_TAGINFO_SRCPORT_SHIFT) 
                              | (0x3fff << CPPI41_EM_TAGINFO_DSTTAG_SHIFT);
            /* pktInfo correlates to section 2.2.3.3, page 18 of cppi specification */
            bd->pktInfo  = (PAL_CPPI4_HOSTDESC_PKT_TYPE_ETH << CPPI41_EM_PKTINFO_PKTTYPE_SHIFT) /*26 */
                                        | (CPPI41_EM_PKTINFO_RETPOLICY_RETURN) /*15 */
                                        |(C55_CPPI4x_FD_QMGR << CPPI41_EM_PKTINFO_RETQMGR_SHIFT) /* 12 */
                                        | (1 << CPPI41_EM_PKTINFO_EOPIDX_SHIFT) /* 20 */
                                        | (0 << CPPI41_EM_PKTINFO_ONCHIP_SHIFT) /* 14 */
                                        | (C55_CPPI4x_FD_QNUM(0) << CPPI41_EM_PKTINFO_RETQ_SHIFT); /* 0 */
            


            bd->Buf[0].BufInfo =    (1 <<CPPI41_EM_BUF_VALID_SHIFT) |
                                                (BUF_POOL_MGR0 << CPPI41_EM_BUF_MGR_SHIFT)|
                                                (BMGR0_POOL09 << CPPI41_EM_BUF_POOL_SHIFT);
            
            bd->Buf[1].BufInfo =    (1 <<CPPI41_EM_BUF_VALID_SHIFT)|
                                                (BUF_POOL_MGR0 << CPPI41_EM_BUF_MGR_SHIFT)|
                                                (BMGR0_POOL09 << CPPI41_EM_BUF_POOL_SHIFT);

            /* Invalidate other 2 buffer slots, only 1st buffer slot will be used, 2nd slot still
                needs to be validated */
            for (b_slot = 2; b_slot < VOICENI_CPPI4x_MAX_BUF_SLOT;  b_slot++)
            {
                bd->Buf[b_slot].BufInfo = (0 << CPPI41_EM_BUF_VALID_SHIFT);
            }
            PAL_CPPI4_CACHE_WRITEBACK(bd, CPPI4_BD_LENGTH_FOR_CACHE);
            PAL_cppi4QueuePush (priv->free_bd_queue,
                                (Ptr) PAL_CPPI4_VIRT_2_PHYS(bd), 
                                PAL_CPPI4_DESCSIZE_2_QMGRSIZE(VOICENI_CPPI4x_EMB_BD_SIZE),
                                0);
        }
    }

    return 0;
}


/*******************************************************************************
* FUNCTION:    init_voiceni_ccpi41_queues
*
********************************************************************************
*
* DESCRIPTION:  Initialize and open voiceni transmit and receive cppi queues
* 
* RETURN:     0 if successful.
*
*******************************************************************************/
static int init_voiceni_ccpi41_queues(VOICENID_PRIVATE_T *priv)
{
    Cppi4Queue		   voiceni_rx_queue = {0, C55_CPPI4x_RX_QNUM(0)};
    Cppi4Queue		   voiceni_tx_queue = {0, PPFW_CPPI4x_TX_EGRESS_EMB_QNUM(PAL_CPPI4x_PRTY_HIGH)};

    /* Init the CCPI RX queue for voice NI */
    priv->voiceni_rx_queue =  PAL_cppi4QueueOpen(priv->pal_hnd, voiceni_rx_queue);
    if (priv->voiceni_rx_queue == NULL)
         DPRINTK("VOICE NI module, Voice NI TX Queue Handle is NULL\n");
    else 
        DPRINTK("VOICE NI module, Voice NI RX Queue Handle is VALID\n");

    /* Now init the CCPI TX queue for voice NI */
    priv->voiceni_tx_queue =  PAL_cppi4QueueOpen(priv->pal_hnd, voiceni_tx_queue);
    if (priv->voiceni_rx_queue == NULL)
        DPRINTK("VOICE NI module, Voice NI TX Queue Handle is NULL\n");
    else 
        DPRINTK("VOICE NI module, Voice NI TX Queue Handle is VALID\n");

    /* for now return 0 */
    return 0;
}

/*******************************************************************************
* FUNCTION:    init_dsp_ccpi41_queues
*
********************************************************************************
*
* DESCRIPTION:  Initialize and open DSP transmit and receive cppi queues
* 
* RETURN:     0 if successful.
*
*******************************************************************************/
static int init_dsp_ccpi41_queues(VOICENID_PRIVATE_T *priv)
{
    Cppi4Queue      dsp_rx_queue = {0,DSP_CCPI4X_RX_QNUM};
    Cppi4Queue      dsp_tx_queue = {0,PPFW_CPPI4x_RX_INGRESS_QNUM(PAL_CPPI4x_PRTY_HIGH)};

    /* Init the CCPI RX queue for DSP */
    priv->dsp_rx_queue =  PAL_cppi4QueueOpen(priv->pal_hnd, dsp_rx_queue);
    if (priv->dsp_rx_queue == NULL)
        DPRINTK("VOICE NI module, DSP RX Queue Handle is NULL\n");
    else 
        DPRINTK("VOICE NI module, DSP RX Queue Handle is VALID\n");


    /* Now init the CCPI TX queue for DSP */
    priv->dsp_tx_queue  =  PAL_cppi4QueueOpen(priv->pal_hnd, dsp_tx_queue);
    if (priv->dsp_tx_queue  == NULL)
        DPRINTK("VOICE NI module, DSP TX Queue Handle is NULL\n");
    else 
        DPRINTK("VOICE NI module, DSP TX Queue Handle is VALID\n");

    /* for now return 0 */
    return 0;
} 

/*******************************************************************************
* FUNCTION:    voiceni_open
*
********************************************************************************
*
* DESCRIPTION:  Routine to create voiceni/DSP PID range and PID
* 
* RETURN:     0 if successful.
*
*******************************************************************************/
static int voiceni_create_pid(struct net_device *dev)
{

    VOICENID_PRIVATE_T * priv = netdev_priv(dev);     

    TI_PP_PID voiceni_pid;
    TI_PP_PID_RANGE voiceni_pid_range;
    memset(&voiceni_pid, 0, sizeof(TI_PP_PID));
    /* Need to 1st create create_pid_range */
    memset(&voiceni_pid, 0, sizeof(TI_PP_PID_RANGE));

    voiceni_pid_range.base_index = PP_C55_PID_BASE;
    voiceni_pid_range.count = PP_C55_PID_COUNT;
    voiceni_pid_range.port_num = CPPI41_SRCPORT_C55;
    voiceni_pid_range.type = TI_PP_PID_TYPE_INFRASTRUCTURE;

    if (ti_ppm_config_pid_range(&voiceni_pid_range) != 0)
        DPRINTK("ti_ppm_config_pid_range() error\n");
    else
        DPRINTK("ti_ppm_config_pid_range() success\n");	


    /* populate TI_PP_PID struct */
    voiceni_pid.type = TI_PP_PID_TYPE_INFRASTRUCTURE;
    voiceni_pid.ingress_framing = TI_PP_PID_TYPE_ETHERNET;
    voiceni_pid.dflt_pri_drp    = 0;
    voiceni_pid.dflt_dst_tag    = 0x3FFF; /* 0x3FFF implies none, what does this mean */
    voiceni_pid.dflt_fwd_q      = C55_CPPI4x_RX_QNUM(0);
    voiceni_pid.pri_mapping     = 1;    /* Num prio Qs for fwd, we have only 1*/
    voiceni_pid.tx_pri_q_map[0] = DSP_CCPI4X_RX_QNUM; /* default Q for Egress, PP to DSP*/
    voiceni_pid.tx_hw_data_len  = 0;  /* dont fill this field */
    voiceni_pid.pid_handle      = PP_C55_PID_BASE;
    priv->pid_hnd = ti_ppm_create_pid(&voiceni_pid);
    dev->pid_handle = priv->pid_hnd;


    /* Preparations for VPID creation */
    dev->vpid_block.type               = TI_PP_ETHERNET;   
    dev->vpid_block.parent_pid_handle  = dev->pid_handle;
    dev->vpid_block.egress_mtu         = 0;
    dev->vpid_block.priv_tx_data_len   = 0;

    DPRINTK("voiceni_create_pid() handle returned %d\n", priv->pid_hnd);
    return 0;
}

/*******************************************************************************
* FUNCTION:    voiceni_open
*
********************************************************************************
*
* DESCRIPTION:  VoiceNI Device Open routine
* 
* RETURN:     0 if successful.
*
*******************************************************************************/
static int voiceni_open (struct net_device *dev)
{
    VOICENID_PRIVATE_T *priv =  netdev_priv(dev);
    Cppi4Queue recycleQ;

    /* Initializing resources */
    init_voiceni_free_bd_queue(priv);

    init_voiceni_free_bd_pool(priv);

    
    /* Init voiceNI CCPI41 queues */
    init_voiceni_ccpi41_queues(priv);

    /* Init DSP CCPI41 queues */
    init_dsp_ccpi41_queues(priv);

    /* Init the accumulator channel */
    init_voiceni_ccpi4_accum_channel(priv);

    recycleQ.qMgr   = RECYCLE_INFRA_RX_QMGR;
    recycleQ.qNum   = RECYCLE_INFRA_RX_Q(0);
    priv->recycle_queue_hnd = PAL_cppi4QueueOpen(priv->pal_hnd, recycleQ);
    if (priv->recycle_queue_hnd == NULL)
        DPRINTK("VOICE NI module, Voice Recycle Queue Handle is NULL\n");
    
    tasklet_init(&(priv->tx_tasklet), voiceni_rx_processing, (unsigned long) dev);
    netif_start_queue (dev); 

    
    ti_hil_pp_event (TI_BRIDGE_PORT_FORWARD, (void *)dev);

    

    return 0;
}

/*******************************************************************************
* FUNCTION:    voiceni_open
*
********************************************************************************
*
* DESCRIPTION:  VoiceNI Device Close routine
* 
* RETURN:     0 if successful.
*
*******************************************************************************/
static int voiceni_close (struct net_device *dev)
{
    VOICENID_PRIVATE_T *priv = netdev_priv(dev);

    disable_irq(VOICENI_RXINT_NUM);
    PAL_cppi4AccChClose(priv->acc_hnd, NULL);

    DPRINTK("voiceni_close(): calling netif_stop_queue(), rx interrupt count%d\n", priv->rx_interrupt_count);
    netif_stop_queue(dev);

    /*close vni cppi queues */
    PAL_cppi4QueueClose(priv->pal_hnd, priv->voiceni_rx_queue);
    PAL_cppi4QueueClose(priv->pal_hnd, priv->voiceni_tx_queue);
    /* clse dsp cppi queues */
    PAL_cppi4QueueClose(priv->pal_hnd, priv->dsp_rx_queue);
    PAL_cppi4QueueClose(priv->pal_hnd, priv->dsp_tx_queue);

    PAL_cppi4QueueClose(priv->pal_hnd, priv->recycle_queue_hnd);

    /* close Free BD queue */
    PAL_cppi4QueueClose(priv->pal_hnd, priv->free_bd_queue);
    /* Dealloc DB Descriptors */
    PAL_cppi4DeallocDesc( priv->pal_hnd, PAL_CPPI41_QUEUE_MGR0, priv->free_bd_pool);
    
    if (priv->rxAcc_chan_list_base[0])
        kfree (priv->rxAcc_chan_list_base[0]);

    free_irq(VOICENI_RXINT_NUM, priv);

    
    ti_hil_pp_event (TI_BRIDGE_PORT_DISABLED, (void *)dev);
    
    return 0;
}


/*******************************************************************************
* FUNCTION:    voiceni_start_xmit
*
********************************************************************************
*
* DESCRIPTION:  voiceni transmit routine. Called by MTANI when it needs to 
*               transmit packet to voiceni.
* 
* RETURN:     0 if successful.
*
*******************************************************************************/
static int voiceni_start_xmit (struct sk_buff *skb, struct net_device *dev)
{
    Cppi4EmbdDesc *virtBD, *phyBD;
    Ptr pBuffPtr;
    unsigned int   queueNum = DSP_CCPI4X_RX_QNUM;
    VOICENID_PRIVATE_T *priv = netdev_priv(dev);


    priv->rx_skb_count++;

    /* Need to allocate free BD and buffer*/
    pBuffPtr = PAL_cppi4BufPopBuf(priv->pal_hnd, bufPool);
    if (pBuffPtr == NULL)
    {
        priv->buff_pop_failure++;
        priv->stats.rx_dropped++;
        dev_kfree_skb(skb);
        return 0;
    }
    
    phyBD = (Cppi4EmbdDesc*) PAL_cppi4QueuePop(priv->free_bd_queue);
    if (phyBD == NULL)
    {
        DPRINTK("voiceni_start_xmit() physical bd is NULL\n");
        
    }
    virtBD = PAL_CPPI4_PHYS_2_VIRT(phyBD);

     /* copy skb into buffer and update BD if required */
    if (virtBD != NULL)
    {
        memcpy( (void*)avalanche_no_OperSys_memory_phys_to_virt((unsigned int)pBuffPtr), skb->data, skb->len);
        virtBD->Buf[1].BufInfo = 
            CPPI41_EM_BUF_VALID_MASK | 
            (BUF_POOL_MGR0 <<CPPI41_EM_BUF_MGR_SHIFT) |
            (BMGR0_POOL09<<CPPI41_EM_BUF_POOL_SHIFT) |
             skb->len;
        virtBD->Buf[1].BufPtr = (UINT32)pBuffPtr;
        virtBD->descInfo |= (skb->len) << CPPI41_EM_DESCINFO_PKTLEN_SHIFT;
        
        virtBD->descInfo |=
                                        CPPI41_EM_DESCINFO_DTYPE_EMBEDDED |
                                        CPPI41_EM_DESCINFO_SLOTCNT_MYCNT;
        virtBD->EPI[1] = queueNum; /* to remove */

        PAL_CPPI4_CACHE_WRITEBACK(avalanche_no_OperSys_memory_phys_to_virt((unsigned int)pBuffPtr), skb->len);
    }
    else
    {
        
        priv->bd_queue_pop_failure++;
        priv->stats.rx_dropped++;

        PAL_cppi4BufDecRefCnt(priv->pal_hnd, bufPool, pBuffPtr);
        dev_kfree_skb(skb);
        return 0;
    }

    /* update the queueNm as well */
    /* Set SYNC Q PTID info in egress descriptor */
    if(skb->pp_packet_info.ti_pp_flags == TI_PPM_SESSION_INGRESS_RECORDED)
    {
        memcpy(&(virtBD->EPI[0]), skb->pp_packet_info.ti_epi_header, TI_EPI_HEADER_LEN);
        virtBD->EPI[1] &= ~(0xFFFF);
        virtBD->EPI[1] |= queueNum; 
    }
    else
    {
        virtBD->EPI[1] = queueNum; 
    }

    /* push BD and buffer onto PP receive Q destined for DSP */
    priv->stats.rx_packets++;
    priv->stats.rx_bytes += skb->len;
    priv->tx_bd_count++;
    PAL_CPPI4_CACHE_WRITEBACK(virtBD, VOICENI_CPPI4x_EMB_BD_SIZE);
    PAL_cppi4QueuePush (priv->voiceni_tx_queue,
                        (Ptr) PAL_CPPI4_VIRT_2_PHYS(virtBD),
                        PAL_CPPI4_DESCSIZE_2_QMGRSIZE(VOICENI_CPPI4x_EMB_BD_SIZE),
                        skb->len);
    dev_kfree_skb(skb);
    return 0;
}

static  struct net_device_stats *voiceni_get_stats (struct net_device *dev)
{
    VOICENID_PRIVATE_T *priv = netdev_priv(dev); 
    return &(priv->stats);
}
static int voiceni_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
    int err = -EFAULT;
    void __user *addr = (void __user *) ifr->ifr_ifru.ifru_data;

    switch (cmd)
    {
        case SIOCGETVOICENI_PARAMS:
        if (copy_to_user(addr, &voiceni_pp_config, sizeof(voiceni_pp_config)))
            break;
        err = 0;

        break;
    default:
        err = -EINVAL;
    }
    return err;
}

void voiceni_netdev_setup(struct net_device *ptr_netdev)
{
    DPRINTK("voiceni_netdev_setup being called for  for VOICENI network device\n");
    ptr_netdev->open = voiceni_open;
    ptr_netdev->hard_start_xmit = voiceni_start_xmit;
    ptr_netdev->stop = voiceni_close;
    ptr_netdev->get_stats = voiceni_get_stats;
    ptr_netdev->do_ioctl= voiceni_ioctl;
    /* setup up generic ethernet property fields in net device structure */
    ether_setup(ptr_netdev);
   return;
}
static int voiceni_netdev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
    struct net_device *dev = ptr;
    VOICENID_PRIVATE_T * priv; 

    DPRINTK("voiceni_netdev_event, got event: %d\n", (int)event);

    /* Only care about events for CONFIG_TI_PACM_MTA_INTERFACE net device */
    if (strcmp(dev->name, CONFIG_TI_PACM_MTA_INTERFACE) != 0)
        return 0;
     priv = netdev_priv(voicenid_mcb.net_dev.ptr_device);
    /* at this point, you have a newly registered/unregistered device */
    if (event == NETDEV_UP) 
    {
        
        DPRINTK("voiceni_netdev_event, interface %s is up\n", dev->name);
        priv->ptr_etmani_device = dev_get_by_name(CONFIG_TI_PACM_MTA_INTERFACE);
        return 0;
    }
    
    if (event == NETDEV_DOWN)
    {
        /*unregister_netdevice...
        however it is that you free... */
         priv->ptr_etmani_device = NULL;
        DPRINTK("voiceni_netdev_event, interface %s is down\n", dev->name);
        return 0;
    }
    return 0;
}

static struct notifier_block voiceni_netdev_notifier = {
                .notifier_call = voiceni_netdev_event,
};

/*********************************************************************************
* FUNCTION: voiceni_init_module
*
* DESCRIPTION:Initialization routine for voice network interface kernel device
*********************************************************************************/
static int __init voiceni_init_module(void)
{
    struct net_device*       netdev;
    VOICENID_PRIVATE_T * priv; 
    int ret;
    
    
     DPRINTK("voiceni_init_module: calling alloc_netdev for VOICNI network device\n");
    /* Allocate memory for the network device. */
    netdev = alloc_netdev(sizeof(VOICENID_PRIVATE_T), VOICENI_DEV_NAME, voiceni_netdev_setup);
    if(netdev == NULL)
    {
        DPRINTK(KERN_ERR "Unable to allocate memory for the VOICNI network device\n");
        return -ENOMEM;
    }
      /* Register the network device */ 
    ret = register_netdev (netdev);
    if(ret) 
    {
        DPRINTK(KERN_DEBUG "Unable to register device named %s (%p)...\n", netdev->name, netdev);            
        return ret;
    }
    DPRINTK(KERN_DEBUG "Registered device named %s (%p)...\n", netdev->name, netdev);  
  
    /* need handle to private data of net device */   
    priv = netdev_priv(netdev);
    voicenid_mcb.net_dev.ptr_device= netdev;

    /* Get handle to platform abrastraction layer (PAL) CPPI instance */
    if ((priv->pal_hnd= PAL_cppi4Init(NULL, NULL)) == NULL)
    {
        DPRINTK(KERN_WARNING "voiceni_init_module: PAL CPPI is not initialized yet \n");
        return(-EAGAIN); /* need to check return value */
    }

    /* Create PID and VPID, moved from above PAL_cppi4Init*/
    voiceni_create_pid(netdev);

    voiceni_pp_config.cppi_queue_mgr_id = C55_CPPI4x_FD_QMGR;
    voiceni_pp_config.cppi_bd_queue_id = C55_CPPI4x_FD_QNUM(0);
    voiceni_pp_config.buffer_pool_mgr_id = BUF_POOL_MGR0;
    voiceni_pp_config.buffer_pool_id = BMGR0_POOL09;
    voiceni_pp_config.cppi_rx_queue_id = DSP_CCPI4X_RX_QNUM;
    voiceni_pp_config.cppi_tx_queue_id = PPFW_CPPI4x_RX_INGRESS_QNUM(PAL_CPPI4x_PRTY_HIGH);
    voiceni_pp_config.pid = CPPI41_SRCPORT_C55;

    /* Need handle to MTANI device in order to transmit packets to it */

    priv->ptr_etmani_device = dev_get_by_name(CONFIG_TI_PACM_MTA_INTERFACE);

    /* Regisger voiceni network device notifier in order to receive network 
       device related events */
    register_netdevice_notifier(&voiceni_netdev_notifier);

    create_proc_read_entry("voiceni", 0, NULL, voiceni_init_proc, (void *)priv);
    create_proc_read_entry("voiceni_stats", 0, NULL, voiceni_stats_proc, (void *)priv);
	DPRINTK("VOICE NI module loaded \n");
    return 0;
}

/*********************************************************************************
 * FUNCTION: voiceni_cleanup_module
 *
 * DESCRIPTION:
 *********************************************************************************/
static void __exit voiceni_cleanup_module(void)
{
    /* need to remove proc entries, etc */
    DPRINTK("voice module unloaded\n");
    return;
}

module_init(voiceni_init_module);
module_exit(voiceni_cleanup_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Texas Instruments Incorporated");
MODULE_DESCRIPTION("TI voice VOICENI core module.");
MODULE_SUPPORTED_DEVICE("Texas Instruments voiceni");


