#define BUSY_PIN 4
#define RES_PIN  2
#define DC_PIN   0
#define CS_PIN   15

struct Weather {
    String time;
    int8_t temp;
    int8_t humidity;
    int16_t wind360;
    String windDir;
    int8_t windScale;
    uint8_t windSpeed;
    uint16_t icon;
    String text;
    String updateTime;
};

int si_calendar_status();
void si_calendar();

int si_wifi_status();
void si_wifi();

int si_weather_status();
void si_weather();

int si_screen_status();
void si_screen();

void print_status();