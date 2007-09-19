/*-----------------------------------------------------------------------------
  $Id: arm7main.c,v 1.1 2007/08/27 04:44:30 rhaleblian Exp $

  Simple ARM7 stub (sends RTC, TSC, and X/Y data to the ARM 9)

  $Log: arm7main.c,v $
  Revision 1.1  2007/08/27 04:44:30  rhaleblian
  lcds now shut off when lid is closed.

  Revision 1.2  2007/08/13 04:26:03  rhaleblian
  Suppressed arm7 code, which causes memory and pc stomping.
  Added banner logo and title.
  START reveals book list.

  Revision 1.1  2007/08/12 20:51:16  rhaleblian
  Makefiles reorganized to support separate arm7 and arm9 binaries.
  Added wifi debug stub code - not working yet.
	
  Revision 1.2  2006/10/10 12:07:26  ben
  empty the fifo on interrupt
	
  Revision 1.1.1.1  2006/09/06 10:13:18  ben
  Debug start
	
  Revision 1.2  2005/09/07 20:06:06  wntrmute
  updated for latest libnds changes
	
  Revision 1.8  2005/08/03 05:13:16  wntrmute
  corrected sound code

  ---------------------------------------------------------------------------*/
#include <nds.h>
#include <stdlib.h>
#include <nds/bios.h>
#include <nds/reload.h>
#include <nds/arm7/touch.h>
#include <nds/arm7/clock.h>
#include "wifiData.h"
#include <dswifi7.h>

/**-------------------------------------------------------------------------**/
void startSound(int sampleRate, const void* data, u32 bytes,
		u8 channel, u8 vol,  u8 pan, u8 format) {

  SCHANNEL_TIMER(channel)  = SOUND_FREQ(sampleRate);
  SCHANNEL_SOURCE(channel) = (u32)data;
  SCHANNEL_LENGTH(channel) = bytes >> 2 ;
  SCHANNEL_CR(channel)     = SCHANNEL_ENABLE | SOUND_ONE_SHOT 
    | SOUND_VOL(vol) | SOUND_PAN(pan) | (format==1?SOUND_8BIT:SOUND_16BIT);
}


/**-------------------------------------------------------------------------**/
s32 getFreeSoundChannel() {

  int i;
  for (i=0; i<16; i++) {
    if ( (SCHANNEL_CR(i) & SCHANNEL_ENABLE) == 0 ) return i;
  }
  return -1;
}

int vcount;
touchPosition first,tempPos;


//-----------------------------------------------------------------------------
void VcountHandler() {
//-----------------------------------------------------------------------------
  static int lastbut = -1;
	
  uint16 but=0, x=0, y=0, xpx=0, ypx=0, z1=0, z2=0;

  but = REG_KEYXY;

  if (!( (but ^ lastbut) & (1<<6))) {
 
    tempPos = touchReadXY();

    x = tempPos.x;
    y = tempPos.y;
    xpx = tempPos.px;
    ypx = tempPos.py;
    z1 = tempPos.z1;
    z2 = tempPos.z2;
		
  } else {
    lastbut = but;
    but |= (1 <<6);
  }

  if ( vcount == 80 ) {
    first = tempPos;
  } else {
    if (	abs( xpx - first.px) > 10 || abs( ypx - first.py) > 10 ||
		(but & ( 1<<6)) ) {

      but |= (1 <<6);
      lastbut = but;

    } else { 	
      IPC->mailBusy = 1;
      IPC->touchX			= x;
      IPC->touchY			= y;
      IPC->touchXpx		= xpx;
      IPC->touchYpx		= ypx;
      IPC->touchZ1		= z1;
      IPC->touchZ2		= z2;
      IPC->mailBusy = 0;
    }
  }
  IPC->buttons		= but;
  vcount ^= (80 ^ 130);
  SetYtrigger(vcount);

  // Check if the lid has been closed.
  if(but & BIT(7)) {

    // Save the current interrupt sate.
    u32 ie_save = REG_IE;

    // Turn the speaker down.
    swiChangeSoundBias(0,0x400);

    // Save current power state.
    int power = readPowerManagement(PM_CONTROL_REG);

    // Set sleep LED.
    writePowerManagement(PM_CONTROL_REG, PM_LED_CONTROL(1));

    // Register for the lid interrupt.
    REG_IE = IRQ_LID;
    
    // Power down till we get our interrupt.
    swiSleep(); //waits for PM (lid open) interrupt
    REG_IF = ~0;
    
    // Restore the interrupt state.
    REG_IE = ie_save;
    
    // Restore power state.
    writePowerManagement(PM_CONTROL_REG, power);
    
    // Turn the speaker up.
    swiChangeSoundBias(1,0x400);
  }         
}


