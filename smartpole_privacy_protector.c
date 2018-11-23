#include <string.h>

#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/video/videooverlay.h>

#include <gdk/gdk.h>
#if defined (GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#elif defined (GDK_WINDOWING_WIN32)
#include <gdk/gdkwin32.h>
#elif defined (GDK_WINDOWING_QUARTZ)
#include <gdk/gdkquartz.h>
#endif

/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData {
  GstElement *playbin;           /* Our one and only pipeline */

  GtkWidget *slider;              /* Slider widget to keep track of current position */
  GtkWidget *streams_list;        /* Text widget to display info about the streams */
  gulong slider_update_signal_id; /* Signal ID for the slider update signal */

  GstState state;                 /* Current state of the pipeline */
  gint64 duration;                /* Duration of the clip, in nanoseconds */
} CustomData;

/* This function is called when the GUI toolkit creates the physical window that will hold the video.
 * At this point we can retrieve its handler (which has a different meaning depending on the windowing system)
 * and pass it to GStreamer through the VideoOverlay interface. */
static void realize_cb (GtkWidget *widget, CustomData *data) {
  GdkWindow *window = gtk_widget_get_window (widget);
  guintptr window_handle;

  if (!gdk_window_ensure_native (window))
    g_error ("Couldn't create native window needed for GstVideoOverlay!");

  /* Retrieve window handler from GDK */
#if defined (GDK_WINDOWING_WIN32)
  window_handle = (guintptr)GDK_WINDOW_HWND (window);
#elif defined (GDK_WINDOWING_QUARTZ)
  window_handle = gdk_quartz_window_get_nsview (window);
#elif defined (GDK_WINDOWING_X11)
  window_handle = GDK_WINDOW_XID (window);
#endif
  /* Pass it to playbin, which implements VideoOverlay and will forward it to the video sink */
  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (data->playbin), window_handle);
}

/* This function is called when the PLAY button is clicked */
static void play_cb (GtkButton *button, CustomData *data) {
  gst_element_set_state (data->playbin, GST_STATE_PLAYING);
}

/* This function is called when the PAUSE button is clicked */
static void pause_cb (GtkButton *button, CustomData *data) {
  gst_element_set_state (data->playbin, GST_STATE_PAUSED);
}

/* This function is called when the STOP button is clicked */
static void stop_cb (GtkButton *button, CustomData *data) {
  gst_element_set_state (data->playbin, GST_STATE_READY);
}

/* This function is called when the main window is closed */
static void delete_event_cb (GtkWidget *widget, GdkEvent *event, CustomData *data) {
  stop_cb (NULL, data);
  gtk_main_quit ();
}

/* This function is called everytime the video window needs to be redrawn (due to damage/exposure,
 * rescaling, etc). GStreamer takes care of this in the PAUSED and PLAYING states, otherwise,
 * we simply draw a black rectangle to avoid garbage showing up. */
static gboolean draw_cb (GtkWidget *widget, cairo_t *cr, CustomData *data) {
  if (data->state < GST_STATE_PAUSED) {
    GtkAllocation allocation;

    /* Cairo is a 2D graphics library which we use here to clean the video window.
     * It is used by GStreamer for other reasons, so it will always be available to us. */
    gtk_widget_get_allocation (widget, &allocation);
    cairo_set_source_rgb (cr, 0, 0, 0);
    cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
    cairo_fill (cr);
  }

  return FALSE;
}

/* This function is called when the slider changes its position. We perform a seek to the
 * new position here. */
static void slider_cb (GtkRange *range, CustomData *data) {
  gdouble value = gtk_range_get_value (GTK_RANGE (data->slider));
  gst_element_seek_simple (data->playbin, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
      (gint64)(value * GST_SECOND));
}

