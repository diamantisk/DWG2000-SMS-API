/*
 * util.c
 *
 *  Created on: Mar 29, 2012
 *      Author: carlos
 */


/*
 * From: http://stackoverflow.com/questions/29242/off-the-shelf-c-hex-dump-code
 */

void hexdump(void *ptr, int buflen)
{
  unsigned char *buf = (unsigned char*)ptr;
  int i, j;
  for (i=0; i<buflen; i+=16) {
    printf("%06x: ", i);
    for (j=0; j<16; j++)
      if (i+j < buflen)
        printf("%02x ", buf[i+j]);
      else
        printf("   ");
    printf(" ");
    for (j=0; j<16; j++)
      if (i+j < buflen)
        printf("%c", isprint(buf[i+j]) ? buf[i+j] : '.');
    printf("\n");
  }
}