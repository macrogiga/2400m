#include <stdio.h>
#include <string.h>
#include "HAL_conf.h"
#include "HAL_device.h"
#include "mg_api.h"
#include "bsp.h"

extern char GetConnectedStatus(void);
extern void ChangeBaudRate(void);

#define MS1793S_UART_VERSION_STRING  "IND:Ver1.0\n"


#define MAX_SIZE 200
static u8 txBuf[MAX_SIZE]={0}, rxBuf[MAX_SIZE]={0};
static u16 RxCnt = 0;
static u8 PosW = 0, txLen = 0;

unsigned int RxTimeout = 0;
unsigned int TxTimeout = 0;

extern volatile unsigned int SysTick_Count;
extern u32 BaudRate;
extern unsigned char SleepStop;


int fputc(int ch, FILE *f)
{
    UART_SendData(UART1, (uint16_t) ch);

    while(1)
    {
        if(UART_GetITStatus(UART1, UART_IT_TXIEN))
        {
             UART_ClearITPendingBit(UART1, UART_IT_TXIEN);
             break;
        }
    }

    return ch;
}

int fgetc(FILE *f)
{
    while(1)
    {
        if(UART_GetITStatus(UART1, UART_IT_RXIEN))
        {
            UART_ClearITPendingBit(UART1, UART_IT_RXIEN);  //
            break;
        }
    }
    
    return (int)UART_ReceiveData(UART1);
}

void UART1_IRQHandler(void)                	//����1�жϷ������
{
    if(UART_GetITStatus(UART1, UART_IT_RXIEN)  != RESET)  //�����ж�
    {
        UART_ClearITPendingBit(UART1,UART_IT_RXIEN);
        rxBuf[RxCnt]=UART_ReceiveData(UART1);
        RxTimeout = SysTick_Count + 1000;
        RxCnt++;
        if(RxCnt >= MAX_SIZE)
        {
            RxCnt = 0;
        }
    }
    if(UART_GetITStatus(UART1, UART_IT_TXIEN) != RESET)
    {
        UART_ClearITPendingBit(UART1,UART_IT_TXIEN);
        
        TxTimeout = SysTick_Count + (20000/BaudRate);

        if (PosW < txLen)
        {
            UART_SendData(UART1,txBuf[PosW++]);
        }
        else
        {
            UART_ITConfig(UART1, UART_IT_TXIEN, DISABLE);
        }

        if (PosW >= txLen){
            txLen = 0;
            PosW = 0;
        }
    }
}

void moduleOutData(u8*data, u8 len) //api
{
    unsigned char i = 0;

    if ((txLen+len)<MAX_SIZE)//buff not overflow
    {
        for (i=0;i<len;i++)
        {
            txBuf[txLen+i] = *(data+i);
        }
        txLen += len;
    }
}


#define comrxbuf_wr_pos RxCnt
static u16 comrxbuf_rd_pos = 0; //init, com rx buffer

#ifdef USE_AT_CMD
///////////////FIFO proc for AT cmd///////////////
#define MAX_AT_CMD_BUF_SIZE 52
u8 AtCmdBuf[MAX_AT_CMD_BUF_SIZE]={0}, AtCmdBufDataSize=0;

extern void updateDeviceInfoData(u8* name, u8 len);

//AT+SETNAME=abcdefg
void atcmd_SetName(u8* parameter,u8 len)
{   
    if(len <= 8){
        moduleOutData((u8*)"IND:ERR\n",8);
        return;
    }
    
    updateDeviceInfoData(parameter+8,len-8);
    moduleOutData((u8*)"IND:OK\n",7);
}

//AT+SETINTERVAL=160
void atcmd_SetAdvInt(u8* parameter,u8 len)
{
    int v = 0;
    unsigned char i = 0;
    
    if(len <= 12){
        moduleOutData((u8*)"IND:ERR\n",8);
        return;
    }
    
    for(i = 12 ; i < len ; i ++)
    {
        v *= 10;
        v += (parameter[i] - '0');
    }
    
    if(v > 3200)v = 3200;
    
    ble_set_interval(v);
    
    moduleOutData((u8*)"IND:OK\n",7);
}

static u8 Hex2Int(unsigned char C)
{
    if(C <= '9')
    {
        return C - '0';
    }
    else if(C <= 'F')
    {
        return C - 'A' + 10;
    }
    else
    {
        return C - 'a' + 10;
    }
}

//AT+BLESEND=N,0x123456abc
void atcmd_SendData(u8* parameter,u8 len)
{
    u8* data = parameter;
    u8  data_len = 0, i = 0;
    
    //I am here check the data format
    //tbd...
    if(len <= 8){
        moduleOutData((u8*)"IND:ERR\n",8);
        return;
    }
    
    parameter += 8;//move to N
    while(parameter[0] != ',')
    {
        data_len *= 10;
        data_len += (parameter[0] - '0');
        
        parameter ++;
    }
    
    parameter += 3; //move 0x
    
    for(i = 0 ; i < data_len ; i ++)
    {
        data[i] = ((Hex2Int(parameter[0]) << 4) | Hex2Int(parameter[1]));
        parameter += 2;
    }
    
    //ble send data [data, data_len]
    sconn_notifydata(data,data_len); //pls check the connect status if necessary
    
    moduleOutData((u8*)"IND:OK\n",7);
}

