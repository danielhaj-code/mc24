/* Teste pe PC pentru codul pur C (fără HAL):
 *   gcc -Wall -Wextra -I../stm32/Core/Inc test_host.c ../stm32/Core/Src/filter.c ../stm32/Core/Src/protocol.c -o t && ./t
 */
#include <stdio.h>
#include <string.h>
#include "filter.h"
#include "protocol.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void test_filter(void)
{
    int32_t a[] = { 100, 102, 4000, 101, 99, 0, 100 };      /* două spike-uri */
    CHECK(filter_trimmed_mean(a, 7, 1) == 100);

    int32_t b[] = { -10, -20, -30 };
    CHECK(filter_trimmed_mean(b, 3, 0) == -20);

    int32_t c[] = { 5, 1, 9 };
    CHECK(filter_trimmed_mean(c, 3, 5) == 5);                  /* trim prea mare -> mediană */

    CHECK(level_percent(1500, 1000, 2000) == 50);
    CHECK(level_percent(500, 1000, 2000) == 0);
    CHECK(level_percent(9999, 1000, 2000) == 100);
    CHECK(level_percent(1500, 2000, 1000) == 50);              /* senzor inversat */
    CHECK(level_percent(1500, 1000, 1000) == 0);               /* calibrare degenerată */

    stability_t st;
    stability_init(&st, 3, 10);
    CHECK(stability_push(&st, 100) == 0);
    CHECK(stability_push(&st, 105) == 0);
    CHECK(stability_push(&st, 108) == 1);
    CHECK(stability_push(&st, 200) == 0);                      /* salt -> instabil */
    CHECK(stability_push(&st, 201) == 0);
    CHECK(stability_push(&st, 199) == 1);                      /* 200,201,199 */
}

static void test_parse(void)
{
    proto_cmd_t c;
    c = proto_parse("C,120,3050");   CHECK(c.type == CMD_CALIBRATE && c.a == 120 && c.b == 3050);
    c = proto_parse("C,-5000,8000 "); CHECK(c.type == CMD_CALIBRATE && c.a == -5000);
    c = proto_parse("R");            CHECK(c.type == CMD_READ_NOW);
    c = proto_parse("S,1");          CHECK(c.type == CMD_LINK_STATE && c.a == 1);
    c = proto_parse("S,2");          CHECK(c.type == CMD_INVALID);
    c = proto_parse("C,12");         CHECK(c.type == CMD_INVALID);
    c = proto_parse("C,12,x");       CHECK(c.type == CMD_INVALID);
    c = proto_parse("Rx");           CHECK(c.type == CMD_INVALID);
    c = proto_parse("");             CHECK(c.type == CMD_NONE);
    c = proto_parse("Z,1");          CHECK(c.type == CMD_INVALID);

    char out[24];
    size_t n = proto_format_level(out, sizeof out, 2310, 64, 1);
    CHECK(n == 12 && strcmp(out, "L,2310,64,1\n") == 0);
    n = proto_format_level(out, sizeof out, -8388608, 100, 0);
    CHECK(n == 17 && n <= 20);                                  /* cel mai lung caz HX711 încape în 20 B */
}

static int feed(line_asm_t *la, const char *s, char *got)
{
    int lines = 0;
    for (; *s; s++)
        if (line_asm_push(la, *s)) { strcpy(got, la->buf); lines++; }
    return lines;
}

static void test_line_asm(void)
{
    line_asm_t la;
    char got[64] = "";
    line_asm_init(&la);
    CHECK(feed(&la, "C,1,2\r\n", got) == 1 && strcmp(got, "C,1,2") == 0);
    CHECK(feed(&la, "R", got) == 0);                           /* fără terminator încă */
    CHECK(feed(&la, "\n", got) == 1 && strcmp(got, "R") == 0);
    CHECK(feed(&la, "\n\n", got) == 0);                        /* linii goale ignorate */

    char longline[80];
    memset(longline, 'X', 60); longline[60] = '\n'; longline[61] = '\0';
    CHECK(feed(&la, longline, got) == 0);                      /* prea lungă -> aruncată */
    CHECK(feed(&la, "S,0\n", got) == 1 && strcmp(got, "S,0") == 0);  /* recuperare */
}

int main(void)
{
    test_filter();
    test_parse();
    test_line_asm();
    printf(fails ? "%d teste eșuate\n" : "Toate testele au trecut\n", fails);
    return fails != 0;
}