/* This creates all the GTK+ widgets that compose our application, and registers the callbacks */
static void create_ui (CustomData *data) {
  GtkWidget *main_window;  /* The uppermost window, containing all other windows */
  GtkWidget *video_window; /* The drawing area where the video will be shown */
  GtkWidget *main_box;     /* VBox to hold main_hbox and the controls */
  GtkWidget *main_hbox;    /* HBox to hold the video_window and the stream info text widget */
  GtkWidget *controls;     /* HBox to hold the buttons and the slider */
  GtkWidget *play_button, *pause_button, *stop_button; /* Buttons */

  main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (G_OBJECT (main_window), "delete-event", G_CALLBACK (delete_event_cb), data);

  video_window = gtk_drawing_area_new ();
  gtk_widget_set_double_buffered (video_window, FALSE);
  g_signal_connect (video_window, "realize", G_CALLBACK (realize_cb), data);
  g_signal_connect (video_window, "draw", G_CALLBACK (draw_cb), data);

  play_button = gtk_button_new_from_icon_name ("media-playback-start", GTK_ICON_SIZE_SMALL_TOOLBAR);
  g_signal_connect (G_OBJECT (play_button), "clicked", G_CALLBACK (play_cb), data);

  pause_button = gtk_button_new_from_icon_name ("media-playback-pause", GTK_ICON_SIZE_SMALL_TOOLBAR);
  g_signal_connect (G_OBJECT (pause_button), "clicked", G_CALLBACK (pause_cb), data);

  stop_button = gtk_button_new_from_icon_name ("media-playback-stop", GTK_ICON_SIZE_SMALL_TOOLBAR);
  g_signal_connect (G_OBJECT (stop_button), "clicked", G_CALLBACK (stop_cb), data);

  data->slider = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_scale_set_draw_value (GTK_SCALE (data->slider), 0);
  data->slider_update_signal_id = g_signal_connect (G_OBJECT (data->slider), "value-changed", G_CALLBACK (slider_cb), data);

  data->streams_list = gtk_text_view_new ();
  gtk_text_view_set_editable (GTK_TEXT_VIEW (data->streams_list), FALSE);

  controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (controls), play_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), pause_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), stop_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), data->slider, TRUE, TRUE, 2);

  main_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), video_window, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), data->streams_list, FALSE, FALSE, 2);

  main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start (GTK_BOX (main_box), main_hbox, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), controls, FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (main_window), main_box);
  gtk_window_set_default_size (GTK_WINDOW (main_window), 640, 480);

  gtk_widget_show_all (main_window);
}

/* This function is called periodically to refresh the GUI */
static gboolean refresh_ui (CustomData *data) {
  gint64 current = -1;

  /* We do not want to update anything unless we are in the PAUSED or PLAYING states */
  if (data->state < GST_STATE_PAUSED)
    return TRUE;

  /* If we didn't know it yet, query the stream duration */
  if (!GST_CLOCK_TIME_IS_VALID (data->duration)) {
    if (!gst_element_query_duration (data->playbin, GST_FORMAT_TIME, &data->duration)) {
      g_printerr ("Could not query current duration.\n");
    } else {
      /* Set the range of the slider to the clip duration, in SECONDS */
      gtk_range_set_range (GTK_RANGE (data->slider), 0, (gdouble)data->duration / GST_SECOND);
    }
  }

  if (gst_element_query_position (data->playbin, GST_FORMAT_TIME, &current)) {
    /* Block the "value-changed" signal, so the slider_cb function is not called
     * (which would trigger a seek the user has not requested) */
    g_signal_handler_block (data->slider, data->slider_update_signal_id);
    /* Set the position of the slider to the current pipeline positoin, in SECONDS */
    gtk_range_set_value (GTK_RANGE (data->slider), (gdouble)current / GST_SECOND);
    /* Re-enable the signal */
    g_signal_handler_unblock (data->slider, data->slider_update_signal_id);
  }
  return TRUE;
}

/* This function is called when new metadata is discovered in the stream */
static void tags_cb (GstElement *playbin, gint stream, CustomData *data) {
  /* We are possibly in a GStreamer working thread, so we notify the main
   * thread of this event through a message in the bus */
  gst_element_post_message (playbin,
    gst_message_new_application (GST_OBJECT (playbin),
      gst_structure_new_empty ("tags-changed")));
}

/* This function is called when an error message is posted on the bus */
static void error_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GError *err;
  gchar *debug_info;

  /* Print error details on the screen */
  gst_message_parse_error (msg, &err, &debug_info);
  g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
  g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
  g_clear_error (&err);
  g_free (debug_info);

  /* Set the pipeline to READY (which stops playback) */
  gst_element_set_state (data->playbin, GST_STATE_READY);
}

/* This function is called when an End-Of-Stream message is posted on the bus.
 * We just set the pipeline to READY (which stops playback) */
