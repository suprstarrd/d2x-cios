/*-------------------------------------------------------------

usbstorage.c -- Bulk-only USB mass storage support

Copyright (C) 2008
Sven Peter (svpe) <svpe@gmx.net>

quick port to ehci/ios: Kwiirk
Copyright (C) 2011 davebaol
Copyright (C) 2017 GerbilSoft
Copyright (C) 2022 cyberstudio

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1.	The origin of this software must not be misrepresented; you
must not claim that you wrote the original software. If you use
this software in a product, an acknowledgment in the product
documentation would be appreciated but is not required.

2.	Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3.	This notice may not be removed or altered from any source
distribution.

-------------------------------------------------------------*/

#define ROUNDDOWN32(v)				(((u32)(v)-0x1f)&~0x1f)

//#define	HEAP_SIZE			(32*1024)
//#define	TAG_START			0x1//BADC0DE
#define	TAG_START			0x22112211

#define	CBW_SIZE			31
#define	CBW_SIGNATURE			0x43425355
#define	CBW_IN				(1 << 7)
#define	CBW_OUT				0


#define	CSW_SIZE			13
#define	CSW_SIGNATURE			0x53425355

#define	SCSI_TEST_UNIT_READY		0x00
#define	SCSI_INQUIRY			0x12
#define	SCSI_REQUEST_SENSE		0x03
#define	SCSI_START_STOP			0x1b
#define	SCSI_READ_CAPACITY		0x25
#define	SCSI_READ_10			0x28
#define	SCSI_WRITE_10			0x2A

#define	SCSI_SENSE_REPLY_SIZE		18
#define	SCSI_SENSE_NOT_READY		0x02
#define	SCSI_SENSE_MEDIUM_ERROR		0x03
#define	SCSI_SENSE_HARDWARE_ERROR	0x04

#define	USB_CLASS_MASS_STORAGE		0x08
#define USB_CLASS_HUB				0x09

#define	MASS_STORAGE_RBC_COMMANDS		0x01
#define	MASS_STORAGE_ATA_COMMANDS		0x02
#define	MASS_STORAGE_QIC_COMMANDS		0x03
#define	MASS_STORAGE_UFI_COMMANDS		0x04
#define	MASS_STORAGE_SFF8070_COMMANDS	0x05
#define	MASS_STORAGE_SCSI_COMMANDS		0x06

#define	MASS_STORAGE_BULK_ONLY		0x50

#define USBSTORAGE_GET_MAX_LUN		0xFE
#define USBSTORAGE_RESET		0xFF

#define	USB_ENDPOINT_BULK		0x02

#define USBSTORAGE_CYCLE_RETRIES	3	// retry at 2s, 4s, 8s. Better chance of not timing out during a reset than retrying 10 times at 1x intervals.

#define MAX_TRANSFER_SIZE			4096

#define DEVLIST_MAXSIZE    8

extern u32 ums_mode;       //2022-03-05 USB Mass Storage init should be done once per USB port, even if there are multiple LUNs

extern int handshake_mode; // 0->timeout force -ENODEV (unplug_device) 1->timeout return -ETIMEDOUT

//static bool first_access=true;

static usbstorage_handle __usbfd;
static u8 __lun[2] = {16,16};
static u8 __mounted[2] = {0,0};	//2022-03-02 if both LUNs are umounted we can close the USB port
static u16 __vid = 0;
static u16 __pid = 0;
u32 current_drive = 0;			//This is set by the EHCI loop's USB_IOCTL_UMS_SET_PORT extension

//0x1377E000

//#define MEM_PRINT 1

#ifdef MEM_PRINT

// this dump from 0x13750000 to 0x13770000 log messages 

char mem_cad[32];

char *mem_log=(char *) 0x13750000;

#include <stdarg.h>    // for the s_printf function

void int_char(int num)
{
int sign=num<0;
int n,m;

	if(num==0)
		{
		mem_cad[0]='0';mem_cad[1]=0;
		return;
		}

	for(n=0;n<10;n++)
		{
		m=num % 10;num/=10;if(m<0) m=-m;
		mem_cad[25-n]=48+m;
		}

	mem_cad[26]=0;

	n=0;m=16;
	if(sign) {mem_cad[n]='-';n++;}

	while(mem_cad[m]=='0') m++;

	if(mem_cad[m]==0) m--;

	while(mem_cad[m]) 
	 {
	 mem_cad[n]=mem_cad[m];
	 n++;m++;
	 }
	mem_cad[n]=0;

}

void hex_char(u32 num)
{
int n,m;

	if(num==0)
		{
		mem_cad[0]='0';mem_cad[1]=0;
		return;
		}

	for(n=0;n<8;n++)
		{
		m=num & 15;num>>=4;
		if(m>=10) m+=7;
		mem_cad[23-n]=48+m;
		}

	mem_cad[24]=0;

	n=0;m=16;
    
	mem_cad[n]='0';n++;
	mem_cad[n]='x';n++;

	while(mem_cad[m]=='0') m++;

	if(mem_cad[m]==0) m--;

	while(mem_cad[m]) 
	 {
	 mem_cad[n]=mem_cad[m];
	 n++;m++;
	 }
	mem_cad[n]=0;

}


void s_printf(char *format,...)
{
 va_list	opt;
 char *t;
 int pos=0;

 int val,n;

 char *s;

 if(mem_log>(char *) 0x13770000) mem_log=(char *) 0x13750000;

 t=mem_log;
 va_start(opt, format);

 while(format[0])
	{
	if(format[0]!='%') {*mem_log++=*format++;pos++;}
	else
		{
		format++;
		switch(format[0])
			{
			case 'd':
			case 'i':
				val=va_arg(opt,int);
				int_char(val);
				n=strlen(mem_cad);
				memcpy(mem_log,mem_cad,n);
				mem_log+=n;pos+=n;
				break;

			case 'x':
				val=va_arg(opt,int);
                hex_char((u32) val);
				n=strlen(mem_cad);
				memcpy(mem_log,mem_cad,n);
				mem_log+=n;pos+=n;
				break;

			case 's':
				s=va_arg(opt,char *);
			    n=strlen(s);
				memcpy(mem_log,s,n);
				mem_log+=n;pos+=n;
				break;

			}
		 format++;
		}
	
	}
   
	va_end(opt);

	os_sync_after_write((void *) t, pos);
	
	
}

void log_status(char *s)
{
u32 status=ehci_readl( &ehci->regs->status);
u32 statusp=ehci_readl(&ehci->regs->port_status[0]);

s_printf("    log_status  (%s)\n",s);
s_printf("    status: %x %s%s%s%s%s%s%s%s%s%s\n",
		status,
		(status & STS_ASS) ? " Async" : "",
		(status & STS_PSS) ? " Periodic" : "",
		(status & STS_RECL) ? " Recl" : "",
		(status & STS_HALT) ? " Halt" : "",
		(status & STS_IAA) ? " IAA" : "",
		(status & STS_FATAL) ? " FATAL" : "",
		(status & STS_FLR) ? " FLR" : "",
		(status & STS_PCD) ? " PCD" : "",
		(status & STS_ERR) ? " ERR" : "",
		(status & STS_INT) ? " INT" : ""
		);

 s_printf("    status port: %x\n", statusp);
}
			
