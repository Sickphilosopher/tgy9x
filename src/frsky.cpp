/*
 * Authors - Bertrand Songis <bsongis@gmail.com>, Bryan J.Rentoul (Gruvin) <gruvin@gmail.com> and Philip Moss
 *
 * Adapted from jeti.cpp code by Karl Szmutny <shadow@privy.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include "er9x.h"
#include "frsky.h"

// Enumerate FrSky packet codes
#define LINKPKT         0xfe
#define USRPKT          0xfd
#define A11PKT          0xfc
#define A12PKT          0xfb
#define A21PKT          0xfa
#define A22PKT          0xf9
#define ALRM_REQUEST    0xf8
#define RSSIRXPKT       0xf7
#define RSSITXPKT       0xf6
#define RSSI_REQUEST		0xf1

#define START_STOP      0x7e
#define BYTESTUFF       0x7d
#define STUFF_MASK      0x20

// Translate hub data positions
// Add a top bit, first word of two word value
// When top bit found, just save index(with top bit) and value
// When next value received, if saved value has top bit set, stoore the value and clear top bit
//   then process latest values, saving them if necessary.
const prog_uint8_t APM Fr_indices[] = 
{
	HUBDATALENGTH-1,
	FR_GPS_ALT | 0x80,
	FR_TEMP1,
	FR_RPM,
	FR_FUEL,
	FR_TEMP2,
	FR_CELL_V,
	FR_GPS_ALTd,
	HUBDATALENGTH-1,HUBDATALENGTH-1,HUBDATALENGTH-1,HUBDATALENGTH-1,
	HUBDATALENGTH-1,HUBDATALENGTH-1,HUBDATALENGTH-1,HUBDATALENGTH-1,
	FR_ALT_BARO | 0x80,
	FR_GPS_SPEED | 0x80,
	FR_GPS_LONG | 0x80,
	FR_GPS_LAT | 0x80,
	FR_COURSE,
	FR_GPS_DATMON,
	FR_GPS_YEAR,
	FR_GPS_HRMIN,
	FR_GPS_SEC,
	FR_GPS_SPEEDd,
	FR_GPS_LONGd,
	FR_GPS_LATd,
	FR_COURSEd,
	HUBDATALENGTH-1,HUBDATALENGTH-1,HUBDATALENGTH-1,HUBDATALENGTH-1,
	FR_ALT_BAROd,
	FR_LONG_E_W,
	FR_LAT_N_S,
	FR_ACCX,
	FR_ACCY,
	FR_ACCZ,
	HUBDATALENGTH-1,
	FR_CURRENT,
	FR_V_AMP | 0x80,
	FR_V_AMPd
} ;

uint8_t frskyRxBuffer[19];   // Receive buffer. 9 bytes (full packet), worst case 18 bytes with byte-stuffing (+1)
uint8_t frskyTxBuffer[19];   // Ditto for transmit buffer
uint8_t frskyTxBufferCount = 0;
uint8_t FrskyRxBufferReady = 0;
uint8_t frskyStreaming = 0;
uint8_t frskyUsrStreaming = 0;

FrskyData frskyTelemetry[4];
//FrskyData frskyRSSI[2];
Frsky_current_info Frsky_current[2] ;

uint8_t frskyRSSIlevel[2] ;
uint8_t frskyRSSItype[2] ;

struct FrskyAlarm {
  uint8_t level;    // The alarm's 'urgency' level. 0=disabled, 1=yellow, 2=orange, 3=red
  uint8_t greater;  // 1 = 'if greater than'. 0 = 'if less than'
  uint8_t value;    // The threshold above or below which the alarm will sound
};
struct FrskyAlarm frskyAlarms[4];

uint8_t Frsky_user_state ;
uint8_t Frsky_user_stuff ;
uint8_t Frsky_user_id ;
uint8_t Frsky_user_lobyte ;

int16_t FrskyHubData[HUBDATALENGTH] ;  // All 38 words
int16_t FrskyHubMin[HUBMINMAXLEN] ;
int16_t FrskyHubMax[HUBMINMAXLEN] ;

uint8_t FrskyVolts[12];
uint8_t FrskyBattCells=0;



void store_hub_data( uint8_t index, uint16_t value )
{
	if ( index < HUBDATALENGTH )
	{
		FrskyHubData[index] = value ;
		if ( g_model.FrSkyGpsAlt )
		{
			if ( index == FR_GPS_ALT )
			{
				FrskyHubData[FR_ALT_BARO] = FrskyHubData[FR_GPS_ALT] ;		// Copy Gps Alt instead
				index = FR_ALT_BARO ;			// For max and min
			}
		}
		if ( index < HUBMINMAXLEN )
		{
			if ( FrskyHubMax[index] < FrskyHubData[index] )
			{	FrskyHubMax[index] = FrskyHubData[index] ;
			}
			if ( FrskyHubMin[index] > FrskyHubData[index] )
			{	FrskyHubMin[index] = FrskyHubData[index] ;
			}	
		}
		if ( index == FR_CELL_V )			// Cell Voltage
		{
			// It appears the cell voltage bytes are in the wrong order
//  							uint8_t battnumber = ( FrskyHubData[6] >> 12 ) & 0x000F ;
  		uint8_t battnumber = ((uint8_t)value >> 4 ) & 0x000F ;
  		if (FrskyBattCells < battnumber+1)
			{
 				if (battnumber+1>=6)
				{
  				FrskyBattCells=6;
  			}
				else
				{
  				FrskyBattCells=battnumber+1;
  			}
  		}
  		FrskyVolts[battnumber] = ( ( ( value & 0x0F ) << 8 ) + (value >> 8) ) / 10 ;
			if ( FrskyVolts[battnumber] < FrskyHubData[FR_CELL_MIN] )
			{
				FrskyHubData[FR_CELL_MIN] = FrskyVolts[battnumber] ;
			}
		}
		if ( index == FR_RPM )			// RPM
		{
			FrskyHubData[FR_RPM] *= (g_model.numBlades == 2 ) ? 15 : ( (g_model.numBlades == 1 ) ? 20 : 30 ) ;
		}
	}	
}


void frsky_proc_user_byte( uint8_t byte )
{
	if (g_model.FrSkyUsrProto == 0)  // FrSky Hub
	{
	
  	if ( Frsky_user_state == 0 )
		{ // Waiting for 0x5E
			if ( byte == 0x5E )
			{
				Frsky_user_state = 1 ;			
			}		
		}
		else
		{ // In a packet
			if ( byte == 0x5E )
			{ // 
				Frsky_user_state = 1 ;			
			}
			else
			{
				if ( byte == 0x5D )
				{
					Frsky_user_stuff = 1 ;  // Byte stuffing active
				}
				else
				{
					if ( Frsky_user_stuff )
					{
						Frsky_user_stuff = 0 ;
						byte ^= 0x60 ;  // Unstuff
					}
  	      if ( Frsky_user_state == 1 )
					{
						if ( byte > 57 )
						{
							byte -= 17 ;		// Move voltage-amp sensors							
						}
					  Frsky_user_id	= pgm_read_byte( &Fr_indices[byte] ) ;
						Frsky_user_state = 2 ;
					}
  	      else if ( Frsky_user_state == 2 )
					{
					  Frsky_user_lobyte	= byte ;
						Frsky_user_state = 3 ;
					}
					else
					{
						store_hub_data( Frsky_user_id & 0x7F, ( byte << 8 ) + Frsky_user_lobyte ) ;
						Frsky_user_state = 0 ;
					}
				}
			}		 
		}
	}
	else if (g_model.FrSkyUsrProto == 1)  // WS How High
	{
    if ( frskyUsrStreaming < (FRSKY_TIMEOUT10ms*3 - 10))  // At least 100mS passed since last data received
		{
			Frsky_user_lobyte = byte ;
		}
		else
		{
			store_hub_data( FR_ALT_BARO, ( byte << 8 ) + Frsky_user_lobyte ) ;	 // Store altitude info
		}				
	}
}

void frskyPushValue(uint8_t & i, uint8_t value);

/*
   Called from somewhere in the main loop or a low prioirty interrupt
   routine perhaps. This funtcion processes Fr-Sky telemetry data packets
   assembled byt he USART0_RX_vect) ISR function (below) and stores
   extracted data in global variables for use by other parts of the program.

   Packets can be any of the following:

    - A1/A2/RSSI telemtry data
    - Alarm level/mode/threshold settings for Ch1A, Ch1B, Ch2A, Ch2B
    - User Data packets
*/

