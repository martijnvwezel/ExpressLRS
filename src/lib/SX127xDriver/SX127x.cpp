#include "SX127x.h"
#include "logging.h"

SX127xHal hal;
SX127xDriver *SX127xDriver::instance = NULL;

#ifdef USE_SX1276_RFO_HF
  #ifndef OPT_USE_SX1276_RFO_HF
    #define OPT_USE_SX1276_RFO_HF true
  #endif
#else
  #define OPT_USE_SX1276_RFO_HF false
#endif

const uint8_t SX127x_AllowedSyncwords[105] =
    {0, 5, 6, 7, 11, 12, 13, 15, 18,
     21, 23, 26, 29, 30, 31, 33, 34,
     37, 38, 39, 40, 42, 44, 50, 51,
     54, 55, 57, 58, 59, 61, 63, 65,
     67, 68, 71, 77, 78, 79, 80, 82,
     84, 86, 89, 92, 94, 96, 97, 99,
     101, 102, 105, 106, 109, 111, 113, 115,
     117, 118, 119, 121, 122, 124, 126, 127,
     129, 130, 138, 143, 161, 170, 172, 173,
     175, 180, 181, 182, 187, 190, 191, 192,
     193, 196, 199, 201, 204, 205, 208, 209,
     212, 213, 219, 220, 221, 223, 227, 229,
     235, 239, 240, 242, 243, 246, 247, 255};

//////////////////////////////////////////////

SX127xDriver::SX127xDriver(): SX12xxDriverCommon()
{
  instance = this;
  PayloadLength = 8; // Dummy default value which is overwritten during setup.
  currFreq = 0; // leave as 0 to ensure that it gets set
}

bool SX127xDriver::Begin()
{
  DBGLN("SX127x Driver Begin");
  hal.IsrCallback = &SX127xDriver::IsrCallback;
  hal.init();

  if (DetectChip())
  {
    ConfigLoraDefaults();
    return true;
  }
  else
  {
    return false;
  }
}

void SX127xDriver::End()
{
  SetMode(SX127x_OPMODE_SLEEP);
  hal.end();
  RemoveCallbacks();
}

void SX127xDriver::ConfigLoraDefaults()
{
  hal.writeRegister(SX127X_REG_OP_MODE, SX127x_OPMODE_SLEEP);
  hal.writeRegister(SX127X_REG_OP_MODE, ModFSKorLoRa); //must be written in sleep mode
  SetMode(SX127x_OPMODE_STANDBY);

  hal.writeRegister(SX127X_REG_PAYLOAD_LENGTH, PayloadLength);
  SetSyncWord(currSyncWord);
  hal.writeRegister(SX127X_REG_FIFO_TX_BASE_ADDR, SX127X_FIFO_TX_BASE_ADDR_MAX);
  hal.writeRegister(SX127X_REG_FIFO_RX_BASE_ADDR, SX127X_FIFO_RX_BASE_ADDR_MAX);
  hal.setRegValue(SX127X_REG_DIO_MAPPING_1, 0b11000000, 7, 6); //undocumented "hack", looking at Table 18 from datasheet SX127X_REG_DIO_MAPPING_1 = 11 appears to be unspported by infact it generates an intterupt on both RXdone and TXdone, this saves switching modes.
  hal.writeRegister(SX127X_REG_LNA, SX127X_LNA_BOOST_ON);
  hal.writeRegister(SX1278_REG_MODEM_CONFIG_3, SX1278_AGC_AUTO_ON | SX1278_LOW_DATA_RATE_OPT_OFF);
  hal.setRegValue(SX127X_REG_OCP, SX127X_OCP_ON | SX127X_OCP_150MA, 5, 0); //150ma max current
  SetPreambleLength(SX127X_PREAMBLE_LENGTH_LSB);
  hal.setRegValue(SX127X_REG_INVERT_IQ, (uint8_t)IQinverted, 6, 6);
}

