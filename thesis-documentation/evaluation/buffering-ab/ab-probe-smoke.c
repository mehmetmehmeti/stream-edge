#include "ab-probe.h"
#include <stdio.h>
static GMainLoop *loop;
static gboolean bus_message(GstBus *bus,GstMessage *message,gpointer unused) {
    (void)bus;(void)unused;
    if(GST_MESSAGE_TYPE(message)==GST_MESSAGE_ERROR){GError *error=NULL;gchar *debug=NULL;gst_message_parse_error(message,&error,&debug);fprintf(stderr,"%s\n",error->message);g_clear_error(&error);g_free(debug);g_main_loop_quit(loop);}
    if(GST_MESSAGE_TYPE(message)==GST_MESSAGE_EOS)g_main_loop_quit(loop);
    return TRUE;
}
int main(int argc,char **argv) {
    gst_init(&argc,&argv);
    if(argc!=2)return 2;
    GError *error=NULL;GstElement *pipeline=gst_parse_launch(argv[1],&error);
    if(error||!pipeline){fprintf(stderr,"%s\n",error?error->message:"No pipeline");return 1;}
    loop=g_main_loop_new(NULL,FALSE);
    GstBus *bus=gst_element_get_bus(pipeline);gst_bus_add_watch(bus,bus_message,NULL);gst_object_unref(bus);
    if(gst_element_set_state(pipeline,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE)return 1;
    g_main_loop_run(loop);gst_element_set_state(pipeline,GST_STATE_NULL);gst_object_unref(pipeline);g_main_loop_unref(loop);return 0;
}