static void eos_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  g_print ("End-Of-Stream reached.\n");
  gst_element_set_state (data->playbin, GST_STATE_READY);
}

/* This function is called when the pipeline changes states. We use it to
 * keep track of the current state. */
static void state_changed_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GstState old_state, new_state, pending_state;
  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->playbin)) { 
    data->state = new_state;
    g_print ("State set to %s\n", gst_element_state_get_name (new_state));
    if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {
      /* For extra responsiveness, we refresh the GUI as soon as we reach the PAUSED state */
      refresh_ui (data);
    }
    /* dump graph for (some) pipeline state changes */

    {
    gchar *state_transition_name;
    state_transition_name = g_strdup_printf ("%s_%s",
            gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
      gchar *dump_name = g_strconcat ("cctv", state_transition_name,
          NULL);
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (data->playbin),
          GST_DEBUG_GRAPH_SHOW_ALL, dump_name);
      g_free (dump_name);
    }

    
  }
}

/* Extract metadata from all the streams and write it to the text widget in the GUI */
static void analyze_streams (CustomData *data) {
  gint i;
  GstTagList *tags;
  gchar *str, *total_str;
  guint rate;
  gint n_video, n_audio, n_text;
  GtkTextBuffer *text;

  /* Clean current contents of the widget */
  text = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->streams_list));
  gtk_text_buffer_set_text (text, "", -1);

  /* Read some properties */
  g_object_get (data->playbin, "n-video", &n_video, NULL);
  g_object_get (data->playbin, "n-audio", &n_audio, NULL);
  g_object_get (data->playbin, "n-text", &n_text, NULL);

  for (i = 0; i < n_video; i++) {
    tags = NULL;
    /* Retrieve the stream's video tags */
    g_signal_emit_by_name (data->playbin, "get-video-tags", i, &tags);
    if (tags) {
      total_str = g_strdup_printf ("video stream %d:\n", i);
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
      gst_tag_list_get_string (tags, GST_TAG_VIDEO_CODEC, &str);
      total_str = g_strdup_printf ("  codec: %s\n", str ? str : "unknown");
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
      g_free (str);
      gst_tag_list_free (tags);
    }
  }

  for (i = 0; i < n_audio; i++) {
    tags = NULL;
    /* Retrieve the stream's audio tags */
    g_signal_emit_by_name (data->playbin, "get-audio-tags", i, &tags);
    if (tags) {
      total_str = g_strdup_printf ("\naudio stream %d:\n", i);
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
      if (gst_tag_list_get_string (tags, GST_TAG_AUDIO_CODEC, &str)) {
        total_str = g_strdup_printf ("  codec: %s\n", str);
        gtk_text_buffer_insert_at_cursor (text, total_str, -1);
        g_free (total_str);
        g_free (str);
      }
      if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
        total_str = g_strdup_printf ("  language: %s\n", str);
        gtk_text_buffer_insert_at_cursor (text, total_str, -1);
        g_free (total_str);
        g_free (str);
      }
      if (gst_tag_list_get_uint (tags, GST_TAG_BITRATE, &rate)) {
        total_str = g_strdup_printf ("  bitrate: %d\n", rate);
        gtk_text_buffer_insert_at_cursor (text, total_str, -1);
        g_free (total_str);
      }
      gst_tag_list_free (tags);
    }
  }

  for (i = 0; i < n_text; i++) {
    tags = NULL;
    /* Retrieve the stream's subtitle tags */
    g_signal_emit_by_name (data->playbin, "get-text-tags", i, &tags);
    if (tags) {
      total_str = g_strdup_printf ("\nsubtitle stream %d:\n", i);
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
      if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
        total_str = g_strdup_printf ("  language: %s\n", str);
        gtk_text_buffer_insert_at_cursor (text, total_str, -1);
        g_free (total_str);
        g_free (str);
      }
      gst_tag_list_free (tags);
    }
  }
}

/* This function is called when an "application" message is posted on the bus.
 * Here we retrieve the message posted by the tags_cb callback */
static void application_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  if (g_strcmp0 (gst_structure_get_name (gst_message_get_structure (msg)), "tags-changed") == 0) {
    /* If the message is the "tags-changed" (only one we are currently issuing), update
     * the stream info GUI */
    //analyze_streams (data);
  }
}