void SX127xDriver::SetBandwidthCodingRate(SX127x_Bandwidth bw, SX127x_CodingRate cr)
{
  if ((currBW != bw) || (currCR != cr))
  {
    if (currSF == SX127x_SF_6) // set SF6 optimizations
    {
      hal.writeRegister(SX127X_REG_MODEM_CONFIG_1, bw | cr | SX1278_HEADER_IMPL_MODE);
      hal.setRegValue(SX127X_REG_MODEM_CONFIG_2, SX1278_RX_CRC_MODE_OFF, 2, 2);
    }
    else
    {
      if (headerExplMode)
      {
        hal.writeRegister(SX127X_REG_MODEM_CONFIG_1, bw | cr | SX1278_HEADER_EXPL_MODE);
      }
      else
      {
        hal.writeRegister(SX127X_REG_MODEM_CONFIG_1, bw | cr | SX1278_HEADER_IMPL_MODE);
      }

      if (crcEnabled)
      {
        hal.setRegValue(SX127X_REG_MODEM_CONFIG_2, SX1278_RX_CRC_MODE_ON, 2, 2);
      }
      else
      {
        hal.setRegValue(SX127X_REG_MODEM_CONFIG_2, SX1278_RX_CRC_MODE_OFF, 2, 2);
      }
    }

    if (bw == SX127x_BW_500_00_KHZ)
    {
      //datasheet errata reconmendation http://caxapa.ru/thumbs/972894/SX1276_77_8_ErrataNote_1.1_STD.pdf
      hal.writeRegister(0x36, 0x02);
      hal.writeRegister(0x3a, 0x64);
    }
    else
    {
      hal.writeRegister(0x36, 0x03);
    }
    currCR = cr;
    currBW = bw;
  }
}

bool SyncWordOk(uint8_t syncWord)
{
  for (unsigned int i = 0; i < sizeof(SX127x_AllowedSyncwords); i++)
  {
    if (syncWord == SX127x_AllowedSyncwords[i])
    {
      return true;
    }
  }
  return false;
}

void SX127xDriver::SetSyncWord(uint8_t syncWord)
{
  uint8_t _syncWord = syncWord;

  while (SyncWordOk(_syncWord) == false)
  {
    _syncWord++;
  }

  if(syncWord != _syncWord){
    DBGLN("Using syncword: %d instead of: %d", _syncWord, syncWord);
  }

  hal.writeRegister(SX127X_REG_SYNC_WORD, _syncWord);
  currSyncWord = _syncWord;
}

void SX127xDriver::SetOutputPower(uint8_t Power)
{
  SetMode(SX127x_OPMODE_STANDBY);
  if (OPT_USE_SX1276_RFO_HF)
  {
    hal.writeRegister(SX127X_REG_PA_CONFIG, SX127X_PA_SELECT_RFO | SX127X_MAX_OUTPUT_POWER_RFO_HF | Power);
  }
  else
  {
    hal.writeRegister(SX127X_REG_PA_CONFIG, SX127X_PA_SELECT_BOOST | SX127X_MAX_OUTPUT_POWER | Power);
  }
  currPWR = Power;
}

void SX127xDriver::SetPreambleLength(uint8_t PreambleLen)
{
  if (currPreambleLen != PreambleLen)
  {
    hal.writeRegister(SX127X_REG_PREAMBLE_LSB, PreambleLen);
    currPreambleLen = PreambleLen;
  }
}

