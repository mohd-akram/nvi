#include <stdio.h>
#include <string.h>

#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include "gtkviscreen.h"

#define DEFAULT_VI_SCREEN_WIDTH_CHARS     80
#define DEFAULT_VI_SCREEN_HEIGHT_LINES    25
#define VI_SCREEN_BORDER_ROOM         1

enum {
  ARG_0,
  ARG_VADJUSTMENT,
};

enum {
    RESIZED,
    LAST_SIGNAL
};

static void gtk_vi_screen_class_init     (GtkViScreenClass   *klass);
static void gtk_vi_screen_set_arg        (GtkObject      *object,
					  GtkArg         *arg,
					  guint           arg_id);
static void gtk_vi_screen_get_arg        (GtkObject      *object,
					  GtkArg         *arg,
					  guint           arg_id);
static void gtk_vi_screen_init           (GtkViScreen        *vi);
static void gtk_vi_screen_destroy        (GtkObject      *object);
static void gtk_vi_screen_realize        (GtkWidget      *widget);
/*
static void gtk_vi_screen_map (GtkWidget *widget);
static void gtk_vi_screen_unmap (GtkWidget *widget);
*/
static void gtk_vi_screen_size_request   (GtkWidget      *widget,
				      GtkRequisition *requisition);
static void gtk_vi_screen_size_allocate  (GtkWidget      *widget,
				      GtkAllocation  *allocation);
/*
static void gtk_vi_screen_adjustment     (GtkAdjustment  *adjustment,
				      GtkViScreen        *text);
*/

static void gtk_vi_screen_draw              (GtkWidget         *widget,
					 GdkRectangle      *area);
static gint gtk_vi_screen_expose            (GtkWidget         *widget,
					 GdkEventExpose    *event);

static void recompute_geometry (GtkViScreen* vi);
static void expose_text (GtkViScreen* vi, GdkRectangle *area, gboolean cursor);
static void draw_lines(GtkViScreen *vi, gint y, gint x, gint ymax, gint xmax);
static void mark_lines(GtkViScreen *vi, gint ymin, gint xmin, gint ymax, gint xmax);

static GtkWidgetClass *parent_class = NULL;
static guint vi_screen_signals[LAST_SIGNAL] = { 0 };

#define CharAt(scr,y,x)	scr->chars + (y) * scr->cols + x
#define FlagAt(scr,y,x)	(scr->reverse + (y) * scr->cols + x)

#define COLOR_STANDARD	    0x00
#define COLOR_STANDOUT	    0x01

/* XXX */
enum { SA_ALTERNATE, SA_INVERSE };

void
gtk_vi_screen_attribute(GtkViScreen *vi, gint attribute, gint on)
{
    switch (attribute) {
    case SA_INVERSE:
	vi->color = on ? COLOR_STANDOUT : COLOR_STANDARD;
	break;
    }
}

void
gtk_vi_screen_move(GtkViScreen *vi, gint row, gint col)
{
    mark_lines(vi, vi->cury, vi->curx, vi->cury+1, vi->curx+1);
    vi->curx = col;
    vi->cury = row;
    mark_lines(vi, vi->cury, vi->curx, vi->cury+1, vi->curx+1);
}

void
gtk_vi_screen_clrtoel (GtkViScreen *vi)
{
    memset(CharAt(vi,vi->cury,vi->curx), ' ', vi->cols - vi->curx);
    memset(FlagAt(vi,vi->cury,vi->curx), COLOR_STANDARD, vi->cols - vi->curx);
    mark_lines(vi, vi->cury, vi->curx, vi->cury+1, vi->cols);
}

void
gtk_vi_screen_addstr(GtkViScreen *vi, const char *str, int len)
{
    /*
    fprintf(stderr, "%d --%.*s--\n", len, len, str);
    */
    memcpy(CharAt(vi,vi->cury,vi->curx), str, len);
    memset(FlagAt(vi,vi->cury,vi->curx), vi->color, len);
    mark_lines(vi, vi->cury, vi->curx, vi->cury+1, vi->curx + len);
    if ((vi->curx += len) >= vi->cols) {
	if (++vi->cury >= vi->rows) {
	    vi->cury = vi->rows-1;
	    vi->curx = vi->cols-1;
	} else {
	    vi->curx = 0;
	}
    }
}