/**-------------------------------------------------------------------------**/
void VblankHandler(void) {

  static int heartbeat = 0;

  uint16 but=0, x=0, y=0, xpx=0, ypx=0, z1=0, z2=0, batt=0, aux=0;
  int t1=0, t2=0;
  uint32 temp=0;
  uint8 ct[sizeof(IPC->time.curtime)];
  u32 i;

  // Update the heartbeat
  heartbeat++;

  // Read the touch screen

  but = REG_KEYXY;

  if (!(but & (1<<6))) {

    touchPosition tempPos = touchReadXY();

    x = tempPos.x;
    y = tempPos.y;
    xpx = tempPos.px;
    ypx = tempPos.py;
  }

  z1 = touchRead(TSC_MEASURE_Z1);
  z2 = touchRead(TSC_MEASURE_Z2);

	
  batt = touchRead(TSC_MEASURE_BATTERY);
  aux  = touchRead(TSC_MEASURE_AUX);

  // Read the time
  rtcGetTime((uint8 *)ct);
  BCDToInteger((uint8 *)&(ct[1]), 7);

  // Read the temperature
  temp = touchReadTemperature(&t1, &t2);

  // Update the IPC struct
  //IPC->heartbeat	= heartbeat;
  IPC->buttons		= but;
  IPC->touchX			= x;
  IPC->touchY			= y;
  IPC->touchXpx		= xpx;
  IPC->touchYpx		= ypx;
  IPC->touchZ1		= z1;
  IPC->touchZ2		= z2;
  IPC->battery		= batt;
  IPC->aux			= aux;

  for(i=0; i<sizeof(ct); i++) {
    IPC->time.curtime[i] = ct[i];
  }

  IPC->temperature = temp;
  IPC->tdiode1 = t1;
  IPC->tdiode2 = t2;


  //sound code  :)
  TransferSound *snd = IPC->soundData;
  IPC->soundData = 0;

  if (0 != snd) {

    for (i=0; i<snd->count; i++) {
      s32 chan = getFreeSoundChannel();

      if (chan >= 0) {
	startSound(snd->data[i].rate, snd->data[i].data, snd->data[i].len, chan, snd->data[i].vol, snd->data[i].pan, snd->data[i].format);
      }
    }
  }

  Wifi_Update(); // update wireless in vblank

}

/**-------------------------------------------------------------------------**/
// callback to allow wifi library to notify arm9
void arm7_synctoarm9() { // send fifo message
  REG_IPC_FIFO_TX = 0x87654321;
}

/**-------------------------------------------------------------------------**/
// interrupt handler to allow incoming notifications from arm9

#define GET_BRIGHTNESS      (0x1211B210)
#define SET_BRIGHTNESS_0    (0x1211B211)
#define SET_BRIGHTNESS_1    (0x1211B212)
#define SET_BRIGHTNESS_2    (0x1211B213)
#define SET_BRIGHTNESS_3    (0x1211B214)

void arm7_fifo() { // check incoming fifo messages
  int syncd = 0;
  while ( !(REG_IPC_FIFO_CR & IPC_FIFO_RECV_EMPTY)) {
    u32 msg = REG_IPC_FIFO_RX;
    if ( msg == 0x87654321 && !syncd) {
      syncd = 1;
      Wifi_Sync();
    }
    else if(msg == GET_BRIGHTNESS)
    {
      // send back the value (0 - 3)
      REG_IPC_FIFO_TX = readPowerManagement((4)) - 64;
    }
    else if(msg == SET_BRIGHTNESS_0)
    {
      // write value (0)
      writePowerManagement((4), 0);
    }
    else if(msg == SET_BRIGHTNESS_1)
    {
      // write value (1)
      writePowerManagement((4), 1);
    }
    else if(msg == SET_BRIGHTNESS_2)
    {
      // write value (2)
      writePowerManagement((4), 2);
    }
    else if(msg == SET_BRIGHTNESS_3)
    {
      // write value (3)
      writePowerManagement((4), 3);
    }
  }
}


