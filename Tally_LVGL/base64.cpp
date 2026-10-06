#include "base64.hpp"
#include <string.h>

const char base64_chars[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

unsigned int encode_base64(unsigned char *input, unsigned int input_length, unsigned char *output) {
  unsigned int i = 0, j = 0;

  while (i < input_length) {
    unsigned char a = input[i++];
    unsigned char b = (i < input_length) ? input[i++] : 0;
    unsigned char c = (i < input_length) ? input[i++] : 0;

    unsigned char bitmap = 0;
    if (i > input_length) bitmap |= (3 - (input_length - (i - 3)));

    output[j++] = base64_chars[(a & 252) >> 2];
    output[j++] = base64_chars[(((a & 3) << 4) | ((b & 240) >> 4))];
    if ((bitmap & 1) == 0) output[j++] = base64_chars[(((b & 15) << 2) | ((c & 192) >> 6))];
    if ((bitmap & 2) == 0) output[j++] = base64_chars[(c & 63)];
  }

  output[j++] = 0;

  return j;
}

unsigned int decode_base64(unsigned char *input, unsigned char *output) {
  int lookup[256];
  memset(lookup, -1, sizeof(lookup));

  for (int i = 0; i < 64; i++) {
    lookup[(unsigned char)base64_chars[i]] = i;
  }

  unsigned int i = 0, j = 0;
  unsigned char a[4], b[4];

  while (i < strlen((const char *)input)) {
    for (int k = 0; k < 4 && i < strlen((const char *)input); k++) {
      unsigned char c = input[i++];
      if (lookup[c] == -1) {
        if (c == '=') {
          a[k] = 0;
        } else {
          k--;
          continue;
        }
      } else {
        a[k] = lookup[c];
      }
    }

    b[0] = (a[0] << 2) | (a[1] >> 4);
    b[1] = (a[1] << 4) | (a[2] >> 2);
    b[2] = (a[2] << 6) | (a[3]);

    output[j++] = b[0];
    if (i < strlen((const char *)input) || a[2] != 0) output[j++] = b[1];
    if (i < strlen((const char *)input) || a[3] != 0) output[j++] = b[2];
  }

  return j;
}