//AT+SETADVFLAG=x		x=0/1, 0 disable adv, 1 enable adv
void atcmd_SetAdvFlag(u8* parameter,u8 len)
{
    if(len <= 11){
        moduleOutData((u8*)"IND:ERR\n",8);
        return;
    }
    
    parameter += 11;//move to x
    
    if('0' == parameter[0]) //diable adv
    {
        ble_set_adv_enableFlag(0);
    }
    else
    {
        ble_set_adv_enableFlag(1);
    }
    
    moduleOutData((u8*)"IND:OK\n",7);
}

//AT+DISCON, disconnect the connection is any
void atcmd_DisconnecteBle(u8* parameter,u8 len)
{
    if(len != 6){
        moduleOutData((u8*)"IND:ERR\n",8);
        return;
    }
    ble_disconnect();
    moduleOutData((u8*)"IND:OK\n",7);
}

//AT+LOWPOWER=x		x=0/1/2
void atcmd_LowPower(u8* parameter,u8 len)
{
    if(len <= 9){
        moduleOutData((u8*)"IND:ERR\n",8);
        return;
    }
    
    parameter += 9;//move to x
    
    if (0x31 == parameter[0])
    {
        SleepStop = 1;
    }
    else if (0x32 == parameter[0])
    {
        SleepStop = 2;
    }
    else
    {
        SleepStop = 0;
    }
    
    moduleOutData((u8*)"IND:OK\n",7);
}

static unsigned char WaitSetBaud=0;
//AT+SETBAUD
void atcmd_SetBaud(u8* parameter,u8 len)
{
    int v = 0;
    unsigned char i = 0;
    
    if(len <= 8){
        moduleOutData((u8*)"IND:ERR\n",8);
        return;
    }
    
    for(i = 8 ; i < len ; i ++)
    {
        v *= 10;
        v += (parameter[i] - '0');
    }
    
    if (BaudRate != v)
    {
        BaudRate = v;
        WaitSetBaud = 1;
    }else{
        moduleOutData((u8*)"IND:OK\n",7);
    }

}

void atcmd_Minfo(u8* parameter,u8 len);
void atcmd_Help(u8* parameter,u8 len);
    
#define MAX_AT_CMD_NAME_SIZE 16
typedef void (*ATCMDFUNC)(u8* cmd/*NULL ended, leading with the cmd NAME string, checking usage*/,u8 len);    
typedef struct _tagATCMD
{
    ATCMDFUNC func;
    u8 name[MAX_AT_CMD_NAME_SIZE]; //max len is 11 bytes
}AT_CMD_FUNC;

//function list
AT_CMD_FUNC at_func_list[]=
{
    {atcmd_SetName,"SETNAME="},
    {atcmd_SetAdvInt,"SETINTERVAL="},
    {atcmd_SendData,"BLESEND="},
    {atcmd_LowPower,"LOWPOWER="},
    {atcmd_SetBaud,"SETBAUD="},
    {atcmd_SetAdvFlag,"SETADVFLAG="},
    {atcmd_DisconnecteBle,"DISCON"},
    {atcmd_Minfo,"MINFO"},
    {atcmd_Help,"HELP"},
};


static u8 IsExactCmdInclude(u8* data, const u8* cmd)
{
    u8 i = 0, len = strlen((const char*)cmd);
    
    for(i = 0 ; i < len ; i ++)
    {        
        if(cmd[i] != data[i])
        {
            return 0;
        }
    }
    
    return 1;
}

#define at_cmd_num  (sizeof(at_func_list)/sizeof(at_func_list[0]))

//AT+MINFO
void atcmd_Minfo(u8* parameter,u8 len)
{    
    moduleOutData((u8*)"IND:OK\n",7);
    
    moduleOutData((u8*)MS1793S_UART_VERSION_STRING,sizeof(MS1793S_UART_VERSION_STRING)-1);    
    
    moduleOutData((u8*)"IND:CON=",8);
    
    if(GetConnectedStatus())
    {
        moduleOutData((u8*)"1\n",2);
    }
    else
    {
        moduleOutData((u8*)"0\n",2);
    }
}

//AT+HELP
void atcmd_Help(u8* parameter,u8 len)
{
    u8 i = 0, templen = 0;
    u8 name[20]="AT+";
    
    moduleOutData((u8*)"IND:OK\n",7);
    
    moduleOutData((u8*)MS1793S_UART_VERSION_STRING,sizeof(MS1793S_UART_VERSION_STRING)-1);
    
    for(i = 0; i < at_cmd_num ; i ++)
    {
        strcpy((char*)name+3,((char*)at_func_list[i].name));
        templen = strlen((char*)name);
        name[templen] = 0X0A;
        moduleOutData((u8*)name,templen+1); //NULL terminated format
    }
}

