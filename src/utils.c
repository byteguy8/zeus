#include "utils.h"

#include "essentials/memory.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#if _WIN32
    #include <shlwapi.h>
    #include <windows.h>
#elif __linux__
    #define _POSIX_C_SOURCE 200809L

    #include <errno.h>
    #include <time.h>
    #include <libgen.h>
    #include <sys/utsname.h>
#endif

//-----------------------------  FILE SYSTEM  ------------------------------//
#ifdef _WIN32
    #define ACCESS_MODE_EXISTS 0
    #define ACCESS_MODE_WRITE_ONLY 2
    #define ACCESS_MODE_READ_ONLY 4
    #define ACCESS_MODE_READ_AND_WRITE 6

    inline int utils_files_exists(const char *pathname){
        return _access(pathname, ACCESS_MODE_EXISTS) == 0;
    }

    inline int utils_files_can_read(const char *pathname){
        return access(pathname, ACCESS_MODE_READ_ONLY) == 0;
    }

    int utils_files_is_directory(const char *pathname){
        DWORD attributes = GetFileAttributesA(pathname);
        return attributes & FILE_ATTRIBUTE_DIRECTORY;
    }

    int utils_files_is_regular(const char *pathname){
	    DWORD attributes = GetFileAttributesA(pathname);
        return attributes & FILE_ATTRIBUTE_ARCHIVE;
    }

    inline char *utils_files_parent_pathname(const Allocator *allocator, const char *pathname){
        char *cloned_pathname = memory_clone_cstr(allocator, pathname, NULL);
        PathRemoveFileSpecA(cloned_pathname);
        return cloned_pathname;
    }

    char *utils_files_cwd(const Allocator *allocator){
        DWORD buff_len = 0;
        LPTSTR buff = NULL;

        buff_len = GetCurrentDirectory(buff_len, buff);
        buff = MEMORY_ALLOC(allocator, CHAR, buff_len);

        if(!buff){
            return NULL;
        }

        if(GetCurrentDirectory(buff_len, buff) == 0){
            MEMORY_DEALLOC(allocator, CHAR, buff_len, buff);
            return NULL;
        }

        return buff;
    }
#elif __linux__
    inline int utils_files_exists(const char *pathname){
        return access(pathname, F_OK) == 0;
    }

    inline int utils_files_can_read(const char *pathname){
        return access(pathname, R_OK) == 0;
    }

    int utils_files_is_directory(char *pathname){
        struct stat file = {0};

        if(stat(pathname, &file) == -1){
            return -1;
        }

        return S_ISDIR(file.st_mode);
    }

    int utils_files_is_regular(char *pathname){
        struct stat file = {0};

        if(stat(pathname, &file) == -1){
            return -1;
        }

        return S_ISREG(file.st_mode);
    }

    inline char *utils_files_parent_pathname(const Allocator *allocator, const char *pathname){
        char *cloned_pathname = memory_clone_cstr(allocator, pathname, NULL);
        return dirname(cloned_pathname);
    }

    char *utils_files_cwd(const Allocator *allocator){
        char *pathname = getcwd(NULL, 0);
        size_t pathname_len = strlen(pathname);
        char *cloned_pathname = MEMORY_ALLOC(allocator, char, pathname_len + 1);

        if(!cloned_pathname){
            free(pathname);
            return NULL;
        }

        memcpy(cloned_pathname, pathname, pathname_len);
        cloned_pathname[pathname_len] = '\0';

        free(pathname);

        return cloned_pathname;
    }
#endif
//---------------------------------  TIME  ---------------------------------//
#ifdef _WIN32
    int64_t utils_millis(){
        FILETIME current_filetime = {0};
        GetSystemTimeAsFileTime(&current_filetime);

        SYSTEMTIME epoch_systime = {0};
        epoch_systime.wYear = 1970;
        epoch_systime.wMonth = 1;
        epoch_systime.wDayOfWeek = 4;
        epoch_systime.wDay = 1;

        FILETIME epoch_filetime = {0};
        SystemTimeToFileTime(&epoch_systime, &epoch_filetime);

        ULARGE_INTEGER current_ularge = {0};
        current_ularge.u.LowPart = current_filetime.dwLowDateTime;
        current_ularge.u.HighPart = current_filetime.dwHighDateTime;

        ULARGE_INTEGER epoch_ularge = {0};
        epoch_ularge.u.LowPart = epoch_filetime.dwLowDateTime;
        epoch_ularge.u.HighPart = epoch_filetime.dwHighDateTime;

        ULARGE_INTEGER new_current_ularge = {0};
        new_current_ularge.QuadPart = current_ularge.QuadPart - epoch_ularge.QuadPart;

        return (new_current_ularge.QuadPart * 100) / 1000000;
    }

    // must avoid use value of 0 for 'time'
    void utils_sleep(int64_t time){
        Sleep(time);
    }
