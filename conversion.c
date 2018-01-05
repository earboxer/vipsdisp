/* Manage conversion of images for display: zoom, colour, load, etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include <vips/vips.h>

#include "disp.h"

/* The size of the tiles that we use for the libvips cache.
 */
static const int tile_size = 128;

/* conversion is actually a drawing area the size of the widget on screen: we 
 * do all scrolling ourselves.
 */
G_DEFINE_TYPE( Conversion, conversion, G_OBJECT );

enum {

	/* Our signals. 
	 */
	SIG_PRELOAD,
	SIG_LOAD,
	SIG_POSTLOAD,

	SIG_LAST
};

static guint conversion_signals[SIG_LAST] = { 0 };

static void
conversion_finalize( GObject *object )
{
	Conversion *conversion = (Conversion *) widget;

	G_OBJECT_CLASS( conversion_parent_class )->finalize( object );
}

static void
conversion_empty( Conversion *conversion )
{
	if( conversion->image ) { 
		if( conversion->preeval_sig ) { 
			g_signal_handler_disconnect( conversion->image, 
				conversion->preeval_sig ); 
			conversion->preeval_sig = 0;
		}

		if( conversion->eval_sig ) { 
			g_signal_handler_disconnect( conversion->image, 
				conversion->eval_sig ); 
			conversion->eval_sig = 0;
		}

		if( conversion->posteval_sig ) { 
			g_signal_handler_disconnect( conversion->image, 
				conversion->posteval_sig ); 
			conversion->posteval_sig = 0;
		}
	}


	VIPS_UNREF( conversion->srgb_region );
	VIPS_UNREF( conversion->srgb );
	VIPS_UNREF( conversion->mask_region );
	VIPS_UNREF( conversion->mask );
	VIPS_UNREF( conversion->display_region );
	VIPS_UNREF( conversion->display );
	VIPS_UNREF( conversion->image_region );

	VIPS_UNREF( conversion->image );
}

static void
conversion_dispose( GObject *object )
{
	Conversion *conversion = (Conversion *) widget;

	conversion_empty( conversion );

	G_OBJECT_CLASS( conversion_parent_class )->dispose( object );
}

static void
conversion_set_property( GObject *object, 
	guint prop_id, const GValue *value, GParamSpec *pspec )
{
	Conversion *conversion = (Conversion *) object;

	switch( prop_id ) {

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID( object, prop_id, pspec );
		break;
	}
}

static void
conversion_get_property( GObject *object, 
	guint prop_id, GValue *value, GParamSpec *pspec )
{
	Conversion *conversion = (Conversion *) object;

	switch( prop_id ) {

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID( object, prop_id, pspec );
		break;
	}
}

static void
conversion_init( Conversion *conversion )
{
	printf( "conversion_init:\n" ); 

	conversion->mag = 1;
}

static void
conversion_class_init( ConversionClass *class )
{
	GObjectClass *gobject_class = G_OBJECT_CLASS( class );
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS( class );

	printf( "conversion_class_init:\n" ); 

	gobject_class->finalize = conversion_finalize;
	gobject_class->dispose = conversion_dispose;
	gobject_class->set_property = conversion_set_property;
	gobject_class->get_property = conversion_get_property;

	conversion_signals[SIG_PRELOAD] = g_signal_new( "preload",
		G_TYPE_FROM_CLASS( class ),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET( ConversionClass, preload ), 
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER );

	conversion_signals[SIG_LOAD] = g_signal_new( "load",
		G_TYPE_FROM_CLASS( class ),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET( ConversionClass, load ), 
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER );

	conversion_signals[SIG_POSTLOAD] = g_signal_new( "postload",
		G_TYPE_FROM_CLASS( class ),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET( ConversionClass, postload ), 
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER );

}

/* Make the screen image. This is the thing we display pixel values from in
 * the status bar.
 */
