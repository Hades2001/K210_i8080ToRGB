/* Copyright 2018 Canaan Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdio.h>
#include "bsp.h"
#include "system_config.h"
#include "gpiohs.h"
#include "fpioa.h"
#include "sysctl.h"
#include "spi.h"
#include "uarths.h"
#include "timer.h"

#include "utils.h"

#include "Pic.h"
#include "Font.h"
#include "sdcard.h"
#include "ff.h"
#include "diskio.h"

#include "dmac.h"

#define     LCD_BL_ON       0x61
#define     LCD_BL_OFF      0x60
#define     LCD_DISOLAY_ON  0x38

void InitLCDGPIO( void )
{
    fpioa_set_function(37, FUNC_GPIOHS0 + RST_GPIONUM);
    fpioa_set_function(38, FUNC_GPIOHS0 + DCX_GPIONUM);
    fpioa_set_function(36, FUNC_SPI0_SS3);
    fpioa_set_function(39, FUNC_SPI0_SCLK);

    gpiohs_set_drive_mode(DCX_GPIONUM, GPIO_DM_OUTPUT);
    gpiohs_set_pin(DCX_GPIONUM, GPIO_PV_HIGH);//GPIO_DM_INPUT

    gpiohs_set_drive_mode(RST_GPIONUM, GPIO_DM_OUTPUT);
    gpiohs_set_pin(RST_GPIONUM, GPIO_PV_HIGH);
    fpioa_set_io_driving( 39 , FPIOA_DRIVING_7 );

    sysctl_set_spi0_dvp_data(1);

    sysctl_set_power_mode(SYSCTL_POWER_BANK6, SYSCTL_POWER_V18);
    sysctl_set_power_mode(SYSCTL_POWER_BANK7, SYSCTL_POWER_V18);
    msleep(15);
}

void InitLCDHard( void )
{
    InitLCDGPIO();
    spi_init(SPI_CHANNEL, SPI_WORK_MODE_0, SPI_FF_OCTAL, 32, 0);
    spi_set_clk_rate(SPI_CHANNEL, 25000000);
}

void LCDSendCMD( uint8_t CMDData )
{
    gpiohs_set_pin(DCX_GPIONUM, GPIO_PV_HIGH);

    spi_init(SPI_CHANNEL, SPI_WORK_MODE_0, SPI_FF_OCTAL, 8, 0);
    spi_init_non_standard(SPI_CHANNEL, 8 /*instrction length*/, 0 /*address length*/, 0 /*wait cycles*/,
                          SPI_AITM_AS_FRAME_FORMAT /*spi address trans mode*/);
    spi_send_data_normal_dma(DMAC_CHANNEL0, SPI_CHANNEL, SPI_SLAVE_SELECT, (uint8_t *)(&CMDData), 1, SPI_TRANS_CHAR);
}

int DmacTransfer_irq( void *ctx )
{
    printf("Dmac End\r\n");
    return 0;
}

void spi_Mysend_32Bitdata(spi_device_num_t spi_num, spi_chip_select_t chip_select, uint8_t *Data , uint32_t  Length )
{
    volatile spi_t *spi_handle = spi[spi_num];
    uint8_t tmod_offset = 8;

    set_bit(&spi_handle->ctrlr0, 3 << tmod_offset, SPI_TMOD_TRANS << tmod_offset);

    spi_handle->ssienr = 0x01;
    spi_handle->ser = 1U << chip_select;

    for( uint8_t i = 0 ; i < ( Length / 4 ) ;i++ )
    spi_handle->dr[0] = ((uint32_t *)Data)[i];

    while ((spi_handle->sr & 0x05) != 0x04);

    spi_handle->ser = 0x00;
    spi_handle->ssienr = 0x00;
}

uint32_t *buf;

void spi_Mysend_Dmac( sysctl_dma_channel_t channel , spi_device_num_t spi_num , 
                      spi_chip_select_t chip_select , uint8_t *Data , uint32_t Length )
{
    volatile spi_t *spi_handle = spi[spi_num];
    uint8_t tmod_offset = 8;

    set_bit(&spi_handle->ctrlr0, 3 << tmod_offset, SPI_TMOD_TRANS << tmod_offset);

    spi_handle->dmacr = 0x2;    /*enable dma transmit*/
    spi_handle->ssienr = 0x01;

    sysctl_dma_select((sysctl_dma_channel_t) channel, SYSCTL_DMA_SELECT_SSI0_TX_REQ + spi_num * 2 );

    dmac_set_channel_param(channel, Data, (void *)(&spi_handle->dr[0]), DMAC_ADDR_INCREMENT, DMAC_ADDR_NOCHANGE,
                                DMAC_MSIZE_4, DMAC_TRANS_WIDTH_32, Length / 4 );

    dmac_channel_enable(channel);

    spi_handle->ser = 1U << chip_select;
}