void SX127xDriver::SetSpreadingFactor(SX127x_SpreadingFactor sf)
{
  if (currSF != sf)
  {
    hal.setRegValue(SX127X_REG_MODEM_CONFIG_2, sf | SX127X_TX_MODE_SINGLE, 7, 3);
    if (sf == SX127x_SF_6)
    {
      hal.setRegValue(SX127X_REG_DETECT_OPTIMIZE, SX127X_DETECT_OPTIMIZE_SF_6, 2, 0);
      hal.writeRegister(SX127X_REG_DETECTION_THRESHOLD, SX127X_DETECTION_THRESHOLD_SF_6);
    }
    else
    {
      hal.setRegValue(SX127X_REG_DETECT_OPTIMIZE, SX127X_DETECT_OPTIMIZE_SF_7_12, 2, 0);
      hal.writeRegister(SX127X_REG_DETECTION_THRESHOLD, SX127X_DETECTION_THRESHOLD_SF_7_12);
    }
    currSF = sf;
  }
}

void ICACHE_RAM_ATTR SX127xDriver::SetFrequencyHz(uint32_t freq)
{
  currFreq = freq;
  SetMode(SX127x_OPMODE_STANDBY);

  int32_t FRQ = ((uint32_t)((double)freq / (double)FREQ_STEP));

  uint8_t FRQ_MSB = (uint8_t)((FRQ >> 16) & 0xFF);
  uint8_t FRQ_MID = (uint8_t)((FRQ >> 8) & 0xFF);
  uint8_t FRQ_LSB = (uint8_t)(FRQ & 0xFF);

  WORD_ALIGNED_ATTR uint8_t outbuff[3] = {FRQ_MSB, FRQ_MID, FRQ_LSB}; //check speedup

  hal.writeRegisterBurst(SX127X_REG_FRF_MSB, outbuff, sizeof(outbuff));
}

void ICACHE_RAM_ATTR SX127xDriver::SetFrequencyReg(uint32_t freq)
{
  currFreq = freq;
  SetMode(SX127x_OPMODE_STANDBY);

  uint8_t FRQ_MSB = (uint8_t)((freq >> 16) & 0xFF);
  uint8_t FRQ_MID = (uint8_t)((freq >> 8) & 0xFF);
  uint8_t FRQ_LSB = (uint8_t)(freq & 0xFF);

  WORD_ALIGNED_ATTR uint8_t outbuff[3] = {FRQ_MSB, FRQ_MID, FRQ_LSB}; //check speedup

  hal.writeRegisterBurst(SX127X_REG_FRF_MSB, outbuff, sizeof(outbuff));
}

void ICACHE_RAM_ATTR SX127xDriver::SetRxTimeoutUs(uint32_t interval)
{
  timeoutSymbols = 0; // no timeout i.e. use continuous mode
  if (interval)
  {
    unsigned int spread = 0;
    switch (currSF)
    {
    case SX127x_SF_6:
      spread = 6;
      break;
    case SX127x_SF_7:
      spread = 7;
      break;
    case SX127x_SF_8:
      spread = 8;
      break;
    case SX127x_SF_9:
      spread = 9;
      break;
    case SX127x_SF_10:
      spread = 10;
      break;
    case SX127x_SF_11:
      spread = 11;
      break;
    case SX127x_SF_12:
      spread = 12;
      break;
    }
    uint32_t symbolTimeUs = ((uint32_t)(1 << spread)) * 1000000 / GetCurrBandwidth();
    timeoutSymbols = interval / symbolTimeUs;
    hal.setRegValue(SX127X_REG_MODEM_CONFIG_2, timeoutSymbols >> 8, 1, 0);  // set the timeout MSB
    hal.setRegValue(SX127X_REG_SYMB_TIMEOUT_LSB, timeoutSymbols & 0xFF);
    DBGLN("SetRxTimeout(%u), symbolTime=%uus symbols=%u", interval, (uint32_t)symbolTimeUs, timeoutSymbols)
  }
}