#endif


static s32 __usbstorage_reset(usbstorage_handle *dev,int hard_reset);
static s32 __usbstorage_clearerrors(usbstorage_handle *dev, u8 lun);
static s32 __usbstorage_start_stop(usbstorage_handle *dev, u8 lun, u8 start_stop);

// ehci driver has its own timeout.
static s32 __USB_BlkMsgTimeout(usbstorage_handle *dev, u8 bEndpoint, u16 wLength, void *rpData)
{
	return USB_WriteBlkMsg(dev->usb_fd, bEndpoint, wLength, rpData);
}

static s32 __USB_CtrlMsgTimeout(usbstorage_handle *dev, u8 bmRequestType, u8 bmRequest, u16 wValue, u16 wIndex, u16 wLength, void *rpData)
{
	return USB_WriteCtrlMsg(dev->usb_fd, bmRequestType, bmRequest, wValue, wIndex, wLength, rpData);
}

static s32 __send_cbw(usbstorage_handle *dev, u8 lun, u32 len, u8 flags, const u8 *cb, u8 cbLen)
{
	s32 retval = USBSTORAGE_OK;

	if(cbLen == 0 || cbLen > 16 || !dev->buffer)
		return -EINVAL;
	memset(dev->buffer, 0, CBW_SIZE);

	((u32*)dev->buffer)[0]=cpu_to_le32(CBW_SIGNATURE);
	((u32*)dev->buffer)[1]=cpu_to_le32(dev->tag);
	((u32*)dev->buffer)[2]=cpu_to_le32(len);
	dev->buffer[12] = flags;
	dev->buffer[13] = lun;
	// linux usb/storage/protocol.c seems to say only difference is padding
        // and fixed cw size
        if(dev->ata_protocol)
                dev->buffer[14] = 12; 
        else
                dev->buffer[14] = (cbLen > 6 ? 0x10 : 6);
        //debug_printf("send cb of size %d\n",dev->buffer[14]);
	memcpy(dev->buffer + 15, cb, cbLen);
        //hexdump(dev->buffer,CBW_SIZE);
	retval = __USB_BlkMsgTimeout(dev, dev->ep_out, CBW_SIZE, (void *)dev->buffer);

	if(retval == CBW_SIZE) return USBSTORAGE_OK;
	else if(retval >= 0) return USBSTORAGE_ESHORTWRITE;

	return retval;
}

static s32 __read_csw(usbstorage_handle *dev, u8 *status, u32 *dataResidue)
{
	s32 retval = USBSTORAGE_OK;
	u32 signature, tag, _dataResidue, _status;

	memset(dev->buffer, 0xff, CSW_SIZE);

	retval = __USB_BlkMsgTimeout(dev, dev->ep_in, CSW_SIZE, dev->buffer);
        //print_hex_dump_bytes("csv resp:",DUMP_PREFIX_OFFSET,dev->buffer,CSW_SIZE);

	if(retval >= 0 && retval != CSW_SIZE) return USBSTORAGE_ESHORTREAD;
	else if(retval < 0) return retval;

	signature = le32_to_cpu(((u32*)dev->buffer)[0]);
	tag = le32_to_cpu(((u32*)dev->buffer)[1]);
	_dataResidue = le32_to_cpu(((u32*)dev->buffer)[2]);
	_status = dev->buffer[12];
        //debug_printf("csv status: %d\n",_status);
	if(signature != CSW_SIGNATURE) {
               // BUG();
                return USBSTORAGE_ESIGNATURE;
        }

	if(dataResidue != NULL)
		*dataResidue = _dataResidue;
	if(status != NULL)
		*status = _status;

	if(tag != dev->tag) return USBSTORAGE_ETAG;
	dev->tag++;

	return USBSTORAGE_OK;
}

extern u32 usb_timeout;


static s32 __cycle(usbstorage_handle *dev, u8 lun, u8 *buffer, u32 len, u8 *cb, u8 cbLen, u8 write, u8 *_status, u32 *_dataResidue)
{
	s32 retval = USBSTORAGE_OK;

	u8 status = 0;
	u32 dataResidue = 0;
	u32 thisLen;

	u8 *buffer2=buffer;
	u32 len2=len;

	s8 retries = USBSTORAGE_CYCLE_RETRIES + 1;

	do
	{
#if defined(MEM_PRINT)
    if(retval < 0)
	{
		u32 sec=(cb[2]<<24)|(cb[3]<<16)|(cb[4]<<8)|cb[5];
		debug_printf("%d %x %d %d\t%d\n", retval, cb[0], lun, sec, get_timer());
	}
#endif
	if(retval==-ENODEV) {unplug_device=1;return -ENODEV;}
	
	

    if(retval < 0)
          retval=__usbstorage_reset(dev,retries < USBSTORAGE_CYCLE_RETRIES);
	
	retries--;
	if(retval<0){
		if (__mounted[current_drive])
			continue; // nuevo
		else
		{
			debug_printf("dismounted giving up %d %d %d %d\n",retval,retries,usb_timeout,get_timer());
			break;
		}
	}
	buffer=buffer2;
	len=len2;

		if(write)
		{

			retval = __send_cbw(dev, lun, len, CBW_OUT, cb, cbLen);
			if(retval < 0)
			{
				debug_printf("__send_cbw CBW_OUT failed retval=%d retries=%d, resetting...\n",retval,retries);
				continue;//reset+retry
			}
			while(len > 0)
			{
                thisLen=len;
				retval = __USB_BlkMsgTimeout(dev, dev->ep_out, thisLen, buffer);
           
				if(retval==-ENODEV || retval==-ETIMEDOUT) break;

				if(retval < 0)
				{
					debug_printf("__USB_BlkMsgTimeout CBW_OUT failed retval=%d retries=%d, resetting...\n",retval,retries);
					continue;//reset+retry
				}
				if(retval != thisLen && len > 0)
				{
					retval = USBSTORAGE_EDATARESIDUE;
					debug_printf("Something wrong with data length CBW_OUT retval=%d retries=%d, resetting...\n",retval,retries);
					continue;//reset+retry
				}
				len -= retval;
				buffer += retval;
			}

			if(retval < 0)
			{
				debug_printf("__cycle CBW_OUT retval=%d retries=%d, resetting...\n",retval,retries);
				continue;
			}
		}
		else
		{
			retval = __send_cbw(dev, lun, len, CBW_IN, cb, cbLen);

			if(retval < 0)
			{
				debug_printf("__send_cbw CBW_IN failed retval=%d retries=%d usb_timeout=%d, resetting...\n",retval,retries,usb_timeout);
				continue; //reset+retry
			}
			while(len > 0)
			{
                thisLen=len;
				
				retval = __USB_BlkMsgTimeout(dev, dev->ep_in, thisLen, buffer);
				
				if(retval==-ENODEV || retval==-ETIMEDOUT) break;
				
				if(retval < 0)
				{
					debug_printf("__USB_BlkMsgTimeout CBW_IN failed retval=%d retries=%d, resetting...\n",retval,retries);
					continue; //reset+retry
                                //hexdump(buffer,retval);
				}               
				len -= retval;
				buffer += retval;

				if(retval != thisLen)
                                {
                                        retval = -1;
										debug_printf("Something wrong with data length CBW_IN retval=%d retries=%d, resetting...\n",retval,retries);
                                        continue; //reset+retry
                                }
			}

			if(retval < 0)
			{
				debug_printf("__cycle CBW_IN retval=%d retries=%d timeout=%d, resetting...\n",retval,retries,usb_timeout);
				continue;
			}
		}

		retval = __read_csw(dev, &status, &dataResidue);

		if(retval < 0)
		{
			debug_printf("__read_csw retval=%d retries=%d, resetting...\n",retval,retries);
			continue;
		}
		retval = USBSTORAGE_OK;
	} while(retval < 0 && retries > 0);

    // force unplug
	if(retval < 0 && retries <= 0 && handshake_mode==0) {unplug_device=1;return -ENODEV;}

	if(retval < 0 && retval != USBSTORAGE_ETIMEDOUT)
	{
		debug_printf("__cycle failed retval=%d retries=%d, resetting...\n",retval,retries);
		if(__usbstorage_reset(dev,0) == USBSTORAGE_ETIMEDOUT)
			retval = USBSTORAGE_ETIMEDOUT;
	}


	if(_status != NULL)
		*_status = status;
	if(_dataResidue != NULL)
		*_dataResidue = dataResidue;

	return retval;
}

