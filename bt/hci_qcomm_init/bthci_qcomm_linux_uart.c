/**
-----------------------------------------------------------------------------
Copyright (c) 2007-2009, 2011-2012, 2014 Qualcomm Technologies, Inc.
All Rights Reserved. Qualcomm Technologies Proprietary and Confidential.
-----------------------------------------------------------------------------
 *
 * @file bthci_qcomm_uart_linux.c
 * This file contains the Linux UART specific code for Bluetooth QUALCOMM SOC initialization
 */

/*===========================================================================

                        EDIT HISTORY FOR MODULE

  This section contains comments describing changes made to the module.
  Notice that changes are listed in reverse chronological order. Please
  use ISO format for dates.

  $Header: //linux/pkgs/proprietary/bt/main/source/hci_qcomm_init/bthci_qcomm_linux_uart.c#5 $
  $DateTime: 2008/12/12 14:34:53 $
  $Author: mfeldman $


  when        who  what, where, why
  ----------  ---  -----------------------------------------------------------
  2011-07-18  bn   Added support for 8960 for sending NVM tags to RIVA.
  2008-12-12  jmf  Fix -i <starting_baud> option and change baud after TX done.
  2008-08-28  jmf  Add error string from open fail.
  2008-08-08  jmf  Do select for all reads; pass in default arg vals from config.
  2008-03-31  jmf  Allow arbitrary baud rate change on Linux UART side.
  2008-03-13  jmf  Add new edit history format and P4 version control string.
  2007-09-06   jn  Adapted from AMSS/WM version.
===========================================================================*/


#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#ifdef ANDROID
#include <cutils/properties.h>
#endif

#include "comdef.h"
#include "bthci_qcomm.h"
#include "bthci_qcomm_pfal.h"
#include "bthci_qcomm_linux.h"


/* JN:  This is from hci_uart.h, the kernel header filer */
/* Ioctls */
#define HCIUARTSETPROTO         _IOW('U', 200, int)
#define HCIUARTGETPROTO         _IOR('U', 201, int)

/* UART protocols */
#define HCI_UART_MAX_PROTO      4

#define HCI_UART_H4             0
#define HCI_UART_BCSP           1
#define HCI_UART_3WIRE          2
#define HCI_UART_H4DS           3


/* JMF: speed_t and Bnnnn from ../../kernel/include/asm-arm/termbits.h */
/* To use the "new" way to set arbitrary baud rates, we need to define
   termios2 and BOTHER from the kernel header files.  We can't just
   include the kernel version of termios because it conflicts with GCCs
   standard (glibc) header files.  Note that the serial driver won't be
   able to support arbitrary baud rates because (on 7k family) the high
   speeds reference clocks are generated by the modem via table/switch.
*/
#if !defined(BOTHER) && 0  /* WE_STILL_NEED_HIGH_SPEED_HACK */
/* but let's try to provide what we'd see in a modern termios.h here */

#define    BOTHER 0010000

#if !defined(NCCS)
#define NCCS 19
#endif /* NCCS */

struct termios2 {
        tcflag_t c_iflag;               /* input mode flags */
        tcflag_t c_oflag;               /* output mode flags */
        tcflag_t c_cflag;               /* control mode flags */
        tcflag_t c_lflag;               /* local mode flags */
        cc_t c_line;                    /* line discipline */
        cc_t c_cc[NCCS];                /* control characters */
        speed_t c_ispeed;               /* input speed */
        speed_t c_ospeed;               /* output speed */
};

#if !defined(TCGETS2)
#define TCGETS2         _IOR('T',0x2A, struct termios2)
#define TCSETS2         _IOW('T',0x2B, struct termios2)
#endif /* TCGETS2 */

#endif /* BOTHER */

#define BAUDCLAUS(i) case (i): return ( B##i ) 