bool SX127xDriver::DetectChip()
{
  uint8_t i = 0;
  bool flagFound = false;
  while ((i < 3) && !flagFound)
  {
    uint8_t version = hal.readRegister(SX127X_REG_VERSION);
    DBG("%x", version);
    if (version == 0x12)
    {
      flagFound = true;
    }
    else
    {
      DBGLN(" not found! (%d of 3 tries) REG_VERSION == 0x%x", i+1, version);
      delay(200);
      i++;
    }
  }

  if (!flagFound)
  {
    DBGLN(" not found!");
    return false;
  }
  else
  {
    DBGLN(" found! (match by REG_VERSION == 0x12)");
  }
  hal.setRegValue(SX127X_REG_OP_MODE, SX127x_OPMODE_SLEEP, 2, 0);
  return true;
}

/////////////////////////////////////TX functions/////////////////////////////////////////

void ICACHE_RAM_ATTR SX127xDriver::TXnbISR()
{
  currOpmode = SX127x_OPMODE_STANDBY; //goes into standby after transmission
  //TXdoneMicros = micros();
  TXdoneCallback();
}

void ICACHE_RAM_ATTR SX127xDriver::TXnb(uint8_t * data, uint8_t size)
{
  // if (currOpmode == SX127x_OPMODE_TX)
  // {
  //   DBGLN("abort TX");
  //   return; // we were already TXing so abort. this should never happen!!!
  // }
  SetMode(SX127x_OPMODE_STANDBY);

  hal.TXenable();
  hal.writeRegister(SX127X_REG_FIFO_ADDR_PTR, SX127X_FIFO_TX_BASE_ADDR_MAX);
  hal.writeRegisterFIFO(data, size);

  SetMode(SX127x_OPMODE_TX);
}

///////////////////////////////////RX Functions Non-Blocking///////////////////////////////////////////

void ICACHE_RAM_ATTR SX127xDriver::RXnbISR()
{
  hal.readRegisterFIFO(RXdataBuffer, PayloadLength);
  if (timeoutSymbols)
  {
    // From page 42 of the datasheet rev 7
    // In Rx Single mode, the device will return to Standby mode as soon as the interrupt occurs
    currOpmode = SX127x_OPMODE_STANDBY;
  }
  RXdoneCallback(SX12XX_RX_OK);
}

void ICACHE_RAM_ATTR SX127xDriver::RXnb()
{
  // if (currOpmode == SX127x_OPMODE_RXCONTINUOUS)
  // {
  //   DBGLN("abort RX");
  //   return; // we were already TXing so abort
  // }
  SetMode(SX127x_OPMODE_STANDBY);
  hal.RXenable();
  hal.writeRegister(SX127X_REG_FIFO_ADDR_PTR, SX127X_FIFO_RX_BASE_ADDR_MAX);
  if (timeoutSymbols)
  {
    SetMode(SX127x_OPMODE_RXSINGLE);
  }
  else
  {
    SetMode(SX127x_OPMODE_RXCONTINUOUS);
  }
}

void ICACHE_RAM_ATTR SX127xDriver::GetLastPacketStats()
{
  LastPacketRSSI = GetLastPacketRSSI();
  LastPacketSNRRaw = GetLastPacketSNRRaw();
  // https://www.mouser.com/datasheet/2/761/sx1276-1278113.pdf
  // Section 3.5.5 (page 87)
  int8_t negOffset = (LastPacketSNRRaw < 0) ? (LastPacketSNRRaw / RADIO_SNR_SCALE) : 0;
  LastPacketRSSI += negOffset;
}

void ICACHE_RAM_ATTR SX127xDriver::SetMode(SX127x_RadioOPmodes mode)
{ //if radio is not already in the required mode set it to the requested mod
  if (currOpmode != mode)
  {
    hal.writeRegister(SX127X_REG_OP_MODE, mode);
    currOpmode = mode;
  }
}

void SX127xDriver::Config(uint8_t bw, uint8_t sf, uint8_t cr, uint32_t freq, uint8_t preambleLen, bool InvertIQ, uint8_t PayloadLength, uint32_t interval)
{
  Config(bw, sf, cr, freq, preambleLen, currSyncWord, InvertIQ, PayloadLength, interval);
}