static s32 __usbstorage_start_stop(usbstorage_handle *dev, u8 lun, u8 start_stop)
{
	#if 1
	/* For a decade we've commented out this code. But I am reinstating this, because we want spin up. Spin up is
	 * the solution to error handling and must not be commented out, whereas spin down causes stutter, or worse,
	 * errors, and better not be called.
	 *
	 * Also I believe this was commented out because of a hang, but this is not the root cause of the hang. (Hard
	 * reset failing and calling hard reset reentrantly.)
	 *
	 * No one has called spin down in the past, and no one is spinning down now. The drive has an idle timer and
	 * it spins down when there is no activity. To deal with this, there has always been a watchdog timer that
	 * keeps the currently used drive active by reading some sector every 10 seconds. The other drive should go
	 * to sleep. That's by design because there is no point keeping both drives running. But this means there is
	 * a chance the other drive will be called to duty again in the future and it needs to be spun up.
	 */
	s32 retval;
	u8 cmd[16];
	
	u8 status = 0;

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = SCSI_START_STOP;
	cmd[1] = (lun << 5) | 1;	//"If the IMMED bit set to one, then the device server shall return status as soon as the CDB has been validated." - SCSI Command Reference Manual
	cmd[4] = start_stop & 3;
	cmd[5] = 0;
	//memset(sense, 0, SCSI_SENSE_REPLY_SIZE);
	retval = __cycle(dev, lun, NULL, 0, cmd, 6, 0, &status, NULL);
	if(retval < 0) goto error;

	if(status == SCSI_SENSE_NOT_READY || status == SCSI_SENSE_MEDIUM_ERROR || status == SCSI_SENSE_HARDWARE_ERROR) 
					retval = USBSTORAGE_ESENSE;
error:
	return retval;
	#else
	return 0;
	#endif
}


static s32 __usbstorage_clearerrors(usbstorage_handle *dev, u8 lun)
{
	s32 retval;
	u8 cmd[16];
	u8 *sense= USB_Alloc(SCSI_SENSE_REPLY_SIZE);
	u8 status = 0;
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = SCSI_TEST_UNIT_READY;
	int n;

	if(!sense) return -ENOMEM;
    


		for(n=0;n<5;n++)
			{
			
			retval = __cycle(dev, lun, NULL, 0, cmd, 6, 1, &status, NULL);

			#ifdef MEM_PRINT
			s_printf("    SCSI_TEST_UNIT_READY %i# ret %i\n", n, retval);
			#endif
		
			if(retval==-ENODEV) goto error;

			
			if(retval==0) break;
			}
		if(retval<0) goto error;
		

	if(status != 0)
	{
		cmd[0] = SCSI_REQUEST_SENSE;
		cmd[1] = lun << 5;
		cmd[4] = SCSI_SENSE_REPLY_SIZE;
		cmd[5] = 0;
		memset(sense, 0, SCSI_SENSE_REPLY_SIZE);
		retval = __cycle(dev, lun, sense, SCSI_SENSE_REPLY_SIZE, cmd, 6, 0, NULL, NULL);

		#ifdef MEM_PRINT
		s_printf("    SCSI_REQUEST_SENSE ret %i\n", retval);
		#endif

		if(retval < 0) goto error;
		
		status = sense[2] & 0x0F;

		#ifdef MEM_PRINT
		s_printf("    SCSI_REQUEST_SENSE status %x\n", status);
		#endif

		
		if(status == SCSI_SENSE_NOT_READY || status == SCSI_SENSE_MEDIUM_ERROR || status == SCSI_SENSE_HARDWARE_ERROR) 
                        retval = USBSTORAGE_ESENSE;
	}
error:
        USB_Free(sense);
	return retval;
}

/* "The Hollywood includes a simple 32-bit timer running at 1/128th of the Starlet core clock
 * frequency (~243Mhz)... The timer register is incremented every 1/128th of the core clock
 * frequency, or around every 526.7 nanoseconds." - wiibrew.org
 */
