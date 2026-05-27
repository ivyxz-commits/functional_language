#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

typedef struct{
    int64_t length; //длина строки - 8 байт
    char data[];
}LangStr;

//parse_int(str*) -> int64
int64_t lang_parse_int(LangStr* str){

    char buf[32]; //max int - 19
    int64_t len = str->length < 31 ? str->length : 31;
    memcpy(buf, str->data, len);
    buf[len] = '\0'; //чтобы функция понимала что это строка

    return (int64_t)strtol(buf, NULL, 10);
}

int64_t lang_parse_float(LangStr* str){
    char buf[64];

    int64_t len = str->length < 63 ? str->length : 63;
    memcpy(buf, str->data, len);
    buf[len] = '\0';

    double result = strtod(buf, NULL);
    int64_t bits;
    memcpy(&bits, &result, 8); //bit_cast

    return bits; //битовое представление float64
}

//print_float - принимает float64 как int64_t bits И печатает его как double
void lang_print_float(int64_t bits){
    double value;
    memcpy(&value, &bits, 8);

    printf("%g\n", value);
}