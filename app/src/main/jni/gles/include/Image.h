#ifndef __AAIMAGE_H_
#define __AAIMAGE_H_
#include <stdint.h>

 #define        RGBA32_R8G8B8A8        0x101

 #ifdef __cplusplus
 extern "C" {
 #endif

 typedef struct __image__
 {
	 uint32_t    pixelFormat;
	 uint32_t    width;
	 uint32_t    height;
	 void*       plane[4];
	 int32_t     pitch[4];
 }Image;

#define fillYUV420PImage(img, data, linesize, width, height)\
 uint8_t *src = (data[0]);\
 uint8_t *dst = (uint8_t *)(img->plane[0]);\
 int srcline = (linesize[0]);\
 int32_t dstline = (img->pitch[0]);\
 for(int i=0; i < (height);i++){\
  memcpy(dst, src, width);\
  dst += dstline;\
  src += srcline;\
 }\
 int height2 = (height) / 2;\
 int width2 = (width) / 2;\
 src = (data[1]);\
 dst = (uint8_t *)(img->plane[1]);\
 srcline = (linesize[1]);\
 dstline = (img->pitch[1]);\
 for(int i=0; i < height2;i++){\
  memcpy(dst, src, width2);\
  dst += dstline;\
  src += srcline;\
 }\
 src = (data[2]);\
 dst = (uint8_t *)(img->plane[2]);\
 srcline = (linesize)[2];\
 dstline = (img)->pitch[2];\
 for(int i=0; i < height2;i++){\
  memcpy(dst, src, width2);\
  dst += dstline;\
  src += srcline;\
 }

 #ifdef __cplusplus
 }
 #endif

 #endif /* __AAIMAGE_H_ */