#define STARLET_HW_TIMER_ONE_SECOND	(243UL*1000000UL/128UL)
// The following is actually 2 seconds because handshake shifts the timeout left by 1 bit.
#define DEFAULT_UMS_TIMEOUT			(1UL*(STARLET_HW_TIMER_ONE_SECOND))		// 2 seconds
#define TIMEOUT_AFTER_HARD_RESET	(6UL*(STARLET_HW_TIMER_ONE_SECOND))
// Number of soft reset attempts before we use hard reset. It is not advisable to use hard
// reset to clear an error while spinning up because hard reset will unplug the drive and
// make it spin down, causing an infinite vicious cycle. Allow enough time for soft reset
// before giving up and use hard reset.
#define SOFT_RESET_RETRIES	1
static s32 __usbstorage_reset(usbstorage_handle *dev,int hard_reset)
{
	s32 retval;
	// u32 old_usb_timeout=usb_timeout;
    //int retry = hard_reset;
    int retry = 0;  //first try soft reset
 retry:
        if (retry >= (SOFT_RESET_RETRIES)){
                u8 conf;
                debug_printf("reset device..\n");
				usb_timeout=1000*1000;
				// USB reset may spin down both drives.
                retval = ehci_reset_device(dev->usb_fd);
				ehci_msleep(10);
				if(retval==-ENODEV) return retval;

                if(retval < 0 && retval != -7004)
                        goto end;
                // reset configuration
                if(USB_GetConfiguration(dev->usb_fd, &conf) < 0)
                        goto end;
                if(/*conf != dev->configuration &&*/ USB_SetConfiguration(dev->usb_fd, dev->configuration) < 0)
                        goto end;
                if(dev->altInterface != 0 && USB_SetAlternativeInterface(dev->usb_fd, dev->interface, dev->altInterface) < 0)
                        goto end;
				if(__lun[current_drive] != 16)
					{
					if(USBStorage_MountLUN(&__usbfd, __lun[current_drive]) < 0)
					   //deliberately return OK? the mount may have failed, but the reset is considered to have succeeded?
                       goto end;
					   __mounted[current_drive]=1;
					}
				usb_timeout=(TIMEOUT_AFTER_HARD_RESET);
        }
	/* A vicious cycle would have occurred during a file copy between 2 drives if we give the drives insufficient
	 * timeout. Drive 0 spins up from standby and timeout, which leads to a port reset. That kills drive 1, too,
	 * and causes it to spin down. Now drive 1 is accessed. It spins up, but timeout, and does another port reset.
	 * This time, it's drive 0 getting killed, ad infinitum. To give error handling a chance, give it more time.
	 */
	usb_timeout<<=1;	// increasingly lenient as we keep retrying. (see __cycle())
	debug_printf("usbstorage reset..%d\n",usb_timeout);
	retval = __USB_CtrlMsgTimeout(dev, (USB_CTRLTYPE_DIR_HOST2DEVICE | USB_CTRLTYPE_TYPE_CLASS | USB_CTRLTYPE_REC_INTERFACE), USBSTORAGE_RESET, 0, dev->interface, 0, NULL);

	#ifdef MEM_PRINT
	s_printf("usbstorage reset: Reset ret %i\n",retval);
	
	#endif

	/* FIXME?: some devices return -7004 here which definitely violates the usb ms protocol but they still seem to be working... */
        if(retval < 0 && retval != -7004)
                goto end;


	/* gives device enough time to process the reset */
	ehci_msleep(10);
	

        debug_printf("clear halt on bulk ep..\n");
	retval = USB_ClearHalt(dev->usb_fd, dev->ep_in);

	#ifdef MEM_PRINT
	s_printf("usbstorage reset: clearhalt in ret %i\n",retval);
	
	#endif
	if(retval < 0)
		goto end;
			ehci_msleep(10);
	retval = USB_ClearHalt(dev->usb_fd, dev->ep_out);

	#ifdef MEM_PRINT
	s_printf("usbstorage reset: clearhalt in ret %i\n",retval);
	
	#endif
	if(retval < 0)
		goto end;
    ehci_msleep(50);
    // usb_timeout=old_usb_timeout; 
	return retval;

end:
	if(retval==-ENODEV) return retval;
#ifdef HOMEBREW
	if(disable_hardreset) return retval;
        if(retry < 1){ //only 1 hard reset
                ehci_msleep(100);
                debug_printf("retry with hard reset..\n");
                retry ++;
                goto retry;
        }        
#else
		/* __cycle() calls __usbstorage_reset() to handle timeouts, but __usbstorage_reset() calls
		 * __cycle() to do stuff that has the potential to timeout. Infinite reentrancy. Check and
		 * prevent that from happening with the new handshake_mode=2.
		 */
        if(retry < 5 && (handshake_mode < 2 || (retry+1) < (SOFT_RESET_RETRIES))) {	// We must NOT hard reset if we are already hard resetting
                ehci_msleep(100);
                debug_printf("retry with hard reset..\n");
                
                retry ++;
                goto retry;
        }        
#endif      
	// usb_timeout=old_usb_timeout;  
	return retval;
}



int my_memcmp(char *a, char *b, int size_b)
{
int n;

for(n=0;n<size_b;n++) 
	{
	if(*a!=*b) return 1;
	a++;b++;
	}
return 0;
}

static usb_devdesc _old_udd; // used for device change protection

s32 try_status=0;

