/*
 * sensor.c — citirea senzorului de nivel (interfața în sensor.h). Una din cele două implementări
 * e compilată, după SENSOR_USE_HX711 din app_config.h / linia de compilare.
 *
 * Folosit doar de app.c (sensor_init în app_init, sensor_read în do_reading, o dată pe secundă).
 * Folosește hadc1 și pinii configurați în main.c, filter_trimmed_mean() din filter.c și HAL.
 * Ambele variante: 15 eșantioane -> medie trunchiată (aruncă 3 jos + 3 sus) -> o valoare brută.
 * Totul rulează în bucla principală, blocant; nicio întrerupere proprie.
 */
#include "sensor.h"
#include "app_config.h"
#include "filter.h"
#include "main.h"   /* HAL + handle-ul hadc1, definit de mână în main.c */

#if !SENSOR_USE_HX711
/* ===================== Senzor analogic rezistiv ===================== */
/* Senzorul formează un divizor de tensiune alimentat din PE4 (3,3 V); ieșirea lui merge pe PA5.
 * Valoarea ADC (0..4095) crește/scade cu nivelul apei; sensul nu contează, calibrarea îl absoarbe. */
extern ADC_HandleTypeDef hadc1;   /* redundant cu main.h, dar inofensiv */

/* Pune alimentarea senzorului pe LOW (MX_GPIO_Init a făcut-o deja; aici e garanția explicită). */
void sensor_init(void)
{
    /* Senzorul stă nealimentat între citiri ca să nu corodeze electrozii. */
    HAL_GPIO_WritePin(SENS_PWR_PORT, SENS_PWR_PIN, GPIO_PIN_RESET);
}

/* O conversie ADC pe canalul 5. Pune rezultatul (0..4095) în *v și întoarce 1; 0 dacă ADC-ul nu
 * pornește sau conversia nu se termină în 10 ms (normal durează ~4,3 µs, deci timeout = defect).
 * Oprește ADC-ul după fiecare eșantion: următorul HAL_ADC_Start pornește o conversie curată. */
static int adc_sample(int32_t *v)
{
    if (HAL_ADC_Start(&hadc1) != HAL_OK) return 0;
    int ok = HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK;
    if (ok) *v = (int32_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return ok;
}

/* Citire completă analogică: alimentează senzorul, așteaptă SENSOR_SETTLE_MS (5 ms, cât să se
 * stabilizeze divizorul și capacitățile cablului), ia 15 eșantioane la rând (~65 µs), apoi
 * taie alimentarea. Senzorul stă alimentat doar ~5 ms pe secundă (<1%): curent continuu prin apă
 * ar provoca electroliză și corodarea electrozilor.
 * Durată totală ≈ 5 ms (HAL_Delay garantează minim SENSOR_SETTLE_MS, poate +1 ms). */
int sensor_read(int32_t *out)
{
    int32_t s[SAMPLES_PER_READING];
    int n = 0;

    HAL_GPIO_WritePin(SENS_PWR_PORT, SENS_PWR_PIN, GPIO_PIN_SET);
    HAL_Delay(SENSOR_SETTLE_MS);
    for (int i = 0; i < SAMPLES_PER_READING; i++)
        if (adc_sample(&s[n])) n++;
    HAL_GPIO_WritePin(SENS_PWR_PORT, SENS_PWR_PIN, GPIO_PIN_RESET);

    /* sub 7 eșantioane bune din 15 => ADC defect; nu raportăm o valoare nesigură */
    if (n < SAMPLES_PER_READING / 2) return 0;
    /* toate 15 bune: trim fix 3; altfel trim proporțional (~20% la fiecare capăt) */
    *out = filter_trimmed_mean(s, n, n == SAMPLES_PER_READING ? TRIM_COUNT : n / 5);
    return 1;
}

#else
/* ===================== Celulă de sarcină + HX711 ===================== */
/* HX711 = ADC sigma-delta de 24 biți cu amplificator pentru punți tensometrice. Interfața e serială
 * proprie (nu SPI): DT (date) iese LOW când un eșantion e gata; MCU dă impulsuri pe SCK și citește
 * câte un bit pe DT după fiecare front crescător, MSB primul. Numărul de impulsuri (25/26/27)
 * alege canalul și câștigul pentru URMĂTOAREA conversie. Bit-bang pe GPIO (PE2 = DT, PE3 = SCK). */

/* Pornește contorul de cicluri DWT->CYCCNT (32 biți, la frecvența nucleului: 180 MHz), folosit
 * pentru întârzieri de ordinul µs. TRCENA activează blocul de trasare/depanare care conține DWT;
 * fără el CYCCNT nu numără (când nu e conectat debugger-ul). */
static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* Așteptare activă de `us` microsecunde cu CYCCNT (1 µs = 180 cicluri). Scăderea pe unsigned
 * rămâne corectă și la depășirea contorului (la fiecare ~23,8 s). Valabil pentru us < ~23 s.
 * HAL_Delay nu ajută aici: rezoluția lui e 1 ms. */
static void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks) { }
}

