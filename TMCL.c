/*
 * TMCL.c
 *
 *  Created on: 31.03.2020
 *      Author: OK / ED
 */

#include "TMCL.h"
#include "hal/system/SysTick.h"
#include "hal/comm/SPI.h"
#include "hal/tmcl/TMCL-Variables.h"
#include <stdlib.h>

#ifdef USE_USB_INTERFACE
	#include "hal/comm/USB.h"
#endif

	#define AIRCR_VECTKEY_MASK    ((uint32_t)0x05FA0000)

	uint8_t ResetRequested;
	uint32_t TMCLLastError;
	volatile uint32_t TMCLTickTimer;
	extern const char *VersionString;

	// local used functions
	uint32_t tmcl_handleAxisParameter(uint8_t motor, uint8_t command, uint8_t type, int32_t *value);

	void tmcl_rotateLeft();
	void tmcl_rotateRight();
	void tmcl_motorStop();
	void tmcl_setOutput();
	void tmcl_getInput();
	void tmcl_getVersion();
	void tmcl_boot();
	void tmcl_softwareReset();

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
        case TMCL_readRegisterChannel_1:
        	if (ActualCommand.Motor == 0)
        		ActualReply.Value.Int32 = tmc4671_readInt(ActualCommand.Motor, ActualCommand.Type);
        	else if (ActualCommand.Motor == 1)
        		ActualReply.Value.Int32 = tmc6200_readInt(ActualCommand.Motor, ActualCommand.Type);
        	break;
        case TMCL_writeRegisterChannel_1:
        	if (ActualCommand.Motor == 0)
        		tmc4671_writeInt(ActualCommand.Motor, ActualCommand.Type, ActualCommand.Value.Int32);
        	else if (ActualCommand.Motor == 1)
        		tmc6200_writeInt(ActualCommand.Motor, ActualCommand.Type, ActualCommand.Value.Int32);
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

#ifdef USE_USB_INTERFACE
    uint8_t USBCmd[9];
    uint8_t USBReply[9];
#endif

    /* send reply for last TMCL request */

#ifdef USE_USB_INTERFACE

    if(TMCLCommandState==TCS_USB)
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