s32 USBStorage_Open(usbstorage_handle *dev, struct ehci_device *fd)
{
	s32 retval = -1;
	u8 conf,*max_lun = NULL;
	u32 iConf, iInterface, iEp;
	static usb_devdesc udd;
	usb_configurationdesc *ucd;
	usb_interfacedesc *uid;
	usb_endpointdesc *ued;
	
//	__lun[current_drive]= 16; // select bad LUN
	// We won't get max LUN at all - that's deferred until the user asks for Drive 1, or if Drive 0 is not found. Maybe never.
//	max_lun = USB_Alloc(1);
//	if(max_lun==NULL) return -ENOMEM;

	memset(dev, 0, sizeof(*dev));

	dev->tag = TAG_START;
        dev->usb_fd = fd;

    
	retval = USB_GetDescriptors(dev->usb_fd, &udd);

	#ifdef MEM_PRINT
	s_printf("USBStorage_Open(): USB_GetDescriptors %i\n",retval);
	#ifdef MEM_PRINT
	log_status("after USB_GetDescriptors");
	#endif
	
	#endif

	if(retval < 0)
		goto free_and_return;

	// test device changed without unmount (prevent device write corruption)
	if(ums_mode)
		{
		if(my_memcmp((void *) &_old_udd, (void *) &udd, sizeof(usb_devdesc)-4)) 
			{
//			USB_Free(max_lun);
			USB_FreeDescriptors(&udd);
			#ifdef MEM_PRINT
			s_printf("USBStorage_Open(): device changed!!!\n");
			
			#endif
			return -ENODEV;
			}
		}
	
	_old_udd= udd;
	try_status=-128;
	for(iConf = 0; iConf < udd.bNumConfigurations; iConf++)
	{
		ucd = &udd.configurations[iConf];		
		for(iInterface = 0; iInterface < ucd->bNumInterfaces; iInterface++)
		{
			uid = &ucd->interfaces[iInterface];
                  //      debug_printf("interface %d, class:%x subclass %x protocol %x\n",iInterface,uid->bInterfaceClass,uid->bInterfaceSubClass, uid->bInterfaceProtocol);
			if(uid->bInterfaceClass    == USB_CLASS_MASS_STORAGE &&
			   (uid->bInterfaceSubClass == MASS_STORAGE_SCSI_COMMANDS
				|| uid->bInterfaceSubClass == MASS_STORAGE_RBC_COMMANDS
                || uid->bInterfaceSubClass == MASS_STORAGE_ATA_COMMANDS
                || uid->bInterfaceSubClass == MASS_STORAGE_QIC_COMMANDS
				|| uid->bInterfaceSubClass == MASS_STORAGE_UFI_COMMANDS
				|| uid->bInterfaceSubClass == MASS_STORAGE_SFF8070_COMMANDS) &&
			   uid->bInterfaceProtocol == MASS_STORAGE_BULK_ONLY && uid->bNumEndpoints>=2)
			{
				
				dev->ata_protocol = 0;
                if(uid->bInterfaceSubClass != MASS_STORAGE_SCSI_COMMANDS || uid->bInterfaceSubClass != MASS_STORAGE_RBC_COMMANDS)
                                        dev->ata_protocol = 1;

				#ifdef MEM_PRINT
				s_printf("USBStorage_Open(): interface subclass %i ata_prot %i \n",uid->bInterfaceSubClass, dev->ata_protocol);
				
				#endif
				dev->ep_in = dev->ep_out = 0;
				for(iEp = 0; iEp < uid->bNumEndpoints; iEp++)
				{
					ued = &uid->endpoints[iEp];
					if(ued->bmAttributes != USB_ENDPOINT_BULK)
						continue;

					if(ued->bEndpointAddress & USB_ENDPOINT_IN)
						dev->ep_in = ued->bEndpointAddress;
					else
						dev->ep_out = ued->bEndpointAddress;
				}
				if(dev->ep_in != 0 && dev->ep_out != 0)
				{
					dev->configuration = ucd->bConfigurationValue;
					dev->interface = uid->bInterfaceNumber;
					dev->altInterface = uid->bAlternateSetting;
					
					goto found;
				}
			}
		else
			{


			if(uid->endpoints != NULL)
				USB_Free(uid->endpoints);
			uid->endpoints=NULL;

			if(uid->extra != NULL)
				USB_Free(uid->extra);
			uid->extra=NULL;

			if(uid->bInterfaceClass == USB_CLASS_HUB)
				{
				retval = USBSTORAGE_ENOINTERFACE;
				try_status= -20000;

				USB_FreeDescriptors(&udd);
        
				goto free_and_return;
				}

			if(uid->bInterfaceClass    == USB_CLASS_MASS_STORAGE &&
			   uid->bInterfaceProtocol == MASS_STORAGE_BULK_ONLY && uid->bNumEndpoints>=2)
					{
					try_status= -(10000+uid->bInterfaceSubClass);
					}
			}
		}
	}
    
	#ifdef MEM_PRINT
	s_printf("USBStorage_Open(): cannot find any interface!!!\n");
	
	#endif
	USB_FreeDescriptors(&udd);
	retval = USBSTORAGE_ENOINTERFACE;
        debug_printf("cannot find any interface\n");
	goto free_and_return;

found:
	USB_FreeDescriptors(&udd);

	retval = USBSTORAGE_EINIT;
	try_status=-1201;

	#ifdef MEM_PRINT
	s_printf("USBStorage_Open(): conf: %x altInterface: %x\n", dev->configuration, dev->altInterface);
	
	#endif

	if(USB_GetConfiguration(dev->usb_fd, &conf) < 0)
		goto free_and_return;
	try_status=-1202;

	#ifdef MEM_PRINT
	log_status("after USB_GetConfiguration");
	#endif

	#ifdef MEM_PRINT
	if(conf != dev->configuration)
		s_printf("USBStorage_Open(): changing conf from %x\n", conf);
	
	#endif
	if(/*conf != dev->configuration &&*/ USB_SetConfiguration(dev->usb_fd, dev->configuration) < 0)
		goto free_and_return;

	try_status=-1203;
	if(dev->altInterface != 0 && USB_SetAlternativeInterface(dev->usb_fd, dev->interface, dev->altInterface) < 0)
		goto free_and_return;

	try_status=-1204;
	
	#ifdef MEM_PRINT
	log_status("Before USBStorage_Reset");
	#endif
	retval = USBStorage_Reset(dev);
	#ifdef MEM_PRINT
	log_status("After USBStorage_Reset");
	#endif
	if(retval < 0)
		goto free_and_return;

   

/*	retval = __USB_CtrlMsgTimeout(dev, (USB_CTRLTYPE_DIR_DEVICE2HOST | USB_CTRLTYPE_TYPE_CLASS | USB_CTRLTYPE_REC_INTERFACE), USBSTORAGE_GET_MAX_LUN, 0, dev->interface, 1, max_lun);
	if(retval < 0 )
		dev->max_lun = 1;
	else
		dev->max_lun = (*max_lun+1);
	

	if(retval == USBSTORAGE_ETIMEDOUT)*/

	/* NOTE: from usbmassbulk_10.pdf "Devices that do not support multiple LUNs may STALL this command." */
	dev->max_lun = 1; // max_lun can be from 1 to 16, but some devices do not support lun.
	// Assume single drive by default. We will query maxlun IF user asks for Drive 1 OR LUN 0 cannot be mounted.
	retval = USBSTORAGE_OK;

	/*if(dev->max_lun == 0)
		dev->max_lun++;*/

	/* taken from linux usbstorage module (drivers/usb/storage/transport.c) */
	/*
	 * Some devices (i.e. Iomega Zip100) need this -- apparently
	 * the bulk pipes get STALLed when the GetMaxLUN request is
	 * processed.   This is, in theory, harmless to all other devices
	 * (regardless of if they stall or not).
	 */
	//USB_ClearHalt(dev->usb_fd, dev->ep_in);
	//USB_ClearHalt(dev->usb_fd, dev->ep_out);

	dev->buffer = USB_Alloc(MAX_TRANSFER_SIZE+16);
	
	if(dev->buffer == NULL) {retval = -ENOMEM;try_status=-1205;}
	else retval = USBSTORAGE_OK;

free_and_return:

	if(max_lun!=NULL) USB_Free(max_lun);

	if(retval < 0)
	{
		if(dev->buffer != NULL)
			USB_Free(dev->buffer);
		memset(dev, 0, sizeof(*dev));

		#ifdef MEM_PRINT
		s_printf("USBStorage_Open(): try_status %i\n",try_status);
		
		#endif
		return retval;
	}

	#ifdef MEM_PRINT
	s_printf("USBStorage_Open(): return 0\n");
	
	#endif

	return 0;
}

s32 USBStorage_Close(usbstorage_handle *dev)
{
	__mounted[current_drive] = 0;
	//2022-03-01 Close the entire USB port only if all drives are unmounted
	int i;
	for (i=0; i<sizeof(__mounted)/sizeof(__mounted[0]); i++)
	{
		if (0 != __mounted[i])
			return 0;
	}
	if(dev->buffer != NULL)
		USB_Free(dev->buffer);
	memset(dev, 0, sizeof(*dev));
	// If all LUNs are umounted or otherwise closed and app does an IOS_CLOSE afterwards the USB port itself will be closed.
	ums_mode=0;
	unplug_device=0;
	return 0;
}

s32 USBStorage_Reset(usbstorage_handle *dev)
{
	s32 retval;

	retval = __usbstorage_reset(dev,0);

	return retval;
}

s32 USBStorage_GetMaxLUN(usbstorage_handle *dev)
{

	return dev->max_lun;
}


s32 USBStorage_MountLUN(usbstorage_handle *dev, u8 lun)
{
	s32 retval;
    int f=handshake_mode;

	if(lun >= dev->max_lun)
		return -EINVAL;
	// Some drive/controller combinations requires at least 2 seconds to even attempt to spin up
	usb_timeout=(DEFAULT_UMS_TIMEOUT);
	handshake_mode=2;	// New handshake_mode, preventing a failing hard reset from infinitely calling a hard reset.

	retval= __usbstorage_start_stop(dev, lun, 1);

	#ifdef MEM_PRINT
	   s_printf("    start_stop cmd ret %i\n",retval);
	#endif
    if(retval < 0)
		goto ret;
	
	retval = __usbstorage_clearerrors(dev, lun);
	if(retval < 0)
		goto ret;
	usb_timeout=1000*1000;
	retval = USBStorage_Inquiry(dev, lun);
	#ifdef MEM_PRINT
	   s_printf("    Inquiry ret %i\n",retval);
	#endif
	if(retval < 0)
		goto ret;
	retval = USBStorage_ReadCapacity(dev, lun, &dev->sector_size[lun], &dev->n_sector[lun]);
	#ifdef MEM_PRINT
	   s_printf("    ReadCapacity ret %i\n",retval);
	#endif
ret:
	handshake_mode=f;
	return retval;
}

