#if defined(NRF51)

#include "app_pwm.h"

APP_PWM_INSTANCE(PWM1,1);                   // Create the instance "PWM1" using TIMER1.
static int32_t s_period = 1;

void pwm_ready_callback(uint32_t pwm_id)
{
  //empty
}

void pwmInitC(int32_t period, uint32_t dataPin)
{
  ret_code_t err_code;

  app_pwm_config_t pwm1_cfg = APP_PWM_DEFAULT_CONFIG_1CH(period, dataPin);
  pwm1_cfg.pin_polarity[0] = APP_PWM_POLARITY_ACTIVE_HIGH;

  err_code = app_pwm_init(&PWM1,&pwm1_cfg,pwm_ready_callback);
  APP_ERROR_CHECK(err_code);

  s_period = period;
}

void pwmEnable()
{
  app_pwm_enable(&PWM1);
}


void pwmDisable()
{
  app_pwm_disable(&PWM1);
}

bool pwmSetDuty(uint32_t d)
{
  d = d * 100 / s_period;

  if (app_pwm_channel_duty_get(&PWM1, 0) != d)
  {
    return app_pwm_channel_duty_set(&PWM1, 0, d) != NRF_ERROR_BUSY;
  }

  return true;
}


#elif defined(NRF52)

#include "nrf_drv_pwm.h"

static nrf_drv_pwm_t m_pwm0 = NRF_DRV_PWM_INSTANCE(0);
static bool s_pwm0Init = false;

void pwmInitC(int32_t period, uint32_t dataPin)
{
  if (!s_pwm0Init)
  {      
    s_pwm0Init = true;

    nrf_drv_pwm_config_t const config0 =
    {
        .output_pins =
        {
            dataPin | NRF_DRV_PWM_PIN_INVERTED, // channel 0
            NRF_DRV_PWM_PIN_NOT_USED, // channel 1
            NRF_DRV_PWM_PIN_NOT_USED, // channel 2
            NRF_DRV_PWM_PIN_NOT_USED  // channel 3
        },
        .irq_priority = -1, //APP_IRQ_PRIORITY_LOWEST
        .base_clock   = NRF_PWM_CLK_1MHz,
        .count_mode   = NRF_PWM_MODE_UP,
        .top_value    = 20000, // 1 / 50hz at 1MHz
        .load_mode    = NRF_PWM_LOAD_INDIVIDUAL,
        .step_mode    = NRF_PWM_STEP_AUTO
    };
    uint32_t                   err_code;
    err_code = nrf_drv_pwm_init(&m_pwm0, &config0, NULL);
    APP_ERROR_CHECK(err_code);
  }
}


void pwmDisable()
{
  if (!nrf_drv_pwm_is_stopped(&m_pwm0)) 
  {
    bool wait_until_stopped = true;
    nrf_drv_pwm_stop(&m_pwm0, wait_until_stopped);
  }
  
}

void pwmEnable()
{
  //

 // pwmDisable();
}

void PrintHWDuty(int16_t d);

bool pwmSetDuty(uint32_t d)
{
  pwmDisable();

  if (d == 0)
    return;

  int16_t duty = d;
  duty = 20000 - duty;
  // This array cannot be allocated on stack (hence "static") and it must
  // be in RAM (hence no "const", though its content is not changed).
  static nrf_pwm_values_individual_t  /*const*/ seq_values[] =
  {
      { 0, 0, 0, 0 }
  };

  PrintHWDuty( duty);

  seq_values[0].channel_0 = duty;

  nrf_pwm_sequence_t const seq =
  {
      .values.p_individual = seq_values,
      .length              = NRF_PWM_VALUES_LENGTH(seq_values),
      .repeats             = 0,
      .end_delay           = 0
  };

  nrf_drv_pwm_simple_playback(&m_pwm0, &seq, 65535, NRF_DRV_PWM_FLAG_LOOP);

  return true;
}

#endif