/*
 * TMCL.c
 *
 *  Created on: 31.03.2020
 *      Author: OK / ED
 */

#include "TMCL.h"
#include "hal/system/SysTick.h"
#include "hal/system/SystemInfo.h"
#include "hal/system/Debug.h"
#include "hal/comm/Eeprom.h"
#include "hal/comm/SPI.h"
#include "hal/tmcl/TMCL-Variables.h"
#include "TOSV.h"

#ifdef USE_UART_INTERFACE
	#include "hal/comm/UART.h"
#endif

#ifdef USE_USB_INTERFACE
	#include "hal/comm/USB.h"
#endif

	#define AIRCR_VECTKEY_MASK    ((uint32_t)0x05FA0000)

	uint8_t ResetRequested;
	extern const char *VersionString;
	uint32_t TMCM_MOTOR_CONFIG_SIZE = sizeof(TMotorConfig);

	// local used functions
	uint32_t tmcl_handleAxisParameter(uint8_t motor, uint8_t command, uint8_t type, int32_t *value);

	void tmcl_rotateLeft();
	void tmcl_rotateRight();
	void tmcl_motorStop();
	void tmcl_setOutput();
	void tmcl_getInput();
	void tmcl_getVersion();
	void tmcl_firmwareDefault();
	void tmcl_boot();
	void tmcl_softwareReset();

  void tmcl_setVentilatorParameter(void);
  void tmcl_getVentilatorParameter(void);
  void tmcl_startVentilator(void);
  void tmcl_stopVentilator(void);

// => SPI wrapper for TMC-API
u8 tmc4671_readwriteByte(u8 motor, u8 data, u8 lastTransfer)
{
	return weasel_spi_readWriteByte(motor, data, lastTransfer);
}

u8 tmc6200_readwriteByte(u8 motor, u8 data, u8 lastTransfer)
{
	return dragon_spi_readWriteByte(motor, data, lastTransfer);
}
// <= SPI wrapper

/* execute the TMCL-Command stored in "ActualCommand" */
void tmcl_executeActualCommand()
{
	// prepare reply command
	ActualReply.Opcode = ActualCommand.Opcode;
	ActualReply.Status = REPLY_OK;
	ActualReply.Value.Int32 = ActualCommand.Value.Int32;

	// handle command
	switch(ActualCommand.Opcode)
	{
    	case TMCL_ROR:
    		tmcl_rotateRight();
    		break;
    	case TMCL_ROL:
    		tmcl_rotateLeft();
    		break;
    	case TMCL_MST:
    		tmcl_motorStop();
    		break;
    	case TMCL_SAP:
    	case TMCL_GAP:
    	case TMCL_STAP:
    	case TMCL_RSAP:
    		{
    			int32_t value = ActualCommand.Value.Int32;
    			ActualReply.Status = tmcl_handleAxisParameter(ActualCommand.Motor, ActualCommand.Opcode, ActualCommand.Type, &value);
    			ActualReply.Value.Int32 = value;
    	    }
    		break;
        case TMCL_SIO:
        	tmcl_setOutput();
        	break;
        case TMCL_GIO:
            tmcl_getInput();
          break;
    	case TMCL_GetVersion:
    		tmcl_getVersion();
    		break;
        case TMCL_FactoryDefault:
        	tmcl_firmwareDefault();
        	break;
        case TMCL_readRegisterChannel_1:
        	if (ActualCommand.Motor == 0)
        		ActualReply.Value.Int32 = tmc4671_readInt(DEFAULT_MC, ActualCommand.Type);
        	else if (ActualCommand.Motor == 1)
        		ActualReply.Value.Int32 = tmc6200_readInt(DEFAULT_DRV, ActualCommand.Type);
        	break;
        case TMCL_writeRegisterChannel_1:
        	if (ActualCommand.Motor == 0)
        		tmc4671_writeInt(DEFAULT_MC, ActualCommand.Type, ActualCommand.Value.Int32);
        	else if (ActualCommand.Motor == 1)
        		tmc6200_writeInt(DEFAULT_DRV, ActualCommand.Type, ActualCommand.Value.Int32);
          break;

			case TMCL_SetVentilatorParameter:
				tmcl_setVentilatorParameter();
				break;
				
			case TMCL_GetVentilatorParameter:
				tmcl_getVentilatorParameter();
				break;
				
			case TMCL_StartVentilator:
			case TMCL_UF0:   //TEST_OK: to be removed later
				tmcl_startVentilator();
				break;
				
			case TMCL_StopVentilator:
			case TMCL_UF1:   //TEST_OK: to be removed later
				tmcl_stopVentilator();
				break;

    	case TMCL_Boot:
    		tmcl_boot();
    		break;
    	case TMCL_SoftwareReset:
    		tmcl_softwareReset();
    		break;
    	default:
    		ActualReply.Status = REPLY_INVALID_CMD;
    		break;
	}
}

/* initialize tmcl */
void tmcl_init()
{
}