void
gtk_vi_screen_deleteln(GtkViScreen *vi)
{
    gint y = vi->cury;
    gint rows = vi->rows - (y+1);

    memmove(CharAt(vi,y,0), CharAt(vi,y+1,0), rows * vi->cols);
    memset(CharAt(vi,vi->rows-1,0), ' ', vi->cols);
    memmove(FlagAt(vi,y,0), FlagAt(vi,y+1,0), rows * vi->cols);
    memset(FlagAt(vi,vi->rows-1,0), COLOR_STANDARD, vi->cols);
    mark_lines(vi, y, 0, vi->rows, vi->cols);
}

void
gtk_vi_screen_insertln(GtkViScreen *vi)
{
    gint y = vi->cury;
    gint rows = vi->rows - (y+1);

    memmove(CharAt(vi,y+1,0), CharAt(vi,y,0), rows * vi->cols);
    memset(CharAt(vi,y,0), ' ', vi->cols);
    memmove(FlagAt(vi,y+1,0), FlagAt(vi,y,0), rows * vi->cols);
    memset(FlagAt(vi,y,0), COLOR_STANDARD, vi->cols);
    mark_lines(vi, y, 0, vi->rows, vi->cols);
}

void
gtk_vi_screen_refresh(GtkViScreen *vi)
{
    if (vi->marked_maxy == 0)
	return;
    draw_lines(vi, vi->marked_y, vi->marked_x, vi->marked_maxy, vi->marked_maxx);
    vi->marked_x = vi->cols;
    vi->marked_y = vi->rows;
    vi->marked_maxx = 0;
    vi->marked_maxy = 0;
}

void
gtk_vi_screen_rewrite(GtkViScreen *vi, gint row)
{
    memset(FlagAt(vi,row,0), COLOR_STANDARD, vi->cols);
    mark_lines(vi, row, 0, row+1, vi->cols);
}

