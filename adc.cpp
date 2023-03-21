#include "gj/commands.h"
#include "gj/serial.h"
#include "gj/eventmanager.h"

#if defined(NRF51)
#include "nrf_drv_adc.h"
#elif NRF_MODULE_ENABLED(SAADC)
#include "nrf_drv_saadc.h"
#endif

#include "gj/config.h"

class Adc
{
public:
  Adc();
  ~Adc();

  struct FinishInfo
  {
    Adc *m_adc;
    const int16_t *m_values;
    uint32_t m_sampleCount;
    int16_t m_divider;
  };
  typedef std::function<void(const FinishInfo &info)> Function;

  void Init(Function callback);
  void StartSampling(uint32_t analogInput, uint32_t sampleCount);

private:
  uint32_t m_sampleCount = 0;
  Function m_callback = {};
  uint32_t m_eventIndex = 0;

  uint32_t m_sampleCapacity = 0;
  int16_t *m_buffer = nullptr;

  static Adc *ms_instance;

  static void TimerCallback();
  static void CallUserCallback();

#if NRF_MODULE_ENABLED(ADC)
  typedef nrf_drv_adc_evt_t ArgType;
  nrf_drv_adc_channel_t m_channel_config;
  const nrf_drv_adc_evt_type_t m_doneEvent = NRF_DRV_ADC_EVT_DONE;
#elif NRF_MODULE_ENABLED(SAADC)
  typedef nrf_drv_saadc_evt_t ArgType;
  const nrf_drv_saadc_evt_type_t m_doneEvent = NRF_DRV_SAADC_EVT_DONE;
#endif

  static void DriverCallback(ArgType const * p_event);
};

Adc* Adc::ms_instance = nullptr;

Adc::Adc()
{
  ms_instance = this;
} 
Adc::~Adc()
{
  ms_instance = nullptr;
} 
void Adc::Init(Function callback)
{
  m_callback = callback;

#if NRF_MODULE_ENABLED(ADC)
  ret_code_t ret_code;
  nrf_drv_adc_config_t config = NRF_DRV_ADC_DEFAULT_CONFIG;

  ret_code = nrf_drv_adc_init(&config, DriverCallback);
  APP_ERROR_CHECK(ret_code);
#elif NRF_MODULE_ENABLED(SAADC)
  ret_code_t err_code;
  err_code = nrf_drv_saadc_init(NULL, DriverCallback);
  APP_ERROR_CHECK(err_code);
#endif
}

void Adc::StartSampling(uint32_t analogInput, uint32_t sampleCount)
{
  if (sampleCount > m_sampleCapacity)
  {
    m_sampleCapacity = sampleCount;
    delete [] m_buffer;
    m_buffer = new int16_t[sampleCount];
  }

  m_sampleCount = sampleCount;


#if NRF_MODULE_ENABLED(ADC)

  /*
    P0.26/AIN0
    P0.27/AIN1
    P0.01/AIN2
    P0.02/AIN3
    P0.03/AIN4
    P0.04/AIN5
    P0.05/AIN6
    P0.06/AIN7
  */

  m_channel_config = NRF_DRV_ADC_DEFAULT_CHANNEL(1 << analogInput); /**< Channel instance. Default configuration used. */
  m_channel_config.config.config.input = NRF_ADC_CONFIG_SCALING_INPUT_ONE_THIRD;

  nrf_drv_adc_channel_enable(&m_channel_config);
#elif NRF_MODULE_ENABLED(SAADC)
  {
    /*
    P0.02/AIN0
    P0.03/AIN1
    P0.04/AIN2
    P0.05/AIN3
    P0.28/AIN4
    P0.29/AIN5
    P0.30/AIN6
    P0.31/AIN7
    */

  ret_code_t err_code;
  nrf_saadc_channel_config_t channel_config =
      NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE((nrf_saadc_input_t)(analogInput + 1));
  err_code = nrf_drv_saadc_channel_init(0, &channel_config);
  APP_ERROR_CHECK(err_code);
  }
#endif

  m_eventIndex = 0;
  GJEventManager->DelayAdd(TimerCallback, 100000);
}

