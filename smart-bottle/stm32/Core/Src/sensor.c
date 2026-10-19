#include "sensor.h"
#include "app_config.h"
#include "filter.h"
#include "main.h"   /* HAL + handle-uri generate de CubeMX */

#if !SENSOR_USE_HX711
/* ===================== Senzor analogic rezistiv ===================== */
extern ADC_HandleTypeDef hadc1;

void sensor_init(void)
{
    /* Senzorul stă nealimentat între citiri ca să nu corodeze electrozii. */
    HAL_GPIO_WritePin(SENS_PWR_PORT, SENS_PWR_PIN, GPIO_PIN_RESET);
}

static int adc_sample(int32_t *v)
{
    if (HAL_ADC_Start(&hadc1) != HAL_OK) return 0;
    int ok = HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK;
    if (ok) *v = (int32_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return ok;
}

int sensor_read(int32_t *out)
{
    int32_t s[SAMPLES_PER_READING];
    int n = 0;

    HAL_GPIO_WritePin(SENS_PWR_PORT, SENS_PWR_PIN, GPIO_PIN_SET);
    HAL_Delay(SENSOR_SETTLE_MS);
    for (int i = 0; i < SAMPLES_PER_READING; i++)
        if (adc_sample(&s[n])) n++;
    HAL_GPIO_WritePin(SENS_PWR_PORT, SENS_PWR_PIN, GPIO_PIN_RESET);

    if (n < SAMPLES_PER_READING / 2) return 0;
    *out = filter_trimmed_mean(s, n, n == SAMPLES_PER_READING ? TRIM_COUNT : n / 5);
    return 1;
}

#else
/* ===================== Celulă de sarcină + HX711 ===================== */

static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks) { }
}

void sensor_init(void)
{
    dwt_init();
    HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_RESET); /* SCK LOW = activ */
}

/* Un eșantion de 24 biți, canal A, câștig 128 (25 de impulsuri SCK). */
static int hx711_sample(int32_t *v)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_GPIO_ReadPin(HX711_DT_PORT, HX711_DT_PIN) == GPIO_PIN_SET)
        if (HAL_GetTick() - t0 > HX711_TIMEOUT_MS) return 0;   /* lipsă modul / cablaj */

    uint32_t data = 0;
    /* SCK HIGH > 60 µs pune HX711 în power-down, deci fără întreruperi aici (~50 µs). */
    __disable_irq();
    for (int i = 0; i < 24; i++) {
        HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_SET);
        delay_us(1);
        data = (data << 1) | (HAL_GPIO_ReadPin(HX711_DT_PORT, HX711_DT_PIN) == GPIO_PIN_SET);
        HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_RESET);
        delay_us(1);
    }
    HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_SET);   /* al 25-lea: A/128 */
    delay_us(1);
    HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_RESET);
    __enable_irq();

    if (data & 0x800000U) data |= 0xFF000000U;   /* extensie de semn complement față de 2 */
    *v = (int32_t)data;
    return 1;
}

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