/* process next TMCL command */
void tmcl_processCommand()
{
	static uint8_t TMCLCommandState;
    uint32_t i;

#ifdef USE_UART_INTERFACE
    uint8_t Byte;
    static uint8_t UARTCmd[9];
    static uint8_t UARTCount;
#endif

#ifdef USE_USB_INTERFACE
    uint8_t USBCmd[9];
    uint8_t USBReply[9];
#endif

    /* send reply for last TMCL request */

#ifdef USE_UART_INTERFACE

    if(TMCLCommandState==TCS_UART)  // reply via UART
    {
    	if(TMCLReplyFormat==RF_STANDARD)
    	{
    		uint8_t Checksum = moduleConfig.serialHostAddress+moduleConfig.serialModuleAddress+
    							ActualReply.Status+ActualReply.Opcode+
								ActualReply.Value.Byte[3]+ActualReply.Value.Byte[2]+
								ActualReply.Value.Byte[1]+ActualReply.Value.Byte[0];

    		uart_write(moduleConfig.serialHostAddress);
    		uart_write(moduleConfig.serialModuleAddress);
    		uart_write(ActualReply.Status);
    		uart_write(ActualReply.Opcode);
    		uart_write(ActualReply.Value.Byte[3]);
    		uart_write(ActualReply.Value.Byte[2]);
    		uart_write(ActualReply.Value.Byte[1]);
    		uart_write(ActualReply.Value.Byte[0]);
    		uart_write(Checksum);
    	}
    	else if(TMCLReplyFormat==RF_SPECIAL)
    	{
    		for(i=0; i<9; i++)
    		{
    			uart_write(SpecialReply[i]);
    		}
    	}
    }
    else if(TMCLCommandState==TCS_UART_ERROR)  // last command had a wrong checksum
    {
    	ActualReply.Opcode = 0;
    	ActualReply.Status = REPLY_CHKERR;
    	ActualReply.Value.Int32 = 0;

    	uint8_t Checksum = moduleConfig.serialHostAddress + moduleConfig.serialModuleAddress+
    		  	  	  	  ActualReply.Status+ActualReply.Opcode+
						  ActualReply.Value.Byte[3] + ActualReply.Value.Byte[2]+
						  ActualReply.Value.Byte[1] + ActualReply.Value.Byte[0];

    	uart_write(moduleConfig.serialHostAddress);
    	uart_write(moduleConfig.serialModuleAddress);
    	uart_write(ActualReply.Status);
    	uart_write(ActualReply.Opcode);
    	uart_write(ActualReply.Value.Byte[3]);
    	uart_write(ActualReply.Value.Byte[2]);
    	uart_write(ActualReply.Value.Byte[1]);
    	uart_write(ActualReply.Value.Byte[0]);
    	uart_write(Checksum);
    }

#endif

#ifdef USE_USB_INTERFACE

    if(TMCLCommandState==TCS_USB) // reply via USB
    {
    	if(TMCLReplyFormat==RF_STANDARD)
    	{
    		uint8_t Checksum = moduleConfig.serialHostAddress + moduleConfig.serialModuleAddress+
    				ActualReply.Status+ActualReply.Opcode+
    				ActualReply.Value.Byte[3] + ActualReply.Value.Byte[2] +
    				ActualReply.Value.Byte[1] + ActualReply.Value.Byte[0];

    		USBReply[0] = moduleConfig.serialHostAddress;
    		USBReply[1] = moduleConfig.serialModuleAddress;
    		USBReply[2] = ActualReply.Status;
    		USBReply[3] = ActualReply.Opcode;
    		USBReply[4] = ActualReply.Value.Byte[3];
    		USBReply[5] = ActualReply.Value.Byte[2];
    		USBReply[6] = ActualReply.Value.Byte[1];
    		USBReply[7] = ActualReply.Value.Byte[0];
    		USBReply[8] = Checksum;
    	}
    	else if(TMCLReplyFormat==RF_SPECIAL)
    	{
    		for(i=0; i<9; i++)
    		{
    			USBReply[i]=SpecialReply[i];
    		}
    	}
    	usb_sendData(USBReply, 9);
    }
    else if(TMCLCommandState==TCS_USB_ERROR)  // last command had a wrong checksum
    {
    	ActualReply.Opcode = 0;
    	ActualReply.Status = REPLY_CHKERR;
    	ActualReply.Value.Int32 = 0;

    	uint8_t Checksum = moduleConfig.serialHostAddress + moduleConfig.serialModuleAddress +
    			ActualReply.Status+ActualReply.Opcode+
    			ActualReply.Value.Byte[3]+
    			ActualReply.Value.Byte[2]+
    			ActualReply.Value.Byte[1]+
    			ActualReply.Value.Byte[0];

    	USBReply[0] = moduleConfig.serialHostAddress;
    	USBReply[1] = moduleConfig.serialModuleAddress;
    	USBReply[2] = ActualReply.Status;
    	USBReply[3] = ActualReply.Opcode;
    	USBReply[4] = ActualReply.Value.Byte[3];
    	USBReply[5] = ActualReply.Value.Byte[2];
    	USBReply[6] = ActualReply.Value.Byte[1];
    	USBReply[7] = ActualReply.Value.Byte[0];
    	USBReply[8] = Checksum;

    	usb_sendData(USBReply, 9);
    }

#endif

    // reset command state (reply has been send)
  	TMCLCommandState = TCS_IDLE;
  	TMCLReplyFormat = RF_STANDARD;

  	// last command was a reset?
  	if(ResetRequested)
  		tmcl_resetCPU(true);

  	/* read next request */

#ifdef USE_UART_INTERFACE

  	if(uart_read((char *)&Byte))  // new UART request available?
  	{
  		if(uart_checkTimeout())
  			UARTCount=0;  // discard everything when there has been a command timeout

  		UARTCmd[UARTCount++] = Byte;

  		if(UARTCount==9)  // Nine bytes have been received without timeout
  		{
  			UARTCount=0;
  			systemInfo_incCommunicationLoopCounter();

  			if(UARTCmd[0] == moduleConfig.serialModuleAddress)  // is this our address?
  			{
  				uint8_t checksum = 0;
  				for(i=0; i<8; i++)
  					checksum+=UARTCmd[i];

  				if(checksum==UARTCmd[8])  // is the checksum correct?
  				{
  					ActualCommand.Opcode=UARTCmd[1];
  					ActualCommand.Type=UARTCmd[2];
  					ActualCommand.Motor=UARTCmd[3];
  					ActualCommand.Value.Byte[3]=UARTCmd[4];
  					ActualCommand.Value.Byte[2]=UARTCmd[5];
  					ActualCommand.Value.Byte[1]=UARTCmd[6];
  					ActualCommand.Value.Byte[0]=UARTCmd[7];
  					TMCLCommandState = TCS_UART;
  				}
  				else TMCLCommandState = TCS_UART_ERROR;  //Checksum wrong
  			}
  		}
    }
#endif


#ifdef USE_USB_INTERFACE

    if(usb_getUSBCmd(USBCmd))
    {
    	systemInfo_incCommunicationLoopCounter();

    	if(USBCmd[0] == moduleConfig.serialModuleAddress)	 // check address
    	{
    		uint8_t checksum=0;
    		for(i=0; i<8; i++)
    			checksum+=USBCmd[i];

    		if(checksum==USBCmd[8])  // check checksum
    		{
    			ActualCommand.Opcode=USBCmd[1];
    			ActualCommand.Type=USBCmd[2];
    			ActualCommand.Motor=USBCmd[3];
    			ActualCommand.Value.Byte[3]=USBCmd[4];
    			ActualCommand.Value.Byte[2]=USBCmd[5];
    			ActualCommand.Value.Byte[1]=USBCmd[6];
    			ActualCommand.Value.Byte[0]=USBCmd[7];
    			TMCLCommandState = TCS_USB;
    		} else TMCLCommandState = TCS_USB_ERROR;  // checksum was wrong
    	}
    }
#endif

   	// handle request after successful reading
   	if(TMCLCommandState!=TCS_IDLE && TMCLCommandState!=TCS_UART_ERROR && TMCLCommandState!=TCS_USB_ERROR)
   		tmcl_executeActualCommand();
}

