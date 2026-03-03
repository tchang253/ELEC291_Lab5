#include <EFM8LB1.h>
#include <stdio.h>            
#include <math.h>        
#include "cap_serial.h"
#include "global.h"

//for capacitance meter
double cap_from_period(double period_s)
{
    double cap_f;
    if(period_s <= 0.0)
        return -1.0;

    cap_f = period_s / (LN2 * (RA + 2.0 * RB));
    return cap_f;
}
static void cap_unit_select_internal(double cap_f, double* out_value, cap_unit* out_unit)
{
    double cap_abs;

    cap_abs = cap_f < 0.0 ? -cap_f : cap_f;

    if (cap_abs < 1e-9)
    {
        *out_unit = CAP_PF;
        *out_value = cap_f * 1e12;
    }
    else if (cap_abs < 9e-8)
    {
        *out_unit = CAP_NF;
        *out_value = cap_f * 1e9;
    }
    else if (cap_abs < 1e-3)
    {
        *out_unit = CAP_UF;
        *out_value = cap_f * 1e6;
    }
    else if (cap_abs < 1.0)
    {
        *out_unit = CAP_MF;
        *out_value = cap_f * 1e3;
    }
    else
    {
        *out_unit = CAP_F;
        *out_value = cap_f;
    }
}
void cap_unit_select_split(double cap_f, double* out_value, cap_unit* out_unit)
{
    cap_unit_select_internal(cap_f, out_value, out_unit);
}


static const char* cap_unit_str(cap_unit unit)
{
    switch(unit)
    {
        case CAP_PF: return "pF";
        case CAP_NF: return "nF";
        case CAP_UF: return "uF";
        case CAP_MF: return "mF";
        case CAP_F:  return "F";
        default:     return "F";
    }
}
void print_cap_val_format(double cap_f)
{
    double disp_value;
    cap_unit disp_unit;
    const char* unit_str;

    if (cap_f < 0.0)
    {
        printf("CAP,C=ERR\r\n");
        return;
    }

    cap_unit_select_internal(cap_f, &disp_value, &disp_unit);
    unit_str = cap_unit_str(disp_unit);
    printf("CAP,C=%.7f,%s\r\n", disp_value, unit_str);
}


//for resistance (ohmmeter)
double resistance_from_period(double period_s, double cap_known_f, double rb_known_ohm)
{   
    double rx;
    if(period_s <= 0.0)
        return -1.0;

    rx = (period_s / (LN2 * cap_known_f)) - 2.0 * rb_known_ohm;

    if(rx < 0.0)
        return -1.0;
        
    return rx;
}

static void res_unit_select_internal(double res_ohm, double* out_value, res_unit* out_unit)
{
    double res_abs;

    res_abs = res_ohm < 0.0 ? -res_ohm : res_ohm;

    if(res_abs < 1e3)
    {
        *out_unit = RES_OHM;
        *out_value = res_ohm;
    }
    else if(res_abs < 1e6)
    {
        *out_unit = RES_KOHM;
        *out_value = res_ohm / 1e3;
    }
    else
    {
        *out_unit = RES_MOHM;
        *out_value = res_ohm / 1e6;
    }
}

void res_unit_select_split(double res_ohm, double* out_value, res_unit* out_unit)
{
    res_unit_select_internal(res_ohm, out_value, out_unit);
}

static const char* res_unit_str(res_unit unit)
{
    switch(unit)
    {
        case RES_OHM: return "Ohm";
        case RES_KOHM: return "kOhm";
        case RES_MOHM: return "MOhm";
        default: return "Ohm";
    }
}

void print_res_val_format(double res_ohm)
{
    double disp_value;
    res_unit disp_unit;
    const char* unit_str;

    if(res_ohm < 0.0)
    {
        printf("RES,R=ERR\r\n");
        return;
    }

    res_unit_select_internal(res_ohm, &disp_value, &disp_unit);
    unit_str = res_unit_str(disp_unit);
    printf("RES,R=%.4f,%s\r\n", disp_value, unit_str);
}