static VipsImage *
conversion_display_image( Conversion *conversion, VipsImage *in )
{
	VipsImage *image;
	VipsImage *x;

	/* image redisplays the head of the pipeline. Hold a ref to it as we
	 * work.
	 */
	image = in;
	g_object_ref( image ); 

	if( conversion->mag < 0 ) {
		if( vips_subsample( image, &x, 
			-conversion->mag, -conversion->mag, NULL ) ) {
			g_object_unref( image );
			return( NULL ); 
		}
		g_object_unref( image );
		image = x;
	}
	else { 
		if( vips_zoom( image, &x, 
			conversion->mag, conversion->mag, NULL ) ) {
			g_object_unref( image );
			return( NULL ); 
		}
		g_object_unref( image );
		image = x;
	}

	return( image );
}

/* Make the srgb image we paint with. 
 */
static VipsImage *
conversion_srgb_image( Conversion *conversion, VipsImage *in, 
	VipsImage **mask )
{
	VipsImage *image;
	VipsImage *x;

	/* image redisplays the head of the pipeline. Hold a ref to it as we
	 * work.
	 */
	image = in;
	g_object_ref( image ); 

	/* This won't work for CMYK, you need to mess about with ICC profiles
	 * for that, but it will work for everything else.
	 */
	if( vips_colourspace( image, &x, VIPS_INTERPRETATION_sRGB, NULL ) ) {
		g_object_unref( image );
		return( NULL ); 
	}
	g_object_unref( image );
	image = x;

	/* Drop any alpha.
	 */
	if( vips_extract_band( image, &x, 0, "n", 3, NULL ) ) {
		g_object_unref( image );
		return( NULL ); 
	}
	g_object_unref( image );
	image = x;

	/* Do a huge blur .. this is a slow operation, and handy for
	 * debugging.
	if( vips_gaussblur( image, &x, 100.0, NULL ) ) {
		g_object_unref( image );
		return( NULL ); 
	}
	g_object_unref( image );
	image = x;
	 */

	return( image );
}

static int
conversion_update_conversion( Conversion *conversion )
{
	if( conversion->image ) { 
		if( conversion->srgb_region )
			printf( "** junking region %p\n", 
				conversion->srgb_region );

		VIPS_UNREF( conversion->mask_region );
		VIPS_UNREF( conversion->mask );
		VIPS_UNREF( conversion->srgb_region );
		VIPS_UNREF( conversion->srgb );
		VIPS_UNREF( conversion->display_region );
		VIPS_UNREF( conversion->display );
		VIPS_UNREF( conversion->image_region );

		if( !(conversion->image_region = 
			vips_region_new( conversion->image )) )
			return( -1 ); 

		if( !(conversion->display = 
			conversion_display_image( conversion, 
				conversion->image )) ) 
			return( -1 ); 
		if( !(conversion->display_region = 
			vips_region_new( conversion->display )) )
			return( -1 ); 

		if( !(conversion->srgb = 
			conversion_srgb_image( conversion, 
				conversion->display, &conversion->mask )) ) 
			return( -1 ); 
		if( !(conversion->srgb_region = 
			vips_region_new( conversion->srgb )) )
			return( -1 ); 
		if( !(conversion->mask_region = 
			vips_region_new( conversion->mask )) )
			return( -1 ); 

		conversion->image_rect.width = conversion->display->Xsize;
		conversion->image_rect.height = conversion->display->Ysize;

		printf( "conversion_update_conversion: image size %d x %d\n", 
			conversion->image_rect.width, 
			conversion->image_rect.height );
		printf( "** srgb image %p\n", conversion->srgb );
		printf( "** new region %p\n", conversion->srgb_region );

		gtk_widget_set_size_request( GTK_WIDGET( conversion ),
			conversion->image_rect.width, 
			conversion->image_rect.height );
	}

	return( 0 );
}

static void
conversion_preeval( VipsImage *image, 
	VipsProgress *progress, Conversion *conversion )
{
	g_signal_emit( conversion, 
		conversion_signals[SIG_PRELOAD], 0, progress );
}

static void
conversion_eval( VipsImage *image, 
	VipsProgress *progress, Conversion *conversion )
{
	g_signal_emit( conversion, 
		conversion_signals[SIG_LOAD], 0, progress );
}

static void
conversion_posteval( VipsImage *image, 
	VipsProgress *progress, Conversion *conversion )
{
	g_signal_emit( conversion, 
		conversion_signals[SIG_POSTLOAD], 0, progress );
}