/* TMCL command ROL */
void tmcl_rotateLeft()
{
	bldc_setTargetVelocity(ActualCommand.Motor, -ActualCommand.Value.Int32);
}

/* TMCL command ROR */
void tmcl_rotateRight()
{
	bldc_setTargetVelocity(ActualCommand.Motor, ActualCommand.Value.Int32);
}

/* TMCL command MST */
void tmcl_motorStop()
{
	bldc_setTargetVelocity(ActualCommand.Motor, 0);
}

uint32_t tmcl_handleAxisParameter(uint8_t motor, uint8_t command, uint8_t type, int32_t *value)
{
	uint32_t errors = REPLY_OK;

	if(motor < NUMBER_OF_MOTORS)
	{
		switch(type)
		{
			// ===== general info =====

			case 0: // status flags
				if (command == TMCL_GAP)
				{
					*value = flags_getAllStatusFlags(motor);
				}
				break;
			case 1: // supply voltage
				if (command == TMCL_GAP)
				{
					*value = bldc_getSupplyVoltage();
				}
				break;
			case 2: // driver temperature
				if (command == TMCL_GAP)
				{
					*value = bldc_getMotorTemperature();
				}
				break;

			// ===== ADC measurement =====

			case 3: // adc_I0_raw
				if (command == TMCL_GAP)
				{
					tmc4671_writeInt(motor, TMC4671_ADC_RAW_ADDR, ADC_RAW_ADDR_ADC_I1_RAW_ADC_I0_RAW);
					*value = TMC4671_FIELD_READ(motor, TMC4671_ADC_RAW_DATA, TMC4671_ADC_I0_RAW_MASK, TMC4671_ADC_I0_RAW_SHIFT);
				}
				break;
			case 4: // adc_I1_raw
				if (command == TMCL_GAP)
				{
					tmc4671_writeInt(motor, TMC4671_ADC_RAW_ADDR, ADC_RAW_ADDR_ADC_I1_RAW_ADC_I0_RAW);
					*value = TMC4671_FIELD_READ(motor, TMC4671_ADC_RAW_DATA, TMC4671_ADC_I1_RAW_MASK, TMC4671_ADC_I1_RAW_SHIFT);
				}
				break;
			case 5: // current_phase_U
				if (command == TMCL_GAP)
					*value = (int16_t)TMC4671_FIELD_READ(motor, TMC4671_ADC_IWY_IUX, TMC4671_ADC_IUX_MASK, TMC4671_ADC_IUX_SHIFT);
				break;
			case 6: // current_phase_V
				if (command == TMCL_GAP)
					*value = (int16_t)TMC4671_FIELD_READ(motor, TMC4671_ADC_IV, TMC4671_ADC_IV_MASK, TMC4671_ADC_IV_SHIFT);
				break;
			case 7: // current_phase_W
				if (command == TMCL_GAP)
				{
					*value = (int16_t)TMC4671_FIELD_READ(motor, TMC4671_ADC_IWY_IUX, TMC4671_ADC_IWY_MASK, TMC4671_ADC_IWY_SHIFT);
				}
				break;

			// ===== ADC offset configuration =====

			case 8: // dual-shunt adc_I0 offset
				if (command == TMCL_SAP)
				{
					if ((*value >= 0) && (*value < 65536))
						bldc_setAdcI0Offset(motor, *value);
					else
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					*value = bldc_getAdcI0Offset(motor);
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].adc_I0_offset-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].adc_I0_offset, sizeof(motorConfig[motor].adc_I0_offset));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].adc_I0_offset-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].adc_I0_offset, sizeof(motorConfig[motor].adc_I0_offset));
				}
				break;
			case 9: // dual-shunt adc_I1 offset
				if (command == TMCL_SAP)
				{
					if ((*value >= 0) && (*value < 65536))
						bldc_setAdcI1Offset(motor, *value);
					else
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					*value = bldc_getAdcI1Offset(motor);
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].adc_I1_offset-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].adc_I1_offset, sizeof(motorConfig[motor].adc_I1_offset));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].adc_I1_offset-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].adc_I1_offset, sizeof(motorConfig[motor].adc_I1_offset));
				}
				break;

			// ===== motor settings =====

			case 10: // motor pole pairs
				if (command == TMCL_SAP)
				{
					if((*value >= 1) && (*value <= 255))
						bldc_updateMotorPolePairs(motor, *value);
					else
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					*value = bldc_getMotorPolePairs(motor);
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].motorPolePairs-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].motorPolePairs, sizeof(motorConfig[motor].motorPolePairs));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].motorPolePairs-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].motorPolePairs, sizeof(motorConfig[motor].motorPolePairs));
				}
				break;
			case 11: // max current
				if (command == TMCL_SAP)
				{
					if((*value >= 0) && (*value <= MAX_CURRENT))
						bldc_updateMaxMotorCurrent(motor, *value);
					else
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					*value = bldc_getMaxMotorCurrent(motor);
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].maximumCurrent-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].maximumCurrent, sizeof(motorConfig[motor].maximumCurrent));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].maximumCurrent-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].maximumCurrent, sizeof(motorConfig[motor].maximumCurrent));
				}
				break;
			case 12: // open loop current
				if (command == TMCL_SAP)
				{
					if((*value >= 0) && (*value <= MAX_CURRENT))
						motorConfig[motor].openLoopCurrent = *value;
					else
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					*value = motorConfig[motor].openLoopCurrent;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].openLoopCurrent-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].openLoopCurrent, sizeof(motorConfig[motor].openLoopCurrent));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].openLoopCurrent-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].openLoopCurrent, sizeof(motorConfig[motor].openLoopCurrent));
				}
				break;
		      case 13: // motor direction (shaftBit)
		          if (command == TMCL_SAP) {
		            if (!bldc_setMotorDirection(motor, *value))
		              errors = REPLY_INVALID_VALUE;
		          } else if (command == TMCL_GAP) {
		            *value = bldc_getMotorDirection(motor);
		          } else if (command == TMCL_STAP) {
		        	  eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].shaftBit-(u32)&motorConfig[motor],
		        			  (u8 *)&motorConfig[motor].shaftBit, sizeof(motorConfig[motor].shaftBit));
		          } else if (command == TMCL_RSAP) {
		        	  eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].shaftBit-(u32)&motorConfig[motor],
		        			  (u8 *)&motorConfig[motor].shaftBit, sizeof(motorConfig[motor].shaftBit));
		          }
		          break;
			case 14: // motor type
				if (command == TMCL_GAP)
				{
					*value = motorConfig[motor].motorType; // firmware supports BLDC motor only
				}
				break;
			case 15: // commutation mode ("sensor_selection")
				if (command == TMCL_SAP)
				{
					if (!bldc_setCommutationMode(motor, *value))
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					*value = bldc_getCommutationMode(motor);
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].commutationMode-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].commutationMode, sizeof(motorConfig[motor].commutationMode));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].commutationMode-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].commutationMode, sizeof(motorConfig[motor].commutationMode));
				}
				break;

			// ===== pwm settings =====

			case 16: // set weasel pwm frequency
				if (command == TMCL_SAP)
				{
					motorConfig[motor].pwm_freq = *value;
					tmc4671_writeInt(motor, TMC4671_PWM_MAXCNT, 25000000 / (*value) * 4 - 1);
				} else if (command == TMCL_GAP) {
					*value = motorConfig[motor].pwm_freq;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].pwm_freq-(u32)&motorConfig[motor],
						(u8 *)&motorConfig[motor].pwm_freq, sizeof(motorConfig[motor].pwm_freq));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].pwm_freq-(u32)&motorConfig[motor],
						(u8 *)&motorConfig[motor].pwm_freq, sizeof(motorConfig[motor].pwm_freq));
				}
				break;
			case 17: // placeholder for PWM_BBM_H time
				break;
			case 18: // placeholder for PWM_BBM_H time
				break;

			// ===== torque mode settings =====

			case 20: // target current
				if (command == TMCL_SAP)
				{
					if(!bldc_setTargetMotorCurrent(motor, *value))
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					*value = bldc_getTargetMotorCurrent(motor);
				}
				break;
			case 21: // actual current
				if (command == TMCL_GAP)
					*value = bldc_getActualMotorCurrent(motor);
				break;

			// ===== velocity mode settings =====

			case 24: // target velocity
				if (command == TMCL_SAP)
				{
					if(!bldc_setTargetVelocity(motor, *value))
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					*value = bldc_getTargetVelocity(motor);
				}
				break;
			case 25: // ramp velocity
				if (command == TMCL_GAP)
				{
					*value = bldc_getRampGeneratorVelocity(motor);
				}
				break;
			case 26: // actual velocity
				if (command == TMCL_GAP)
				{
					*value = bldc_getActualVelocity(motor);
				}
				break;
			case 27: // max velocity
				if (command == TMCL_SAP)
				{
					if (!bldc_setMaxVelocity(motor, *value))
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					*value = motorConfig[motor].maxVelocity;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].maxVelocity-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].maxVelocity, sizeof(motorConfig[motor].maxVelocity));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].maxVelocity-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].maxVelocity, sizeof(motorConfig[motor].maxVelocity));
				}
				break;
			case 28: // enable velocity ramp
				if (command == TMCL_SAP)
				{
					if (!bldc_setRampEnabled(motor, *value))
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					*value = motorConfig[motor].useVelocityRamp ? 1:0;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].useVelocityRamp-(u32)&motorConfig[motor],
						(u8 *)&motorConfig[motor].useVelocityRamp, sizeof(motorConfig[motor].useVelocityRamp));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].useVelocityRamp-(u32)&motorConfig[motor],
						(u8 *)&motorConfig[motor].useVelocityRamp, sizeof(motorConfig[motor].useVelocityRamp));
				}
				break;
			case 29: // acceleration
				if (command == TMCL_SAP)
				{
					if (!bldc_setAcceleration(motor, *value))
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					*value = motorConfig[motor].acceleration;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].acceleration-(u32)&motorConfig[motor],
						(u8 *)&motorConfig[motor].acceleration, sizeof(motorConfig[motor].acceleration));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].acceleration-(u32)&motorConfig[motor],
						(u8 *)&motorConfig[motor].acceleration, sizeof(motorConfig[motor].acceleration));
				}
				break;
			case 30: // placeholder for deceleration (ED)
				break;

			// ===== pressure mode settings =====

			case 31: // target pressure
				if (command == TMCL_SAP)
				{
					if(!bldc_setTargetPressure(motor, *value))
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					*value = bldc_getTargetPressure(motor);
				}
				break;
			case 32: // ramp pressure
				if (command == TMCL_GAP) {
					*value = bldc_getRampPressure(motor);
				}
				break;
			case 33: // actual pressure
				if (command == TMCL_GAP)
				{
					*value = bldc_getActualPressure(motor);
				}
				break;
			case 34: // max pressure
				if (command == TMCL_SAP)
				{
					motorConfig[motor].maxPressure = *value;
				} else if (command == TMCL_GAP) {
					*value = motorConfig[motor].maxPressure;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].maxPressure-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].maxPressure, sizeof(motorConfig[motor].maxPressure));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].maxPressure-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].maxPressure, sizeof(motorConfig[motor].maxPressure));
				}
				break;

			// ===== pi controller settings =====

			case 35: // torque P
				if (command == TMCL_SAP)
				{
					if((*value >= 0) && (*value <= 32767))
					{
						motorConfig[motor].pidTorque_P_param = *value;
						tmc4671_setTorqueFluxPI(motor, motorConfig[motor].pidTorque_P_param, motorConfig[motor].pidTorque_I_param);
					} else {
						errors = REPLY_INVALID_VALUE;
					}
				} else if (command == TMCL_GAP) {
					*value = motorConfig[motor].pidTorque_P_param;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].pidTorque_P_param-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].pidTorque_P_param, sizeof(motorConfig[motor].pidTorque_P_param));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].pidTorque_P_param-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].pidTorque_P_param, sizeof(motorConfig[motor].pidTorque_P_param));
				}
				break;
			case 36: // torque I
				if (command == TMCL_SAP)
				{
					if((*value >= 0) && (*value <= 32767))
					{
						motorConfig[motor].pidTorque_I_param = *value;
						tmc4671_setTorqueFluxPI(motor, motorConfig[motor].pidTorque_P_param, motorConfig[motor].pidTorque_I_param);
					} else {
						errors = REPLY_INVALID_VALUE;
					}
				} else if (command == TMCL_GAP) {
					*value = motorConfig[motor].pidTorque_I_param;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].pidTorque_I_param-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].pidTorque_I_param, sizeof(motorConfig[motor].pidTorque_I_param));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].pidTorque_I_param-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].pidTorque_I_param, sizeof(motorConfig[motor].pidTorque_I_param));
				}
				break;
			case 37: // velocity P
				if (command == TMCL_SAP)
				{
					if((*value >= 0) && (*value <= 32767))
					{
						motorConfig[motor].pidVelocity_P_param = *value;
						tmc4671_setVelocityPI(motor, motorConfig[motor].pidVelocity_P_param, motorConfig[motor].pidVelocity_I_param);
					}
					else
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					*value = motorConfig[motor].pidVelocity_P_param;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].pidVelocity_P_param-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].pidVelocity_P_param, sizeof(motorConfig[motor].pidVelocity_P_param));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].pidVelocity_P_param-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].pidVelocity_P_param, sizeof(motorConfig[motor].pidVelocity_P_param));
				}
				break;
			case 38: // velocity I
				if (command == TMCL_SAP)
				{
					if((*value >= 0) && (*value <= 32767))
					{
						motorConfig[motor].pidVelocity_I_param = *value;
						tmc4671_setVelocityPI(motor, motorConfig[motor].pidVelocity_P_param, motorConfig[motor].pidVelocity_I_param);
					} else {
						errors = REPLY_INVALID_VALUE;
					}
				} else if (command == TMCL_GAP) {
					*value = motorConfig[motor].pidVelocity_I_param;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].pidVelocity_I_param-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].pidVelocity_I_param, sizeof(motorConfig[motor].pidVelocity_I_param));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].pidVelocity_I_param-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].pidVelocity_I_param, sizeof(motorConfig[motor].pidVelocity_I_param));
				}
				break;

			case 39: // pressure P
				if (command == TMCL_SAP)
				{
					if((*value >= 0) && (*value <= 32767))
					{
						motorConfig[motor].pidPressure_P_param = *value;
					}
					else
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					*value = motorConfig[motor].pidPressure_P_param;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].pidPressure_P_param-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].pidPressure_P_param, sizeof(motorConfig[motor].pidPressure_P_param));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].pidPressure_P_param-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].pidPressure_P_param, sizeof(motorConfig[motor].pidPressure_P_param));
				}
				break;
			case 40: // pressure I
				if (command == TMCL_SAP)
				{
					if((*value >= 0) && (*value <= 32767))
					{
						motorConfig[motor].pidPressure_I_param = *value;
					}
					else
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					*value = motorConfig[motor].pidPressure_I_param;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].pidPressure_I_param-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].pidPressure_I_param, sizeof(motorConfig[motor].pidPressure_I_param));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].pidPressure_I_param-(u32)&motorConfig[motor],
							(u8 *)&motorConfig[motor].pidPressure_I_param, sizeof(motorConfig[motor].pidPressure_I_param));
				}
				break;

			case 41: // torque I-Sum
				if (command == TMCL_GAP)
				{
					*value = tmc4671_readFieldWithDependency(motor, TMC4671_PID_ERROR_DATA, TMC4671_PID_ERROR_ADDR, 4, TMC4671_PID_TORQUE_ERROR_SUM_MASK, TMC4671_PID_TORQUE_ERROR_SUM_SHIFT);
				}
				break;
			case 42: // flux I-Sum
				if (command == TMCL_GAP)
				{
					*value = tmc4671_readFieldWithDependency(motor, TMC4671_PID_ERROR_DATA, TMC4671_PID_ERROR_ADDR, 5, TMC4671_PID_FLUX_ERROR_SUM_MASK, TMC4671_PID_FLUX_ERROR_SUM_SHIFT);
				}
				break;
			case 43: // velocity I-Sum
				if (command == TMCL_GAP) {
					*value = tmc4671_readFieldWithDependency(motor, TMC4671_PID_ERROR_DATA, TMC4671_PID_ERROR_ADDR, 6, TMC4671_PID_VELOCITY_ERROR_SUM_MASK, TMC4671_PID_VELOCITY_ERROR_SUM_SHIFT) / motorConfig[motor].motorPolePairs;
				}
				break;
			case 44: // pressure I-Sum
				if (command == TMCL_GAP) {
					*value = bldc_getPressureErrorSum(motor);
				}
				break;

			case 47: // actual open loop commutation angle
				if (command == TMCL_GAP)
				{
					*value = FIELD_GET(tmc4671_readInt(motor, TMC4671_OPENLOOP_PHI), TMC4671_OPENLOOP_PHI_MASK, TMC4671_OPENLOOP_PHI_SHIFT);
				}
				break;
			case 48: // actual digital hall angle
				if (command == TMCL_GAP)
				{
					*value = FIELD_GET(tmc4671_readInt(motor, TMC4671_HALL_PHI_E_INTERPOLATED_PHI_E), TMC4671_HALL_PHI_E_MASK, TMC4671_HALL_PHI_E_SHIFT);
				}
				break;

			// ===== hall sensor settings =====

			case 50: // hall polarity
				if (command == TMCL_SAP)
				{
					if((*value == 0) || (*value == 1))
					{
						motorConfig[motor].hallPolarity = *value;
						bldc_updateHallSettings(motor);
					}
					else
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					motorConfig[motor].hallPolarity = (tmc4671_readInt(motor, TMC4671_HALL_MODE) & TMC4671_HALL_POLARITY_MASK) ? 1 : 0;
					*value = motorConfig[motor].hallPolarity;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].hallPolarity-(u32)&motorConfig[motor],
						(u8 *)&motorConfig[motor].hallPolarity, sizeof(motorConfig[motor].hallPolarity));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].hallPolarity-(u32)&motorConfig[motor],
						(u8 *)&motorConfig[motor].hallPolarity, sizeof(motorConfig[motor].hallPolarity));
				}
				break;
			case 51: // hall direction
				if (command == TMCL_SAP)
				{
					if((*value == 0) || (*value == 1))
					{
						motorConfig[motor].hallDirection = *value;
						bldc_updateHallSettings(motor);
					}
					else
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					motorConfig[motor].hallDirection = (tmc4671_readInt(motor, TMC4671_HALL_MODE) & TMC4671_HALL_DIRECTION_MASK) ? 1 : 0;
					*value = motorConfig[motor].hallDirection;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].hallDirection-(u32)&motorConfig[motor],
						(u8 *)&motorConfig[motor].hallDirection, sizeof(motorConfig[motor].hallDirection));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].hallPolarity-(u32)&motorConfig[motor],
						(u8 *)&motorConfig[motor].hallDirection, sizeof(motorConfig[motor].hallDirection));
				}
				break;
			case 52: // hall interpolation
				if (command == TMCL_SAP)
				{
					if((*value == 0) || (*value == 1))
					{
						motorConfig[motor].hallInterpolation = *value;
						bldc_updateHallSettings(motor);
					}
					else
						errors = REPLY_INVALID_VALUE;
				} else if (command == TMCL_GAP) {
					motorConfig[motor].hallInterpolation = (tmc4671_readInt(motor, TMC4671_HALL_MODE) & TMC4671_HALL_INTERPOLATION_MASK) ? 1 : 0;
					*value = motorConfig[motor].hallInterpolation;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].hallInterpolation-(u32)&motorConfig[motor],
						(u8 *)&motorConfig[motor].hallInterpolation, sizeof(motorConfig[motor].hallInterpolation));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].hallInterpolation-(u32)&motorConfig[motor],
						(u8 *)&motorConfig[motor].hallInterpolation, sizeof(motorConfig[motor].hallInterpolation));
				}
				break;
			case 53: // hall phi_e offset
				if (command == TMCL_SAP)
				{
					motorConfig[motor].hallPhiEOffset = *value;
					bldc_updateHallSettings(motor);
				} else if (command == TMCL_GAP) {
					motorConfig[motor].hallPhiEOffset = FIELD_GET(tmc4671_readInt(motor, TMC4671_HALL_PHI_E_PHI_M_OFFSET), TMC4671_HALL_PHI_E_OFFSET_MASK, TMC4671_HALL_PHI_E_OFFSET_SHIFT);
					*value = motorConfig[motor].hallPhiEOffset;
				} else if (command == TMCL_STAP) {
					eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].hallPhiEOffset-(u32)&motorConfig[motor],
						(u8 *)&motorConfig[motor].hallPhiEOffset, sizeof(motorConfig[motor].hallPhiEOffset));
				} else if (command == TMCL_RSAP) {
					eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].hallPhiEOffset-(u32)&motorConfig[motor],
						(u8 *)&motorConfig[motor].hallPhiEOffset, sizeof(motorConfig[motor].hallPhiEOffset));
				}
				break;
			case 54: // raw hall sensor inputs
				if (command == TMCL_GAP) {
					*value = (tmc4671_readInt(motor, TMC4671_INPUTS_RAW) & (TMC4671_HALL_WY_OF_HALL_RAW_MASK | TMC4671_HALL_V_OF_HALL_RAW_MASK | TMC4671_HALL_UX_OF_HALL_RAW_MASK)) >> TMC4671_HALL_UX_OF_HALL_RAW_SHIFT;
				}
				break;

			// ===== brake chopper settings  ===== (placeholder (ED))