//#define GST_DEBUG_DUMP_DOT_DIR "~/gstreamer_workspace/dot_data"
int _main(int argc, char *argv[]) {
  CustomData data;
  GstStateChangeReturn ret;
  GstBus *bus;
  //GstError *error = NULL;


  /* Initialize GTK */
  gtk_init (&argc, &argv);

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Initialize our data structure */
  memset (&data, 0, sizeof (data));
  data.duration = GST_CLOCK_TIME_NONE;

  //#gst-launch-1.0 rtspsrc location=rtsp://10.100.100.100:8554/test latency=200 ! decodebin ! videoconvert ! faceblur ! videoconvert ! ximagesink

  /* Create the elements */
  //data.playbin = gst_element_factory_make ("playbin", "playbin");
  //data.playbin = gst_pipeline_new ("bin");

  data.playbin = gst_parse_launch("rtspsrc location=rtsp://10.100.100.100:8554/test latency=200 ! decodebin ! videoconvert ! faceblur ! videoconvert ! ximagesink", NULL);

  g_setenv("GST_DEBUG_DUMP_DOT_DIR", "~/gstreamer_workspace/dot_data", 1);
    if (g_getenv ("GST_DEBUG_DUMP_DOT_DIR") != NULL) {
    printf ("SIGHUP: dumping dot file snapshot ...\n");
  } else {
    printf ("SIGHUP: not dumping dot file snapshot, GST_DEBUG_DUMP_DOT_DIR "
        "environment variable not set.\n");
  }


  if (!data.playbin) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }
  //GstElement *src;
  //GstElement *decodebin;
  //GstElement *filter1;
  //GstElement *converter;


  /*
  // rtsp source
  src = gst_element_factory_make ("rtspsrc", "src");
  // CCTV address
  g_object_set (G_OBJECT (src), "location", "rtsp://10.178.134.100:8554/test", NULL);
  // latency 200
  g_object_set (G_OBJECT (src), "latency",200,NULL);
  decodebin = gst_element_factory_make ("openh264dec","decode");  
  filter1 = gst_element_factory_make("capsfilter","filter");
  converter  = gst_element_factory_make ("videoconvert", "converter");

  gst_bin_add_many (GST_BIN (pipeline), source, decoder, converter, sink, NULL); 

  if (!gst_element_link_many (src,  decoder, converter, sink, NULL)) {
    g_print ("Failed to link some elements!\n");
    return -1;
  }
  */
  /* Set the URI to play */
  //g_object_set (data.playbin, "uri", "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm", NULL);
  //g_object_set (data.playbin, "uri", "rtsp://10.178.134.100:8554/test", NULL);

  /* Connect to interesting signals in playbin */
  g_signal_connect (G_OBJECT (data.playbin), "video-tags-changed", (GCallback) tags_cb, &data);
  g_signal_connect (G_OBJECT (data.playbin), "audio-tags-changed", (GCallback) tags_cb, &data);
  g_signal_connect (G_OBJECT (data.playbin), "text-tags-changed", (GCallback) tags_cb, &data);

  /* Create the GUI */
  create_ui (&data);

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  bus = gst_element_get_bus (data.playbin);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, &data);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, &data);
  g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, &data);
  g_signal_connect (G_OBJECT (bus), "message::application", (GCallback)application_cb, &data);
  gst_object_unref (bus);

  /* Start playing */
  ret = gst_element_set_state (data.playbin, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.playbin);
    return -1;
  }

  /* Register a function that GLib will call every second */
  g_timeout_add_seconds (1, (GSourceFunc)refresh_ui, &data);

  /* Start the GTK main loop. We will not regain control until gtk_main_quit is called. */
  gtk_main ();

  /* Free resources */
  gst_element_set_state (data.playbin, GST_STATE_NULL);
  gst_object_unref (data.playbin);
  return 0;
}

static void window_closed (GtkWidget * widget, GdkEvent * event, gpointer user_data)
{
  GstElement *pipeline = user_data;

  gtk_widget_hide (widget);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gtk_main_quit ();
}