void SX127xDriver::Config(uint8_t bw, uint8_t sf, uint8_t cr, uint32_t freq, uint8_t preambleLen, uint8_t syncWord, bool InvertIQ, uint8_t PayloadLength, uint32_t interval)
{
  PayloadLength = PayloadLength;
  IQinverted = InvertIQ;
  ConfigLoraDefaults();
  SetPreambleLength(preambleLen);
  SetOutputPower(currPWR);
  SetSpreadingFactor((SX127x_SpreadingFactor)sf);
  SetBandwidthCodingRate((SX127x_Bandwidth)bw, (SX127x_CodingRate)cr);
  SetFrequencyReg(freq);
  SetRxTimeoutUs(interval);
}

uint32_t ICACHE_RAM_ATTR SX127xDriver::GetCurrBandwidth()
{
  switch (currBW)
  {
  case SX127x_BW_7_80_KHZ:
    return 7.8E3;
  case SX127x_BW_10_40_KHZ:
    return 10.4E3;
  case SX127x_BW_15_60_KHZ:
    return 15.6E3;
  case SX127x_BW_20_80_KHZ:
    return 20.8E3;
  case SX127x_BW_31_25_KHZ:
    return 31.25E3;
  case SX127x_BW_41_70_KHZ:
    return 41.7E3;
  case SX127x_BW_62_50_KHZ:
    return 62.5E3;
  case SX127x_BW_125_00_KHZ:
    return 125E3;
  case SX127x_BW_250_00_KHZ:
    return 250E3;
  case SX127x_BW_500_00_KHZ:
    return 500E3;
  }
  return -1;
}

uint32_t ICACHE_RAM_ATTR SX127xDriver::GetCurrBandwidthNormalisedShifted() // this is basically just used for speedier calc of the freq offset, pre compiled for 32mhz xtal
{

  switch (currBW)
  {
  case SX127x_BW_7_80_KHZ:
    return 1026;
  case SX127x_BW_10_40_KHZ:
    return 769;
  case SX127x_BW_15_60_KHZ:
    return 513;
  case SX127x_BW_20_80_KHZ:
    return 385;
  case SX127x_BW_31_25_KHZ:
    return 256;
  case SX127x_BW_41_70_KHZ:
    return 192;
  case SX127x_BW_62_50_KHZ:
    return 128;
  case SX127x_BW_125_00_KHZ:
    return 64;
  case SX127x_BW_250_00_KHZ:
    return 32;
  case SX127x_BW_500_00_KHZ:
    return 16;
  }
  return -1;
}

/**
 * Set the PPMcorrection register to adjust data rate to frequency error
 * @param offset is in Hz or FREQ_STEP (FREQ_HZ_TO_REG_VAL) units, whichever
 *    was used to SetFrequencyHz/SetFrequencyReg
 */
void ICACHE_RAM_ATTR SX127xDriver::SetPPMoffsetReg(int32_t offset)
{
  int8_t offsetPPM = (offset * 1e6 / currFreq) * 95 / 100;
  hal.writeRegister(SX127x_PPMOFFSET, (uint8_t)offsetPPM);
}

bool ICACHE_RAM_ATTR SX127xDriver::GetFrequencyErrorbool()
{
  return (hal.readRegister(SX127X_REG_FEI_MSB) & 0b1000) >> 3; // returns true if pos freq error, neg if false
}

int32_t ICACHE_RAM_ATTR SX127xDriver::GetFrequencyError()
{

  WORD_ALIGNED_ATTR uint8_t reg[3] = {0x0, 0x0, 0x0};
  hal.readRegisterBurst(SX127X_REG_FEI_MSB, sizeof(reg), reg);

  uint32_t RegFei = ((reg[0] & 0b0111) << 16) + (reg[1] << 8) + reg[2];

  int32_t intFreqError = RegFei;

  if ((reg[0] & 0b1000) >> 3)
  {
    intFreqError -= 524288; // Sign bit is on
  }

  int32_t fErrorHZ = (intFreqError >> 3) * (SX127xDriver::GetCurrBandwidthNormalisedShifted()); // bit shift hackery so we don't have to use floaty bois; the >> 3 is intentional and is a simplification of the formula on page 114 of sx1276 datasheet
  fErrorHZ >>= 4;

  return fErrorHZ;
}

