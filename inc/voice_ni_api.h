/*
*  Copyright 2011, Texas Instruments Incorporated.
*
* Description:
*   This interface is the bridge between user and kernel space for the VOICENI 
*   module.
*
*/

#ifndef  VOICE_NI_API_FILE_HEADER_INC
#define  VOICE_NI_API_FILE_HEADER_INC

#include <linux/sockios.h>


#define SIOCGETVOICENI_PARAMS (SIOCDEVPRIVATE +0)

#define VOICENI_DEV_NAME "vni0"

/*******************************************************************************************
 * Packet Processor Configuration Information needed to be sent to DSP in HW_CONFIG message
 ******************************************************************************************/
typedef struct VOICENI_PP_CONFIG {
  UINT16 pid;                   /* DSP's assigned PID */
  UINT16 cppi_queue_mgr_id;     /* CPPI queue manager ID number */
  UINT16 cppi_bd_queue_id;      /* CPPI buffer descriptor queue ID */
  UINT16 buffer_pool_mgr_id;    /* CPPI buffer pool Manager ID */
  UINT16 buffer_pool_id;        /* CPPI buffer pool ID */
  UINT16 cppi_rx_queue_id;      /* CPPI RX queue ID */
  UINT16 cppi_tx_queue_id;      /* CPPI TX queue ID */
} VOICENI_PP_CONFIG_T;

#endif
