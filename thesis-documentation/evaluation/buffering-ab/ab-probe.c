#include "ab-probe.h"
#undef gst_parse_launch
#include <glib-unix.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct { guint64 id; gint64 entered_us; } Entry;
static GMutex lock;
static GHashTable *entries;
static FILE *output;
static GstElement *observed_queue;
static guint64 input_count, output_count, encoded_count;
static gboolean measuring;
static gint64 clock_us(void) { return g_get_monotonic_time(); }
static void marker(const char *name) {
    fprintf(output,"{\"event\":\"%s\",\"mono_us\":%" G_GINT64_FORMAT ",\"utc_us\":%" G_GINT64_FORMAT ",\"input_count\":%" G_GUINT64_FORMAT ",\"output_count\":%" G_GUINT64_FORMAT ",\"encoded_count\":%" G_GUINT64_FORMAT "}\n",name,clock_us(),g_get_real_time(),input_count,output_count,encoded_count);
}
static gboolean begin_measurement(gpointer unused) {
    (void)unused; g_mutex_lock(&lock); measuring=TRUE; marker("measurement_start"); g_mutex_unlock(&lock); return G_SOURCE_CONTINUE;
}
static gboolean end_measurement(gpointer unused) {
    (void)unused; g_mutex_lock(&lock); measuring=FALSE; marker("measurement_end"); g_mutex_unlock(&lock); return G_SOURCE_CONTINUE;
}
static GstPadProbeReturn probe(GstPad *pad,GstPadProbeInfo *info,gpointer data) {
    (void)pad;
    const guint stage=GPOINTER_TO_UINT(data);
    GstBuffer *buffer=GST_PAD_PROBE_INFO_BUFFER(info);
    if(!buffer) return GST_PAD_PROBE_OK;
    const guint64 pts=GST_BUFFER_PTS(buffer);
    const gint64 now=clock_us();
    g_mutex_lock(&lock);
    Entry *entry=NULL;
    if(stage==0) {
        Entry *fresh=g_new(Entry,1); fresh->id=++input_count; fresh->entered_us=now;
        guint64 *key=g_new(guint64,1); *key=pts;
        g_hash_table_replace(entries,key,fresh); entry=fresh;
    } else {
        entry=g_hash_table_lookup(entries,&pts);
        if(stage==1) ++output_count;
        if(stage==3) ++encoded_count;
    }
    const char *names[]={"input","queue_output","consumer_output","encoded"};
    fprintf(output,"{\"event\":\"%s\",\"mono_us\":%" G_GINT64_FORMAT ",\"pts_ns\":%" G_GUINT64_FORMAT ",\"id\":%" G_GUINT64_FORMAT ",\"entered_us\":%" G_GINT64_FORMAT ",\"measurement\":%s,\"matched\":%s}\n",names[stage],now,pts,entry?entry->id:0,entry?entry->entered_us:0,measuring?"true":"false",entry?"true":"false");
    g_mutex_unlock(&lock);
    return GST_PAD_PROBE_OK;
}
static gboolean sample_queue(gpointer unused) {
    (void)unused;
    guint buffers=0,bytes=0; guint64 time_ns=0;
    g_object_get(observed_queue,"current-level-buffers",&buffers,"current-level-bytes",&bytes,"current-level-time",&time_ns,NULL);
    FILE *statm=fopen("/proc/self/statm","r"); unsigned long total=0,resident=0;
    if(statm){ if(fscanf(statm,"%lu %lu",&total,&resident)!=2) resident=0; fclose(statm); }
    g_mutex_lock(&lock);
    fprintf(output,"{\"event\":\"queue_sample\",\"mono_us\":%" G_GINT64_FORMAT ",\"buffers\":%u,\"bytes\":%u,\"queue_time_ns\":%" G_GUINT64_FORMAT ",\"rss_bytes\":%lu,\"measurement\":%s}\n",clock_us(),buffers,bytes,time_ns,resident*(unsigned long)sysconf(_SC_PAGESIZE),measuring?"true":"false");
    /* Guards abort the trial instead of silently imposing a different queue policy. */
    const gboolean unsafe=buffers>300||resident*(unsigned long)sysconf(_SC_PAGESIZE)>768UL*1024UL*1024UL;
    if(unsafe){marker("safety_abort");fflush(output);}
    g_mutex_unlock(&lock);
    if(unsafe) _exit(75);
    return G_SOURCE_CONTINUE;
}
static void attach(GstElement *element,const char *pad_name,guint stage) {
    GstPad *pad=gst_element_get_static_pad(element,pad_name);
    g_assert(pad!=NULL);
    gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,probe,GUINT_TO_POINTER(stage),NULL);
    gst_object_unref(pad);
}
GstElement *thesis_ab_parse_launch(const gchar *description,GError **error) {
    GstElement *pipeline=gst_parse_launch(description,error);
    const char *log_path=g_getenv("THESIS_AB_LOG");
    if(!pipeline||(error&&*error)||!log_path) return pipeline;
    GstElement *queue=gst_bin_get_by_name(GST_BIN(pipeline),"ab_queue");
    GstElement *delay=gst_bin_get_by_name(GST_BIN(pipeline),"ab_delay");
    GstElement *encoder=gst_bin_get_by_name(GST_BIN(pipeline),"ab_encoder");
    if(!queue||!delay||!encoder||output){
        g_set_error_literal(error,GST_CORE_ERROR,GST_CORE_ERROR_FAILED,"A/B named queue/delay/encoder missing or pipeline already instrumented");
        if(queue)gst_object_unref(queue);
        if(delay)gst_object_unref(delay);
        if(encoder)gst_object_unref(encoder);
        gst_object_unref(pipeline); return NULL;
    }
    output=fopen(log_path,"wx");
    if(!output){g_set_error_literal(error,GST_CORE_ERROR,GST_CORE_ERROR_FAILED,"A/B log could not be created exclusively");gst_object_unref(pipeline);return NULL;}
    setvbuf(output,NULL,_IOLBF,0);
    entries=g_hash_table_new_full(g_int64_hash,g_int64_equal,g_free,g_free);
    observed_queue=queue;
    guint max_buffers=0,max_bytes=0;guint64 max_time=0;gint leaky=0;guint sleep_time=0;
    g_object_get(queue,"max-size-buffers",&max_buffers,"max-size-bytes",&max_bytes,"max-size-time",&max_time,"leaky",&leaky,NULL);
    g_object_get(delay,"sleep-time",&sleep_time,NULL);
    fprintf(output,"{\"event\":\"configuration\",\"mono_us\":%" G_GINT64_FORMAT ",\"pid\":%d,\"max_buffers\":%u,\"max_bytes\":%u,\"max_time_ns\":%" G_GUINT64_FORMAT ",\"leaky\":%d,\"consumer_sleep_us\":%u,\"clock\":\"GLib monotonic microseconds, local publisher process\"}\n",clock_us(),(int)getpid(),max_buffers,max_bytes,max_time,leaky,sleep_time);
    attach(queue,"sink",0);attach(queue,"src",1);attach(delay,"src",2);attach(encoder,"src",3);
    gst_object_unref(delay);gst_object_unref(encoder);
    g_unix_signal_add(SIGUSR1,begin_measurement,NULL);
    g_unix_signal_add(SIGUSR2,end_measurement,NULL);
    g_timeout_add(200,sample_queue,NULL);
    return pipeline;
}