static speed_t convert_baud(uint32 baud_rate)
{
  switch (baud_rate)
  {
    BAUDCLAUS(50);
    BAUDCLAUS(75);
    BAUDCLAUS(110);
    BAUDCLAUS(134);
    BAUDCLAUS(150);
    BAUDCLAUS(200);
    BAUDCLAUS(300);
    BAUDCLAUS(600);
    BAUDCLAUS(1200);
    BAUDCLAUS(1800);
    BAUDCLAUS(2400);
    BAUDCLAUS(4800);
    BAUDCLAUS(9600);
    BAUDCLAUS(19200);
    BAUDCLAUS(38400);
    BAUDCLAUS(57600);
    BAUDCLAUS(115200);
    BAUDCLAUS(230400);
    BAUDCLAUS(460800);
    BAUDCLAUS(500000);
    BAUDCLAUS(576000);
    BAUDCLAUS(921600);
    BAUDCLAUS(1000000);
    BAUDCLAUS(1152000);
    BAUDCLAUS(1500000);
    BAUDCLAUS(2000000);
    BAUDCLAUS(2500000);
    BAUDCLAUS(3000000);
    BAUDCLAUS(3500000);
    BAUDCLAUS(4000000);

#if defined(BOTHER)  /* WE_NO_LONGER_NEED_HIGH_SPEED_HACK */
    default:
      return BOTHER;
#else /* not yet WE_NO_LONGER_NEED_HIGH_SPEED_HACK */
    case 3200000: return B200;
    default: return 0;
#endif /* WE_NO_LONGER_NEED_HIGH_SPEED_HACK */
  } /*NOTREACHED*/
}

extern int fd;    /* JMF: shoddy global */
extern uint32 starting_baud;    /* JMF: shoddy global */

/* Variables to identify the platform */
/*BT HS UART TTY DEVICE */
#define BT_HS_UART_DEVICE "/dev/ttyHS0"
/*BT RIVA-SMD CHANNELS */
#define APPS_RIVA_BT_ACL_CH  "/dev/smd2"
#define APPS_RIVA_BT_CMD_CH  "/dev/smd3"
#define WCNSS_SMD_CH_STATE  "/sys/devices/platform/wcnss_wlan.0/smd_channel_ready"

static char transport_type[PROPERTY_VALUE_MAX];


/*===========================================================================
FUNCTION   bt_hci_set_transport

DESCRIPTION
 sets the type of transport based on the msm type

DEPENDENCIES
  NIL

RETURN VALUE
returns the type of transport
SIDE EFFECTS
  None

===========================================================================*/
bt_hci_transport_device_type bt_hci_set_transport
(
  void
)
{
    int ret;
    bt_hci_transport_device_type bt_hci_transport_device;
#ifdef ANDROID
    ret = property_get("ro.qualcomm.bt.hci_transport", transport_type, NULL);
    if(ret == 0)
        printf("ro.qualcomm.bt.hci_transport not set\n");
    else
        printf("ro.qualcomm.bt.hci_transport: %s \n", transport_type);

    if (!strcasecmp(transport_type, "smd"))
    {
        bt_hci_transport_device.type = BT_HCI_SMD;
        bt_hci_transport_device.name = APPS_RIVA_BT_CMD_CH;
        bt_hci_transport_device.pkt_ind = 1;
    }
    else{
        bt_hci_transport_device.type = BT_HCI_UART;
        bt_hci_transport_device.name = BT_HS_UART_DEVICE;
        bt_hci_transport_device.pkt_ind = 0;
    }
#else
    //TODO: Currently on non-Android linux platforms,
    //transport defualted to UART. For supporting above transports,
    //platform specific changes need to be ported.
    bt_hci_transport_device.type = BT_HCI_UART;
    bt_hci_transport_device.name = BT_HS_UART_DEVICE;
    bt_hci_transport_device.pkt_ind = 0;
#endif

    return bt_hci_transport_device;
}

/*===========================================================================
FUNCTION   bt_hci_pfal_init_transport

DESCRIPTION
  Platform specific routine to intialise the UART/SMD resources.

PLATFORM SPECIFIC DESCRIPTION
  Opens the TTY/SMD device file descriptor, congiures the TTY/SMD device for CTS/RTS
  flow control,sets 115200 for TTY as the default baudrate and starts the Reader
  Thread

DEPENDENCIES
  NIL

RETURN VALUE
  RETURN VALUE
  STATUS_SUCCESS if SUCCESS, else other reasons

SIDE EFFECTS
  None

===========================================================================*/

