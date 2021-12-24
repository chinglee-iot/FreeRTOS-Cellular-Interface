/*
 * FreeRTOS-Cellular-Interface v1.2.0
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 */

/* The config header is always included first. */

#include <stdint.h>

#include "cellular_config.h"
#include "cellular_config_defaults.h"
#include "cellular_common.h"
#include "cellular_common_portable.h"
#include "cellular_hl7802.h"

/*-----------------------------------------------------------*/

#define ENBABLE_MODULE_UE_RETRY_COUNT      ( 3U )
#define ENBABLE_MODULE_UE_RETRY_TIMEOUT    ( 5000U )
#define HL7802_MAX_BAND_CFG                ( 21U )

/*-----------------------------------------------------------*/

typedef struct Hl7802BandConfig
{
    char catm1BandCfg[ HL7802_MAX_BAND_CFG ];
    char nbiotBandCfg[ HL7802_MAX_BAND_CFG ];
    char gsmBandCfg[ HL7802_MAX_BAND_CFG ];
} Hl7802BandConfig_t;

/*-----------------------------------------------------------*/

static CellularError_t sendAtCommandWithRetryTimeout( CellularContext_t * pContext,
                                                      const CellularAtReq_t * pAtReq );
static CellularError_t Cellular_GetBandCfg( CellularContext_t * pContext,
                                            Hl7802BandConfig_t * pBandCfg );
static CellularPktStatus_t prvRecvFuncGetBandCfg( CellularContext_t * pContext,
                                                  const CellularATCommandResponse_t * pAtResp,
                                                  void * pData,
                                                  uint16_t dataLen );

/*-----------------------------------------------------------*/

static cellularModuleContext_t cellularHl7802Context = { 0 };

/* FreeRTOS Cellular Common Library porting interface. */
/* coverity[misra_c_2012_rule_8_7_violation] */
const char * CellularSrcTokenErrorTable[] =
{ "ERROR", "BUSY", "NO CARRIER", "NO ANSWER", "NO DIALTONE", "ABORTED", "+CMS ERROR", "+CME ERROR", "SEND FAIL" };
/* FreeRTOS Cellular Common Library porting interface. */
/* coverity[misra_c_2012_rule_8_7_violation] */
uint32_t CellularSrcTokenErrorTableSize = sizeof( CellularSrcTokenErrorTable ) / sizeof( char * );

/* FreeRTOS Cellular Common Library porting interface. */
/* coverity[misra_c_2012_rule_8_7_violation] */
const char * CellularSrcTokenSuccessTable[] =
{ "OK" };
/* FreeRTOS Cellular Common Library porting interface. */
/* coverity[misra_c_2012_rule_8_7_violation] */
uint32_t CellularSrcTokenSuccessTableSize = sizeof( CellularSrcTokenSuccessTable ) / sizeof( char * );

/* FreeRTOS Cellular Common Library porting interface. */
/* coverity[misra_c_2012_rule_8_7_violation] */
const char * CellularUrcTokenWoPrefixTable[] = { 0 };

/* FreeRTOS Cellular Common Library porting interface. */
/* coverity[misra_c_2012_rule_8_7_violation] */
uint32_t CellularUrcTokenWoPrefixTableSize = 0;

/*-----------------------------------------------------------*/