// status play -> link rtspsrc
static void on_pad_added (GstElement *element, GstPad *pad, gpointer data) {

	GstPad *sinkpad;
	GstElement *demux = (GstElement *) data;
	sinkpad = gst_element_get_static_pad (demux, "sink");
	gst_pad_link (pad, sinkpad);
	gst_object_unref (sinkpad);
}
static int _g_is_faceblur_onoff = 0; // default off
static void button_faceblur_onoff_func(GtkWidget *widget, gpointer *data )
{
  printf("button_faceblur_onoff_func\r\n");
  GstElement *faceblur = (GstElement *) data;
  if(_g_is_faceblur_onoff == 0)
  {
      //g_object_set (G_OBJECT (faceblur), "display", 1, NULL);
      _g_is_faceblur_onoff = 1;
      gtk_button_set_label(GTK_BUTTON(widget), "face SHOW");
  }
  else
  {
      //g_object_set (G_OBJECT (faceblur), "display", 0, NULL);
      _g_is_faceblur_onoff = 0;
      gtk_button_set_label(GTK_BUTTON(widget), "face HIDE");

  }   
}

static int _g_is_facearea_onoff = 0; // default off
static void button_facearea_onoff_func(GtkWidget *widget, gpointer *data )
{
  printf("button_facearea_onoff_func\r\n");
  GstElement *facedetect = (GstElement *) data;
  if(_g_is_facearea_onoff == 0)
  {
      g_object_set (G_OBJECT (facedetect), "display", 1, NULL);
      _g_is_facearea_onoff = 1;
      gtk_button_set_label(GTK_BUTTON(widget), "faceArea HIDE");
  }
  else
  {
      g_object_set (G_OBJECT (facedetect), "display", 0, NULL);
      _g_is_facearea_onoff = 0;
      gtk_button_set_label(GTK_BUTTON(widget), "faceArea SHOW");

  } 
}
static int _g_is_numberplateblur_onoff = 0; // default off
static void button_numberplateblur_onoff_func(GtkWidget *widget, gpointer *data )
{
  printf("button_numberplateblur_onoff_func\r\n");
  GstElement *numberplateblur = (GstElement *) data;
  if(_g_is_numberplateblur_onoff == 0)
  {
      //g_object_set (G_OBJECT (numberplateblur), "display", 1, NULL);
      _g_is_numberplateblur_onoff = 1;
      gtk_button_set_label(GTK_BUTTON(widget), "numberPlate SHOW");
  }
  else
  {
      //g_object_set (G_OBJECT (numberplateblur), "display", 0, NULL);
      _g_is_numberplateblur_onoff = 0;
      gtk_button_set_label(GTK_BUTTON(widget), "numberPlate HIDE");

  }
}