s32 USBStorage_Inquiry(usbstorage_handle *dev, u8 lun)
{
	s32 retval;
	u8 cmd[] = {SCSI_INQUIRY, lun << 5,0,0,36,0};
	u8 *response = USB_Alloc(36);

	if(!response) return -ENOMEM;

	retval = __cycle(dev, lun, response, 36, cmd, 6, 0, NULL, NULL);
        //print_hex_dump_bytes("inquiry result:",DUMP_PREFIX_OFFSET,response,36);
        USB_Free(response);
	return retval;
}
s32 USBStorage_ReadCapacity(usbstorage_handle *dev, u8 lun, u32 *sector_size, u32 *n_sectors)
{
	s32 retval;

	// FIX by digicroxx
	// Specifications state that SCSI_READ_CAPACITY has a 10 byte long Command Descriptor Block.
	// See page 32 at https://www.usb.org/developers/devclass_docs/usbmass-ufi10.pdf 
	// and https://en.wikipedia.org/wiki/SCSI_Read_Capacity_Command
	// Certain devices strictly require this command lenght.
	u8 cmd[] = {SCSI_READ_CAPACITY, lun << 5, 0, 0, 0, 0, 0, 0, 0, 0}; 
	u8 *response = USB_Alloc(8);
        u32 val;
	if(!response) return -ENOMEM;

	retval = __cycle(dev, lun, response, 8, cmd, sizeof(cmd), 0, NULL, NULL);
	if(retval >= 0)
	{
                
                memcpy(&val, response, 4);
		if(n_sectors != NULL)
                        *n_sectors = be32_to_cpu(val);
                memcpy(&val, response + 4, 4);
		if(sector_size != NULL)
			*sector_size = be32_to_cpu(val);
		retval = USBSTORAGE_OK;
	}
        USB_Free(response);
	return retval;
}



static s32 __USBStorage_Read(usbstorage_handle *dev, u8 lun, u32 sector, u16 n_sectors, u8 *buffer)
{
	u8 status = 0;
	s32 retval;
	u8 cmd[] = {
		SCSI_READ_10,
		lun << 5,
		sector >> 24,
		sector >> 16,
		sector >>  8,
		sector,
		0,
		n_sectors >> 8,
		n_sectors,
		0
		};
	if(lun >= dev->max_lun || dev->sector_size[lun] == 0 || !dev)
		return -EINVAL;
    
	retval = __cycle(dev, lun, buffer, ((u32) n_sectors) * dev->sector_size[lun], cmd, sizeof(cmd), 0, &status, NULL);
	if(retval > 0 && status != 0)
		retval = USBSTORAGE_ESTATUS;
	return retval;
}

static s32 __USBStorage_Write(usbstorage_handle *dev, u8 lun, u32 sector, u16 n_sectors, const u8 *buffer)
{
	u8 status = 0;
	s32 retval;
	u8 cmd[] = {
		SCSI_WRITE_10,
		lun << 5,
		sector >> 24,
		sector >> 16,
		sector >> 8,
		sector,
		0,
		n_sectors >> 8,
		n_sectors,
		0
		};
	if(lun >= dev->max_lun || dev->sector_size[lun] == 0)
		return -EINVAL;
	retval = __cycle(dev, lun, (u8 *)buffer, ((u32) n_sectors ) * dev->sector_size[lun], cmd, sizeof(cmd), 1, &status, NULL);
	if(retval > 0 && status != 0)
		retval = USBSTORAGE_ESTATUS;
	return retval;
}

s32 USBStorage_Read(usbstorage_handle *dev, u8 lun, u32 sector, u16 n_sectors, u8 *buffer)
{
	u32 max_sectors=n_sectors;
	u32 sectors;
	s32 ret=-1;

	// FIX d2x v6 final
	// This line has been replaced with the old one from d2x v3
	// since it breaks libfat/libntfs compatibility.
	//if(((u32) n_sectors) * dev->sector_size[lun]>64*1024) max_sectors= 64*1024/dev->sector_size[lun]; // Hermes: surely it fix a problem with some devices...
	if(n_sectors * dev->sector_size[lun]>32768) max_sectors= 32768/dev->sector_size[lun];

	while(n_sectors>0)
	{
		sectors=n_sectors>max_sectors ? max_sectors: n_sectors;
		ret=__USBStorage_Read(dev, lun, sector, sectors, buffer);
		if(ret<0) return ret;
		
		n_sectors-=sectors;
		sector+=sectors;
		buffer+=sectors * dev->sector_size[lun];
	}

	return ret;
}

s32 USBStorage_Write(usbstorage_handle *dev, u8 lun, u32 sector, u16 n_sectors, const u8 *buffer)
{
u32 max_sectors=n_sectors;
u32 sectors;
s32 ret=-1;

	// FIX d2x v6 final
	// This line has been replaced with the old one from d2x v3
	// since it breaks libfat/libntfs compatibility.
	//if(((u32) n_sectors) * dev->sector_size[lun]>64*1024) max_sectors=64*1024/dev->sector_size[lun]; // Hermes: surely it fix a problem with some devices...
	if((n_sectors * dev->sector_size[lun])>32768) max_sectors=32768/dev->sector_size[lun];

	while(n_sectors>0)
		{
		sectors=n_sectors>max_sectors ? max_sectors: n_sectors;
		ret=__USBStorage_Write(dev, lun, sector, sectors, buffer);
		if(ret<0) return ret;
		
		n_sectors-=sectors;
		sector+=sectors;
		buffer+=sectors * dev->sector_size[lun];
		}

return ret;
}

extern u32 next_sector;
// Before reading/writing any sector, check if drive is mounted first, and automount it if it isn't.
static bool check_if_dismounted(void)
{
	if(__mounted[current_drive])
		return false;		// already mounted. Good.

	if(!ums_mode)			// If USB Mass Storage init never done, no hope.
		return true;

	if(__lun[current_drive] >= 16)
		return true;		// Previous dismount, also no hope.

	if(USBStorage_MountLUN(&__usbfd, __lun[current_drive])>=0)
	{
		__mounted[current_drive]=1;	// Mark drive as mounted
		return false;		// successful wakeup. Good.
	}

	return true;
}

/*
The following is for implementing the ioctl interface inpired by the disc_io.h
as used by libfat

This opens the first lun of the first usbstorage device found.
*/



/* perform 512 time the same read */
s32 USBStorage_Read_Stress(u32 sector, u32 numSectors, void *buffer)
{
   s32 retval;
   int i;
   next_sector = sector;	// remember some close-by sector for the watchdog timer handler
   for(i=0;i<512;i++){
		   if(check_if_dismounted())
				return false;
           retval = USBStorage_Read(&__usbfd, __lun[current_drive], sector, numSectors, buffer);
           sector+=numSectors;
           if(retval == USBSTORAGE_ETIMEDOUT)
           {
                   //2022-03-05 mount flag setting now done by USBStorage_Close() __mounted = 0;
                   USBStorage_Close(&__usbfd);
           }
           if(retval < 0)
                   return false;
   }
   return true;

}