uint8_t LinkAveCount ;

void processFrskyPacket(uint8_t *packet)
{
  // What type of packet?
  switch (packet[0])
  {
    case A22PKT:
    case A21PKT:
    case A12PKT:
    case A11PKT:
      {
        struct FrskyAlarm *alarmptr ;
        alarmptr = &frskyAlarms[(packet[0]-A22PKT)] ;
        alarmptr->value = packet[1];
        alarmptr->greater = packet[2] & 0x01;
        alarmptr->level = packet[3] & 0x03;
      }
      break;
    case LINKPKT: // A1/A2/RSSI values
			// From a scope, this seems to be sent every about every 35mS
			LinkAveCount += 1 ;
      frskyTelemetry[0].set(packet[1]); FrskyHubData[FR_A1_COPY] =  frskyTelemetry[0].value ;
      frskyTelemetry[1].set(packet[2]); FrskyHubData[FR_A2_COPY] =  frskyTelemetry[1].value ;
      frskyTelemetry[2].set(packet[3]);	FrskyHubData[FR_RXRSI_COPY] =  frskyTelemetry[2].value ;
      frskyTelemetry[3].set(packet[4] / 2); FrskyHubData[FR_TXRSI_COPY] =  frskyTelemetry[3].value ;
			if ( LinkAveCount > 15 )
			{
				LinkAveCount = 0 ;
			}
//      frskyRSSI[0].set(packet[3]);
//      frskyRSSI[1].set(packet[4] / 2);
      break;

		case RSSIRXPKT :
			frskyRSSIlevel[1] = packet[1] ;
			frskyRSSItype[1] = packet[3] ;
		break ;

		case RSSITXPKT :
			frskyRSSIlevel[0] = packet[1] ;
			frskyRSSItype[0] = packet[3] ;
		break ;

    case USRPKT: // User Data packet
    {
			uint8_t i, j ;
			i = packet[1] + 3 ;  // User bytes end
			j = 3 ;              // Index to user bytes
			while ( j < i )
			{
				frsky_proc_user_byte( packet[j] ) ;
        frskyUsrStreaming = FRSKY_TIMEOUT10ms*3; // reset counter only if valid frsky packets are being detected
				j += 1 ;
			}
    }	
    break;
  }

  FrskyRxBufferReady = 0;
  frskyStreaming = FRSKY_TIMEOUT10ms; // reset counter only if valid frsky packets are being detected
}