/**-------------------------------------------------------------------------**/
int main( void) {
  
  // reload
  LOADNDS->PATH = 0;

  // enable & prepare fifo asap
  REG_IPC_FIFO_CR = IPC_FIFO_ENABLE | IPC_FIFO_SEND_CLEAR; 
  // Reset the clock if needed
  rtcReset();

  //enable sound
  powerON(POWER_SOUND);
  SOUND_CR = SOUND_ENABLE | SOUND_VOL(0x7F);
  IPC->soundData = 0;

  irqInit();
  irqSet(IRQ_VBLANK, VblankHandler);
  irqEnable(IRQ_VBLANK);

  irqSet(IRQ_VCOUNT, VcountHandler);
  irqEnable(IRQ_VCOUNT);

  irqSet(IRQ_WIFI, Wifi_Interrupt); // set up wifi interrupt
  irqEnable(IRQ_WIFI);

  { // sync with arm9 and init wifi
    u32 fifo_temp;   

    while(1) { // wait for magic number
      while(REG_IPC_FIFO_CR&IPC_FIFO_RECV_EMPTY) swiWaitForVBlank();
      fifo_temp=REG_IPC_FIFO_RX;
      if(fifo_temp==0x12345678) break;
    }
    while(REG_IPC_FIFO_CR&IPC_FIFO_RECV_EMPTY) swiWaitForVBlank();
    fifo_temp=REG_IPC_FIFO_RX; // give next value to wifi_init
    Wifi_Init(fifo_temp);
    
    irqSet(IRQ_FIFO_NOT_EMPTY,arm7_fifo); // set up fifo irq
    irqEnable(IRQ_FIFO_NOT_EMPTY);
    REG_IPC_FIFO_CR = IPC_FIFO_ENABLE | IPC_FIFO_RECV_IRQ;

    Wifi_SetSyncHandler(arm7_synctoarm9); // allow wifi lib to notify arm9
  } // arm7 wifi init complete

  // Keep the ARM7 out of main RAM
  while (1) {
    if (LOADNDS->PATH != 0) {
      LOADNDS->ARM7FUNC(LOADNDS->PATH);
    }
    swiWaitForVBlank();
  }
}


/** a new version, reading also the wep key, is already availabe */
#if 0
void wifiMain(){
	wifiData->read=false;
	IPC->mailData=0;
	IPC->mailSize=0;
	// Reset the clock if needed
	rtcReset();

	//enable sound
	powerON(POWER_SOUND);
	SOUND_CR = SOUND_ENABLE | SOUND_VOL(0x7F);
	IPC->soundData = 0;

	irqInit();
	irqSet(IRQ_VBLANK, VblankHandler);
	irqEnable(IRQ_VBLANK);
	/*
	irqSet(IRQ_WIFI, Wifi_Interrupt);
	irqEnable(IRQ_WIFI);
	 */
	/*** MODIFICATION *///(*(vuint16*)0x0400010E)

	irqSet(IRQ_IPC_SYNC, Wifi_Update);
	irqEnable(IRQ_IPC_SYNC);
	REG_IPC_SYNC = IPC_SYNC_IRQ_ENABLE;

	/**** end */

	// trade some mail, to get a pointer from arm9
	while(IPC->mailData!=2) { swiWaitForVBlank(); IPC->mailSize=0; }
	IPC->mailSize=2;
	while(IPC->mailData==2) swiWaitForVBlank();
	/*
	Wifi_Init(IPC->mailData);
	char add[4];
	char sn[1];
	int base,i=0,j,k;

	for (base=0x3FA00;base<=0x3FC00;base+=0x100){
		Read_Flash2(base+0x40, wifiData->ssid[i], 32);
		Read_Flash2(base+0x80, wifiData->wepKey[i], 13);
		j=0;
		wifiData->wepMode[i]=0;
		for(k=0;k<5;k++)
			j|=wifiData->wepKey[i][k];
		if (j!=0)
			wifiData->wepMode[i]++;
		for(k=5;k<13;k++)
			j|=wifiData->wepKey[i][k];
		if (j!=0)
			wifiData->wepMode[i]++;
		Read_Flash2(base+0xC0, add, 4);
		wifiData->ipAddress[i] = readIP(add);
		Read_Flash2(base+0xC4, add, 4);
		wifiData->gateway[i] = readIP(add);
		Read_Flash2(base+0xC8, add, 4);
		wifiData->dns1[i] = readIP(add);
		Read_Flash2(base+0xCC, add, 4);
		wifiData->dns2[i] = readIP(add);
		Read_Flash2(base+0xD0,sn, 1);
   	    wifiData->subnetLength[i]=sn[0];
		i++;
	}
	Read_Flash2(0x36,wifiData->mac, 6);
	*/
	wifiData->read=true;
}
#endif

//-----------------------------------------------------------------------------
int main2(int argc, char ** argv) {

	IPC->aux=PM_BACKLIGHT_BOTTOM | PM_BACKLIGHT_TOP;
	//IPC->aux=0;
	//	wifiMain();
	LOADNDS->PATH = 0;
	// Keep the ARM7 out of main RAM
	while (1) {
		if (LOADNDS->PATH != 0) {
			LOADNDS->ARM7FUNC(LOADNDS->PATH);
		}
		swiWaitForVBlank();
	}
}

