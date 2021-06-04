/**

IIR Gauss Filter v1.0
By Stephan Soller <stephan.soller@helionweb.de>
Based on the paper "Recursive implementation of the Gaussian filter" by Ian T. Young and Lucas J. van Vliet.
Licensed under the MIT license

QUICK START

	#include ...
	#include ...
	#define IIR_GAUSS_BLUR_IMPLEMENTATION
	#include "iir_gauss_blur.h"
	...
	int width = 0, height = 0, components = 1;
	uint8_t* image = stbi_load("foo.png", &width, &height, &components, 0);
	float sigma = 10;
	iir_gauss_blur(width, height, components, image, sigma);
	stbi_write_png("foo.blurred.png", width, height, components, image, 0);

This example uses stb_image.h to load the image, then blurs it and writes the result using stb_image_write.h.
`sigma` controls the strength of the blur. Higher values give you a blurrier image.

DOCUMENTATION

This is a single header file library. You'll have to define IIR_GAUSS_BLUR_IMPLEMENTATION before including this file to
get the implementation. Otherwise just the header will be included.

The library only has a single function: iir_gauss_blur(width, height, components, image, sigma).

- `width` and `height` are the dimensions of the image in pixels.
- `components` is the number of bytes per pixel. 1 for a grayscale image, 3 for RGB and 4 for RGBA.
  The function can handle an arbitrary number of channels, so 2 or 7 will work as well.
- `image` is a pointer to the image data with `width * height` pixels, each pixel having `components` bytes (interleaved
  8-bit components). There is no padding between the scanlines of the image.
  This is the format used by stb_image.h and stb_image_write.h and easy to work with.
- `sigma` is the strength of the blur. It's a number > 0.5 and most people seem to just eyeball it.
  Start with e.g. a sigma of 5 and go up or down until you have the blurriness you want.
  There are more informed ways to choose this parameter, see CHOOSING SIGMA below.

The function mallocs an internal float buffer with the same dimensions as the image. If that turns out to be a
bottleneck fell free to move that out of the function. The source code is quite short and straight forward (even if the
math isn't).

The function is an implementation of the paper "Recursive implementation of the Gaussian filter" by Ian T. Young and
Lucas J. van Vliet. It has nothing to do with recursive function calls, instead it's a special way to construct a
filter. Other (convolution based) gauss filters apply a kernel for each pixel and the kernel grows as sigma gets larger.
Meaning their performance degrades the more blurry you want your image to be.

Instead The algorithm in the paper gets it done in just a few passes: A horizontal forward and backward pass and a
vertical forward and backward pass. The work done is independent of the blur radius and so you can have ridiculously
large blur radii without any performance impact.

CHOOSING SIGMA

There seem to be several rules of thumb out there to get a sigma for a given "blur radius". Usually this is something
like `radius = 2 * sigma`. So if you want to have a blur radius of 10px you can use `sigma = (1.0 / 2.0) * radius` to
get the sigma for it (5.0). I'm not sure what that "radius" is supposed to mean though.

For my own projects I came up with two different kinds of blur radii and how to get a sigma for them: Given a big white
area on a black background, how far will the white "bleed out" into the surrounding black? How large is the distance
until the white (255) gets blurred down to something barely visible (smaller than 16) or even to nothing (smaller than
1)? There are to estimates to get the sigma for those radii:

	sigma = (1.0 / 1.42) * radius16;
	sigma = (1.0 / 3.66) * radius1;

Personally I use `radius16` to calculate the sigma when blurring normal images. Think: I want to blur a pixel across a
circle with the radius x so it's impact is barely visible at the edges.

When I need to calculate padding I use `radius1`: When I have a black border of 100px around the image I can use a
`radius1` of 100 and be reasonable sure that I still got black at the edges. So given a `radius1` blur strength I can
use it as a padding width as well.

I created those estimates by applying different sigmas (1 to 100) to a test image and measuring the effects with GIMP.
So take it with a grain of salt (or many). They're reasonable estimates but by no means exact. I tried to solve the
normal distribution to calculate the perfect sigma but gave up after a lot of confusion. If you know an exact solution
let me know. :)

VERSION HISTORY

v1.0  2018-08-30  Initial release

**/
#ifndef IIR_GAUSS_BLUR_HEADER
#define IIR_GAUSS_BLUR_HEADER

template <typename T>
void iir_gauss_blur(unsigned int width, unsigned int height, unsigned char components, T* image, float sigma);

#endif  // IIR_GAUSS_BLUR_HEADER

#ifdef IIR_GAUSS_BLUR_IMPLEMENTATION
#include <stdlib.h>
#include <math.h>