static CellularError_t sendAtCommandWithRetryTimeout( CellularContext_t * pContext,
                                                      const CellularAtReq_t * pAtReq )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;
    CellularPktStatus_t pktStatus = CELLULAR_PKT_STATUS_OK;
    uint8_t tryCount = 0;

    if( pAtReq == NULL )
    {
        cellularStatus = CELLULAR_BAD_PARAMETER;
    }
    else
    {
        for( ; tryCount < ENBABLE_MODULE_UE_RETRY_COUNT; tryCount++ )
        {
            pktStatus = _Cellular_TimeoutAtcmdRequestWithCallback( pContext, *pAtReq, ENBABLE_MODULE_UE_RETRY_TIMEOUT );
            cellularStatus = _Cellular_TranslatePktStatus( pktStatus );

            if( cellularStatus == CELLULAR_SUCCESS )
            {
                break;
            }
        }
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

static CellularPktStatus_t prvRecvFuncGetBandCfg( CellularContext_t * pContext,
                                                  const CellularATCommandResponse_t * pAtResp,
                                                  void * pData,
                                                  uint16_t dataLen )
{
    CellularPktStatus_t pktStatus = CELLULAR_PKT_STATUS_OK;
    CellularATError_t atCoreStatus = CELLULAR_AT_SUCCESS;
    Hl7802BandConfig_t * pBandCfg = NULL;
    const CellularATCommandLine_t * pCommnadItem = NULL;
    char * pInputLine = NULL;
    char * pToken = NULL;
    char * pRatBand = NULL;

    if( pContext == NULL )
    {
        LogError( ( "prvRecvFuncGetBandCfg: Invalid context." ) );
        pktStatus = CELLULAR_PKT_STATUS_FAILURE;
    }
    else if( pAtResp == NULL )
    {
        LogError( ( "prvRecvFuncGetBandCfg: Invalid pAtResp." ) );
        pktStatus = CELLULAR_PKT_STATUS_BAD_PARAM;
    }
    else if( ( pData == NULL ) || ( dataLen != sizeof( Hl7802BandConfig_t ) ) )
    {
        LogError( ( "prvRecvFuncGetBandCfg: Invalid pData %p or dataLen %u.", pData, dataLen ) );
        pktStatus = CELLULAR_PKT_STATUS_BAD_PARAM;
    }
    else
    {
        pBandCfg = ( Hl7802BandConfig_t * ) pData;
        pCommnadItem = pAtResp->pItm;

        while( pCommnadItem != NULL )
        {
            pInputLine = pCommnadItem->pLine;
            LogDebug( ( "prvRecvFuncGetBandCfg: input line %s", pInputLine ) );

            /* Remove the line prefix. */
            atCoreStatus = Cellular_ATRemovePrefix( &pInputLine );

            /* Parse the RAT field. */
            if( atCoreStatus == CELLULAR_AT_SUCCESS )
            {
                atCoreStatus = Cellular_ATGetNextTok( &pInputLine, &pToken );
            }

            if( atCoreStatus == CELLULAR_AT_SUCCESS )
            {
                atCoreStatus = Cellular_ATRemoveLeadingWhiteSpaces( &pToken );
            }

            if( atCoreStatus == CELLULAR_AT_SUCCESS )
            {
                switch( *pToken )
                {
                    case '0':
                        pRatBand = pBandCfg->catm1BandCfg;
                        break;

                    case '1':
                        pRatBand = pBandCfg->nbiotBandCfg;
                        break;

                    case '2':
                        pRatBand = pBandCfg->gsmBandCfg;
                        break;

                    default:
                        pRatBand = NULL;
                        LogError( ( "prvRecvFuncGetBandCfg: unknown RAT %s", pToken ) );
                        atCoreStatus = CELLULAR_AT_ERROR;
                        break;
                }
            }

            if( atCoreStatus == CELLULAR_AT_SUCCESS )
            {
                /* Copy the band configuration. */
                strncpy( pRatBand, pInputLine, HL7802_MAX_BAND_CFG );
            }
            else
            {
                LogError( ( "prvRecvFuncGetBandCfg process response line error" ) );
                pktStatus = _Cellular_TranslateAtCoreStatus( atCoreStatus );
                break;
            }

            pCommnadItem = pCommnadItem->pNext;
        }
    }

    return pktStatus;
}

/*-----------------------------------------------------------*/

static CellularError_t Cellular_GetBandCfg( CellularContext_t * pContext,
                                            Hl7802BandConfig_t * pBandCfg )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;
    CellularPktStatus_t pktStatus = CELLULAR_PKT_STATUS_OK;
    CellularAtReq_t atReqGetBndCfg =
    {
        "AT+KBNDCFG?",
        CELLULAR_AT_MULTI_WITH_PREFIX,
        "+KBNDCFG",
        prvRecvFuncGetBandCfg,
        pBandCfg,
        sizeof( Hl7802BandConfig_t )
    };

    /* pContext and pBandCfg are checked in Cellular_ModuleEnableUe function. */
    ( void ) memset( pBandCfg, 0, sizeof( Hl7802BandConfig_t ) );
    pktStatus = _Cellular_AtcmdRequestWithCallback( pContext, atReqGetBndCfg );

    if( pktStatus != CELLULAR_PKT_STATUS_OK )
    {
        LogError( ( "Cellular_GetBandCfg: couldn't retrieve band configurations." ) );
        cellularStatus = _Cellular_TranslatePktStatus( pktStatus );
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

/* FreeRTOS Cellular Common Library porting interface. */
/* coverity[misra_c_2012_rule_8_7_violation] */
CellularError_t Cellular_ModuleInit( const CellularContext_t * pContext,
                                     void ** ppModuleContext )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;
    uint32_t i = 0;

    if( pContext == NULL )
    {
        cellularStatus = CELLULAR_INVALID_HANDLE;
    }
    else if( ppModuleContext == NULL )
    {
        cellularStatus = CELLULAR_BAD_PARAMETER;
    }
    else
    {
        /* Initialize the module context. */
        ( void ) memset( &cellularHl7802Context, 0, sizeof( cellularModuleContext_t ) );

        for( i = 0; i < TCP_SESSION_TABLE_LEGNTH; i++ )
        {
            cellularHl7802Context.pSessionMap[ i ] = INVALID_SOCKET_INDEX;
        }

        *ppModuleContext = ( void * ) &cellularHl7802Context;
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

/* FreeRTOS Cellular Common Library porting interface. */
/* coverity[misra_c_2012_rule_8_7_violation] */
CellularError_t Cellular_ModuleCleanUp( const CellularContext_t * pContext )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;

    if( pContext == NULL )
    {
        cellularStatus = CELLULAR_INVALID_HANDLE;
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

/* FreeRTOS Cellular Common Library porting interface. */
/* coverity[misra_c_2012_rule_8_7_violation] */
CellularError_t Cellular_ModuleEnableUE( CellularContext_t * pContext )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;
    CellularAtReq_t atReqGetNoResult =
    {
        NULL,
        CELLULAR_AT_NO_RESULT,
        NULL,
        NULL,
        NULL,
        0
    };
    CellularAtReq_t atReqGetWithResult =
    {
        NULL,
        CELLULAR_AT_MULTI_WO_PREFIX,
        NULL,
        NULL,
        NULL,
        0
    };
    Hl7802BandConfig_t bandCfg = { 0 };

    if( pContext != NULL )
    {
        /* Disable echo. */
        atReqGetWithResult.pAtCmd = "ATE0";
        cellularStatus = sendAtCommandWithRetryTimeout( pContext, &atReqGetWithResult );

        if( cellularStatus == CELLULAR_SUCCESS )
        {
            /* Disable DTR function. */
            atReqGetNoResult.pAtCmd = "AT&D0";
            cellularStatus = sendAtCommandWithRetryTimeout( pContext, &atReqGetNoResult );
        }

        if( cellularStatus == CELLULAR_SUCCESS )
        {
            /* Enable RTS/CTS hardware flow control. */
            atReqGetNoResult.pAtCmd = "AT+IFC=2,2";
            cellularStatus = sendAtCommandWithRetryTimeout( pContext, &atReqGetNoResult );
        }

        /* Set Radio Access Technology. */
        if( cellularStatus == CELLULAR_SUCCESS )
        {
            /* In the Write format, <mode>=0 is used to switch to the first RAT
             * in the preferred RAT list (PRL), and fall back to subsequent RATS
             * in the PRL if cell coverage is lost. If the PRL is empty, switch to
             * CAT-M1. To set the PRL, see AT+KSELACQ. */
            atReqGetNoResult.pAtCmd = "AT+KSRAT=0";
            cellularStatus = sendAtCommandWithRetryTimeout( pContext, &atReqGetNoResult );
        }

        /* Set Default Radio Access Technology. */
        if( cellularStatus == CELLULAR_SUCCESS )
        {
            switch( CELLULAR_CONFIG_DEFAULT_RAT )
            {
                case CELLULAR_RAT_CATM1:
                    atReqGetNoResult.pAtCmd = "AT+KSELACQ=0,1";
                    break;

                case CELLULAR_RAT_NBIOT:
                    atReqGetNoResult.pAtCmd = "AT+KSELACQ=0,2";
                    break;

                case CELLULAR_RAT_GSM:
                    atReqGetNoResult.pAtCmd = "AT+KSELACQ=0,3";
                    break;

                default:
                    break;
            }

            cellularStatus = sendAtCommandWithRetryTimeout( pContext, &atReqGetNoResult );
        }

        /* Set Configured LTE Band. */
        if( cellularStatus == CELLULAR_SUCCESS )
        {
            cellularStatus = Cellular_GetBandCfg( pContext, &bandCfg );
        }

        if( cellularStatus == CELLULAR_SUCCESS )
        {
            if( strcmp( bandCfg.catm1BandCfg, CELLULAR_CONFIG_HL7802_CATM1_BAND ) != 0 )
            {
                LogInfo( ( "Cellular_ModuleEnableUE : CAT-M1 band desired %s actual %s",
                           CELLULAR_CONFIG_HL7802_CATM1_BAND, bandCfg.catm1BandCfg ) );
                atReqGetNoResult.pAtCmd = "AT+KBNDCFG=0,"CELLULAR_CONFIG_HL7802_CATM1_BAND;
                cellularStatus = sendAtCommandWithRetryTimeout( pContext, &atReqGetNoResult );
            }
        }

        if( cellularStatus == CELLULAR_SUCCESS )
        {
            if( strcmp( bandCfg.nbiotBandCfg, CELLULAR_CONFIG_HL7802_NBIOT_BAND ) != 0 )
            {
                LogInfo( ( "Cellular_ModuleEnableUE : NBIOT band desired %s actual %s",
                           CELLULAR_CONFIG_HL7802_NBIOT_BAND, bandCfg.nbiotBandCfg ) );
                atReqGetNoResult.pAtCmd = "AT+KBNDCFG=1,"CELLULAR_CONFIG_HL7802_NBIOT_BAND;
                cellularStatus = sendAtCommandWithRetryTimeout( pContext, &atReqGetNoResult );
            }
        }

        /* Disable standalone sleep mode. */
        if( cellularStatus == CELLULAR_SUCCESS )
        {
            atReqGetNoResult.pAtCmd = "AT+KSLEEP=2";
            cellularStatus = sendAtCommandWithRetryTimeout( pContext, &atReqGetNoResult );
        }

        /* Force initialization of radio to consider new configured bands. */
        if( cellularStatus == CELLULAR_SUCCESS )
        {
            atReqGetNoResult.pAtCmd = "AT+CFUN=1,1";
            cellularStatus = sendAtCommandWithRetryTimeout( pContext, &atReqGetNoResult );
        }

        Platform_Delay( CELLULAR_HL7802_RESET_DELAY_MS );

        /* Disable echo after reboot device. */
        if( cellularStatus == CELLULAR_SUCCESS )
        {
            atReqGetWithResult.pAtCmd = "ATE0";
            cellularStatus = sendAtCommandWithRetryTimeout( pContext, &atReqGetWithResult );
        }
    }

    return cellularStatus;
}

/*-----------------------------------------------------------*/

/* FreeRTOS Cellular Common Library porting interface. */
/* coverity[misra_c_2012_rule_8_7_violation] */
CellularError_t Cellular_ModuleEnableUrc( CellularContext_t * pContext )
{
    CellularError_t cellularStatus = CELLULAR_SUCCESS;
    CellularAtReq_t atReqGetNoResult =
    {
        NULL,
        CELLULAR_AT_NO_RESULT,
        NULL,
        NULL,
        NULL,
        0
    };

    atReqGetNoResult.pAtCmd = "AT+COPS=3,2";
    ( void ) _Cellular_AtcmdRequestWithCallback( pContext, atReqGetNoResult );

    atReqGetNoResult.pAtCmd = "AT+CREG=2";
    ( void ) _Cellular_AtcmdRequestWithCallback( pContext, atReqGetNoResult );

    atReqGetNoResult.pAtCmd = "AT+CEREG=2";
    ( void ) _Cellular_AtcmdRequestWithCallback( pContext, atReqGetNoResult );

    atReqGetNoResult.pAtCmd = "AT+CTZR=1";
    ( void ) _Cellular_AtcmdRequestWithCallback( pContext, atReqGetNoResult );

    return cellularStatus;
}

/*-----------------------------------------------------------*/

uint32_t _Cellular_GetSocketId( CellularContext_t * pContext,
                                uint8_t sessionId )
{
    cellularModuleContext_t * pModuleContext = NULL;
    uint32_t socketIndex = INVALID_SOCKET_INDEX;
    CellularError_t cellularStatus = CELLULAR_SUCCESS;

    if( pContext != NULL )
    {
        cellularStatus = _Cellular_GetModuleContext( pContext, ( void ** ) &pModuleContext );
    }
    else
    {
        cellularStatus = CELLULAR_BAD_PARAMETER;
    }

    if( ( cellularStatus == CELLULAR_SUCCESS ) &&
        ( sessionId >= MIN_TCP_SESSION_ID ) && ( sessionId <= MAX_TCP_SESSION_ID ) )
    {
        socketIndex = pModuleContext->pSessionMap[ sessionId ];
    }

    return socketIndex;
}