// Receive buffer state machine state defs
#define frskyDataIdle    0
#define frskyDataStart   1
#define frskyDataInFrame 2
#define frskyDataXOR     3
/*
   Receive serial (RS-232) characters, detecting and storing each Fr-Sky 
   0x7e-framed packet as it arrives.  When a complete packet has been 
   received, process its data into storage variables.  NOTE: This is an 
   interrupt routine and should not get too lengthy. I originally had
   the buffer being checked in the perMain function (because per10ms
   isn't quite often enough for data streaming at 9600baud) but alas
   that scheme lost packets also. So each packet is parsed as it arrives,
   directly at the ISR function (through a call to frskyProcessPacket).
   
   If this proves a problem in the future, then I'll just have to implement
   a second buffer to receive data while one buffer is being processed (slowly).
*/

ISR(USART0_RX_vect)
{
  uint8_t stat;
  uint8_t data;
  
  static uint8_t numPktBytes = 0;
  static uint8_t dataState = frskyDataIdle;
  
	UCSR0B &= ~(1 << RXCIE0); // disable Interrupt
	sei() ;
  stat = UCSR0A; // USART control and Status Register 0 A

    /*
              bit      7      6      5      4      3      2      1      0
                      RxC0  TxC0  UDRE0    FE0   DOR0   UPE0   U2X0  MPCM0
             
              RxC0:   Receive complete
              TXC0:   Transmit Complete
              UDRE0:  USART Data Register Empty
              FE0:    Frame Error
              DOR0:   Data OverRun
              UPE0:   USART Parity Error
              U2X0:   Double Tx Speed
              PCM0:   MultiProcessor Comms Mode
    */
    // rh = UCSR0B; //USART control and Status Register 0 B

    /*
              bit      7      6      5      4      3      2      1      0
                   RXCIE0 TxCIE0 UDRIE0  RXEN0  TXEN0 UCSZ02  RXB80  TXB80
             
              RxCIE0:   Receive Complete int enable
              TXCIE0:   Transmit Complete int enable
              UDRIE0:   USART Data Register Empty int enable
              RXEN0:    Rx Enable
              TXEN0:    Tx Enable
              UCSZ02:   Character Size bit 2
              RXB80:    Rx data bit 8
              TXB80:    Tx data bit 8
    */

  data = UDR0; // USART data register 0

  if (stat & ((1 << FE0) | (1 << DOR0) | (1 << UPE0)))
  { // discard buffer and start fresh on any comms error
    FrskyRxBufferReady = 0;
    numPktBytes = 0;
  } 
  else
  {
    if (FrskyRxBufferReady == 0) // can't get more data if the buffer hasn't been cleared
    {
      switch (dataState) 
      {
        case frskyDataStart:
          if (data == START_STOP) break; // Remain in userDataStart if possible 0x7e,0x7e doublet found.

          if (numPktBytes < 19)
            frskyRxBuffer[numPktBytes++] = data;
          dataState = frskyDataInFrame;
          break;

        case frskyDataInFrame:
          if (data == BYTESTUFF)
          { 
              dataState = frskyDataXOR; // XOR next byte
              break; 
          }
          if (data == START_STOP) // end of frame detected
          {
            processFrskyPacket(frskyRxBuffer); // FrskyRxBufferReady = 1;
            dataState = frskyDataIdle;
            break;
          }
          frskyRxBuffer[numPktBytes++] = data;
          break;

        case frskyDataXOR:
          if (numPktBytes < 19)
            frskyRxBuffer[numPktBytes++] = data ^ STUFF_MASK;
          dataState = frskyDataInFrame;
          break;

        case frskyDataIdle:
          if (data == START_STOP)
          {
            numPktBytes = 0;
            dataState = frskyDataStart;
          }
          break;

      } // switch
    } // if (FrskyRxBufferReady == 0)
  }
	cli() ;
  UCSR0B |= (1 << RXCIE0); // enable Interrupt
}

