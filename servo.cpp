#include "gj/commands.h"
#include "gj/serial.h"
#include "gj/eventmanager.h"
#include "gj/config.h"
#include "gj/sensor.h"
#include "gj/datetime.h"
#include "nrf_delay.h"

//#define ENABLE_SERVO_PINS

#if defined(NRF51)
  #define SERVO_DATA_PIN 10
  #define SERVO_PWR_PIN 9
#elif defined(NRF52)
  #define SERVO_DATA_PIN 11
  #define SERVO_PWR_PIN 12
#endif

DEFINE_CONFIG_INT32(svo.powerpin, svo_powerpin, SERVO_PWR_PIN);
DEFINE_CONFIG_INT32(svo.datapin, svo_datapin, SERVO_DATA_PIN);
DEFINE_CONFIG_INT32(svo.autooff, svo_autooff, 1000);
DEFINE_CONFIG_INT32(svo.lowpi, svo_lowpin, 19);
DEFINE_CONFIG_INT32(svo.highpin, svo_highpin, 20);
DEFINE_CONFIG_INT32(svo.pinevents, svo_pinevents, 0); //disable by default, uses too much power
DEFINE_CONFIG_INT32(svo.anglelow, svo_anglelow, 1);
DEFINE_CONFIG_INT32(svo.anglehigh, svo_anglehigh, 150);

#define PWM_PERIOD 10000

uint32_t servoDuty = 0;

extern "C"
{

void pwmInitC(int32_t period, uint32_t dataPin);
void pwmEnable();
void pwmDisable();
bool pwmSetDuty(uint32_t d);

void PrintHWDuty(int16_t d)
{
  //SER("HW duty=%d\n\r", d);
}

}

enum class Steps
{
  None,
  PowerUp,
  SetDuty,
  CancelDuty,
  PowerDown,
  CleanUp,
  Count
};

Steps step = Steps::None;

void IncreaseStep()
{
  step = (Steps)((int32_t)step + 1);
}

uint32_t ReadPositionPin(uint32_t pin)
{
  bool isInput = true;
  int32_t pull = 1;
  SetupPin(pin, isInput, pull);

  const uint32_t pos = ReadPin(pin);

  //disable pull to prevent unwanted power usage
  SetupPin(pin, isInput, 0);

  return pos;
}

void SetDutyEvent()
{
  if (pwmSetDuty(servoDuty) == false)
  {
    GJEventManager->Add(SetDutyEvent);
  }
}

void ProcessStep()
{
  IncreaseStep();

  printf("Step %d %d\n", step, (uint32_t)GetElapsedMicros());
  if (step == Steps::PowerUp)
  {
    uint32_t pinIndex = GJ_CONF_INT32_VALUE(svo_powerpin);
    WritePin(pinIndex, 1);
    pwmEnable();
  }
  else if (step == Steps::SetDuty)
  {
    GJEventManager->Add(SetDutyEvent);
  }
  else if (step == Steps::CancelDuty)
  {
    servoDuty = 0;
    GJEventManager->Add(SetDutyEvent);
  }
  else if (step == Steps::PowerDown)
  {
    uint32_t pinIndex = GJ_CONF_INT32_VALUE(svo_powerpin);
    WritePin(pinIndex, 0);
    pwmDisable();
  }
  else if (step == Steps::CleanUp)
  {
    //Setting data pin to HIGH saves 50 uA 
    uint32_t dataPin = GJ_CONF_INT32_VALUE(svo_datapin);
    WritePin(dataPin, 1);

    uint32_t lowPin = GJ_CONF_INT32_VALUE(svo_lowpin);
    uint32_t highPin = GJ_CONF_INT32_VALUE(svo_highpin);

    SER("low pin(%d) state:%d\n", lowPin, ReadPositionPin(lowPin));
    SER("high pin(%d) state:%d\n", highPin, ReadPositionPin(highPin));    
  }

  const uint32_t dutyDuration = GJ_CONF_INT32_VALUE(svo_autooff);

  //microseconds
  const uint32_t stepLength[(uint32_t)Steps::Count] = {
    0,                    //None
    1000,                 //PowerUp
    dutyDuration * 1000,  //SetDuty
    50000,                //CancelDuty
    1000,                 //PowerDown
    0,                    //CleanUp
  };

  const uint32_t duration = stepLength[(int32_t)step];
  if (duration != 0)
  {
    GJEventManager->DelayAdd(ProcessStep, duration);
  }
}

void SetServoAngle(uint32_t angle)
{
  uint32_t minPulse = 500;
  uint32_t maxPulse = 2400;
  uint32_t pulseRange = maxPulse - minPulse;
  
  const uint32_t pulse = pulseRange * angle / 180 + minPulse;

  #ifdef NRF51
    //servoDuty = pulse * 100 / PWM_PERIOD;
    servoDuty = pulse;
  #elif defined(NRF52)
    servoDuty = pulse;
  #endif

  SER("servo angle=%d pulse=%d duty=%d\r\n", angle, pulse, servoDuty);

  step = Steps::None;
  GJEventManager->Add(ProcessStep);
}

void Command_HeatLow()
{
  uint32_t angle = GJ_CONF_INT32_VALUE(svo_anglelow);
  SetServoAngle(angle);
}

void Command_HeatHigh()
{
  uint32_t angle = GJ_CONF_INT32_VALUE(svo_anglehigh);
  SetServoAngle(angle);
}

void Command_ServoAngle(const char *command)
{
  CommandInfo info;
  GetCommandInfo(command, info);

  char arg[16];

  strncpy(arg, command + info.m_argsOffset[0], info.m_argsLength[0]);
  arg[info.m_argsLength[0]] = 0;

  uint32_t angle = atoi(arg);
  angle = Min<uint32_t>(angle, 180);

  SetServoAngle(angle);
}

void Command_ServoDuty(const char *command)
{
  CommandInfo info;
  GetCommandInfo(command, info);

  char arg[16];

  strncpy(arg, command + info.m_argsOffset[0], info.m_argsLength[0]);
  arg[info.m_argsLength[0]] = 0;

  servoDuty = atoi(arg);
  //servoDuty = Min<uint32_t>(servoDuty, 100);

  SER("servo duty=%d\r\n", servoDuty);

  step = Steps::None;
  GJEventManager->Add(ProcessStep);
}

DEFINE_COMMAND_ARGS(servoangle, Command_ServoAngle);
DEFINE_COMMAND_ARGS(servoduty, Command_ServoDuty);
DEFINE_COMMAND_NO_ARGS(heathigh, Command_HeatHigh);
DEFINE_COMMAND_NO_ARGS(heatlow, Command_HeatLow);

void servoInit(void)
{
  REFERENCE_COMMAND(servoangle);
  REFERENCE_COMMAND(servoduty);
  REFERENCE_COMMAND(heatlow);
  REFERENCE_COMMAND(heathigh);

  uint32_t powerPin = GJ_CONF_INT32_VALUE(svo_powerpin);
  uint32_t dataPin = GJ_CONF_INT32_VALUE(svo_datapin);

  bool input = false;
  bool pullDown = false;
  SetupPin(powerPin, input, false);
  WritePin(powerPin, 0);

  pwmInitC(PWM_PERIOD, dataPin);
  
  //Setting that high saves 50 uA
  WritePin(dataPin, 1);

  //REMOVE ME
  //SetServoAngle(180);
  //SER("SERVO SERVO!!!");
}


