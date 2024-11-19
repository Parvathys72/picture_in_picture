#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <stdio.h>

#define MAIN_VIDEO_URI "file:///home/mediaclient/Downloads/sample-1.mp4"
#define PIP_VIDEO_URI "file:///home/mediaclient/Downloads/sample-5.mp4"

static void on_pad_added(GstElement *element, GstPad *pad, GstElement *target) {
    GstPad *sinkpad = gst_element_get_static_pad(target, "sink");
    if (!gst_pad_is_linked(sinkpad)) {
        if (gst_pad_link(pad, sinkpad) != GST_PAD_LINK_OK) {
            g_printerr("Failed to link pads: %s -> %s\n", GST_PAD_NAME(pad), GST_PAD_NAME(sinkpad));
        } else {
            g_print("Linked pad: %s -> %s\n", GST_PAD_NAME(pad), GST_PAD_NAME(sinkpad));
        }
    }
    gst_object_unref(sinkpad);
}

int main(int argc, char *argv[]) {
    GstElement *pipeline, *source_main, *source_pip, *decode_main, *decode_pip;
    GstElement *video_convert_main, *video_convert_pip, *video_mixer, *video_sink;
    GstBus *bus;
    GstMessage *msg;

    gst_init(&argc, &argv);

    // Create elements
    pipeline = gst_pipeline_new("pip-pipeline");
    source_main = gst_element_factory_make("uridecodebin", "main-source");
    source_pip = gst_element_factory_make("uridecodebin", "pip-source");
    decode_main = gst_element_factory_make("decodebin", "main-decoder");
    decode_pip = gst_element_factory_make("decodebin", "pip-decoder");
    video_convert_main = gst_element_factory_make("videoconvert", "main-converter");
    video_convert_pip = gst_element_factory_make("videoconvert", "pip-converter");
    video_mixer = gst_element_factory_make("compositor", "video-mixer");
    video_sink = gst_element_factory_make("autovideosink", "video-sink");

    if (!pipeline || !source_main || !source_pip || !decode_main || !decode_pip ||
        !video_convert_main || !video_convert_pip || !video_mixer || !video_sink) {
        g_printerr("Not all elements could be created.\n");
        return -1;
    }

    // Set URIs
    g_object_set(source_main, "uri", MAIN_VIDEO_URI, NULL);
    g_object_set(source_pip, "uri", PIP_VIDEO_URI, NULL);

    // Add elements to pipeline
    gst_bin_add_many(GST_BIN(pipeline), source_main, source_pip, decode_main, decode_pip,
                     video_convert_main, video_convert_pip, video_mixer, video_sink, NULL);

    // Link elements (static parts)
    gst_element_link_many(video_convert_main, video_mixer, NULL);
    gst_element_link_many(video_convert_pip, video_mixer, NULL);
    gst_element_link(video_mixer, video_sink);

    // Connect pad-added signal to handle dynamic pads for decodebin
    g_signal_connect(source_main, "pad-added", G_CALLBACK(on_pad_added), video_convert_main);
    g_signal_connect(source_pip, "pad-added", G_CALLBACK(on_pad_added), video_convert_pip);
    g_signal_connect(decode_main, "pad-added", G_CALLBACK(on_pad_added), video_convert_main);
    g_signal_connect(decode_pip, "pad-added", G_CALLBACK(on_pad_added), video_convert_pip);

    // Request pads from compositor and link them to the converter output
    GstPad *srcpad_main = gst_element_get_static_pad(video_convert_main, "src");
    GstPad *srcpad_pip = gst_element_get_static_pad(video_convert_pip, "src");

    GstPad *sinkpad_main = gst_element_get_request_pad(video_mixer, "sink_0");
    GstPad *sinkpad_pip = gst_element_get_request_pad(video_mixer, "sink_1");

    if (gst_pad_link(srcpad_main, sinkpad_main) != GST_PAD_LINK_OK) {
        g_printerr("Failed to link main video pad to compositor\n");
    }
    if (gst_pad_link(srcpad_pip, sinkpad_pip) != GST_PAD_LINK_OK) {
        g_printerr("Failed to link PiP video pad to compositor\n");
    }

    gst_object_unref(srcpad_main);
    gst_object_unref(srcpad_pip);
    gst_object_unref(sinkpad_main);
    gst_object_unref(sinkpad_pip);

    // Set pipeline to playing
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // Wait for error or end-of-stream
    bus = gst_element_get_bus(pipeline);
    msg = gst_bus_poll(bus, GST_MESSAGE_ERROR | GST_MESSAGE_EOS, GST_CLOCK_TIME_NONE);

    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
        GError *err;
        gchar *debug_info;
        gst_message_parse_error(msg, &err, &debug_info);
        g_printerr("Error: %s\n", err->message);
        g_error_free(err);
        g_free(debug_info);
    }

    gst_message_unref(msg);
    gst_object_unref(bus);

    // Stop pipeline
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return 0;
}
