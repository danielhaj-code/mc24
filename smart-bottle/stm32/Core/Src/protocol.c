#include "protocol.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

/* Citește un întreg zecimal; avansează *p. Întoarce 1 la succes. */
static int parse_int(const char **p, int32_t *out)
{
    char *end;
    errno = 0;
    long v = strtol(*p, &end, 10);
    if (end == *p || errno == ERANGE || v < INT32_MIN || v > INT32_MAX) return 0;
    *out = (int32_t)v;
    *p = end;
    return 1;
}

static int at_end(const char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    return *p == '\0';
}

/* Citește exact n întregi de forma ",x,y,z" până la sfârșitul liniei. */
static int parse_fields(const char *p, int32_t *v, int n)
{
    for (int i = 0; i < n; i++) {
        if (*p++ != ',') return 0;
        if (!parse_int(&p, &v[i])) return 0;
    }
    return at_end(p);
}

proto_cmd_t proto_parse(const char *line)
{
    proto_cmd_t cmd = { CMD_NONE, 0, 0, 0 };
    int32_t v[3] = { 0, 0, 0 };
    if (!line) return cmd;
    while (*line && isspace((unsigned char)*line)) line++;
    if (*line == '\0') return cmd;

    const char *p = line + 1;
    cmd.type = CMD_INVALID;

    switch (line[0]) {
    case 'R':
        if (at_end(p)) cmd.type = CMD_READ_NOW;
        break;
    case 'C':
        if (*p++ != ',') break;
        if (!parse_int(&p, &cmd.a)) break;
        if (*p++ != ',') break;
        if (!parse_int(&p, &cmd.b)) break;
        if (at_end(p)) cmd.type = CMD_CALIBRATE;
        break;
    case 'S':
        if (*p++ != ',') break;
        if (!parse_int(&p, &cmd.a)) break;
        if (at_end(p) && (cmd.a == 0 || cmd.a == 1)) cmd.type = CMD_LINK_STATE;
        break;
    case 'M':
        if (parse_fields(p, v, 2) && v[0] >= 0 && v[1] > 0) cmd.type = CMD_VOLUME;
        break;
    case 'T':
        if (parse_fields(p, v, 3) && v[0] >= 0 && v[1] > 0 && v[2] >= 0) cmd.type = CMD_TODAY;
        break;
    case 'U':
        if (parse_fields(p, v, 1) && v[0] >= -1) cmd.type = CMD_SINCE;
        break;
    case 'W':
        if (parse_fields(p, v, 2) && v[0] >= 0 && v[0] <= 6 && v[1] >= 0) cmd.type = CMD_DAY;
        break;
    case 'H':
        if (parse_fields(p, v, 2) && v[0] >= 0 && v[1] >= 0 && v[1] <= 2) cmd.type = CMD_HEARTBEAT;
        break;
    default:
        break;
    }
    if (cmd.type == CMD_HEARTBEAT || cmd.type == CMD_VOLUME || cmd.type == CMD_TODAY || cmd.type == CMD_SINCE || cmd.type == CMD_DAY) {
        cmd.a = v[0]; cmd.b = v[1]; cmd.c = v[2];
    }
    return cmd;
}

size_t proto_format_level(char *out, size_t cap, int32_t raw, int pct, int stable)
{
    int n = snprintf(out, cap, "L,%ld,%d,%d\n", (long)raw, pct, stable ? 1 : 0);
    if (n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

size_t proto_format_cal(char *out, size_t cap, int32_t empty, int32_t full)
{
    int n = snprintf(out, cap, "K,%ld,%ld\n", (long)empty, (long)full);
    if (n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

void line_asm_init(line_asm_t *la)
{
    la->len = 0;
    la->overflow = 0;
    la->buf[0] = '\0';
}

int line_asm_push(line_asm_t *la, char c)
{
    if (c == '\n') {
        int complete = !la->overflow && la->len > 0;
        la->buf[la->len] = '\0';
        if (la->len > 0 && la->buf[la->len - 1] == '\r') la->buf[--la->len] = '\0';
        if (!complete) la->len = 0;   /* linie prea lungă sau goală: aruncată */
        la->overflow = 0;
        if (complete) {
            la->len = 0;               /* bufferul rămâne valid până la următorul push */
            return 1;
        }
        return 0;
    }
    if (la->overflow) return 0;
    if (la->len >= PROTO_LINE_MAX - 1) {
        la->overflow = 1;
        la->len = 0;
        return 0;
    }
    la->buf[la->len++] = c;
    return 0;
}
