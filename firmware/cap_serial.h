#ifndef CAP_SERIAL_H
#define CAP_SERIAL_H

#include <stdint.h>

typedef enum {
    CAP_PF,
    CAP_NF,
    CAP_UF,
    CAP_MF,
    CAP_F
} cap_unit;

//for calculating cap_val from period
double cap_from_period(double period_s);

//for choosing the unit via output pointers (SDCC compatible)
void cap_unit_select_split(double cap_f, double *out_value, cap_unit *out_unit);

//for printing lines: CAP,C=10.23,uF
void print_cap_val_format(double cap_f);

//for resistance meter (ohmmeter)
typedef enum {
    RES_OHM,
    RES_KOHM,
    RES_MOHM
} res_unit;

//for calculating resistance from period
double resistance_from_period(double period_s, double cap_known_f, double rb_known_ohm);

//for choosing the unit via output pointers (SDCC compatible)
void res_unit_select_split(double res_ohm, double *out_value, res_unit *out_unit);

//for printing lines: RES,R=1023.25,kOHM
void print_res_val_format(double res_ohm);

#endif