static void
conversion_attach_progress( Conversion *conversion )
{
	g_assert( !conversion->preeval_sig );
	g_assert( !conversion->eval_sig );
	g_assert( !conversion->posteval_sig );

	/* Attach an eval callback: this will tick down if we 
	 * have to decode this image.
	 */
	vips_image_set_progress( conversion->image, TRUE ); 
	conversion->preeval_sig = 
		g_signal_connect( conversion->image, "preeval",
			G_CALLBACK( conversion_preeval ), 
			conversion );
	conversion->eval_sig = 
		g_signal_connect( conversion->image, "eval",
			G_CALLBACK( conversion_eval ), 
			conversion );
	conversion->posteval_sig = 
		g_signal_connect( conversion->image, "posteval",
			G_CALLBACK( conversion_posteval ), 
			conversion );
}

int
conversion_set_file( Conversion *conversion, GFile *file )
{
	conversion_empty( conversion );

	if( file != NULL ) {
		gchar *path;
		gchar *contents;
		gsize length;

		if( (path = g_file_get_path( file )) ) {
			if( !(conversion->image = 
				vips_image_new_from_file( path, NULL )) ) {
				g_free( path ); 
				return( -1 );
			}
			g_free( path ); 
		}
		else if( g_file_load_contents( file, NULL, 
			&contents, &length, NULL, NULL ) ) {
			if( !(conversion->image =
				vips_image_new_from_buffer( contents, length, 
					"", NULL )) ) {
				g_free( contents );
				return( -1 ); 
			}

			g_signal_connect( conversion->image, "close",
				G_CALLBACK( conversion_close_memory ), 
				contents );
		}
		else {
			vips_error( "conversion", 
				"unable to load GFile object" );
			return( -1 );
		}

		conversion_attach_progress( conversion ); 
	}

	conversion_update_conversion( conversion );

	return( 0 );
}

int
conversion_get_mag( Conversion *conversion )
{
	return( conversion->mag );
}

void
conversion_set_mag( Conversion *conversion, int mag )
{
	if( mag > -600 &&
		mag < 1000000 &&
		conversion->mag != mag ) { 
		printf( "conversion_set_mag: %d\n", mag ); 

		conversion->mag = mag;
		conversion_update_conversion( conversion );
	}
}

gboolean
conversion_get_image_size( Conversion *conversion, 
	int *width, int *height )
{
	if( conversion->image ) {
		*width = conversion->image->Xsize;
		*height = conversion->image->Ysize;

		return( TRUE ); 
	}
	else
		return( FALSE );
}

gboolean
conversion_get_display_image_size( Conversion *conversion, 
	int *width, int *height )
{
	if( conversion->display ) {
		*width = conversion->display->Xsize;
		*height = conversion->display->Ysize;

		return( TRUE ); 
	}
	else
		return( FALSE );
}

/* Map to underlying image coordinates from display image coordinates.
 */
void
conversion_to_image_cods( Conversion *conversion,
	int display_x, int display_y, int *image_x, int *image_y )
{
	if( conversion->mag > 0 ) {
		*image_x = display_x / conversion->mag;
		*image_y = display_y / conversion->mag;
	}
	else {
		*image_x = display_x * -conversion->mag;
		*image_y = display_y * -conversion->mag;
	}
}

/* Map to display cods from underlying image coordinates.
 */
void
conversion_to_display_cods( Conversion *conversion,
	int image_x, int image_y, int *display_x, int *display_y )
{
	if( conversion->mag > 0 ) {
		*display_x = image_x * conversion->mag;
		*display_y = image_y * conversion->mag;
	}
	else {
		*display_x = image_x / -conversion->mag;
		*display_y = image_y / -conversion->mag;
	}
}

VipsPel *
conversion_get_ink( Conversion *conversion, int x, int y )
{
	VipsRect rect;

	rect.left = x;
	rect.top = y;
	rect.width = 1;
	rect.height = 1;
	if( vips_region_prepare( conversion->image_region, &rect ) )
		return( NULL );

	return( VIPS_REGION_ADDR( conversion->image_region, x, y ) );  
}

Conversion *
conversion_new( void ) 
{
	Conversion *conversion;

	printf( "conversion_new:\n" ); 

	conversion = g_object_new( conversion_get_type(),
		NULL );

	return( conversion ); 
}