/*
   USART0 (transmit) Data Register Emtpy ISR
   Usef to transmit FrSky data packets, which are buffered in frskyTXBuffer. 
*/
uint8_t frskyTxISRIndex = 0;
ISR(USART0_UDRE_vect)
{
  if (frskyTxBufferCount > 0) 
  {
    UDR0 = frskyTxBuffer[frskyTxISRIndex++];
    frskyTxBufferCount--;
  } else
    UCSR0B &= ~(1 << UDRIE0); // disable UDRE0 interrupt
}

/******************************************/

void frskyTransmitBuffer()
{
  frskyTxISRIndex = 0;
  UCSR0B |= (1 << UDRIE0); // enable  UDRE0 interrupt
}


uint8_t FrskyAlarmSendState = 0 ;
uint8_t FrskyDelay = 0 ;
uint8_t FrskyRSSIsend = 0 ;


static void FRSKY10mspoll(void)
{
  if (FrskyDelay)
  {
    FrskyDelay -= 1 ;
    return ;
  }

  if (frskyTxBufferCount)
  {
    return; // we only have one buffer. If it's in use, then we can't send yet.
  }

  // Now send a packet
  {
		uint8_t i ;
		uint8_t j = 1 ;

		for ( i = 0 ; i < 7 ; i += 1, j <<= 1 )
		{
			if ( FrskyAlarmSendState & j )
			{
				break ;				
			}			
		}
    FrskyAlarmSendState &= ~j ;
		if ( i < 4 )
		{
	    uint8_t channel = 1 - (i / 2);
  	  uint8_t alarm = 1 - (i % 2);
    
		//	FRSKY_setTxPacket( A22PKT + i, g_eeGeneral.frskyinternalalarm ? 0 :g_model.frsky.channels[channel].alarms_value[alarm],
		//														 ALARM_GREATER(channel, alarm), ALARM_LEVEL(channel, alarm) ) ;					
		
		            FRSKY_setTxPacket( A22PKT + i, g_model.frsky.channels[channel].alarms_value[alarm],
                                                                 ALARM_GREATER(channel, alarm), g_eeGeneral.frskyinternalalarm ? 0 :ALARM_LEVEL(channel, alarm) ) ;
	
									 
		}
		else if( i < 6 )
		{
			i &= 1 ;
			FRSKY_setTxPacket( RSSITXPKT+i, frskyRSSIlevel[i], 0, frskyRSSItype[i] ) ;
		}
		else if (i == 6)
		{
// Send packet requesting all RSSIalarm settings be sent back to us
			FRSKY_setTxPacket( RSSI_REQUEST, 0, 0, 0 ) ;
		}
		else
		{
			return ;
		}
    FrskyDelay = 5 ; // 50mS
    frskyTransmitBuffer(); 
  }
}