#ifdef USE_USB_INTERFACE

    if(usb_getUSBCmd(USBCmd))
    {
    	if(USBCmd[0] == moduleConfig.serialModuleAddress)	 // check address
    	{
    		uint8_t Checksum=0;
    		for(i=0; i<8; i++)
    			Checksum+=USBCmd[i];

    		if(Checksum==USBCmd[8])  // check checksum
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
	//bldc_setTargetVelocity(-ActualCommand.Value.Int32);
}

/* TMCL command ROR */
void tmcl_rotateRight()
{
	//bldc_setTargetVelocity(ActualCommand.Value.Int32);
}

/* TMCL command MST */
void tmcl_motorStop()
{
	//bldc_setTargetVelocity(0);
}

uint32_t tmcl_handleAxisParameter(uint8_t motor, uint8_t command, uint8_t type, int32_t *value)
{
//	UNUSED(command);
	uint32_t errors = REPLY_OK;

	if(motor == 0)
	{
		switch(type)
		{
			// ===== ADC measurement =====
			case 0: // adc_I0_raw
				if (command == TMCL_GAP)
				{
					tmc4671_writeInt(motor, TMC4671_ADC_RAW_ADDR, 0 /*ADC_RAW_ADDR_ADC_I1_RAW_ADC_I0_RAW*/);
					*value = TMC4671_FIELD_READ(motor, TMC4671_ADC_RAW_DATA, TMC4671_ADC_I0_RAW_MASK, TMC4671_ADC_I0_RAW_SHIFT);
				}
				break;
			case 1: // adc_I1_raw
				if (command == TMCL_GAP)
				{
					tmc4671_writeInt(motor, TMC4671_ADC_RAW_ADDR, 0 /*ADC_RAW_ADDR_ADC_I1_RAW_ADC_I0_RAW*/);
					*value = TMC4671_FIELD_READ(motor, TMC4671_ADC_RAW_DATA, TMC4671_ADC_I1_RAW_MASK, TMC4671_ADC_I1_RAW_SHIFT);
				}
				break;

				// ===== ADC settings =====
				case 5: // dual-shunt phase_A offset
					if (command == TMCL_SAP) {
						if (!bldc_setAdcI0Offset(motor, *value))
							errors = REPLY_INVALID_VALUE;
					} else if (command == TMCL_GAP) {
						*value = bldc_getAdcI0Offset(motor);
					}
//					else if (command == TMCL_STAP) {
//						eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig.adc_I0_offset-(u32)&motorConfig,
//								(u8 *)&motorConfig.adc_I0_offset, sizeof(motorConfig.adc_I0_offset));
//					} else if (command == TMCL_RSAP) {
//						eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig.adc_I0_offset-(u32)&motorConfig,
//								(u8 *)&motorConfig.adc_I0_offset, sizeof(motorConfig.adc_I0_offset));
//					}
					break;
				case 6: // dual-shunt phase_B offset
					if (command == TMCL_SAP) {
						if (!bldc_setAdcI1Offset(motor, *value))
							errors = REPLY_INVALID_VALUE;
					} else if (command == TMCL_GAP) {
						*value = bldc_getAdcI1Offset(motor);
					}
//					else if (command == TMCL_STAP) {
//						eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig.adc_I1_offset-(u32)&motorConfig,
//								(u8 *)&motorConfig.adc_I1_offset, sizeof(motorConfig.adc_I1_offset));
//					} else if (command == TMCL_RSAP) {
//						eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig.adc_I1_offset-(u32)&motorConfig,
//								(u8 *)&motorConfig.adc_I1_offset, sizeof(motorConfig.adc_I1_offset));
//					}
					break;

					// ===== motor settings =====
					case 10: // motor pole pairs
						if (command == TMCL_SAP) {
							if((*value >= 1) && (*value <= 255))
								bldc_updateMotorPolePairs(motor, *value);
							else
								errors = REPLY_INVALID_VALUE;
						} else if (command == TMCL_GAP) {
							*value = bldc_getMotorPolePairs(motor);
						}
//						else if (command == TMCL_STAP) {
//							eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig.motorPolePairs-(u32)&motorConfig,
//									(u8 *)&motorConfig.motorPolePairs, sizeof(motorConfig.motorPolePairs));
//						} else if (command == TMCL_RSAP) {
//							eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig.motorPolePairs-(u32)&motorConfig,
//									(u8 *)&motorConfig.motorPolePairs, sizeof(motorConfig.motorPolePairs));
//						}
						break;
					case 11: // max current
						if (command == TMCL_SAP) {
							if((*value >= 0) && (*value <= TMCM_MAX_TORQUE))
								bldc_updateMaxMotorCurrent(motor, *value);
							else
								errors = REPLY_INVALID_VALUE;
						} else if (command == TMCL_GAP) {
							*value = bldc_getMaxMotorCurrent(motor);
						}
//						else if (command == TMCL_STAP) {
//							eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig.maximumCurrent-(u32)&motorConfig,
//									(u8 *)&motorConfig.maximumCurrent, sizeof(motorConfig.maximumCurrent));
//						} else if (command == TMCL_RSAP) {
//							eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig.maximumCurrent-(u32)&motorConfig,
//									(u8 *)&motorConfig.maximumCurrent, sizeof(motorConfig.maximumCurrent));
//						}
						break;
					case 12: // open loop current
						if (command == TMCL_SAP) {
							if((*value >= 0) && (*value <= TMCM_MAX_TORQUE))
								motorConfig.openLoopCurrent = *value;
							else
								errors = REPLY_INVALID_VALUE;
						} else if (command == TMCL_GAP) {
							*value = motorConfig.openLoopCurrent;
						}
//						else if (command == TMCL_STAP) {
//							eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig.openLoopCurrent-(u32)&motorConfig,
//									(u8 *)&motorConfig.openLoopCurrent, sizeof(motorConfig.openLoopCurrent));
//						} else if (command == TMCL_RSAP) {
//							eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig.openLoopCurrent-(u32)&motorConfig,
//									(u8 *)&motorConfig.openLoopCurrent, sizeof(motorConfig.openLoopCurrent));
//						}
						break;
//				      case 13: // motor direction (shaftBit)
//				          if (command == TMCL_SAP) {
//				            if (!bldc_setMotorDirection(motor, *value))
//				              errors = REPLY_INVALID_VALUE;
//				          } else if (command == TMCL_GAP) {
//				            *value = bldc_getMotorDirection(motor);
//				          }
//				          else if (command == TMCL_STAP) {
//				        	  eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig.shaftBit-(u32)&motorConfig,
//				        			  (u8 *)&motorConfig.shaftBit, sizeof(motorConfig.shaftBit));
//				          } else if (command == TMCL_RSAP) {
//				        	  eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig.shaftBit-(u32)&motorConfig,
//				        			  (u8 *)&motorConfig.shaftBit, sizeof(motorConfig.shaftBit));
//				          }
				          break;
					case 14: // motor type
						if (command == TMCL_SAP) {
							if (!bldc_setMotorType(motor, *value))
								errors = REPLY_INVALID_VALUE;
						} else if (command == TMCL_GAP) {
							*value = bldc_getMotorType(motor);
						}
//						else if (command == TMCL_STAP) {
//							eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig.motorType-(u32)&motorConfig,
//									(u8 *)&motorConfig.motorType, sizeof(motorConfig.motorType));
//						} else if (command == TMCL_RSAP) {
//							eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig.motorType-(u32)&motorConfig,
//									(u8 *)&motorConfig.motorType, sizeof(motorConfig.motorType));
//						}
						break;

					case 15: // commutation mode ("sensor_m_selection")
						if (command == TMCL_SAP) {
							if (!bldc_setCommutationMode(*value))
								errors = REPLY_INVALID_VALUE;
						} else if (command == TMCL_GAP) {
							*value = bldc_getCommutationMode();
						}
//						else if (command == TMCL_STAP) {
//							eeprom_writeConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].commutationMode-(u32)&motorConfig[motor],
//									(u8 *)&motorConfig[motor].commutationMode, sizeof(motorConfig[motor].commutationMode));
//						} else if (command == TMCL_RSAP) {
//							eeprom_readConfigBlock(TMCM_ADDR_MOTOR_CONFIG+motor*TMCM_MOTOR_CONFIG_SIZE+(u32)&motorConfig[motor].commutationMode-(u32)&motorConfig[motor],
//									(u8 *)&motorConfig[motor].commutationMode, sizeof(motorConfig[motor].commutationMode));
//						}
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

/* deinit NVIC */
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

/* special command to switch to bootloader mode */
void tmcl_boot()
{
	if(ActualCommand.Type==0x81 && ActualCommand.Motor==0x92 &&
			ActualCommand.Value.Byte[3]==0xa3 && ActualCommand.Value.Byte[2]==0xb4 &&
			ActualCommand.Value.Byte[1]==0xc5 && ActualCommand.Value.Byte[0]==0xd6)
	{
		for (int i = 0; i < NUMBER_OF_MOTORS; i++)
			tmcm_disableDriver(i);

#ifdef USE_USB_INTERFACE
		usb_detach();
		uint32_t lastCheck = systick_getTimer();
		while(abs(systick_getTimer()-lastCheck) < 1000) {;}
#endif
	    __disable_irq();
		NVIC_DeInit();
		SysTick->CTRL = 0;
	    DMA_Cmd(DMA2_Stream0, DISABLE);
	    DMA_DeInit(DMA2_Stream0);
		ADC_DeInit();
	    EXTI_DeInit();
	    TIM_DeInit(TIM1);

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
