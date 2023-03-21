#include "app_error.h"
#include "softdevice_handler.h"
#include "nrf_log_ctrl.h"
#include "nrf_gpio.h"
#include "nrf_drv_clock.h"

#include "gj/base.h"
#include "gj/gjbleserver.h"
#include "gj/eventmanager.h"
#include "gj/commands.h"
#include "gj/config.h"
#include "gj/gjota.h"
#include "gj/esputils.h"
#include "gj/nrf51utils.h"
#include "gj/datetime.h"
#include "gj/file.h"
#include "gj/appendonlyfile.h"
#include "gj/platform.h"
#include "gj/sensor.h"

#include "readwriteints.h"

DEFINE_CONFIG_INT32(modulesuffix, modulesuffix, 0);

void InitAdc();

static void power_manage(void)
{
    if (softdevice_handler_is_enabled())
    {
      uint32_t err_code = sd_app_evt_wait();

      APP_ERROR_CHECK(err_code);
    }
    else
    {
      __WFE();
    }
}

GJBLEServer bleServer;
GJOTA ota;
BuiltInTemperatureSensor tempSensor;

const char *GetHostName()
{
  static char hostName[6] = {'h', 'e', 'a', 't', 0, 0};

  char suffix = (char)GJ_CONF_INT32_VALUE(modulesuffix);
  if (suffix == 0)
    suffix = 'X';

  hostName[4] = suffix;

  return hostName;
}

void PrintVersion()
{
  const char *s_buildDate =  __DATE__ "," __TIME__;
  extern int __vectors_load_start__;
  const char *hostName = GetHostName();
  const char *chipName = nullptr;
  const uint32_t partition = GetPartitionIndex();
  #if defined(NRF51)
    chipName = "NRF51";
  #elif defined(NRF52)
    chipName = "NRF52";
  #else
    chipName = "unknown Chip";
  #endif
  SER("%s Shop Heat Partition %d (0x%x) Build:%s\r\n", chipName, partition, &__vectors_load_start__, s_buildDate);
  SER("DeviceID:0x%x%x\n\r", NRF_FICR->DEVICEID[0], NRF_FICR->DEVICEID[1]);
  SER("Hostname:%s\n\r", hostName);
}

void Command_Version()
{
  PrintVersion();
}

void Command_TempDie()
{
  uint32_t temp = tempSensor.GetValue();
  SER("Die Temp:%d\r\n", temp);
}

DEFINE_COMMAND_NO_ARGS(version, Command_Version);
DEFINE_COMMAND_NO_ARGS(tempdie, Command_TempDie);

//map file names to static flash locations
#if (NRF_FLASH_SECTOR_SIZE == 1024)
  #define FILE_SECTOR_BOOT      0x1bc00
  #define FILE_SECTOR_TEMPREADS 0x3d000
  #define FILE_SECTOR_LASTDATE  0x3f800
  #define FILE_SECTOR_CONFIG    0x3fc00
#elif (NRF_FLASH_SECTOR_SIZE == 4096)
  #define FILE_SECTOR_BOOT      0x7b000
  #define FILE_SECTOR_TEMPREADS 0x7c000
  #define FILE_SECTOR_LASTDATE  0x7e000
  #define FILE_SECTOR_CONFIG    0x7f000
#endif

DEFINE_FILE_SECTORS(boot,       "/boot",      FILE_SECTOR_BOOT,       1);
DEFINE_FILE_SECTORS(tempreads,  "/tempreads", FILE_SECTOR_TEMPREADS,  8);
DEFINE_FILE_SECTORS(lastdate,   "/lastdate",  FILE_SECTOR_LASTDATE,   1);
DEFINE_FILE_SECTORS(config,     "/config",    FILE_SECTOR_CONFIG,     1);

#if defined(NRF51)
  BEGIN_BOOT_PARTITIONS()
  DEFINE_BOOT_PARTITION(0, 0x1c000, 0x10000)
  DEFINE_BOOT_PARTITION(1, 0x2d000, 0x10000)
  END_BOOT_PARTITIONS()
#elif defined(NRF52)
  BEGIN_BOOT_PARTITIONS()
  DEFINE_BOOT_PARTITION(0, 0x20000, 0x20000)
  DEFINE_BOOT_PARTITION(1, 0x40000, 0x20000)
  END_BOOT_PARTITIONS()
#endif

void servoInit();

int main(void)
{
  for (uint32_t i = 0 ; i < 32 ; ++i)
    nrf_gpio_cfg_default(i);

  REFERENCE_COMMAND(tempdie);
  REFERENCE_COMMAND(version);
  
  Delay(100);

  InitMultiboot();

  InitializeDateTime();
  InitCommands(0);
  InitSerial();
  InitESPUtils();
  InitFileSystem("");

  InitConfig();
  PrintConfig();

  PrintVersion();

  GJOTA *otaInit = nullptr;
  ota.Init();
  otaInit = &ota;

  uint32_t maxEvents = 4;
  GJEventManager = new EventManager(maxEvents);

  servoInit();
  InitAdc();

  const char *hostName = GetHostName();
  //note:must initialize after InitFStorage()
  bleServer.Init(hostName, otaInit);

  //InitTemps();
  InitReadWriteInts();

  

  for (;;)
  {
      bleServer.Update();
      GJEventManager->WaitForEvents(0);

      bool bleIdle = bleServer.IsIdle();
      bool evIdle = GJEventManager->IsIdle();
      bool const isIdle = bleIdle && evIdle;
      if (isIdle)
      {
          power_manage();
      }
  }
}