//  uint8_t i = 0;

//  for (int alarm=0; alarm<2; alarm++) {
//    frskyTxBuffer[i++] = START_STOP;        // Start of packet
//    frskyTxBuffer[i++] = (RSSI1PKT-alarm);  // f7 - f6
//    frskyPushValue(i, g_eeGeneral.frskyRssiAlarms[alarm].value+50-(10*i));
//    {
//      uint8_t *ptr ;
//      ptr = &frskyTxBuffer[i] ;
//      *ptr++ = 0x00 ;
//      *ptr++ = g_eeGeneral.frskyRssiAlarms[alarm].level;
//      *ptr++ = 0x00 ;
//      *ptr++ = 0x00 ;
//      *ptr++ = 0x00 ;
//      *ptr++ = 0x00 ;
//      *ptr++ = 0x00 ;
//      *ptr++ = START_STOP;        // End of packet
//      i += 8 ;
//    }
//  }

//  frskyTxBufferCount = i;



void FRSKY_setTxPacket( uint8_t type, uint8_t value, uint8_t p1, uint8_t p2 )
{
	uint8_t i = 0;
  frskyTxBuffer[i++] = START_STOP;        // Start of packet
  frskyTxBuffer[i++] = type ;
  frskyPushValue(i, value) ;
  {
    uint8_t *ptr ;
    ptr = &frskyTxBuffer[i] ;
    *ptr++ = p1 ;
    *ptr++ = p2 ;
    *ptr++ = 0x00 ;
    *ptr++ = 0x00 ;
    *ptr++ = 0x00 ;
    *ptr++ = 0x00 ;
    *ptr++ = 0x00 ;
    *ptr++ = START_STOP ;        // End of packet
	}
	frskyTxBufferCount = i + 8 ;
}

enum AlarmLevel FRSKY_alarmRaised(uint8_t idx, uint8_t alarm)
{
    uint8_t value ;
    uint8_t alarm_value ;
		enum AlarmLevel level = alarm_off ;
  for (int i=0; i<2; i++) {
        if ( ( alarm == i ) || ( alarm > 1 ) )
        {
        if ( ( level = (enum AlarmLevel)ALARM_LEVEL(idx, i) ) != alarm_off) {
                value = frskyTelemetry[idx].value ;
                alarm_value = g_model.frsky.channels[idx].alarms_value[i] ;
          if (ALARM_GREATER(idx, i)) {
            if (value > alarm_value)
              return level;
          }
          else {
            if (value < alarm_value)
              return level;
          }
        }
        }
  }
  return alarm_off;
}