/* Pornește DWT și pune SCK pe LOW (SCK ținut HIGH > 60 µs ar opri HX711). */
void sensor_init(void)
{
    dwt_init();
    HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_RESET); /* SCK LOW = activ */
}

/* Un eșantion de 24 biți, canal A, câștig 128 (25 de impulsuri SCK).
 * Așteaptă ca DT să coboare (eșantion gata). Fără modul, DT rămâne HIGH prin pull-up-ul din
 * MX_GPIO_Init și funcția iese cu 0 după HX711_TIMEOUT_MS (200 ms) — nu se blochează.
 * Întoarce 1 și pune în *v valoarea cu semn (-8388608..8388607).
 * Efect: întreruperile sunt oprite ~50 µs; cel mult un tick SysTick e amânat, nu pierdut. */
static int hx711_sample(int32_t *v)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_GPIO_ReadPin(HX711_DT_PORT, HX711_DT_PIN) == GPIO_PIN_SET)
        if (HAL_GetTick() - t0 > HX711_TIMEOUT_MS) return 0;   /* lipsă modul / cablaj */

    uint32_t data = 0;
    /* SCK HIGH > 60 µs pune HX711 în power-down, deci fără întreruperi aici (~50 µs).
     * Fiecare bit: SCK HIGH, 1 µs (HX711 cere ≥ 0,1 µs până DT e valid), citește DT, SCK LOW, 1 µs.
     * Un ISR (UART, SysTick) între front și citire ar putea prelungi HIGH-ul peste 60 µs. */
    __disable_irq();
    for (int i = 0; i < 24; i++) {
        HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_SET);
        delay_us(1);
        data = (data << 1) | (HAL_GPIO_ReadPin(HX711_DT_PORT, HX711_DT_PIN) == GPIO_PIN_SET);   /* MSB primul */
        HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_RESET);
        delay_us(1);
    }
    /* al 25-lea impuls: nu mai aduce date, doar alege canal A / câștig 128 pentru conversia
     * următoare (26 = canal B/32, 27 = canal A/64). Readuce și DT pe HIGH până la eșantionul nou. */
    HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_SET);   /* al 25-lea: A/128 */
    delay_us(1);
    HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_RESET);
    __enable_irq();

    if (data & 0x800000U) data |= 0xFF000000U;   /* extensie de semn complement față de 2 (bit 23 -> 31..24) */
    *v = (int32_t)data;
    return 1;
}

/* Citire completă HX711: 15 eșantioane consecutive (fiecare așteaptă conversia următoare, deci
 * ~1,5 s la 10 SPS), apoi medie trunchiată ca la varianta analogică. Eșantioanele ratate pe timeout
 * sunt doar omise; sub 7 bune -> 0 (eroare). Fără modul conectat blochează 15 × 200 ms = 3 s. */
int sensor_read(int32_t *out)
{
    int32_t s[SAMPLES_PER_READING];
    int n = 0;
    for (int i = 0; i < SAMPLES_PER_READING; i++)
        if (hx711_sample(&s[n])) n++;
    if (n < SAMPLES_PER_READING / 2) return 0;
    *out = filter_trimmed_mean(s, n, n == SAMPLES_PER_READING ? TRIM_COUNT : n / 5);
    return 1;
}
#endif