template <typename T>
void iir_gauss_blur(unsigned int width, unsigned int height, unsigned char components, T* image, float sigma) {
	// Create IDX macro but push any previous definition (and restore it later) so we don't overwrite a macro the user has possibly defined before us
	#pragma push_macro("IDX")
	#define IDX(x, y, n) ((y)*width*components + (x)*components + n)
	
	// Allocate buffers
	float* buffer = (float*)malloc(width * height * components * sizeof(buffer[0]));
	
	// Calculate filter parameters for a specified sigma
	// Use Equation 11b to determine q, do nothing if sigma is to small (should have no effect) or negative (doesn't make sense)
	float q;
	if (sigma >= 2.5)
		q = 0.98711 * sigma - 0.96330;
	else if (sigma >= 0.5)
		q = 3.97156 - 4.14554 * sqrtf(1.0 - 0.26891 * sigma);
	else
		return;
	
	// Use equation 8c to determine b0, b1, b2 and b3
	float b0 = 1.57825 + 2.44413*q + 1.4281*q*q + 0.422205*q*q*q;
	float b1 = 2.44413*q + 2.85619*q*q + 1.26661*q*q*q;
	float b2 = -( 1.4281*q*q + 1.26661*q*q*q );
	float b3 = 0.422205*q*q*q;
	// Use equation 10 to determine B
	float B = 1.0 - (b1 + b2 + b3) / b0;
	
	// Horizontal forward pass (from paper: Implement the forward filter with equation 9a)
	// The data is loaded from the byte image but stored in the float buffer
	for(unsigned int y = 0; y < height; y++) {
		float prev1[components], prev2[components], prev3[components];
		for(unsigned char n = 0; n < components; n++) {
			prev1[n] = image[IDX(0, y, n)];
			prev2[n] = prev1[n];
			prev3[n] = prev2[n];
		}
		
		for(unsigned int x = 0; x < width; x++) {
			for(unsigned char n = 0; n < components; n++) {
				float val = B * image[IDX(x, y, n)] + (b1 * prev1[n] + b2 * prev2[n] + b3 * prev3[n]) / b0;
				buffer[IDX(x, y, n)] = val;
				prev3[n] = prev2[n];
				prev2[n] = prev1[n];
				prev1[n] = val;
			}
		}
	}
	
	// Horizontal backward pass (from paper: Implement the backward filter with equation 9b)
	for(unsigned int y = height-1; y < height; y--) {
		float prev1[components], prev2[components], prev3[components];
		for(unsigned char n = 0; n < components; n++) {
			prev1[n] = buffer[IDX(width-1, y, n)];
			prev2[n] = prev1[n];
			prev3[n] = prev2[n];
		}
		
		for(unsigned int x = width-1; x < width; x--) {
			for(unsigned char n = 0; n < components; n++) {
				float val = B * buffer[IDX(x, y, n)] + (b1 * prev1[n] + b2 * prev2[n] + b3 * prev3[n]) / b0;
				buffer[IDX(x, y, n)] = val;
				prev3[n] = prev2[n];
				prev2[n] = prev1[n];
				prev1[n] = val;
			}
		}
	}
	
	// Vertical forward pass (from paper: Implement the forward filter with equation 9a)
	for(unsigned int x = 0; x < width; x++) {
		float prev1[components], prev2[components], prev3[components];
		for(unsigned char n = 0; n < components; n++) {
			prev1[n] = buffer[IDX(x, 0, n)];
			prev2[n] = prev1[n];
			prev3[n] = prev2[n];
		}
		
		for(unsigned int y = 0; y < height; y++) {
			for(unsigned char n = 0; n < components; n++) {
				float val = B * buffer[IDX(x, y, n)] + (b1 * prev1[n] + b2 * prev2[n] + b3 * prev3[n]) / b0;
				buffer[IDX(x, y, n)] = val;
				prev3[n] = prev2[n];
				prev2[n] = prev1[n];
				prev1[n] = val;
			}
		}
	}
	
	// Vertical backward pass (from paper: Implement the backward filter with equation 9b)
	// Also write the result back into the byte image
	for(unsigned int x = width-1; x < width; x--) {
		float prev1[components], prev2[components], prev3[components];
		for(unsigned char n = 0; n < components; n++) {
			prev1[n] = buffer[IDX(x, height-1, n)];
			prev2[n] = prev1[n];
			prev3[n] = prev2[n];
		}
		
		for(unsigned int y = height-1; y < height; y--) {
			for(unsigned char n = 0; n < components; n++) {
				float val = B * buffer[IDX(x, y, n)] + (b1 * prev1[n] + b2 * prev2[n] + b3 * prev3[n]) / b0;
				image[IDX(x, y, n)] = val;
				prev3[n] = prev2[n];
				prev2[n] = prev1[n];
				prev1[n] = val;
			}
		}
	}
	
	// Free temporary buffers and restore any potential IDX macro
	free(buffer);
	#pragma pop_macro("IDX")
}
#endif  // IIR_GAUSS_BLUR_IMPLEMENTATION