#elif __linux__
    int64_t utils_millis(){
        struct timespec spec = {0};
        clock_gettime(CLOCK_REALTIME, &spec);

        return spec.tv_nsec / 1e+6 + spec.tv_sec * 1000;
    }

    // must avoid use value of 0 for 'time'
    void utils_sleep(int64_t time){
        usleep(time * 1000);
    }
#endif
//--------------------------------  OTHER  ---------------------------------//
int utils_hexadecimal_str_to_i64(char *str, int64_t *out_value){
    int len = (int)strlen(str);

    if(len <= 2 || len > 18){
        return INVLEN;
    }

    if(strncasecmp(str, "0x", 2) != 0){
        return INVPRE;
    }

    uint8_t value_offset = (len - 2) * 4;
    int64_t value = 0;

    for (int i = 2; i < len; i++){
        char c = str[i];

        if((c < '0' || c > '9') && (c < 'a' || c > 'f') && (c < 'A' || c > 'F')){
            return INVDIG;
        }

        uint64_t nibble = 0;

        switch (c){
            case 'a':
            case 'A':{
                nibble = 10;
                break;
            }
            case 'b':
            case 'B':{
                nibble = 11;
                break;
            }
            case 'c':
            case 'C':{
                nibble = 12;
                break;
            }
            case 'd':
            case 'D':{
                nibble = 13;
                break;
            }
            case 'e':
            case 'E':{
                nibble = 14;
                break;
            }
            case 'f':
            case 'F':{
                nibble = 15;
                break;
            }
            default:{
                nibble = ((int64_t)c) - 48;
            }
        }

        nibble <<= value_offset -= 4;
        value |= nibble;
    }

    *out_value = value;

    return 0;
}

int utils_decimal_str_to_i64(char *str, int64_t *out_value){
    int len = (int)strlen(str);

    if(len == 0){
        return INVLEN;
    }

    int64_t value = 0;
    int is_negative = str[0] == '-';

    for(int i = is_negative ? 1 : 0; i < len; i++){
        char c = str[i];

        if(c < '0' || c > '9'){
            return INVDIG;
        }

        int64_t digit = ((int64_t)c) - 48;

        value *= 10;
        value += digit;
    }

    if(is_negative == 1){
        value *= -1;
    }

    *out_value = value;

    return 0;
}

#define ABS(value)(value < 0 ? value * -1 : value)

int utils_str_to_double(char *raw_str, double *out_value){
    int len = (int)strlen(raw_str);
    int is_negative = 0;
    int is_decimal = 0;
    double special = 10.0;
    double value = 0.0;

    for(int i = 0; i < len; i++){
        char c = raw_str[i];

        if(c == '-' && i == 0){
            is_negative = 1;
            continue;
        }

        if(c == '.'){
			if(i == 0){return 1;}
			is_decimal = 1;
			continue;
		}

        if(c < '0' || c > '9'){return 1;}

        int digit = ((int)c) - 48;

        if(is_decimal){
			double decimal = digit / special;
			value += decimal;
			special *= 10.0;
		}else{
			value *= 10.0;
			value += digit;
		}
    }

    if(is_negative == 1){value *= -1;}

    *out_value = value;

    return 0;
}

DStr *utils_read_source(const char *pathname, const Allocator *allocator){
	FILE *source_file = fopen(pathname, "r");

    if(!source_file){
        return NULL;
    }

	fseek(source_file, 0, SEEK_END);

	size_t source_size = (size_t)ftell(source_file);
    char *buff = MEMORY_ALLOC(allocator, char, source_size + 1);
	DStr *rstr = MEMORY_ALLOC(allocator, DStr, 1);

	fseek(source_file, 0, SEEK_SET);
	fread(buff, 1, source_size, source_file);
	fclose(source_file);

	buff[source_size] = '\0';

	rstr->len = source_size;
	rstr->buff = buff;

	return rstr;
}

char *utils_read_file_as_text(char *pathname, Allocator *allocator, size_t *out_len){
    FILE *file = fopen(pathname, "r");

    if(!file){
        return NULL;
    }

	fseek(file, 0, SEEK_END);

	size_t content_len = (size_t)ftell(file);
    char *content_buff = MEMORY_ALLOC(allocator, char, content_len + 1);

	fseek(file, 0, SEEK_SET);
	fread(content_buff, 1, content_len, file);
	fclose(file);

	content_buff[content_len] = '\0';

    if(out_len){
        *out_len = content_len;
    }

	return content_buff;
}