void LCDSendByte( uint8_t *DataBuf , uint32_t Length )
{
    spi_Mysend_32Bitdata( SPI_CHANNEL , SPI_SLAVE_SELECT , DataBuf , Length );
    //spi_Mysend_Dmac( DMAC_CHANNEL1 , SPI_CHANNEL , SPI_SLAVE_SELECT , DataBuf , Length);
}

uint8_t     TestBuff[1600];
uint8_t     LCD_Buff[320*240*2]__attribute__((aligned(64)));
uint32_t    FpsTime;
uint32_t    PageCount;
uint8_t     *pBuff = &ImageData[0];

void irq_RS_Sync()
{
    if( FpsTime > 150 )
        PageCount = 0;
    else 
        PageCount ++;

    if( PageCount < 240 )
    LCDSendByte( &LCD_Buff[ PageCount * 640 ] , 640 );

    FpsTime = 0;
}
void irq_time( void )
{
    FpsTime ++ ;
}

#define LCDDisMode_normal   0x01
#define LCDDisMode_TranBK   0x00
#define LCDDisMode_Reverse  0x10

void LCDPrintChar( uint16_t XPos , uint16_t YPos , char CharData ,uint8_t Mode , uint16_t FontColor ,uint16_t BKColor)
{
    for( uint8_t y = 0 ; y < 7 ; y++ )
    {
        uint8_t Data = FontLib[ ( CharData - 0x20 ) * 7 + y ];
        for( uint8_t x = 0 ; x < 5 ; x++ )
        {
            if( Data & ( 0x80 >> x ))
            {
                if( Mode & LCDDisMode_Reverse )
                {
                    LCD_Buff[ ( (YPos+y) * 640 ) + ( (XPos+x) * 2 ) ]       = ~LCD_Buff[ ( (YPos+y) * 640 ) + ( (XPos+x) * 2 ) ];
                    LCD_Buff[ ( (YPos+y) * 640 ) + ( (XPos+x) * 2 ) + 1 ]   = ~LCD_Buff[ ( (YPos+y) * 640 ) + ( (XPos+x) * 2 ) + 1 ];
                }
                else 
                {
                    LCD_Buff[ ( (YPos+y) * 640 ) + ( (XPos+x) * 2 ) ]       = FontColor & 0x00FF;
                    LCD_Buff[ ( (YPos+y) * 640 ) + ( (XPos+x) * 2 ) + 1 ]   = FontColor >> 8;
                }
            }
            else
            {
                if( Mode & LCDDisMode_normal )
                {
                    if( Mode & LCDDisMode_Reverse )
                    {
                        LCD_Buff[ ( (YPos+y) * 640 ) + ( (XPos+x) * 2 ) ]       = FontColor & 0x00FF;
                        LCD_Buff[ ( (YPos+y) * 640 ) + ( (XPos+x) * 2 ) + 1 ]   = FontColor >> 8;
                    }
                    else
                    {
                        LCD_Buff[ ( (YPos+y) * 640 ) + ( (XPos+x) * 2 ) ]       = BKColor & 0x00FF;
                        LCD_Buff[ ( (YPos+y) * 640 ) + ( (XPos+x) * 2 ) + 1 ]   = BKColor >> 8;
                    }     
                } 
            }
        }
    }
}

void LCDPrintStr( uint16_t XPos , uint16_t YPos , char *Srt , uint8_t Mode , uint16_t FontColor ,uint16_t BKColor )
{
    while( *Srt != '\0' )
    {
        LCDPrintChar( XPos , YPos , *Srt , Mode ,FontColor , BKColor );
        XPos += 6 ; 
        Srt ++;
    }
}

FATFS fs;  		
FIL file;	  			
UINT br,bw;			
FILINFO fileinfo;
// f_open (FATFS *fs, FIL* fp, const TCHAR* path, BYTE mode);
#pragma pack(1)
typedef struct tagBITMAPFILEHEADER { // bmfh 
    WORD    bfType; 
    DWORD   bfSize; 
    DWORD   bfReserved; 
    DWORD   bfOffBits; 
} BITMAPFILEHEADER;

typedef struct tagBITMAPINFOHEADER{ // bmih 
    DWORD  biSize; 
    LONG   biWidth; 
    LONG   biHeight; 
    WORD   biPlanes; 
    WORD   biBitCount;
    DWORD  biCompression; 
    DWORD  biSizeImage; 
    LONG   biXPelsPerMeter; 
    LONG   biYPelsPerMeter; 
    DWORD  biClrUsed; 
    DWORD  biClrImportant; 
} BITMAPINFOHEADER; 