uint8_t ICACHE_RAM_ATTR SX127xDriver::UnsignedGetLastPacketRSSI()
{
  return (hal.getRegValue(SX127X_REG_PKT_RSSI_VALUE));
}

int8_t ICACHE_RAM_ATTR SX127xDriver::GetLastPacketRSSI()
{
  return (-157 + hal.getRegValue(SX127X_REG_PKT_RSSI_VALUE));
}

int8_t ICACHE_RAM_ATTR SX127xDriver::GetCurrRSSI()
{
  return (-157 + hal.getRegValue(SX127X_REG_RSSI_VALUE));
}

int8_t ICACHE_RAM_ATTR SX127xDriver::GetLastPacketSNRRaw()
{
  return (int8_t)hal.getRegValue(SX127X_REG_PKT_SNR_VALUE);;
}

uint8_t ICACHE_RAM_ATTR SX127xDriver::GetIrqFlags()
{
  return hal.getRegValue(SX127X_REG_IRQ_FLAGS);
}

void ICACHE_RAM_ATTR SX127xDriver::ClearIrqFlags()
{
  hal.writeRegister(SX127X_REG_IRQ_FLAGS, 0b11111111);
}

// int16_t MeasureNoiseFloor() TODO disabled for now
// {
//     int NUM_READS = RSSI_FLOOR_NUM_READS * FHSSgetChannelCount();
//     float returnval = 0;

//     for (uint32_t freq = 0; freq < FHSSgetChannelCount(); freq++)
//     {
//         FHSSsetCurrIndex(freq);
//         Radio.SetMode(SX127X_CAD);

//         for (int i = 0; i < RSSI_FLOOR_NUM_READS; i++)
//         {
//             returnval = returnval + Radio.GetCurrRSSI();
//             delay(5);
//         }
//     }
//     returnval = returnval / NUM_READS;
//     return (returnval);
// }

// uint8_t SX127xDriver::RunCAD() TODO
// {
//   SetMode(SX127X_STANDBY);

//   hal.setRegValue(SX127X_REG_DIO_MAPPING_1, SX127X_DIO0_CAD_DONE | SX127X_DIO1_CAD_DETECTED, 7, 4);

//   SetMode(SX127X_CAD);
//   ClearIrqFlags();

//   uint32_t startTime = millis();

//   while (!digitalRead(SX127x_dio0))
//   {
//     if (millis() > (startTime + 500))
//     {
//       return (CHANNEL_FREE);
//     }
//     else
//     {
//       //yield();
//       if (digitalRead(SX127x_dio1))
//       {
//         ClearIrqFlags();
//         return (PREAMBLE_DETECTED);
//       }
//     }
//   }

//   ClearIrqFlags();
//   return (CHANNEL_FREE);
// }

void ICACHE_RAM_ATTR SX127xDriver::IsrCallback()
{
    uint8_t irqStatus = instance->GetIrqFlags();
    instance->ClearIrqFlags();
    if ((irqStatus & SX127X_CLEAR_IRQ_FLAG_TX_DONE) && (instance->currOpmode == SX127x_OPMODE_TX))
    {
        hal.TXRXdisable();
        instance->TXnbISR();
    }
    if ((irqStatus & SX127X_CLEAR_IRQ_FLAG_RX_DONE) && ((instance->currOpmode == SX127x_OPMODE_RXSINGLE) || (instance->currOpmode == SX127x_OPMODE_RXCONTINUOUS)))
        instance->RXnbISR();
}