//			case 60: // brake chopper enable
//				break;
//			case 61: // brake chopper voltage
//				break;
//			case 62: // brake chopper hysteresis
//				break;
//			case 63: // brake chopper active
//				break;

			// ===== tosv settings =====
			case 100: // enable tosv
				if (command == TMCL_SAP)
				{
					tosv_enableVentilator(&tosvConfig[motor], *value);
				}
				else if (command == TMCL_GAP)
				{
					*value = tosv_isVentilatorEnabled(&tosvConfig[motor]);
				}
				break;
			case 101: // actual state
				if(command==TMCL_GAP)
				{
					*value = tosvConfig[motor].actualState;
				}
				break;
			case 102: // timer
				if(command==TMCL_GAP)
				{
					*value = tosvConfig[motor].timer;
				}
				break;
			case 103: // startup time
				if (command == TMCL_SAP)
				{
					tosvConfig[motor].tStartup = *value;
				}
				else if (command == TMCL_GAP)
				{
					*value = tosvConfig[motor].tStartup;
				}
				break;
			case 104: // inhalation rise time
				if (command == TMCL_SAP)
				{
					tosvConfig[motor].tInhalationRise = *value;
				}
				else if (command == TMCL_GAP)
				{
					*value = tosvConfig[motor].tInhalationRise;
				}
				break;
			case 105: // inhalation pause time
				if (command == TMCL_SAP)
				{
					tosvConfig[motor].tInhalationPause = *value;
				}
				else if (command == TMCL_GAP)
				{
					*value = tosvConfig[motor].tInhalationPause;
				}
				break;
			case 106: // exhalation fall time
				if (command == TMCL_SAP)
				{
					tosvConfig[motor].tExhalationFall = *value;
				}
				else if (command == TMCL_GAP)
				{
					*value = tosvConfig[motor].tExhalationFall;
				}
				break;
			case 107: // exhalation pause time
				if (command == TMCL_SAP)
				{
					tosvConfig[motor].tExhalationPause = *value;
				}
				else if (command == TMCL_GAP)
				{
					*value = tosvConfig[motor].tExhalationPause;
				}
				break;
			case 108: // LIMIT pressure
				if (command == TMCL_SAP)
				{
					tosvConfig[motor].pLIMIT = *value;
				}
				else if (command == TMCL_GAP)
				{
					*value = tosvConfig[motor].pLIMIT;
				}
				break;
			case 109: // PEEP pressure
				if (command == TMCL_SAP)
				{
					tosvConfig[motor].pPEEP = *value;
				}
				else if (command == TMCL_GAP)
				{
					*value = tosvConfig[motor].pPEEP;
				}
				break;

      // ===== TEST_OK: TOSV (to be removed again later) =====