static void AtCmdParser(u8* cmd, u8 len)
{
    u8 i = 0;
    
    for(i = 0 ; i < at_cmd_num ; i++)
    {
        if(IsExactCmdInclude(cmd,at_func_list[i].name))
        {
            //found
            at_func_list[i].func(cmd/*including the CMD name*/,len);
            
            return;
        }
    }

    moduleOutData((u8*)"IND:ERR\n",8);
}


static void AtCmdPreParser(u8* cmd, u8 len)
{
    if(!IsExactCmdInclude(cmd, (const u8*)"AT+")) //AT+MINFO
    {
        moduleOutData((u8*)"IND:ERR\n",8);
        return; //cmd error
    }
    
    AtCmdParser(cmd+3,len-3);
}

static void CheckAtCmdInfo(void) //main entrance
{
    while(comrxbuf_wr_pos != comrxbuf_rd_pos) //not empty
    {
        //has data
        AtCmdBuf[AtCmdBufDataSize++] = rxBuf[comrxbuf_rd_pos];
        
        if(AtCmdBufDataSize >= MAX_AT_CMD_BUF_SIZE)//error found
        {
            AtCmdBufDataSize = 0;//I just reset the position, drop the cmd
        }
        
        if((RxTimeout < SysTick_Count)||(rxBuf[comrxbuf_rd_pos] == 0x0a) || (rxBuf[comrxbuf_rd_pos] == 0x0d))        
        {
            if(AtCmdBufDataSize == 1)
            {
                AtCmdBufDataSize = 0;
            }
            else//found one cmd
            {
                AtCmdBuf[AtCmdBufDataSize-1] = '\0';
                AtCmdPreParser(AtCmdBuf,AtCmdBufDataSize-1);
                
                AtCmdBufDataSize = 0;//move to next cmd if any
            }
        }
        
        comrxbuf_rd_pos ++;
        comrxbuf_rd_pos %= MAX_SIZE; //com buff len
    }
}
#endif


void CheckComPortInData(void) //at cmd NOT supported
{
    u16 send = 0;
    
    if(comrxbuf_wr_pos != comrxbuf_rd_pos)//not empty
    {
        if(!GetConnectedStatus())
        {
            comrxbuf_rd_pos = comrxbuf_wr_pos; //empty the buffer if any
        }
        else //connected
        {
            if(comrxbuf_wr_pos > comrxbuf_rd_pos)
            {
                send = sconn_notifydata(rxBuf+comrxbuf_rd_pos,comrxbuf_wr_pos - comrxbuf_rd_pos);
                comrxbuf_rd_pos += send;
            }
            else 
            {
                send = sconn_notifydata(rxBuf+comrxbuf_rd_pos,MAX_SIZE - comrxbuf_rd_pos);
                comrxbuf_rd_pos += send;
                comrxbuf_rd_pos %= MAX_SIZE;
            }
        }
    }
}

void UsrProcCallback(void) //porting api
{
    //u16 conn_interv = 0;
    //static u16 counter = 0;

    IWDG_ReloadCounter();

/*    if(GetConnectedStatus()){
        //counter ++;
        if(counter == 60){
            conn_interv = sconn_GetConnInterval();
            //if(conn_interv < 8)
            {//24*1.25=30ms
                //SIG_ConnParaUpdateReq(0x0008, 0x0008, 0x0001, 1000);
                //SIG_ConnParaUpdateReq(219, 219, 0x0004, 1000);
            }
            counter = 0;
        }
    }else{
        counter = 0;
    } */

#ifdef USE_AT_CMD
    CheckAtCmdInfo();
#else //AT CMD not supported
    CheckComPortInData();
#endif
    if ((2 != SleepStop)||(!(GPIO_ReadInputData(GPIOA) & 0x4000)))
    {
        if ((txLen) && (0 == PosW))
        {
//            UART_SendData(UART1, txBuf[PosW++]);
            UART_ITConfig(UART1, UART_IT_TXIEN, ENABLE);
            
            TxTimeout = SysTick_Count + (20000/BaudRate);
        }
    }
    if ((SleepStop == 2)&&(RxTimeout < SysTick_Count))
    {
        GPIO_SetBits(GPIOA, GPIO_Pin_13);
        RxTimeout = SysTick_Count + (20000/BaudRate);
    }
    
#ifdef USE_AT_CMD
    if (WaitSetBaud)
    {
        WaitSetBaud++;
        
        if (WaitSetBaud > 2)
        {
            ChangeBaudRate();
            WaitSetBaud = 0;
            moduleOutData((u8*)"IND:OK\n",7);
        }
    }
#endif
}
