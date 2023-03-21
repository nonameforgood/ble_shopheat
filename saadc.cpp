#include "gj/commands.h"
#include "gj/serial.h"
#include "nrf_drv_saadc.h"
#include "gj/config.h"

#define SAMPLES_IN_BUFFER 5
volatile uint8_t state = 1;

static nrf_saadc_value_t     m_buffer_pool[2][SAMPLES_IN_BUFFER];
static uint32_t              m_adc_evt_counter;


DEFINE_CONFIG_INT32(batt.powerpin, batt_powerpin, 7);
DEFINE_CONFIG_INT32(batt.adcchan, batt_adcchan, 2);


extern "C"
{

void saadc_callback(nrf_drv_saadc_evt_t const * p_event)
{
    if (p_event->type == NRF_DRV_SAADC_EVT_DONE)
    {
        ret_code_t err_code;

        err_code = nrf_drv_saadc_buffer_convert(p_event->data.done.p_buffer, SAMPLES_IN_BUFFER);
        APP_ERROR_CHECK(err_code);

        int i;
        SER("ADC event number: %d\r\n", (int)m_adc_evt_counter);

        for (i = 0; i < SAMPLES_IN_BUFFER; i++)
        {
            SER("%d\r\n", p_event->data.done.p_buffer[i]);
        }
        m_adc_evt_counter++;
    }
}
 
void saadc_init(void)
{
    ret_code_t err_code;

    nrf_saadc_channel_config_t channel_config =
        NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_AIN0);

    err_code = nrf_drv_saadc_init(NULL, saadc_callback);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_saadc_channel_init(0, &channel_config);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_saadc_buffer_convert(m_buffer_pool[0], SAMPLES_IN_BUFFER);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_saadc_buffer_convert(m_buffer_pool[1], SAMPLES_IN_BUFFER);
    APP_ERROR_CHECK(err_code);

}


} //extern "C"

uint32_t adcIndex = -1;

void Command_ReadBattery()
{
  adcIndex = 0;
}

DEFINE_COMMAND_NO_ARGS(readbattery, Command_ReadBattery);

void InitAdc()
{
 // err_code = nrf_drv_power_init(NULL);
  //APP_ERROR_CHECK(err_code);

 // ret_code_t ret_code = nrf_pwr_mgmt_init(0);
  //APP_ERROR_CHECK(ret_code);

  saadc_init();

  REFERENCE_COMMAND(readbattery);

  uint32_t powerPin = GJ_CONF_INT32_VALUE(batt_powerpin);

  bool input = false;
  bool pullUp = false;
  SetupPin(powerPin, input, pullUp);
  WritePin(powerPin, 1);
}

void UpdateAdc()
{
  if (adcIndex == -1)
    return;
  
  uint32_t powerPin = GJ_CONF_INT32_VALUE(batt_powerpin);

  if (adcIndex < SAMPLES_IN_BUFFER)
  {
    if (adcIndex == 0)
    { 
      WritePin(powerPin, 0);
      nrf_drv_saadc_buffer_convert(m_buffer_pool[0],SAMPLES_IN_BUFFER);
    }

    //SER("sample adc %d\n", adcIndex);
    nrf_drv_saadc_sample();
    ++adcIndex;
  }
  else
  {
    //SER("sample convert\n");
    nrf_drv_saadc_buffer_convert(m_buffer_pool[0],SAMPLES_IN_BUFFER);
    WritePin(powerPin, 1);
    adcIndex = -1;
  }
}