//  		case 200:
//  			if(command==TMCL_SAP)
//  			{
//  			  if(!TOSV_setPEEP(*value)) errors=REPLY_INVALID_VALUE;
//  			}
//  			else if(command==TMCL_GAP)
//  			{
//          *value=TOSV_getPEEP();
//        }
//  			break;
//
//  		case 201:
//  			if(command==TMCL_SAP)
//  			{
//    			if(!TOSV_setLimit(*value)) errors=REPLY_INVALID_VALUE;
//    		}
//  			else if(command==TMCL_GAP)
//  			{
//  				*value=TOSV_getLimit();
//  			}
//  			break;
//
//  		case 202:
//  			if(command==TMCL_SAP)
//  			{
//    			if(!TOSV_setRiseTime(*value)) errors=REPLY_INVALID_VALUE;
//    		}
//  			else if(command==TMCL_GAP)
//  			{
//  				*value=TOSV_getRiseTime();
//  			}
//  			break;
//
//  		case 203:
//  			if(command==TMCL_SAP)
//  			{
//    			if(!TOSV_setFallTime(*value)) errors=REPLY_INVALID_VALUE;
//    		}
//  			else if(command==TMCL_GAP)
//  			{
//  				*value=TOSV_getFallTime();
//  			}
//  			break;
//
//  		case 204:
//  			if(command==TMCL_SAP)
//  			{
//    			if(!TOSV_setFrequency(*value)) errors=REPLY_INVALID_VALUE;
//    		}
//  			else if(command==TMCL_GAP)
//  			{
//  				*value=TOSV_getFrequency();
//  			}
//  			break;
//
//  		case 205:
//  			if(command==TMCL_SAP)
//  			{
//    			if(!TOSV_setItoE(*value)) errors=REPLY_INVALID_VALUE;
//    		}
//  			else if(command==TMCL_GAP)
//  			{
//  				*value=TOSV_getItoE();
//  			}
//  			break;
//
//  		case 206:
//  			if(command==TMCL_SAP)
//  			{
//    			if(!TOSV_setVolume(*value)) errors=REPLY_INVALID_VALUE;
//    		}
//  			else if(command==TMCL_GAP)
//  			{
//  				*value=TOSV_getVolume();
//  			}
//  			break;
  			
       
			// ===== debugging =====

			case 240: // debug value 0
				if (command == TMCL_SAP)
					debug_setTestVar0(*value);
				else if (command == TMCL_GAP)
					*value = debug_getTestVar0();
				break;
			case 241: // debug value 1
				if (command == TMCL_SAP)
					debug_setTestVar1(*value);
				else if (command == TMCL_GAP)
					*value = debug_getTestVar1();
				break;
			case 242:
				// debug value 2
				if (command == TMCL_SAP)
					debug_setTestVar2(*value);
				else if (command == TMCL_GAP)
					*value = debug_getTestVar2();
				break;
			case 243: // debug value 3
				if (command == TMCL_SAP)
					debug_setTestVar3(*value);
				else if (command == TMCL_GAP)
					*value = debug_getTestVar3();
				break;
			case 244: // debug value 4
				if (command == TMCL_SAP)
					debug_setTestVar4(*value);
				else if (command == TMCL_GAP)
					*value = debug_getTestVar4();
				break;
			case 245: // debug value 5
				if (command == TMCL_SAP)
					debug_setTestVar5(*value);
				else if (command == TMCL_GAP)
					*value = debug_getTestVar5();
				break;
			case 246: // debug value 6
				if (command == TMCL_SAP)
					debug_setTestVar6(*value);
				else if (command == TMCL_GAP)
					*value = debug_getTestVar6();
				break;
			case 247: // debug value 7
				if (command == TMCL_SAP)
					debug_setTestVar7(*value);
				else if (command == TMCL_GAP)
					*value = debug_getTestVar7();
				break;
			case 248: // debug value 8
				if (command == TMCL_SAP)
					debug_setTestVar8(*value);
				else if (command == TMCL_GAP)
					*value = debug_getTestVar8();
				break;
			case 249:
				// debug value 9
				if (command == TMCL_SAP)
					debug_setTestVar9(*value);
				else if (command == TMCL_GAP)
					*value = debug_getTestVar9();
				break;

			case 250:
				if (command == TMCL_GAP)
					*value = systemInfo_getMainLoopsPerSecond();
				break;
			case 251:
				if (command == TMCL_GAP)
					*value = systemInfo_getVelocityLoopsPerSecond();
				break;
			case 252:
				if (command == TMCL_GAP)
					*value = systemInfo_getCommunicationsPerSecond();
				break;

			case 255: // enable/disable mc & driver
				if (command == TMCL_SAP)
				{
					if(*value == 0)
					{
						//bldc_setStopRegulationMode();
						tmcm_disableDriver(motor);
					}
					else
					{
						tmcm_enableDriver(motor);
					}
				} else if (command == TMCL_GAP) {
					*value = tmcm_getDriverState(motor);
				}
				break;
			default:
				ActualReply.Status = REPLY_WRONG_TYPE;
				break;
		}
	}
	else ActualReply.Status = REPLY_INVALID_VALUE;

	return errors;
}