#pragma pack()
bool BMPFile( FATFS *fs , const TCHAR* path )
{
    UINT    fbr;
    BITMAPFILEHEADER    BmpHeadr;
    BITMAPINFOHEADER    InfHeard;

    if( f_open( fs , &file , path , FA_READ ) != FR_OK )
    {
        printf( "Debug:%s:%d \r\n",__FILE__,__LINE__);
        return false;
    }

    if( f_read( &file , &BmpHeadr , sizeof( BmpHeadr ), &fbr ) != FR_OK )
    {
        printf( "Debug:%s:%d \r\n",__FILE__,__LINE__);
        return false;
    }

    LCDPrintStr( 4,104,(char * )path,LCDDisMode_TranBK,0xF800,0x0000);

    if( f_read( &file , &InfHeard , sizeof( InfHeard ), &fbr ) != FR_OK )
    {
        printf( "Debug:%s:%d \r\n",__FILE__,__LINE__);
        return false;
    }

    uint32_t YPos = 32; 
    uint32_t XPos = 0; 
    uint32_t XLine = InfHeard.biWidth * 3;
    uint16_t Color;
    for( uint32_t y = InfHeard.biHeight ; y > 0 ; y-- )
    {
        f_read( &file , TestBuff , XLine , &fbr );
        for( uint32_t x = XLine ; x >0 ; x = x-3 )
        {
            Color = 0;
            Color |= (( TestBuff[x] & 0xF8 ) << 8 );
            Color |= (( TestBuff[x+1] & 0xFC ) << 3 );
            Color |= ( TestBuff[x+2] >> 3 )& 0x1f;

            LCD_Buff[ ( (YPos+y) * 640 ) + ( (XPos+(x/3)) * 2 ) ]       = Color & 0x00FF;
            LCD_Buff[ ( (YPos+y) * 640 ) + ( (XPos+(x/3)) * 2 ) + 1 ]   = Color >> 8;
        }
    }
    
    f_close( &file );

    return true;
}

int core1_function(void *ctx)
{
    uint64_t core = current_coreid();
    printf("Core %ld Fuck STC\r\n", core);

    while(1);
}

int main()
{ 
    uint64_t core = current_coreid();
    
    sysctl_pll_set_freq(SYSCTL_PLL0, PLL0_OUTPUT_FREQ);
    sysctl_pll_set_freq(SYSCTL_PLL1, PLL1_OUTPUT_FREQ);
    sysctl_pll_set_freq(SYSCTL_PLL2, PLL2_OUTPUT_FREQ);
    sysctl_clock_enable(SYSCTL_CLOCK_AI);

    uarths_init();
    InitLCDHard();
    plic_init ();

    fpioa_set_function      ( 12 , FUNC_GPIOHS4 );
	gpiohs_set_drive_mode   ( 4  , GPIO_DM_OUTPUT );
	gpiohs_set_pin          ( 4  , GPIO_PV_LOW );

    fpioa_set_function      ( 17 , FUNC_GPIOHS5 );
	gpiohs_set_drive_mode   ( 5  , GPIO_DM_OUTPUT );
	gpiohs_set_pin          ( 5  , GPIO_PV_LOW );

    uint8_t Res = sd_init();
    printf( "SD Card Init : %d\r\n" , Res);
    
    FRESULT FatfsRes = f_mount( &fs );
    printf( "f_mount : %d\r\n" , FatfsRes);
    
    char StrBuff[64];
    uint16_t    PicNum = 1;

    LCDPrintStr( 4,15,"FUCK STC",LCDDisMode_TranBK,0xF800,0x0000);

    printf("Core %ld Fuck STC\r\n", core);
    register_core1(core1_function, NULL);

    timer_init ( TIMER_DEVICE_0 );
    timer_set_interval ( TIMER_DEVICE_0 , TIMER_CHANNEL_0 , 1e3 );
    timer_set_irq ( TIMER_CHANNEL_0 , TIMER_CHANNEL_0 , irq_time , 1);

    sprintf( StrBuff ,"./%d/BC%04d.bmp",1,369);
    BMPFile( &fs ,StrBuff);

    LCDSendCMD(LCD_BL_ON);
    LCDSendCMD(LCD_DISOLAY_ON);

    dmac_cfg_u_t  dmac_cfg;

    dmac_cfg.data = readq(&dmac->cfg);
    dmac_cfg.cfg.dmac_en = 1;
    dmac_cfg.cfg.int_en = 1;
    writeq(dmac_cfg.data, &dmac->cfg);

    spi_init(SPI_CHANNEL, SPI_WORK_MODE_0, SPI_FF_OCTAL, 32, 1);
    spi_init_non_standard(SPI_CHANNEL, 0 /*instrction length*/, 32 /*address length*/, 0 /*wait cycles*/,
                          SPI_AITM_AS_FRAME_FORMAT /*spi address trans mode*/);

    gpiohs_set_drive_mode   (DCX_GPIONUM, GPIO_DM_INPUT);
    gpiohs_set_pin_edge     (DCX_GPIONUM, GPIO_PE_FALLING );
    gpiohs_set_irq          (DCX_GPIONUM, 2, irq_RS_Sync );

    timer_set_enable ( TIMER_CHANNEL_0 , TIMER_CHANNEL_0 , 1);

    sysctl_enable_irq ();

    while(1)
    {
        /*
        if( PicNum > 949 )
        PicNum = 0;
        sprintf( StrBuff ,"./%d/BC%04d.bmp",1,PicNum);
        BMPFile( &fs ,StrBuff);
        PicNum ++;
        */
    }
}
