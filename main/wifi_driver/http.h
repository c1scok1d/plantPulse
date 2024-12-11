#ifndef __HTTP_H_
#define __HTTP_H_

#include <esp_err.h>

esp_err_t http_post(char *buffer, size_t buffer_len);

#endif