void FRSKY_alarmPlay(uint8_t idx, uint8_t alarm){			
			uint8_t alarmLevel = ALARM_LEVEL(idx, alarm);
			
			if((g_eeGeneral.speakerMode == 1 || g_eeGeneral.speakerMode == 2) && g_eeGeneral.frskyinternalalarm == 1){   // this check is done here so haptic still works even if frsky beeper used.
					switch (alarmLevel){			
								case alarm_off:
														break;
								case alarm_yellow:
														audio.event(g_eeGeneral.FRSkyYellow);
														break;														
								case alarm_orange:
														audio.event(g_eeGeneral.FRSkyOrange);
														break;												
								case alarm_red:
														audio.event(g_eeGeneral.FRSkyRed);
														break;		
					}	
			}
			
}



inline void FRSKY_EnableTXD(void)
{
  frskyTxBufferCount = 0;
  UCSR0B |= (1 << TXEN0) | (1 << UDRIE0); // enable TX and TX interrupt
}

inline void FRSKY_EnableRXD(void)
{

  UCSR0B |= (1 << RXEN0);  // enable RX
  UCSR0B |= (1 << RXCIE0); // enable Interrupt
}

#if 0
void FRSKY_DisableTXD(void)
{
  UCSR0B &= ~((1 << TXEN0) | (1 << UDRIE0)); // disable TX pin and interrupt
}

void FRSKY_DisableRXD(void)
{
  UCSR0B &= ~(1 << RXEN0);  // disable RX
  UCSR0B &= ~(1 << RXCIE0); // disable Interrupt
}
#endif

void FRSKY_Init(void)
{
  // clear frsky variables
  memset(frskyAlarms, 0, sizeof(frskyAlarms));
  resetTelemetry();

  DDRE &= ~(1 << DDE0);    // set RXD0 pin as input
  PORTE &= ~(1 << PORTE0); // disable pullup on RXD0 pin

#undef BAUD
#define BAUD 9600
#include <util/setbaud.h>

  UBRR0H = UBRRH_VALUE;
  UBRR0L = UBRRL_VALUE;
  UCSR0A &= ~(1 << U2X0); // disable double speed operation.

  // set 8 N1
  UCSR0B = 0 | (0 << RXCIE0) | (0 << TXCIE0) | (0 << UDRIE0) | (0 << RXEN0) | (0 << TXEN0) | (0 << UCSZ02);
  UCSR0C = 0 | (1 << UCSZ01) | (1 << UCSZ00);

  
  while (UCSR0A & (1 << RXC0)) UDR0; // flush receive buffer

  // These should be running right from power up on a FrSky enabled '9X.
  FRSKY_EnableTXD(); // enable FrSky-Telemetry reception
  FRSKY_EnableRXD(); // enable FrSky-Telemetry reception
}

#if 0
// Send packet requesting all alarm settings be sent back to us
void frskyAlarmsRefresh()
{

  if (frskyTxBufferCount) return; // we only have one buffer. If it's in use, then we can't send. Sorry.
	FRSKY_setTxPacket( ALRM_REQUEST, 0, 0, 0 )
  frskyTransmitBuffer();
}
#endif

void frskyPushValue(uint8_t & i, uint8_t value)
{
	uint8_t j ;
	j = 0 ;
  // byte stuff the only byte than might need it
  if (value == START_STOP) {
    j = 1 ;
    value = 0x5e;
  }
  else if (value == BYTESTUFF) {
    j = 1 ;
    value = 0x5d;
  }
	if ( j )
	{
		frskyTxBuffer[i++] = BYTESTUFF;
	}
  frskyTxBuffer[i++] = value;
}

void FrskyData::setoffset()
{
	uint8_t x ;
	x = value + offset ;
	offset = x ;
	value = 0 ;
}

void FrskyData::set(uint8_t value)
{
  if ( value > offset )
	{
	  value -= offset ;
	}
	else
	{
		value = 0 ;
	}
	averaging_total += value ;
	if ( LinkAveCount > 15 )
	{
		this->value = averaging_total >> 4 ;
		averaging_total = 0 ;
   	if (max < value)
   	  max = value;
   	if (!min || min > value)
   	  min = value;
	}
}