void Adc::TimerCallback()
{
#if NRF_MODULE_ENABLED(ADC)
  if (ms_instance->m_eventIndex == 0)
  { 
    nrf_drv_adc_buffer_convert(ms_instance->m_buffer,ms_instance->m_sampleCount);
  }
  nrf_drv_adc_sample();
#elif NRF_MODULE_ENABLED(SAADC)
  if (ms_instance->m_eventIndex == 0)
  { 
    nrf_drv_saadc_buffer_convert(ms_instance->m_buffer,ms_instance->m_sampleCount);
  }
  nrf_drv_saadc_sample();
#endif

  ++ms_instance->m_eventIndex;
  if (ms_instance->m_eventIndex < ms_instance->m_sampleCount)
    GJEventManager->DelayAdd(TimerCallback, 1000);
}

void Adc::DriverCallback(ArgType const * p_event)
{
  if (p_event->type == ms_instance->m_doneEvent)
  {
      GJEventManager->Add(CallUserCallback);
  }
}


void Adc::CallUserCallback()
{
#if NRF_MODULE_ENABLED(ADC)
  nrf_drv_adc_channel_disable(&ms_instance->m_channel_config);
#elif NRF_MODULE_ENABLED(SAADC)
  ret_code_t err_code;
  err_code = nrf_drv_saadc_channel_uninit(0);
  APP_ERROR_CHECK(err_code);
#endif

  int32_t resolution = 1024;
  int32_t gain = 6;
  int32_t ref = 60; //0.6V * 100

  int32_t divider = resolution * 100 / (gain * ref);

  FinishInfo info = {ms_instance, ms_instance->m_buffer, ms_instance->m_sampleCount, (int16_t)divider};

  ms_instance->m_callback(info);
}

Adc s_adc;


#if NRF_MODULE_ENABLED(ADC)
  #define DEFAULT_ANALOG_BATT 2
#elif NRF_MODULE_ENABLED(SAADC)
  #define DEFAULT_ANALOG_BATT 0
#endif

DEFINE_CONFIG_INT32(batt.powerpin, batt_powerpin, 7);
DEFINE_CONFIG_INT32(batt.adcchan, batt_adcchan, DEFAULT_ANALOG_BATT);


void AdcCallback(const Adc::FinishInfo &info)
{
  int32_t totalVolt = 0;

  for (int i = 0; i < info.m_sampleCount; i++)
  {
    int32_t volt = info.m_values[i] * 100 / info.m_divider * 2;
    totalVolt += volt;
  }

  totalVolt /= info.m_sampleCount;
  SER("volt=%d\r\n", totalVolt);
}


void Command_ReadBattery()
{
  uint32_t pin = GJ_CONF_INT32_VALUE(batt_adcchan);

  s_adc.StartSampling(pin, 5);
}

void Command_Adc(const char *command)
{
  CommandInfo info;
  GetCommandInfo(command, info);

  if (info.m_argCount < 2)
  {
    return;
  }

  uint32_t input = atoi(info.m_args[0].data());
  uint32_t samples = atoi(info.m_args[1].data());

  s_adc.StartSampling(input, samples);
}


DEFINE_COMMAND_NO_ARGS(readbattery, Command_ReadBattery);
DEFINE_COMMAND_ARGS(adc, Command_Adc);

void InitAdc()
{
  s_adc.Init(AdcCallback);

  REFERENCE_COMMAND(readbattery);
  REFERENCE_COMMAND(adc);

  uint32_t powerPin = GJ_CONF_INT32_VALUE(batt_powerpin);

  bool input = false;
  bool pullUp = false;
  SetupPin(powerPin, input, pullUp);
  WritePin(powerPin, 1);
}
