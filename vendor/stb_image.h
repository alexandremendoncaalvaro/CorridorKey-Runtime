#pragma once
unsigned char* stbi_load(char const*, int*, int*, int*, int);
void stbi_image_free(void*);
char const* stbi_failure_reason(void);