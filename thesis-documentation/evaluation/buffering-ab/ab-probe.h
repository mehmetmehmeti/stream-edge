#pragma once
#include <gst/gst.h>
GstElement *thesis_ab_parse_launch(const gchar *description, GError **error);
#define gst_parse_launch thesis_ab_parse_launch