/* TMCL command SIO */
void tmcl_setOutput()
{
	switch(ActualCommand.Motor)
	{
		case 2: // digital_outputs
			if(ActualCommand.Value.Int32 == 0)
				tmcm_clearModuleSpecificIOPin(ActualCommand.Type);
			else
				tmcm_setModuleSpecificIOPin(ActualCommand.Type);
			break;
		default:
			ActualReply.Status = REPLY_INVALID_VALUE;
			break;
	}
}

/* TMCL command GIO */
void tmcl_getInput()
{
	switch(ActualCommand.Motor)
	{
    	case 0: // digital_inputs
//    		ActualReply.Value.Int32 = tmcm_getModuleSpecificIOPin(ActualCommand.Type);
    		break;
    	case 1: // analog_inputs
    		ActualReply.Value.Int32 = tmcm_getModuleSpecificADCValue(ActualCommand.Type);
    		break;
    	case 2: // digital outputs
    		ActualReply.Value.Int32 = tmcm_getModuleSpecificIOPinStatus(ActualCommand.Type);
    		break;
    	default:
    		ActualReply.Status = REPLY_INVALID_VALUE;
    		break;
	}
}

/* TMCL command 136 (Get Version) */
void tmcl_getVersion()
{
	uint32_t i;

	if(ActualCommand.Type==0)
	{
		TMCLReplyFormat = RF_SPECIAL;
		SpecialReply[0] = moduleConfig.serialHostAddress;
		for(i=0; i<8; i++)
			SpecialReply[i+1]=VersionString[i];
	}
	else if(ActualCommand.Type==1)
	{
		ActualReply.Value.Byte[3] = SW_TYPE_HIGH;
		ActualReply.Value.Byte[2] = SW_TYPE_LOW;
		ActualReply.Value.Byte[1] = SW_VERSION_HIGH;
		ActualReply.Value.Byte[0] = SW_VERSION_LOW;
	}
}