static void _eos_cb (GstBus *bus, GstMessage *msg, CustomData *data)
{
  g_print ("End-Of-Stream reached.\n");
  GstElement *pipeline = (GstElement *) data;

  g_print ("End-Of-Stream reached.\n");
  gst_element_set_state (pipeline, GST_STATE_READY);
  
  /* restart playback if at end */
  if (!gst_element_seek(pipeline, 
            1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
            GST_SEEK_TYPE_SET,  0, //2 seconds (in nanoseconds)
            GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
    g_print("Seek failed!\n");
  }
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
}

//#gst-launch-1.0 rtspsrc location=rtsp://10.100.100.100:8554/test latency=200 ! decodebin ! videoconvert ! faceblur ! videoconvert ! ximagesink
int main(int argc, char *argv[])
{
  GdkWindow *video_window_xwindow;
  GtkWidget *window, *video_window;
  gulong embed_xid;
  GstStateChangeReturn sret;
  GstBus *bus;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  GstElement *pipeline, *source, *appxrtp, *filter, *typefind, *demux , *parse, *decodebin, *videoConvert, *sink, *decoder;
  GstElement *facedetect, *faceblur, *videoConvert2;

  pipeline = gst_pipeline_new ("cctv player");
  source = gst_element_factory_make ("rtspsrc", "source"); g_assert(source);

  //Set CAPS
  g_object_set (G_OBJECT (source), "location", "rtsp://10.178.134.100:8554/test", NULL);
  g_object_set (G_OBJECT (source), "latency",200,NULL);
  demux  = gst_element_factory_make ("rtph264depay", NULL); g_assert(demux );
  parse = gst_element_factory_make ("h264parse", NULL); g_assert(parse);
  filter = gst_element_factory_make("capsfilter", "filter"); g_assert(filter);
  decodebin = gst_element_factory_make ("avdec_h264", NULL); g_assert(decodebin);
  videoConvert = gst_element_factory_make ("videoconvert", NULL); g_assert(videoConvert);
  videoConvert2 = gst_element_factory_make ("videoconvert", NULL); g_assert(videoConvert2);
  sink = gst_element_factory_make ("ximagesink", NULL); g_assert(sink);
  //sink = gst_element_factory_make ("autovideosink", NULL); g_assert(sink);
  faceblur = gst_element_factory_make ("faceblur", NULL); g_assert(sink);
  facedetect = gst_element_factory_make ("facedetect", NULL); g_assert(sink);

  //ADD
  //gst_bin_add_many (GST_BIN (pipeline), source, demux , parse, filter, decodebin, videoConvert, faceblur, facedetect, videoConvert2, sink, NULL);
  gst_bin_add_many (GST_BIN (pipeline), source, demux , parse, filter, decodebin, videoConvert, facedetect, videoConvert2, sink, NULL);

   // listen for newly created pads
  //g_signal_connect(source, "pad-added", G_CALLBACK(on_pad_added),demux );
  g_signal_connect_object(source, "pad-added", G_CALLBACK(on_pad_added), demux, G_CONNECT_AFTER);
  //LINK
// if(!gst_element_link_many(demux , parse, filter, decodebin, videoConvert, faceblur, videoConvert2, sink,NULL))
 if(!gst_element_link_many(demux , parse, filter, decodebin, videoConvert, facedetect, videoConvert2, sink,NULL))
    printf("\nFailed to link parse to sink");

  /* prepare the ui */
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);   

  g_signal_connect (G_OBJECT (window), "delete-event", G_CALLBACK (window_closed), (gpointer) pipeline);
  gtk_window_set_default_size (GTK_WINDOW (window), 600, 500);
  gtk_window_set_title (GTK_WINDOW (window), "gstreamer opencv based CCTV demo");

  GtkWidget *vbox;
  GtkWidget *hbox;
  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

  gtk_container_add(GTK_CONTAINER(window), vbox);

  /* buttons */
  GtkWidget *button_faceblur_onoff;
  button_faceblur_onoff = gtk_button_new_with_label ("face HIDE");
  gtk_widget_set_size_request(button_faceblur_onoff, 300, 80);

  g_signal_connect (button_faceblur_onoff, "clicked",
                      G_CALLBACK (button_faceblur_onoff_func), faceblur);

  GtkWidget *button_facearea_onoff;
  button_facearea_onoff = gtk_button_new_with_label ("faceArea SHOW");
  gtk_widget_set_size_request(button_facearea_onoff, 300, 80);

  g_signal_connect (button_facearea_onoff, "clicked",
                      G_CALLBACK (button_facearea_onoff_func), (gpointer) facedetect);
  g_object_set (G_OBJECT (facedetect), "display", 0,NULL);


  GtkWidget *button_numberplateblur_onoff;
  button_numberplateblur_onoff = gtk_button_new_with_label ("numberPlate HIDE");
  gtk_widget_set_size_request(button_numberplateblur_onoff, 300, 80);

  g_signal_connect (button_numberplateblur_onoff, "clicked",
                      G_CALLBACK (button_numberplateblur_onoff_func), NULL);//(gpointer) image_detect);

  /* video drawing area */
  video_window = gtk_drawing_area_new ();
  //gtk_container_add (GTK_CONTAINER (window), video_window);
  gtk_box_pack_start(GTK_BOX(vbox), video_window, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(hbox), button_faceblur_onoff, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), button_facearea_onoff, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), button_numberplateblur_onoff, TRUE, TRUE, 0);

  gtk_container_set_border_width (GTK_CONTAINER (window), 2);
  gtk_widget_show_all (window);

  video_window_xwindow = gtk_widget_get_window (video_window);
  embed_xid = GDK_WINDOW_XID (video_window_xwindow);
  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (sink), embed_xid);

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (bus);
  //g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, &data);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, &pipeline);
  //g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, &data);
  //g_signal_connect (G_OBJECT (bus), "message::application", (GCallback)application_cb, &data);
  gst_object_unref (bus);
  
  /* run the pipeline */
  sret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (sret == GST_STATE_CHANGE_FAILURE)
    gst_element_set_state (pipeline, GST_STATE_NULL);
  else
    gtk_main ();

  gst_object_unref (pipeline);


return 0;
}