GtkType
gtk_vi_screen_get_type (void)
{
  static GtkType vi_screen_type = 0;
  
  if (!vi_screen_type)
    {
      static const GtkTypeInfo vi_screen_info =
      {
	"GtkViScreen",
	sizeof (GtkViScreen),
	sizeof (GtkViScreenClass),
	(GtkClassInitFunc) gtk_vi_screen_class_init,
	(GtkObjectInitFunc) gtk_vi_screen_init,
	/* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };
      
      vi_screen_type = gtk_type_unique (GTK_TYPE_WIDGET, &vi_screen_info);
    }
  
  return vi_screen_type;
}

static void
gtk_vi_screen_class_init (GtkViScreenClass *class)
{
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;
  
  object_class = (GtkObjectClass*) class;
  widget_class = (GtkWidgetClass*) class;
  parent_class = gtk_type_class (GTK_TYPE_WIDGET);

  vi_screen_signals[RESIZED] =
    gtk_signal_new ("resized",
		    GTK_RUN_FIRST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (GtkViScreenClass, resized),
		    gtk_marshal_NONE__INT_INT,
		    GTK_TYPE_NONE, 2, GTK_TYPE_INT, GTK_TYPE_INT, 0);

  gtk_object_class_add_signals(object_class, vi_screen_signals, LAST_SIGNAL);

  gtk_object_add_arg_type ("GtkViScreen::vadjustment",
			   GTK_TYPE_ADJUSTMENT,
			   GTK_ARG_READWRITE | GTK_ARG_CONSTRUCT,
			   ARG_VADJUSTMENT);

  object_class->set_arg = gtk_vi_screen_set_arg;
  object_class->get_arg = gtk_vi_screen_get_arg;
  object_class->destroy = gtk_vi_screen_destroy;

  widget_class->realize = gtk_vi_screen_realize;
  /*
  widget_class->map = gtk_vi_screen_map;
  widget_class->unmap = gtk_vi_screen_unmap;
  */
  widget_class->size_request = gtk_vi_screen_size_request;
  widget_class->size_allocate = gtk_vi_screen_size_allocate;
  widget_class->draw = gtk_vi_screen_draw;
  widget_class->expose_event = gtk_vi_screen_expose;

  class->rename = NULL;
  class->resized = NULL;
}

static void
gtk_vi_screen_set_arg (GtkObject        *object,
		      GtkArg           *arg,
		      guint             arg_id)
{
  GtkViScreen *vi_screen;
  
  vi_screen = GTK_VI_SCREEN (object);
  
  switch (arg_id)
    {
    case ARG_VADJUSTMENT:
      gtk_vi_screen_set_adjustment (vi_screen, GTK_VALUE_POINTER (*arg));
      break;
    default:
      break;
    }
}

static void
gtk_vi_screen_get_arg (GtkObject        *object,
		      GtkArg           *arg,
		      guint             arg_id)
{
  GtkViScreen *vi_screen;
  
  vi_screen = GTK_VI_SCREEN (object);
  
  switch (arg_id)
    {
    case ARG_VADJUSTMENT:
      GTK_VALUE_POINTER (*arg) = vi_screen->vadj;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
    }
}

static void
gtk_vi_screen_init (GtkViScreen *vi)
{
  GtkStyle *style;

  GTK_WIDGET_SET_FLAGS (vi, GTK_CAN_FOCUS);

  vi->text_area = NULL;
  vi->chars = 0;
  vi->reverse = 0;
  vi->color = COLOR_STANDARD;
  vi->cols = 0;
  vi->rows = 0;

  style = gtk_style_copy(GTK_WIDGET(vi)->style);
  style->font = gdk_font_load("fixed");
  GTK_WIDGET(vi)->style = style;
}

static void
gtk_vi_screen_destroy (GtkObject *object)
{
  GtkViScreen *vi_screen;
  
  g_return_if_fail (object != NULL);
  g_return_if_fail (GTK_IS_VI_SCREEN (object));
  
  vi_screen = (GtkViScreen*) object;

  /*
  gtk_signal_disconnect_by_data (GTK_OBJECT (vi_screen->vadj), vi_screen);
  */

  GTK_OBJECT_CLASS(parent_class)->destroy (object);
}

GtkWidget*
gtk_vi_screen_new (GtkAdjustment *vadj)
{
  GtkWidget *vi;

  vi = gtk_widget_new (GTK_TYPE_VI_SCREEN,
			 "vadjustment", vadj,
			 NULL);


  return vi;
}

void
gtk_vi_screen_set_adjustment (GtkViScreen       *vi_screen,
			  GtkAdjustment *vadj)
{
  g_return_if_fail (vi_screen != NULL);
  g_return_if_fail (GTK_IS_VI_SCREEN (vi_screen));
  if (vadj)
    g_return_if_fail (GTK_IS_ADJUSTMENT (vadj));
  else
    vadj = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 1.0, 0.0, 1.0, 0.0, 0.0));
  
  if (vi_screen->vadj && (vi_screen->vadj != vadj))
    {
      gtk_signal_disconnect_by_data (GTK_OBJECT (vi_screen->vadj), vi_screen);
      gtk_object_unref (GTK_OBJECT (vi_screen->vadj));
    }
  
  if (vi_screen->vadj != vadj)
    {
      vi_screen->vadj = vadj;
      gtk_object_ref (GTK_OBJECT (vi_screen->vadj));
      gtk_object_sink (GTK_OBJECT (vi_screen->vadj));
      
      /*
      gtk_signal_connect (GTK_OBJECT (vi_screen->vadj), "changed",
			  (GtkSignalFunc) gtk_vi_screen_adjustment,
			  vi_screen);
      gtk_signal_connect (GTK_OBJECT (vi_screen->vadj), "value_changed",
			  (GtkSignalFunc) gtk_vi_screen_adjustment,
			  vi_screen);
      gtk_signal_connect (GTK_OBJECT (vi_screen->vadj), "disconnect",
			  (GtkSignalFunc) gtk_vi_screen_disconnect,
			  vi_screen);
      gtk_vi_screen_adjustment (vadj, vi_screen);
      */
    }
}