/* reset EEPROM to firmware defaults */
void tmcl_firmwareDefault()
{
	if(ActualCommand.Type==0 && ActualCommand.Motor==0 && ActualCommand.Value.Int32==1234)
	{
		eeprom_writeConfigByte(TMCM_ADDR_EEPROM_MAGIC, 0);
		tmcl_resetCPU(true);
	}
}
void tmcl_setVentilatorParameter(void)
{
	if(ActualCommand.Motor==0)
	{
  	switch(ActualCommand.Type)
  	{
//  		case 0:
//  			if(!TOSV_setPEEP(ActualCommand.Value.Int32)) ActualReply.Status=REPLY_INVALID_VALUE;
//  			break;
//
//  		case 1:
//  			if(!TOSV_setLimit(ActualCommand.Value.Int32)) ActualReply.Status=REPLY_INVALID_VALUE;
//  			break;
//
//  		case 2:
//  			if(!TOSV_setRiseTime(ActualCommand.Value.Int32)) ActualReply.Status=REPLY_INVALID_VALUE;
//  			break;
//
//  		case 3:
//  			if(!TOSV_setFallTime(ActualCommand.Value.Int32)) ActualReply.Status=REPLY_INVALID_VALUE;
//  			break;
//
//  		case 4:
//  			if(!TOSV_setFrequency(ActualCommand.Value.Int32)) ActualReply.Status=REPLY_INVALID_VALUE;
//  			break;
//
//  		case 5:
//  			if(!TOSV_setItoE(ActualCommand.Value.Int32)) ActualReply.Status=REPLY_INVALID_VALUE;
//  			break;
//
//  		case 6:
//  			if(!TOSV_setVolume(ActualCommand.Value.Int32)) ActualReply.Status=REPLY_INVALID_VALUE;
//  			break;
  			
  		default:
  			ActualReply.Status=REPLY_WRONG_TYPE;
  			break;
  	}
  }
  else ActualReply.Status=REPLY_INVALID_VALUE;
}

