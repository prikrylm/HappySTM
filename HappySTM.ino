//#include <FlashStorage_STM32F1.h>
//#include <FlashStorage_STM32F1.hpp>

/********************************************************** 
 *  USB HID zařízení pro obsluhu Happy pedálů
 *  3x 12-bit osa (z, rX, rY) + 2 tlačítka
 *  
 *  Pokud se zařízení nastrtuje s oběma tlačítky stisknutými, přepne se zařízení do režimu kalibrace.
 *  Pokud nebudou stisknuta obě tlačítka, zařízení se normálně rozběhne.
 */
#include <EEPROM.h>
//#include <flash_stm32.h>
#include <USBComposite.h> // USB Lib from https://github.com/arpruss/USBComposite_stm32f1 - modifikovaný "SignedJoysick z příkladů"


// struktura reportu, který se posílá do PC, jako data HID zařízení
typedef struct
{
  //uint8_t reportID;
  uint8_t button1 : 1;
  uint8_t button2 : 1;
  uint8_t zAxisL : 6;    // kvůli velikosti bytu je Z rozdělena na dvě části
  uint32_t zAxisH : 6;   // zbytek se vleze do uint32
  uint32_t rxAxis : 12;
  uint32_t ryAxis : 12;
  uint32_t unused : 2;  // zbytek nepoužitých bitů, v budoucnu je možné použít na dvě tlačítka, případně jinak.
} __packed SignedJoystickReport_t;


// struktury pro uchování kalibračních dat
typedef struct
{
  uint16_t axisMin;
  uint16_t axisMax;
} axisRange_t;

#define RANGES_DATASIZE 96

typedef union 
{
  byte data[RANGES_DATASIZE];
  struct 
  {
      axisRange_t z;
      axisRange_t rx;
      axisRange_t ry;
  };
}ranges_t;

// třída joysticku (HID zařízení)
class HIDSignedJoystick : public HIDReporter
{
public:
  SignedJoystickReport_t report;
  void begin(void){};
  void end(void){};
  HIDSignedJoystick(USBHID &HID, uint8_t reportID = HID_JOYSTICK_REPORT_ID)
      : HIDReporter(HID, NULL, (uint8_t *)&report, sizeof(report), reportID)
  {
    report.button1 = 0;
    report.button2 = 0;
  }

  void SetZAxis(int val)
  {
    report.zAxisL = val & 0b111111;
    report.zAxisH = (val >> 6) & 0b111111;
  }

  void SetRxAxis(int val)
  {
    report.rxAxis = val & 0xFFF;
  }

  void SetRyAxis(int val)
  {
    report.ryAxis = val & 0xFFF;
  }

  void SetButton1(int val)
  {
    report.button1 = !val & 1;
  }

  void SetButton2(int val)
  {
    report.button2 = !val & 1;
  }

  void SetButton(int num, int val)
  {
    if (num == 1)
    {
      SetButton1(val);
    }
    else
    {
      SetButton2(val);
    }
  }

  void SetButtons(int val)
  {
    report.button1 = val & 0x1;
    val = val >> 1;
    report.button2 = val & 0x1;
  }
};

USBHID HID;
HIDSignedJoystick joy(HID);

uint8 signedJoyReportDescriptor[] = {
    0x05, 0x01,       // USAGE_PAGE (Generic Desktop)
    0x09, 0x04,       // USAGE (Joystick)
    0xa1, 0x01,       // COLLECTION (Application)
    0xa1, 0x00,       //   COLLECTION (Physical)
    0x05, 0x09,       //     USAGE_PAGE (Button)
    0x19, 0x01,       //     USAGE_MINIMUM (Button 1)
    0x29, 0x02,       //     USAGE_MAXIMUM (Button 2)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x25, 0x01,       //     LOGICAL_MAXIMUM (1)
    0x95, 0x02,       //     REPORT_COUNT (2)
    0x75, 0x01,       //     REPORT_SIZE (1)
    0x81, 0x02,       //     INPUT (Data,Var,Abs)
    0x05, 0x01,       //     USAGE_PAGE (Generic Desktop)
    0x09, 0x32,       //     USAGE (Z)
    0x09, 0x33,       //     USAGE (Rx)
    0x09, 0x34,       //     USAGE (Ry)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x0f, //     LOGICAL_MAXIMUM (4095)
    0x95, 0x03,       //     REPORT_COUNT (3)
    0x75, 0x0c,       //     REPORT_SIZE (12)
    0x81, 0x02,       //     INPUT (Data,Var,Abs)
    0x09, 0x00,       //     USAGE (Undefined)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x25, 0x03,       //     LOGICAL_MAXIMUM (3)
    0x75, 0x02,       //     REPORT_SIZE (2)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x81, 0x02,       //   INPUT (Data,Var,Abs)
    0xc0,             //     END_COLLECTION
    0xc0              // END_COLLECTION
};

// nastavení toho, která nožička bude která osa/tlačítko
const int zAxisPin = PA0;
const int rxAxisPin = PA1;
const int ryAxisPin = PA2;
const int button1Pin = PA3;
const int button2Pin = PA4;


// popisky a id HID zařízení
const char ManufacturerName[] = "MzM";
const char DeviceName[] = "Happy Pedals C8";
const char DeviceSerial[] = "1";
uint16_t VendorId = 0x5824;
uint16_t ProductId = 0x0048; // C6 bude 47, C8 bude 48

#define EEPROM_ADDR 0x0000 // offset v ramci flash page emulovane EEPORM
//#define EEPROM_PAGE_ADDR 0x8007800 // posledni xkB z 32 kB Flash - 32k kvuli STM32F103C6, co ma akorat 32 kB Flash.
#define EEPROM_PAGE_ADDR 0x801F800 // C8
#define EEPROM_PAGE_SIZE 0x400