static void
gtk_vi_screen_realize (GtkWidget *widget)
{
  GtkViScreen *vi;
  GdkWindowAttr attributes;
  gint attributes_mask;
  
  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_VI_SCREEN (widget));
  
  vi = GTK_VI_SCREEN (widget);
  GTK_WIDGET_SET_FLAGS (vi, GTK_REALIZED);
  
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.x = widget->allocation.x;
  attributes.y = widget->allocation.y;
  attributes.width = widget->allocation.width;
  attributes.height = widget->allocation.height;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.colormap = gtk_widget_get_colormap (widget);
  attributes.event_mask = gtk_widget_get_events (widget);
  attributes.event_mask |= (GDK_EXPOSURE_MASK |
			    GDK_BUTTON_PRESS_MASK |
			    GDK_BUTTON_RELEASE_MASK |
			    GDK_BUTTON_MOTION_MASK |
			    GDK_ENTER_NOTIFY_MASK |
			    GDK_LEAVE_NOTIFY_MASK |
			    GDK_KEY_PRESS_MASK);
  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
  
  widget->window = gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask);
  gdk_window_set_user_data (widget->window, vi);
  
  attributes.x = (widget->style->klass->xthickness + VI_SCREEN_BORDER_ROOM);
  attributes.y = (widget->style->klass->ythickness + VI_SCREEN_BORDER_ROOM);
  attributes.width = MAX (1, (gint)widget->allocation.width - (gint)attributes.x * 2);
  attributes.height = MAX (1, (gint)widget->allocation.height - (gint)attributes.y * 2);
  
  vi->text_area = gdk_window_new (widget->window, &attributes, attributes_mask);
  gdk_window_set_user_data (vi->text_area, vi);
  
  widget->style = gtk_style_attach (widget->style, widget->window);
  
  /* Can't call gtk_style_set_background here because it's handled specially */
  gdk_window_set_background (widget->window, &widget->style->base[GTK_STATE_NORMAL]);
  gdk_window_set_background (vi->text_area, &widget->style->base[GTK_STATE_NORMAL]);

  vi->gc = gdk_gc_new (vi->text_area);
  /* What's this ? */
  gdk_gc_set_exposures (vi->gc, TRUE);
  gdk_gc_set_foreground (vi->gc, &widget->style->text[GTK_STATE_NORMAL]);

  vi->reverse_gc = gdk_gc_new (vi->text_area);
  gdk_gc_set_foreground (vi->reverse_gc, &widget->style->base[GTK_STATE_NORMAL]);

  gdk_window_show (vi->text_area);

  recompute_geometry (vi);
}  

static void
gtk_vi_screen_size_request (GtkWidget      *widget,
		       GtkRequisition *requisition)
{
  gint xthickness;
  gint ythickness;
  gint char_height;
  gint char_width;
  GtkViScreen *vi;
  
  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_VI_SCREEN (widget));
  g_return_if_fail (requisition != NULL);
  
  vi = GTK_VI_SCREEN (widget);

  xthickness = widget->style->klass->xthickness + VI_SCREEN_BORDER_ROOM;
  ythickness = widget->style->klass->ythickness + VI_SCREEN_BORDER_ROOM;
  
  vi->ch_ascent = widget->style->font->ascent;
  vi->ch_height = (widget->style->font->ascent + widget->style->font->descent);
  vi->ch_width = gdk_text_width (widget->style->font, "A", 1);
  char_height = DEFAULT_VI_SCREEN_HEIGHT_LINES * vi->ch_height;
  char_width = DEFAULT_VI_SCREEN_WIDTH_CHARS * vi->ch_width;
  
  requisition->width  = char_width  + xthickness * 2;
  requisition->height = char_height + ythickness * 2;
}

static void
gtk_vi_screen_size_allocate (GtkWidget     *widget,
			GtkAllocation *allocation)
{
  GtkViScreen *vi;
  
  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_VI_SCREEN (widget));
  g_return_if_fail (allocation != NULL);
  
  vi = GTK_VI_SCREEN (widget);
  
  widget->allocation = *allocation;
  if (GTK_WIDGET_REALIZED (widget))
    {
      gdk_window_move_resize (widget->window,
			      allocation->x, allocation->y,
			      allocation->width, allocation->height);
      
      gdk_window_move_resize (vi->text_area,
			      widget->style->klass->xthickness + VI_SCREEN_BORDER_ROOM,
			      widget->style->klass->ythickness + VI_SCREEN_BORDER_ROOM,
			      MAX (1, (gint)widget->allocation.width - (gint)(widget->style->klass->xthickness +
							  (gint)VI_SCREEN_BORDER_ROOM) * 2),
			      MAX (1, (gint)widget->allocation.height - (gint)(widget->style->klass->ythickness +
							   (gint)VI_SCREEN_BORDER_ROOM) * 2));
      
      recompute_geometry (vi);
    }
}

/*
static void
gtk_vi_screen_adjustment (GtkAdjustment *adjustment,
			 GtkViScreen       *vi_screen)
{
  g_return_if_fail (adjustment != NULL);
  g_return_if_fail (GTK_IS_ADJUSTMENT (adjustment));
  g_return_if_fail (vi_screen != NULL);
  g_return_if_fail (GTK_IS_VI_SCREEN (vi_screen));

}
*/
  
