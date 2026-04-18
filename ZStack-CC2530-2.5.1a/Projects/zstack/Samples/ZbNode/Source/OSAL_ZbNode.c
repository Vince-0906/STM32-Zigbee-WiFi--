/* OSAL 任务注册（Node 角色 = Router） */

#include "ZComDef.h"
#include "hal_drivers.h"
#include "OSAL.h"
#include "OSAL_Tasks.h"
#include "nwk.h"
#include "APS.h"
#include "ZDApp.h"
#if defined ( ZIGBEE_FRAGMENTATION )
  #include "aps_frag.h"
#endif
#include "zb_node.h"

const pTaskEventHandlerFn tasksArr[] = {
  macEventLoop,
  nwk_event_loop,
  Hal_ProcessEvent,
  APS_event_loop,
#if defined ( ZIGBEE_FRAGMENTATION )
  APSF_ProcessEvent,
#endif
  ZDApp_event_loop,
  ZbNode_ProcessEvent
};

const uint8 tasksCnt = sizeof( tasksArr ) / sizeof( tasksArr[0] );
uint16 *tasksEvents;

void osalInitTasks( void )
{
  uint8 taskID = 0;
  tasksEvents = (uint16 *)osal_mem_alloc( sizeof( uint16 ) * tasksCnt);
  osal_memset( tasksEvents, 0, (sizeof( uint16 ) * tasksCnt));

  macTaskInit( taskID++ );
  nwk_init( taskID++ );
  Hal_Init( taskID++ );
  APS_Init( taskID++ );
#if defined ( ZIGBEE_FRAGMENTATION )
  APSF_Init( taskID++ );
#endif
  ZDApp_Init( taskID++ );
  ZbNode_Init( taskID );
}
