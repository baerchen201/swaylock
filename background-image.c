#include <assert.h>
#include <MagickWand/MagickWand.h>
#include "background-image.h"
#include "swaylock.h"
#include "cairo.h"
#include "log.h"

enum background_mode parse_background_mode(const char *mode) {
	if (strcmp(mode, "stretch") == 0) {
		return BACKGROUND_MODE_STRETCH;
	} else if (strcmp(mode, "fill") == 0) {
		return BACKGROUND_MODE_FILL;
	} else if (strcmp(mode, "fit") == 0) {
		return BACKGROUND_MODE_FIT;
	} else if (strcmp(mode, "center") == 0) {
		return BACKGROUND_MODE_CENTER;
	} else if (strcmp(mode, "tile") == 0) {
		return BACKGROUND_MODE_TILE;
	} else if (strcmp(mode, "solid_color") == 0) {
		return BACKGROUND_MODE_SOLID_COLOR;
	}
	swaylock_log(LOG_ERROR, "Unsupported background mode: %s", mode);
	return BACKGROUND_MODE_INVALID;
}

static cairo_status_t
stdio_read_func (void *closure, unsigned char *data, unsigned int size)
{
	FILE *file = closure;

	while (size) {
		const size_t ret = fread(data, 1, size, file);
		size -= ret;
		data += ret;

		if (size && (feof (file) || ferror (file)))
			return CAIRO_STATUS_READ_ERROR;
	}

	return CAIRO_STATUS_SUCCESS;
}


cairo_surface_t *load_background_image(const char *path, const double *blur, const double *opacity) {
	cairo_surface_t *image = NULL;

	FILE *imageFile = fopen(path, "rb");
	if (imageFile == NULL) {
		swaylock_log(LOG_ERROR, "Failed to open background image file: %s.",
				strerror(errno));
		return NULL;
	}

	MagickWandGenesis();
	MagickWand *wand = NewMagickWand();
	if (!MagickReadImageFile(wand, imageFile)) {
		ExceptionType e = MagickGetExceptionType(wand);
		swaylock_log(LOG_ERROR, "Failed to load background image: %s.",
				MagickGetException(wand, &e));
		goto end;
	}
	if (MagickGetImageColorspace(wand) == CMYKColorspace)
		MagickTransformImageColorspace(wand, RGBColorspace);

	if (*blur > 0)
		MagickGaussianBlurImage(wand, *blur, *blur);

	if (*opacity > 0) {
		PixelWand *color = NewPixelWand(), *blend = NewPixelWand();
		const uint16_t actualOpacity = QuantumRange * (*opacity / 100);
		PixelSetRedQuantum(blend, actualOpacity);
		PixelSetGreenQuantum(blend, actualOpacity);
		PixelSetBlueQuantum(blend, actualOpacity);
		PixelSetAlphaQuantum(blend, actualOpacity);

		PixelSetRedQuantum(color, 0);
		PixelSetGreenQuantum(color, 0);
		PixelSetBlueQuantum(color, 0);
		PixelSetAlphaQuantum(color, QuantumRange);

		MagickColorizeImage(wand, color, blend);
	}

	FILE *processedImage = tmpfile();
	MagickWriteImageFile(wand, processedImage);
	fseek(processedImage, 0, 0);

	image = cairo_image_surface_create_from_png_stream(stdio_read_func, processedImage);

	end:
	wand = DestroyMagickWand(wand);

	if (!image)
		return NULL;
	if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
		swaylock_log(LOG_ERROR, "Failed to open background image in cairo: %s.",
				cairo_status_to_string(cairo_surface_status(image)));
		return NULL;
	}
	return image;
}

void render_background_image(cairo_t *cairo, cairo_surface_t *image,
		enum background_mode mode, int buffer_width, int buffer_height) {
	double width = cairo_image_surface_get_width(image);
	double height = cairo_image_surface_get_height(image);

	cairo_save(cairo);
	switch (mode) {
	case BACKGROUND_MODE_STRETCH:
		cairo_scale(cairo,
				(double)buffer_width / width,
				(double)buffer_height / height);
		cairo_set_source_surface(cairo, image, 0, 0);
		break;
	case BACKGROUND_MODE_FILL: {
		double window_ratio = (double)buffer_width / buffer_height;
		double bg_ratio = width / height;

		if (window_ratio > bg_ratio) {
			double scale = (double)buffer_width / width;
			cairo_scale(cairo, scale, scale);
			cairo_set_source_surface(cairo, image,
					0, (double)buffer_height / 2 / scale - height / 2);
		} else {
			double scale = (double)buffer_height / height;
			cairo_scale(cairo, scale, scale);
			cairo_set_source_surface(cairo, image,
					(double)buffer_width / 2 / scale - width / 2, 0);
		}
		break;
	}
	case BACKGROUND_MODE_FIT: {
		double window_ratio = (double)buffer_width / buffer_height;
		double bg_ratio = width / height;

		if (window_ratio > bg_ratio) {
			double scale = (double)buffer_height / height;
			cairo_scale(cairo, scale, scale);
			cairo_set_source_surface(cairo, image,
					(double)buffer_width / 2 / scale - width / 2, 0);
		} else {
			double scale = (double)buffer_width / width;
			cairo_scale(cairo, scale, scale);
			cairo_set_source_surface(cairo, image,
					0, (double)buffer_height / 2 / scale - height / 2);
		}
		break;
	}
	case BACKGROUND_MODE_CENTER:
		/*
		 * Align the unscaled image to integer pixel boundaries
		 * in order to prevent loss of clarity (this only matters
		 * for odd-sized images).
		 */
		cairo_set_source_surface(cairo, image,
				(int)((double)buffer_width / 2 - width / 2),
				(int)((double)buffer_height / 2 - height / 2));
		break;
	case BACKGROUND_MODE_TILE: {
		cairo_pattern_t *pattern = cairo_pattern_create_for_surface(image);
		cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
		cairo_set_source(cairo, pattern);
		break;
	}
	case BACKGROUND_MODE_SOLID_COLOR:
	case BACKGROUND_MODE_INVALID:
		assert(0);
		break;
	}
	cairo_paint(cairo);
	cairo_restore(cairo);
}