void resetTelemetry()
{
	uint8_t i ;
	for ( i = 0 ; i < 4 ; i += 1 )
	{
		frskyTelemetry[i].min = 0 ;
		frskyTelemetry[i].max = 0 ;
	}
//  memset(frskyTelemetry, 0, sizeof(frskyTelemetry));
	FrskyHubData[FR_A1_MAH] = 0 ;
	FrskyHubData[FR_A2_MAH] = 0 ;
	FrskyHubData[FR_CELL_MIN] = 210 ;			// 4.2 volts
//  memset(frskyRSSI, 0, sizeof(frskyRSSI));
}


// Called every 10 mS in interrupt routine
void check_frsky()
{
  // Used to detect presence of valid FrSky telemetry packets inside the
  // last FRSKY_TIMEOUT10ms 10ms intervals
	if (frskyStreaming > 0)
	{
		if ( --frskyStreaming == 0 )
		{
 			FrskyHubData[FR_TXRSI_COPY] = 0 ;
		}
	}
  if (frskyUsrStreaming > 0) frskyUsrStreaming--;
	
  if ( FrskyAlarmSendState )
  {
    FRSKY10mspoll() ;
  }

	for( uint8_t i = 0 ; i < 2 ; i += 1 )
	{
  	if ( g_model.frsky.channels[i].type == 3 )		// Current (A)
  	{
  	  // value * ratio / 100 gives 10ths of amps
  	  // add this every 10 ms, when over 3600, we have 1 mAh
  	  // so subtract 3600 and add 1 to mAh total
  	  // alternatively, add up the raw value, and use 3600 * 100 / ratio for 1mAh
			
			if ( (  Frsky_current[i].Amp_hour_prescale += frskyTelemetry[i].value ) > Frsky_current[i].Amp_hour_boundary )
			{
				 Frsky_current[i].Amp_hour_prescale -= Frsky_current[i].Amp_hour_boundary ;
				FrskyHubData[FR_A1_MAH+i] += 1 ;
			}
  	}	
	}
}

// New model loaded
void FRSKY_setModelAlarms(void)
{
	FrskyBattCells = 0 ;
  FrskyAlarmSendState |= 0x0F ;
	
  Frsky_current[0].Amp_hour_boundary = 360000L/ g_model.frsky.channels[0].ratio ;
	Frsky_current[1].Amp_hour_boundary = 360000L/ g_model.frsky.channels[1].ratio ;
}

struct FrSky_Q_t FrSky_Queue ;

void put_frsky_q( uint8_t index, uint16_t value )
{
	volatile struct FrSky_Q_item_t *r ;	// volatile = Force compiler to use pointer

	r = &FrSky_Queue.items[FrSky_Queue.in_index] ;
	if ( FrSky_Queue.count < 7 )
	{
		r->index = index ;
		r->value = value ;
		++FrSky_Queue.in_index &= 7 ;
		FrSky_Queue.count += 1 ;
	}
}

// If index bit 7 is zero - process now
// else wait until item further down q has bit 7 zero

void process_frsky_q()
{
	volatile struct FrSky_Q_item_t *r ;	// volatile = Force compiler to use pointer
	uint8_t x ;
	uint8_t y ;
	uint8_t z ;

	// Find last item with zero in bit 7 of index
	x = FrSky_Queue.count ;
	z = FrSky_Queue.out_index ;
	while ( x )
	{
		y = (z+x-1) & 0x07 ;
		if ( ( FrSky_Queue.items[y].index & 0x80 ) == 0 )
		{
			break ;		
		}
		x -= 1 ;		
	}
	y = x ;
	while ( x )
	{
		r = &FrSky_Queue.items[z] ;

		store_hub_data( r->index & 0x7F, r->value ) ;
		++z &= 0x07 ;
	}
	
	FrSky_Queue.out_index = z ;	
	cli() ;
	FrSky_Queue.count -= y ;
	sei() ;

}


//uint32_t GpsPosLat ;

//void testgps()
//{
//  GpsPosLat = (((uint32_t)(uint16_t)FrskyHubData[FR_GPS_LAT] / 100) * 1000000) + (((uint32_t)((uint16_t)FrskyHubData[FR_GPS_LAT] % 100) * 10000 + (uint16_t)FrskyHubData[FR_GPS_LATd]) * 5) / 3;
//}