int test_max_lun=1;	// we only get max lun once and here is the flag to remember it.
s32 USBStorage_ScanLUN(void)
{
	if (__mounted[current_drive] && !unplug_device)
		return 0;

	int maxLun,j,retval;

	/*
       maxLun = USBStorage_GetMaxLUN(&__usbfd);
       if(maxLun == USBSTORAGE_ETIMEDOUT)
			{
		    __mounted = 0;
		    USBStorage_Close(&__usbfd);
			return -EINVAL;
			}
*/
	maxLun = __usbfd.max_lun;

	j = 0;			// LUN to start scanning from
	int found = 0;	// "Port"/drive to start scanning from
    if (current_drive > 0)
	{
		for (found = current_drive; found > 0; found--)
			if(__lun[found-1] < 16)
			{
				// This is where we left off the last time we last scanned for LUNs, and is where we start scanning this time.
				j = __lun[found-1] + 1;
				break;
			}
	}
      //for(j = 0; j < maxLun; j++)
	while(1)
       {

		   #ifdef MEM_PRINT
		   s_printf("USBStorage_MountLUN %i#\n", j);
		   #endif
           retval = USBStorage_MountLUN(&__usbfd, j);
		   #ifdef MEM_PRINT
		   s_printf("USBStorage_MountLUN: ret %i\n", retval);
		   #endif

		  
           if(retval == USBSTORAGE_ETIMEDOUT /*&& test_max_lun==0*/)
           { 
               //USBStorage_Reset(&__usbfd);
			   try_status=-121;
			   //2022-03-05 mount flag clearing done by USBStorage_Close() __mounted = 0;
               USBStorage_Close(&__usbfd); 
			   // We do not scan drive 1 while drive 0 has a timeout. This ensures the scan order is stable even in the case
			   // where drive 1 spins up faster than drive 0. So just stop here and return.
			   return -EINVAL;
             //  break;
           }

		   // Read boot sector
		   if(retval >= 0)
		   {
				u8 sector_buf[4096];
				int f=handshake_mode;
				handshake_mode=2;	// disable hard reset
				retval = __USBStorage_Read(&__usbfd, j, 0, 8, sector_buf);
				handshake_mode=f;
				if(retval == USBSTORAGE_ETIMEDOUT)
				{ 
					USBStorage_Close(&__usbfd); 
					return -EINVAL;
				}
				if(retval >= 0)
				{
					debug_printf("lun %d 1st read signature %x %x\t%d\n", j, sector_buf[510], sector_buf[511], get_timer());
					// discard first read. I've seen a controller so buggy which returns the boot sector of LUN 1 when that of LUN 0 is asked for.
					int f=handshake_mode;
					handshake_mode=2;	// disable hard reset
					retval = __USBStorage_Read(&__usbfd, j, 0, 8, sector_buf);
					handshake_mode=f;
					if(retval == USBSTORAGE_ETIMEDOUT)
					{ 
						USBStorage_Close(&__usbfd); 
						return -EINVAL;
					}
					if(retval >= 0)
					{
						debug_printf("lun %d 2nd read signature %x %x\t%d\n", j, sector_buf[510], sector_buf[511], get_timer());
						// Make sure this drive has a valid MBR/GPT signature.
						// If not, it might be a Wii U drive.
						if (sector_buf[510] != 0x55 ||
								(sector_buf[511] != 0xAA && sector_buf[511] != 0xAB))
						{
							j++;
							continue;
						}
					}
			   	}
		   }

           if(retval < 0)
				{
				if(test_max_lun)
					{
					__usbfd.max_lun = 0;
					retval = __USB_CtrlMsgTimeout(&__usbfd, 
						(USB_CTRLTYPE_DIR_DEVICE2HOST | USB_CTRLTYPE_TYPE_CLASS | USB_CTRLTYPE_REC_INTERFACE), 
						USBSTORAGE_GET_MAX_LUN, 0, __usbfd.interface, 1, &__usbfd.max_lun);
					if(retval < 0 )
						__usbfd.max_lun = 1;
					else __usbfd.max_lun++;
					maxLun = __usbfd.max_lun;

					 #ifdef MEM_PRINT
					 s_printf("USBSTORAGE_GET_MAX_LUN ret %i maxlun %i\n", retval,maxLun);
					 #endif
					test_max_lun=0;
					}
				else j++;

				if(j>=maxLun) break;
				continue;
				}
		   /*2022-03-05 vid pid now done right after USBStorage_Open()
           __vid=fd->desc.idVendor;
           __pid=fd->desc.idProduct;*/
		   __mounted[found] = 1;	// Mark drive as mounted
           __lun[found] = j++;		//remember LUN of found disk
		   usb_timeout=1000*1000;
		   try_status=0;
		   // 2022-03-02 yes we found a LUN but don't return just yet, until we have scanned both
		   // LUNs if port 1 is asked for.
		   if (found++ >= current_drive)
	           return 0;
		   /* If e.g. user sets USB Port to 1 for a single bay enclosure, we have just found LUN=0,
		    * initialization is considered done. But the return code of the init itself is a failure
			* because there is no LUN=1.
			*/
       }
	   try_status=-122;
	   //2022-03-05 mounted flag clearing done by USBStorage_Close() __mounted = 0;
	   USBStorage_Close(&__usbfd);

	   #ifdef MEM_PRINT
	   s_printf("USBStorage_MountLUN fail!!!\n");
	   #endif
	  
       return -EINVAL;
}

// temp function before libfat is available */
s32 USBStorage_Try_Device(struct ehci_device *fd)
{
	try_status=-120;
	test_max_lun=1;			// We need to call get max LUN only once (or maybe zero times). Keep track of that with a flag.
	if(USBStorage_Open(&__usbfd, fd) < 0)
		return -EINVAL;
    __vid=fd->desc.idVendor;
    __pid=fd->desc.idProduct;
	int i;
	for (i=0; i<sizeof(__mounted)/sizeof(__mounted[0]); i++)
	{
		__mounted[i] = 0;	// If we are (re)opening USB we are (re)scanning all LUNs. Mark all drives as unmounted.
		__lun[i] = 16;		// and mark as wrong LUN
	}
	return USBStorage_ScanLUN();
}

void USBStorage_Umount(void)
{
	// if(!ums_mode) return;	// If USB Mass Storage init never done, no need to umount anything.
	
	// if(__mounted[current_drive] && !unplug_device)
	// {
	// 	if(__usbstorage_start_stop(&__usbfd, __lun[current_drive], 0x0)==0) // stop
	// 	ehci_msleep(1000);
	// }
	// Umount and Close are almost synonyms because we are very averse to spin down anything.
	USBStorage_Close(&__usbfd);
	__lun[current_drive] = 16;	// remember umounted disk as bad LUN
/* 2022-03-05 these are now done by USBStorage_Close	__mounted=0;
	ums_init_done=0;
	unplug_device=0; */
}


