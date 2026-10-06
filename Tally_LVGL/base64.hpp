#ifndef BASE64_H
#define BASE64_H

#ifdef __cplusplus
extern "C" {
#endif

unsigned int encode_base64(unsigned char *input, unsigned int input_length, unsigned char *output);
unsigned int decode_base64(unsigned char *input, unsigned char *output);

#ifdef __cplusplus
}
#endif

#endif