static void
gtk_vi_screen_draw (GtkWidget    *widget,
	       GdkRectangle *area)
{
  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_VI_SCREEN (widget));
  g_return_if_fail (area != NULL);
  
  if (GTK_WIDGET_DRAWABLE (widget))
    {
      expose_text (GTK_VI_SCREEN (widget), area, TRUE);
      gtk_widget_draw_focus (widget);
    }
}

static gint
gtk_vi_screen_expose (GtkWidget      *widget,
		 GdkEventExpose *event)
{
  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_VI_SCREEN (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);
  
  if (event->window == GTK_VI_SCREEN (widget)->text_area)
    {
      expose_text (GTK_VI_SCREEN (widget), &event->area, TRUE);
    }
  else if (event->count == 0)
    {
      gtk_widget_draw_focus (widget);
    }
  
  return FALSE;
}

static void
recompute_geometry (GtkViScreen* vi)
{
    //gint xthickness;
    //gint ythickness;
    gint height;
    gint width;
    gint rows, cols;

    //xthickness = widget->style->klass->xthickness + VI_SCREEN_BORDER_ROOM;
    //ythickness = widget->style->klass->ythickness + VI_SCREEN_BORDER_ROOM;

    gdk_window_get_size (vi->text_area, &width, &height);

    rows = height / vi->ch_height;
    cols = width / vi->ch_width;

    if (rows == vi->rows && cols == vi->cols)
	return;

    vi->marked_x = vi->cols = cols;
    vi->marked_y = vi->rows = rows;
    vi->marked_maxx = 0;
    vi->marked_maxy = 0;

    g_free(vi->chars);
    vi->chars = g_new(gchar, vi->rows*vi->cols);
    memset(vi->chars, ' ', vi->rows*vi->cols);
    g_free(vi->reverse);
    vi->reverse = g_new(gchar, vi->rows*vi->cols);
    memset(vi->reverse, 0, vi->rows*vi->cols);

    gtk_signal_emit(GTK_OBJECT(vi), vi_screen_signals[RESIZED], vi->rows, vi->cols);
}

static void
expose_text (GtkViScreen* vi, GdkRectangle *area, gboolean cursor)
{
    gint ymax;
    gint xmax, xmin;

    gdk_window_clear_area (vi->text_area, area->x, area->y, 
			    area->width, area->height);
    ymax = MIN((area->y + area->height + vi->ch_height - 1) / vi->ch_height,
		vi->rows);
    xmin = area->x / vi->ch_width;
    xmax = MIN((area->x + area->width + vi->ch_width - 1) / vi->ch_width,
		vi->cols);
    draw_lines(vi, area->y / vi->ch_height, xmin, ymax, xmax);
}

#define Inverse(screen,y,x) \
    ((*FlagAt(screen,y,x) == COLOR_STANDOUT) ^ \
	(screen->cury == y && screen->curx == x))

static void
draw_lines(GtkViScreen *vi, gint ymin, gint xmin, gint ymax, gint xmax)
{
    gint y, x, len;
    gchar *line;
    GdkGC *fg, *bg;

    for (y = ymin, line = vi->chars + y*vi->cols; 
			     y < ymax; ++y, line += vi->cols) {
	for (x = xmin; x < xmax; x+=len) {
	    gchar inverse;
	    for (inverse = Inverse(vi,y,x), len = 1; 
		    x+len < xmax && Inverse(vi,y,x+len) == inverse; ++len);
	    if (inverse) {
		fg = vi->reverse_gc;
		bg = vi->gc;
	    } else {
		bg = vi->reverse_gc;
		fg = vi->gc;
	    }
	    gdk_draw_rectangle(vi->text_area, bg, 1, x * vi->ch_width,
				y * vi->ch_height, len * vi->ch_width,
				vi->ch_height);
	    gdk_draw_text (vi->text_area, GTK_WIDGET(vi)->style->font, fg,
			    x * vi->ch_width, 
			    y * vi->ch_height + vi->ch_ascent, 
			    line+x, len);
	}
    }
}

static void
mark_lines(GtkViScreen *vi, gint ymin, gint xmin, gint ymax, gint xmax)
{
    if (ymin < vi->marked_y) vi->marked_y = ymin;
    if (xmin < vi->marked_x) vi->marked_x = xmin;
    if (ymax > vi->marked_maxy) vi->marked_maxy = ymax;
    if (xmax > vi->marked_maxx) vi->marked_maxx = xmax;
}