s32 USBStorage_Init(void)
{
	int i;
	debug_printf("usbstorage init %d\n", ums_mode);
	if (ums_mode)
		/* 2022-03-04 Assume worse-case behaviour from a single-bay HDD enclosure:
		 * 1, querying max LUN would result in STALL
		 * 2, if accessing LUN=1 without first checking not only will the USB mass storage not
		 *    reject the invalid LUN as an error but it will ignore the LUN and access the
		 *    same drive.
		 * With those in mind, this is the rule:
		 * "Avoid querying max LUN like the plague, UNLESS the user explicitly asks for LUN=1
		 *  with USB Loader GX's USB Port setting or WiiXplorer's BOTH USB Port setting."
		 *
		 * Now consider how the app calls Init. If USB Port is set to 0 or 1, Init is
		 * called only once, with current_drive already set beforehand. If set to "Both",
		 * Init is called twice. There is only one real USB port and that needs to be
		 * initialized only once.
		 * 
		 * If Init is called a second time with current_drive=1 we still need to scan the
		 * second LUN. This is because during the first call we avoided running any max
		 * LUN query. ScanLUN has the logic to continue scanning where we left off.
		 */
		return USBStorage_ScanLUN();
	try_status=-1;      

#ifdef MEM_PRINT
s_printf("\n***************************************************\nUSBStorage_Init()\n***************************************************\n\n");

#endif

	for(i = 0;i<ehci->num_port; i++){
		struct ehci_device *dev = &ehci->devices[i];
				
		dev->port=i;

		if(dev->id != 0){
					
							
			handshake_mode=1;
			if(ehci_reset_port(0)>=0)
			{

				if(USBStorage_Try_Device(dev)==0) 
				{
					//first_access=true;
					handshake_mode=0;
					unplug_device=0;
					#ifdef MEM_PRINT
					s_printf("USBStorage_Init() Ok\n");
									
					#endif

					return 0;
				}
			}

		}
		else
		{
			u32 status;
			handshake_mode=1;
			status = ehci_readl(&ehci->regs->port_status[0]);

			#ifdef MEM_PRINT
			s_printf("USBStorage_Init() status %x\n",status);
					
			#endif
					
			if(status & 1)
			{
				if(ehci_reset_port2(0)<0)
				{
					ehci_msleep(100);
					ehci_reset_port(0);
				}
				return -101;
			}
			else 
			{
				return -100;
			}
		}
	}

	
	return try_status;
}

// FIX d2x v4beta1
// Now it returns an unsigned int to support HDD greater than 1TB.
u32 USBStorage_Get_Capacity(u32*sector_size)
{
	if(__mounted[current_drive] == 1)
	{
		if(sector_size){
			*sector_size = __usbfd.sector_size[__lun[current_drive]];
		}
		return __usbfd.n_sector[__lun[current_drive]];
	}
	return 0;
}

// Returns false if and only if a remount has taken place and that remount is successful
int unplug_procedure(void)
{
	int retval=1;

	if(unplug_device!=0 )
	{
			
		if(__usbfd.usb_fd)
			// If we rescan we will be killing both drives. If the scan returns drives in a different order we
			// will have data corruption. Don't set unplug_device except under circumstances where both drives
			// are gone, such as a real unplug. Don't set unplug_device merely because only one drive has a
			// timeout.
			if(ehci_reset_port2(/*__usbfd.usb_fd->port*/0)>=0)	
			{
				if(__usbfd.buffer != NULL)
					USB_Free(__usbfd.buffer);
				__usbfd.buffer= NULL;

				if(ehci_reset_port(0)>=0)
				{
					handshake_mode=1;
					if(USBStorage_Try_Device(__usbfd.usb_fd)==0)
					{
						retval=0;
						unplug_device=0;
					}
					else
						ums_mode=0;	// mark the entire USB Mass Storage as having been killed
					handshake_mode=0;
				}
				else
					ums_mode=0;		// mark the entire USB Mass Storage as having been killed
			}
		ehci_msleep(100);
	}

	return retval;
}

s32 USBStorage_Read_Sectors(u32 sector, u32 numSectors, void *buffer)
{
   s32 retval=0;
   int retry;
   next_sector = sector;	// remember some close-by sector for the watchdog timer handler
   for(retry=0;retry<16;retry++)
	{
	 if(retry>12) retry=12; // infinite loop
	//ehci_usleep(100);
		if(!unplug_procedure())	//This never returns false UNLESS a dismount/remount has taken place AND that was successful
		{
		 retval=0;
		}
	  	else if(check_if_dismounted())
         return false;
	
		// Don't unplug USB. It would have killed both drives.
		//if(retval == USBSTORAGE_ETIMEDOUT && retry>0)
		   //{
		   //unplug_device=1;
		   /*retval=__usbstorage_reset(&__usbfd,1);
		   if(retval>=0) retval=-666;
		   ehci_msleep(10);*/
		   //}
		if(unplug_device!=0) continue;	// unplug failed. Next thing to retry is still unplug, don't read just yet.
		 //if(retval==-ENODEV) return 0;
		 /* This is NOT a syscall to some os timer. It is implemented with direct hardware register
		  * access.
		  * 
		  * handshake() will multiply timeout by 2
		  */
		usb_timeout=(DEFAULT_UMS_TIMEOUT);
	    if(retval >= 0)
		{
	       handshake_mode=1;	// Let it time out instead of unplug. When there are 2 drives, unplugging will kill both the guilty and the innocent.
		   retval = USBStorage_Read(&__usbfd, __lun[current_drive], sector, numSectors, buffer);
	       handshake_mode=0;
		}
		usb_timeout=1000*1000;
		if(unplug_device!=0 ) continue;
		 //if(retval==-ENODEV) return 0;
		if(retval>=0) break;
	}

  /* if(retval == USBSTORAGE_ETIMEDOUT)
   {
       __mounted = 0;
       USBStorage_Close(&__usbfd);
   }*/
   if(retval < 0)
       return false;
   return true;
}


s32 USBStorage_Write_Sectors(u32 sector, u32 numSectors, const void *buffer)
{
   s32 retval=0;
   int retry;
	next_sector = sector;	// remember some close-by sector for the watchdog timer handler
    for(retry=0;retry<16;retry++)
	{
	 if(retry>12) retry=12; // infinite loop
	
	//ehci_usleep(100);

	  if(!unplug_procedure())
		{
		 retval=0;
		}
	  else if(check_if_dismounted())
         return false;
		//if(retval == USBSTORAGE_ETIMEDOUT && retry>0)
		   //{
		   //unplug_device=1;
		   //retval=__usbstorage_reset(&__usbfd,1);
		   //if(retval>=0) retval=-666;
		   //}
		  if(unplug_device!=0 ) continue;
		usb_timeout=(DEFAULT_UMS_TIMEOUT);	// same as Read above
	    if(retval >=0)
		{
	       handshake_mode=1;
		   retval = USBStorage_Write(&__usbfd, __lun[current_drive], sector, numSectors, buffer);
	       handshake_mode=0;
		}
		usb_timeout=1000*1000;
		if(unplug_device!=0 ) continue;
		if(retval>=0) break;
	}


  /* retval = USBStorage_Write(&__usbfd, __lun, sector, numSectors, buffer);
   if(retval == USBSTORAGE_ETIMEDOUT)
   {
       __mounted = 0;
       USBStorage_Close(&__usbfd);
   }
   */


   if(retval < 0)
       return false;
   return true;
}

