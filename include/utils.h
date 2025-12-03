#ifndef UTILS_H
#define UTILS_H

#include "essentials/memory.h"

#include "types.h"

#include <stdint.h>
#include <string.h>

#ifdef _WIN32
    #include <io.h>
#elif __linux__
    #include <unistd.h>
    #include <sys/stat.h>
#endif
//----------------------------------  OS  ----------------------------------//
#ifndef OS_PATH_SEPARATOR
    #ifdef _WIN32
        #define OS_PATH_SEPARATOR ';'
    #elif __linux__
        #define OS_PATH_SEPARATOR ':'
    #else
        #error "Must define a system path separator"
    #endif
#endif

#ifndef OS_NAME
    #ifdef _WIN32
        #define OS_NAME "Windows"
    #elif __linux__
        #define OS_NAME "Linux"
    #else
        #error "Must define platform name"
    #endif
#endif
//-----------------------------  FILE SYSTEM  ------------------------------//
#ifdef _WIN32
    int utils_files_exists(const char *pathname);
    int utils_files_can_read(const char *pathname);
    int utils_files_is_directory(const char *pathname);
    int utils_files_is_regular(const char *pathname);
#elif __linux__
    int utils_files_exists(const char *pathname);
    int utils_files_can_read(const char *pathname);
    int utils_files_is_directory(char *pathname);
    int utils_files_is_regular(char *pathname);
#endif

char *utils_files_parent_pathname(const Allocator *allocator, const char *pathname);
char *utils_files_cwd(const Allocator *allocator);
//---------------------------------  TIME  ---------------------------------//
int64_t utils_millis();
void utils_sleep(int64_t time);
//--------------------------------  OTHER  ---------------------------------//
#define INVLEN 1
#define INVPRE 2
#define INVDIG 3

int utils_decimal_str_to_i64(char *str, int64_t *out_value);
int utils_hexadecimal_str_to_i64(char *str, int64_t *out_value);
int utils_str_to_double(char *raw_str, double *out_value);

DStr *utils_read_source(const char *pathname, const Allocator *allocator);
char *utils_read_file_as_text(char *pathname, Allocator *allocator, size_t *out_len);

#endif