int bt_hci_pfal_init_transport ( bt_hci_transport_device_type bt_hci_transport_device )
{
  struct termios   term;
  int retry = 0;
  int retry_wcnss_up = 0;
  int fd_wcnss;
  int nread;
  char buf[10];
  char ssrvalue[PROPERTY_VALUE_MAX]= {'\0'};
  ssrvalue[0] = '0';
  fd_wcnss = open(WCNSS_SMD_CH_STATE, (O_RDONLY | O_NOCTTY));

  if(fd_wcnss < 0)
     BTHCI_QCOMM_ERROR("Failed to open the %s \n ", WCNSS_SMD_CH_STATE);
  else {
     while (retry_wcnss_up < 5) {
       nread = read(fd_wcnss, buf, 10);
       BTHCI_QCOMM_ERROR("nread = %d, value = %d  \n ", nread, atoi(buf));
       /*check if the wcnss is ready or not, if ready the buf should be 1*/
       if(atoi(buf)) {
          close(fd_wcnss);
          break;
       }
       usleep(1000000);
       retry_wcnss_up++;
     }
  }
  if(retry_wcnss_up == 5) {
      BTHCI_QCOMM_ERROR("WCNSS subsystem is not up \n ");
      close(fd_wcnss);
      return -1;
  }

  fd = open(bt_hci_transport_device.name, (O_RDWR | O_NOCTTY));

  while ((-1 == fd) && (retry < 7)) {
    BTHCI_QCOMM_ERROR("init_transport: Cannot open %s: %s\n. Retry after 2 seconds",
        bt_hci_transport_device.name, strerror(errno));
    usleep(2000000);
    fd = open(bt_hci_transport_device.name, (O_RDWR | O_NOCTTY));
    retry++;
  }

  if (-1 == fd)
  {
    BTHCI_QCOMM_ERROR("init_transport: Cannot open %s: %s\n",
        bt_hci_transport_device.name, strerror(errno));
    return -1;
  }

  /* Sleep (0.5sec) added giving time for the smd port to be successfully
     opened internally. Currently successful return from open doesn't
     ensure the smd port is successfully opened.
     TODO: Following sleep to be removed once SMD port is successfully
     opened immediately on return from the aforementioned open call */

  property_get("bluetooth.isSSR", ssrvalue, "");
  if(ssrvalue[0] == '1')
  {
      BTHCI_QCOMM_ERROR("bt_hci_pfal_init_transport:going to sleep for .5sec\n");
      if(BT_HCI_SMD == bt_hci_transport_device.type)
          usleep(500000);
  }

  if (tcflush(fd, TCIOFLUSH) < 0)
  {
    BTHCI_QCOMM_ERROR("init_uart: Cannot flush %s\n", bt_hci_transport_device.name);
    close(fd);
    return -1;
  }

  if (tcgetattr(fd, &term) < 0)
  {
    BTHCI_QCOMM_ERROR("init_uart: Error while getting attributes\n");
    close(fd);
    return -1;
  }

  cfmakeraw(&term);

  /* JN: Do I need to make flow control configurable, since 4020 cannot 
   * disable it?
   */
  term.c_cflag |= (CRTSCTS | CLOCAL);

  if (tcsetattr(fd, TCSANOW, &term) < 0)
  {
    BTHCI_QCOMM_ERROR("init_uart: Error while getting attributes\n");
    close(fd);
    return -1;
  }

  if(BT_HCI_UART == bt_hci_transport_device.type)
  {

  /* BTS 4020 will be initialized to autobaud whicr works at 115200.
   * Later the speed maybe changed to some higher rate supported by the serial driver
   * and also the modem's clock generator for the UART.
   */
    if (bt_hci_qcomm_pfal_changebaudrate (starting_baud) == FALSE)
    {
      BTHCI_QCOMM_ERROR("init_uart: Error while determining speed\n");
      close(fd);
      return -1;
    }
  }
  BTHCI_QCOMM_TRACE("Done intiailizing UART\n");
  return fd;
}
/*===========================================================================
FUNCTION   bt_hci_pfal_deinit_transport

DESCRIPTION
  Platform specific routine to de-intialise the UART/SMD resource.

PLATFORM SPECIFIC DESCRIPTION
  Closes the TTY/SMD file descriptor and sets the descriptor value to -1

DEPENDENCIES
  NIL

RETURN VALUE
  RETURN VALUE
  STATUS_SUCCESS if SUCCESS, else other reasons

SIDE EFFECTS
  The Close of the descriptor will trigger a failure in the Reader Thread
  and hence cause a Deinit of the ReaderThread

===========================================================================*/
boolean bt_hci_pfal_deinit_transport()
{
  close(fd);
  fd = -1;
  return TRUE;
}