void tmcl_getVentilatorParameter(void)
{
	if(ActualCommand.Motor==0)
	{
  	switch(ActualCommand.Type)
  	{
//  		case 0:
//  			ActualReply.Value.Int32=TOSV_getPEEP();
//  			break;
//
//  		case 1:
//  			ActualReply.Value.Int32=TOSV_getLimit();
//  			break;
//
//  		case 2:
//  			ActualReply.Value.Int32=TOSV_getRiseTime();
//  			break;
//
//  		case 3:
//  			ActualReply.Value.Int32=TOSV_getFallTime();
//  			break;
//
//  		case 4:
//  			ActualReply.Value.Int32=TOSV_getFrequency();
//  			break;
//
//  		case 5:
//  			ActualReply.Value.Int32=TOSV_getItoE();
//  			break;
//
//  		case 6:
//  			ActualReply.Value.Int32=TOSV_getVolume();
//  			break;
  			
  		default:
  			ActualReply.Status=REPLY_WRONG_TYPE;
  			break;
  	}
  }
  else ActualReply.Status=REPLY_INVALID_VALUE;
}

void tmcl_startVentilator(void)
{
	if(ActualCommand.Type==0)
	{
		if(ActualCommand.Motor==0 && ActualCommand.Value.Int32==0x1234)
		{
//			if(!TOSV_startVentilator()) ActualReply.Status=REPLY_CMD_NOT_AVAILABLE;
		}
		else ActualReply.Status=REPLY_INVALID_VALUE;
	}
	else ActualReply.Status=REPLY_WRONG_TYPE;
}

void tmcl_stopVentilator(void)
{
	if(ActualCommand.Type==0)
	{
		if(ActualCommand.Motor==0 && ActualCommand.Value.Int32==0x4321)
		{
//			TOSV_stopVentilator();
		}
		else ActualReply.Status=REPLY_INVALID_VALUE;
	}
	else ActualReply.Status=REPLY_WRONG_TYPE;
}


/* deinit NVIC */
#if (BOARD_CPU != STM32F103)
void NVIC_DeInit(void)
{
	uint32_t index;

	for(index=0; index<8; index++)
	{
		NVIC->ICER[index] = 0xFFFFFFFF;
		NVIC->ICPR[index] = 0xFFFFFFFF;
	}

	for(index = 0; index < 240; index++)
	{
		NVIC->IP[index] = 0x00000000;
	}
}
#endif

/* special command to switch to bootloader mode */
void tmcl_boot()
{
	if(ActualCommand.Type==0x81 && ActualCommand.Motor==0x92 &&
			ActualCommand.Value.Byte[3]==0xa3 && ActualCommand.Value.Byte[2]==0xb4 &&
			ActualCommand.Value.Byte[1]==0xc5 && ActualCommand.Value.Byte[0]==0xd6)
	{
		for (int i = 0; i < NUMBER_OF_MOTORS; i++)
		{
			tmcm_disableDriver(i);

#ifdef USE_BRAKECHOPPER
			//motorConfig[i].brakeChopperEnabled = 0;
			//tasks_updateBrakeChopperConfig(i);
#endif
		}

#ifdef USE_USB_INTERFACE
		usb_detach();
		uint32_t lastCheck = systick_getTimer();
		while(abs(systick_getTimer()-lastCheck) < 1000) {;}
#endif

#if BOARD_CPU==STM32F103
		asm volatile("CPSID I\n");
		NVIC_DeInit();
		SysTick->CTRL = 0;
		DMA_Cmd(DMA1_Channel1, DISABLE);
		DMA_DeInit(DMA1_Channel1);
		ADC_DeInit(ADC1);
		EXTI_DeInit();
		TIM_DeInit(TIM1);
#else
	    __disable_irq();
		NVIC_DeInit();
		SysTick->CTRL = 0;
	    DMA_Cmd(DMA2_Stream0, DISABLE);
	    DMA_DeInit(DMA2_Stream0);
		ADC_DeInit();
	    EXTI_DeInit();
	    TIM_DeInit(TIM1);
#endif
	    tmcl_resetCPU(false);
	}
}

/* TMCL command 255 (Reset) */
void tmcl_softwareReset()
{
	if(ActualCommand.Value.Int32==1234)
		ResetRequested = true;
}

/* Reset CPU with or without peripherals */
void tmcl_resetCPU(uint8_t resetPeripherals)
{
	if(resetPeripherals)
		SCB->AIRCR = AIRCR_VECTKEY_MASK | (uint32_t)0x04;
	else
		SCB->AIRCR = AIRCR_VECTKEY_MASK | (uint32_t)0x01;
}
