#include <string.h>
#include "HAL_conf.h"
#include "BSP.h"
#include "iwdg.h"
#include "mg_api.h"



extern volatile unsigned int SysTick_Count;

unsigned char *ble_mac_addr;


unsigned char pld_adv[] = {2,1,6, 3,3,0x90,0xfe, 9,0xFF,0x11,0x02,0x9c,0x05,0x93,0xF5,0x40,0xE1};//max 31 Byte

int main(void)
{
    unsigned long temp=0x200000;

    while(temp--);//wait a while for hex programming if using the MCU stop mode, default NOT used.
    
    BSP_Init();

    Write_Iwdg_ON(IWDG_Prescaler_32, 0x4E2); //1s

    SetBleIntRunningMode();
    radio_initBle(TXPWR_0DBM, &ble_mac_addr);
    
    ble_set_adv_data(pld_adv, sizeof(pld_adv)); 
    
    
    ble_run_interrupt_start(160); //160*0.625=100 ms
    
    while(1)
    {
        //key proc
    }
}