int bt_hci_qcomm_nwrite(uint8 *buf, int size)
{
  int tx_bytes = 0, nwrite;
  int i = 0, buf_size = size;

  do
  {
    nwrite = write(fd, (buf + tx_bytes), (size - tx_bytes));
    if (nwrite < 0) 
    {
      perror("Error while writing ->");
      return nwrite;
    }
    if (nwrite == 0)
    {
        fprintf(stderr, "bt_hci_qcomm_nwrite: zero-length write\n");
        return nwrite;
    }

    tx_bytes += nwrite;
    size     -= nwrite;
  } while (tx_bytes < size);

  if (verbose > 2)
  {
    fprintf(stderr, "CMD:");
    for (i = 0; i < buf_size; i++)
    {
      fprintf(stderr, " %02X", buf[i]);
    }
    fprintf(stderr, "%s", "\n");
  }

  return tx_bytes;
}


int bt_hci_qcomm_nread(uint8 *buf, int size)
{
  int rx_bytes = 0, nread;
  fd_set infids;
  struct timeval timeout;

  /* Reading BT SoC can hang if baud rate mismatch, power, or config problem,
   * so call select with timeout to detect lost HCI contact with BT SoC.
   * This is a small, one-time program that's not time-critical, so we
   * call select for every read.  Sometime we see just one char (BREAK)
   * from SoC then nothing (eg. flow lines mis-configured), or we might
   * fail after baud-rate change.
   */

  do
  {
    FD_ZERO (&infids);
    FD_SET (fd, &infids);
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;  /* half second is a long time at 115.2 Kbps */

    if (select (fd + 1, &infids, NULL, NULL, &timeout) < 1)
    {
       fprintf(stderr, "bt_hci_qcomm_nread: read BT SoC timed out.\n");
       return rx_bytes;
    }

    nread = read(fd, (buf + rx_bytes), (size - rx_bytes));
    if (nread < 0) 
    {
      perror("Error while reading ->");
      return nread;
    }

    rx_bytes += nread;

  } while (rx_bytes < size);

  return rx_bytes; 
}

boolean bt_hci_qcomm_pfal_changebaudrate (unsigned long new_baud)
{
  struct termios term;
  boolean        status = TRUE;
  speed_t baud_code;
  speed_t actual_baud_code;
#if defined(BOTHER)
  struct termios2 term2;
#endif /* defined(BOTHER) */
  
  if (tcgetattr(fd, &term) < 0) 
  {
    perror("Can't get port settings");
    status = FALSE;
  }
  else
  {  
    baud_code = convert_baud(new_baud);
#if defined(BOTHER)
    if (ioctl(fd, TCGETS2, &term2) == -1)
    {
      perror("bt_hci_qcomm_pfal_changebaudrate: TCGETS2:");
      return FALSE;
    }
    term2.c_ospeed = term2.c_ispeed = (speed_t) new_baud;
    term2.c_cflag &= ~CBAUD;
    term2.c_cflag |= BOTHER;
    if (ioctl(fd, TCSETS2, &term2) == -1)
    {
      perror("bt_hci_qcomm_pfal_changebaudrate: TCGETS2:");
      return FALSE;
    }
    /* read it back and see what we got */
    if (ioctl(fd, TCGETS2, &term2) == -1)
    {
      perror("bt_hci_qcomm_pfal_changebaudrate: TCGETS2:");
      return FALSE;
    }
    if (verbose)
    {
      fprintf(stderr, "bt_hci_qcomm_pfal_changebaudrate: new rates %d, %d\n", term2.c_ispeed, term2.c_ospeed); 
    }   
#else /* No BOTHER */
    (void) cfsetospeed(&term, baud_code);
    if (tcsetattr(fd, TCSADRAIN, &term) < 0) /* don't change speed until last write done */
    {
      perror("bt_hci_qcomm_pfal_changebaudrate: tcsetattr:");
      status = FALSE;
    }
#endif /* BOTHER */

    /* make sure that we reportedly got the speed we tried to set */
    if (1 < verbose)
    {
      if (tcgetattr(fd, &term) < 0)
      {
        perror("bt_hci_qcomm_pfal_changebaudrate: tcgetattr:");
        status = FALSE;
      }
      if (baud_code != (actual_baud_code = cfgetospeed(&term)))
      {
        fprintf(stderr, "bt_hci_qcomm_pfal_changebaudrate: new baud %lu FAILED, got 0x%x\n", new_baud, actual_baud_code);
      }
      else
      {
        fprintf(stderr, "bt_hci_qcomm_pfal_changebaudrate: new baud %lu SUCCESS, got 0x%x\n", new_baud, actual_baud_code);
      }
    }
  }

  return status;
}