ranges_t axisRanges; // proměnná s kalibračními rozsahy


void setup()
{
  // HID init
  HID.setReportDescriptor(signedJoyReportDescriptor, sizeof(signedJoyReportDescriptor));
  HID.registerComponent();
  USBComposite.setManufacturerString(ManufacturerName);
  USBComposite.setProductString(DeviceName);
  USBComposite.setSerialString(DeviceSerial);
  USBComposite.setVendorId(VendorId);
  USBComposite.setProductId(ProductId);
  USBComposite.begin();
  while (!USBComposite)
    ;

  // nastavení "nožiček" na CPU
  pinMode(zAxisPin, INPUT_ANALOG);
  pinMode(rxAxisPin, INPUT_ANALOG);
  pinMode(ryAxisPin, INPUT_ANALOG);
  pinMode(button1Pin, INPUT_PULLUP);
  pinMode(button2Pin, INPUT_PULLUP);

  
  EEPROM.PageBase0 = EEPROM_PAGE_ADDR;
  EEPROM.PageBase1 = EEPROM_PAGE_ADDR + EEPROM_PAGE_SIZE;      
  EEPROM.PageSize  = EEPROM_PAGE_SIZE;
  
  // proces kalibrace
  // pokud budou pri startu zarizeni stisknute oba knofliky, prejde se ke kalibraci os
  // postupne se zjisti min max u os, indikace pres zobrazeni zarizeni (napr win joystick conf.)
  //  "blikajici" tlacitka indikuji, že je zařízení v režimu kalibrace
  //  kalibrují se všechny osy zároveň
  // po stisku tl 2 se konfigurace zapise a prejde se do std rezimu prace (loop())
  // hodnoty se uchovaji v "EEPROM"

  // dosažené mezní hodnoty os
  int xMi = 4095;
  int yMi = 4095;
  int zMi = 4095;
  int xMa = 0;
  int yMa = 0;
  int zMa = 0;

  if (digitalRead(button2Pin) == 0 && digitalRead(button1Pin) == 0) // obe tlacitka jsou stisknuta
  {
    joy.SetButton1(1); // vypnuto - pracuje se s inverzní logikou, vstupy na tlačítka jsou "pull up" a přizemňují se.
    joy.SetButton2(1);
    joy.sendReport();

    // setting
    // pockam az obe tlacitka uvolni
    while (digitalRead(button2Pin) == 0 || digitalRead(button1Pin) == 0)
    {
      delay(10);
    }

    int cnt = 0;
    int btnState = 0;
    int val = 0; // axis val

    while (digitalRead(button2Pin) == 1) // dokud nechci konec
    {
        joy.SetButtons(btnState); // v režimu nastavení bliká 
  
        val = analogRead(zAxisPin);
        setMinMax(val, &zMi, &zMa);
        joy.SetZAxis(val);
  
        val = analogRead(rxAxisPin);
        setMinMax(val, &xMi, &xMa);
        joy.SetRxAxis(val);
  
        val = analogRead(ryAxisPin);
        setMinMax(val, &yMi, &yMa);
        joy.SetRyAxis(val);
  
        cnt++;
        if (cnt > 5) {
            cnt = 0;
            if (btnState == 0) {
                btnState = 3; //rozsvitit obe tlacitka
            } else {
                btnState = 0;
            }
        }
  
        joy.sendReport();
        delay(100);
    }

    // pokud jsou hodnoty max vetsi jak min, zapsat do eeprom
    axisRanges.z.axisMin = zMi;
    axisRanges.z.axisMax = zMa;
    axisRanges.rx.axisMin = xMi;
    axisRanges.rx.axisMax = xMa;
    axisRanges.ry.axisMin = yMi;
    axisRanges.ry.axisMax = yMa;

    eeprom_write_bytes(EEPROM_ADDR, axisRanges.data);
  }
  else {
    eeprom_read_bytes(EEPROM_ADDR, axisRanges.data);
  }
}

void loop()
{
  joy.SetZAxis(limitMap(analogRead(zAxisPin), axisRanges.z.axisMin, axisRanges.z.axisMax, 0, 4095));
  joy.SetRxAxis(limitMap(analogRead(rxAxisPin), axisRanges.rx.axisMin, axisRanges.rx.axisMax, 0, 4095));
  joy.SetRyAxis(limitMap(analogRead(ryAxisPin), axisRanges.ry.axisMin, axisRanges.ry.axisMax, 0, 4095));
  joy.SetButton1(digitalRead(button1Pin));
  joy.SetButton2(digitalRead(button2Pin));
  joy.sendReport();
}

// primitivni metoda zapisu do EEPROM ??? vylepsit?
void eeprom_write_bytes(int startAddr, const byte b[RANGES_DATASIZE])
{
    int i;

    for (i = 0; i < RANGES_DATASIZE; i++)
    {
        EEPROM.write(startAddr + i, b[i]);
    }
}

// primitivni metoda cteni z EEPROM
void eeprom_read_bytes(int addr, byte b[RANGES_DATASIZE])
{
    byte ch;       
    int bytesRead; 

    bytesRead = 0;                      

    while ((bytesRead < RANGES_DATASIZE))
    {
        ch = EEPROM.read(addr + bytesRead);
        b[bytesRead] = ch; // simpan ke dalam array pengguna
        bytesRead++;
    }
}
int limitMap(int val, int valFrom, int valTo, int rangeFrom, int rangeTo) {
    return min(max(map(val, valFrom, valTo, rangeFrom, rangeTo), rangeFrom), rangeTo);
}

void setMinMax(int val, int* minVal, int* maxVal) {
    if (*maxVal < val) *maxVal = val;
    if (*minVal > val) *minVal = val;
}
