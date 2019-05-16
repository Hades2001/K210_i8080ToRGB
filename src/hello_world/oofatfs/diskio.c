/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2011        */
/*-----------------------------------------------------------------------*/
/* This is a stub disk I/O module that acts as front end of the existing */
/* disk I/O modules and attach it to FatFs module with common interface. */
/*-----------------------------------------------------------------------*/

#include "sdcard.h"
#include "rtc.h"
#include "ff.h"
#include "diskio.h"
#define BLOCK_SIZE            512 /* Block Size in Bytes */

DWORD get_fattime(void) {

        uint32_t year = 0;
        uint32_t mon = 0;
        uint32_t mday = 0;
        uint32_t hour = 0;
        uint32_t min = 0;
        uint32_t sec = 0;
        rtc_timer_get((int *)&year,(int *)&mon,(int *)&mday,(int *)&hour,(int *)&min,(int *)&sec); //Pos
        return (year << 25) | (mon << 21) | (mday << 16) | (hour << 11) | (min << 5) | ( sec / 2 );
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */


DRESULT disk_read (void *drv, BYTE* buff, DWORD sector, UINT count)
{
// SD_Error Status;
sd_read_sector_dma(buff, sector, count);
 return RES_OK;
}

 

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
DRESULT disk_write (void *drv, const BYTE* buff, DWORD sector, UINT count)
{
sd_write_sector_dma((uint8_t *)buff, sector, count); //Pos
 return RES_OK;
}

 


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */

DRESULT disk_ioctl (void *drv, BYTE cmd, void* buff)
{

*((DSTATUS*)buff)=0;
 return RES_OK;
}
        