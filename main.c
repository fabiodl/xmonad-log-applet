/* main.c
 *
 * Copyright (c) 2009 Adam Wick
 * Copyright (c) 2011-2012 Alexander Kojevnikov
 * Copyright (c) 2011 Dan Callaghan
 * Copyright (c) 2012 Ari Croock
 * Copyright (c) 2013 Ziga Ham
 *
 * See LICENSE for licensing information
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus.h>
#include <xcb/xcb_ewmh.h>

#ifdef PANEL_GNOME
#include <panel-applet.h>
#endif
#ifdef PANEL_MATE
#include <mate-panel-applet.h>
#endif
#ifdef PANEL_XFCE4
#include <libxfce4panel/xfce-panel-plugin.h>
#endif

// Misc
static void pixbuff_free(guchar *pixels, gpointer data) {
    free(pixels);
}

// XCB
xcb_connection_t *c;
xcb_ewmh_connection_t ewmh;
int screen_number;
static void xcb_init() {
    xcb_intern_atom_cookie_t *atom_cookie;

    // Connect to X and request the icon
    c = xcb_connect(NULL, &screen_number);
    atom_cookie = xcb_ewmh_init_atoms(c, &ewmh);
    if (atom_cookie == NULL) {
        fprintf(stderr, "Can not request atoms\n");
        xcb_disconnect(c);
        return;
    }

    if (!xcb_ewmh_init_atoms_replies(&ewmh, atom_cookie, NULL)) {
        fprintf(stderr, "Can not get atom replies\n");
        xcb_disconnect(c);
        return;
    }
}

// Get icon using XCB
static GtkWidget* get_icon(long xid)
{
    xcb_window_t w = xid;
    xcb_get_property_cookie_t prop_cookie;
    xcb_ewmh_get_wm_icon_reply_t wm_icon;
    int i;

    prop_cookie = xcb_ewmh_get_wm_icon(&ewmh, w);
    if (!xcb_ewmh_get_wm_icon_reply(&ewmh, prop_cookie, &wm_icon, NULL)) {
        fprintf(stderr, "Can not get icon (window got no icon?)\n");
        return NULL;
    }

    // Take the first icon (sometimes the icon is in multiple resolutions)
    xcb_ewmh_wm_icon_iterator_t iterator = xcb_ewmh_get_wm_icon_iterator(&wm_icon);

    if (iterator.data == NULL) {
        // No icon
        return NULL;
    }

    // RGBA <-> BGRA
    char* data_source = (char*)iterator.data;
    guchar* data = malloc(iterator.width * iterator.height * 4);
    for (i = 0; i < iterator.width * iterator.height; i++) {
        data[4 * i + 0] = data_source[4 * i + 2];
        data[4 * i + 1] = data_source[4 * i + 1];
        data[4 * i + 2] = data_source[4 * i + 0];
        data[4 * i + 3] = data_source[4 * i + 3];
    }

    // Scale the icon
    GdkPixbuf *pixbuf_orig = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, TRUE, 8, iterator.width, iterator.height, iterator.width * 4, pixbuff_free, NULL);
    GdkPixbuf *pixbuf = gdk_pixbuf_scale_simple(pixbuf_orig, 20, 20, GDK_INTERP_BILINEAR);
    g_object_unref(G_OBJECT(pixbuf_orig)); // original data is not used anymore now

    // Create GtkImage from GdkPixbuf
    GtkWidget *widget = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(G_OBJECT(pixbuf)); // GtkImage doesn't assume our reference

    // Cleanup
    xcb_ewmh_get_wm_icon_reply_wipe(&wm_icon);

    return widget;
}

// Change active window on icon click
static void icon_click(GtkWidget *widget, GdkEventButton *event, gpointer xid) {
    xcb_ewmh_request_change_active_window(&ewmh, screen_number, (xcb_window_t)xid, XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER, XCB_CURRENT_TIME, XCB_NONE);
    xcb_flush(c);
}

// DBUS
static void signal_handler(DBusGProxy *obj, const char *msg, GtkWidget *container)
{
    // Clear container
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(container));
    for(iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    // Parse msg and repopulate container
    char *m = (char *)msg;
    while (*m != 0) {
        if (*m == '|') {
            m++;
            break;
        }

        GdkColor color;
        switch (*m) {
            case '*':
                // Current
                gdk_color_parse("green", &color);
                break;
            case '+':
                // Visible
                gdk_color_parse("yellow", &color);
                break;
            case '-':
                // Hidden
                gdk_color_parse("white", &color);
                break;
            case '!':
                // Urgent
                gdk_color_parse("red", &color);
                break;
            default:
                return;
        }
        m++;

        long workspace_id = strtol(m, &m, 0);

        if (*m != ':' || workspace_id == 0) {
            return;
        }
        m++;

        GtkWidget *box = gtk_hbox_new(FALSE,2);
        gtk_container_add(GTK_CONTAINER(container), box);

        char str[10];
        snprintf(str, 10, "%ld:", workspace_id);
        GtkWidget *label = gtk_label_new(str);
        gtk_widget_modify_fg(GTK_WIDGET(label), GTK_STATE_NORMAL, &color);
        gtk_container_add(GTK_CONTAINER(box), label);

        while (*m != 0) {
            long xid = strtol(m, &m, 0);

            if (xid != 0) {
                GtkWidget *icon = get_icon(xid);
                if (icon != NULL) {
                    GtkWidget *eventbox = gtk_event_box_new();
                    gtk_event_box_set_visible_window(GTK_EVENT_BOX(eventbox), FALSE);
                    gtk_container_add(GTK_CONTAINER(eventbox), icon);
                    gtk_container_add(GTK_CONTAINER(box), eventbox);
                    g_signal_connect(G_OBJECT(eventbox), "button_press_event", G_CALLBACK(icon_click), (gpointer)xid);
                }
            }

            if (*m == ';') {
                m++;
                break;
            }
            if (*m != ',') {
                return;
            }
            m++;
        }
    }

    // Window tittle
    GtkWidget *label = gtk_label_new(m);
    gtk_container_add(GTK_CONTAINER(container), label);

    gtk_widget_show_all(container);
}

static void set_up_dbus_transfer(GtkWidget *buf)
{
    DBusGConnection *connection;
    DBusGProxy *proxy;
    GError *error= NULL;

    connection = dbus_g_bus_get(DBUS_BUS_SESSION, &error);
    if(connection == NULL) {
        g_printerr("Failed to open connection: %s\n", error->message);
        g_error_free(error);
        exit(1);
    }

    proxy = dbus_g_proxy_new_for_name(
        connection, "org.xmonad.Log", "/org/xmonad/Log", "org.xmonad.Log");
    error = NULL;

    dbus_g_proxy_add_signal(proxy, "Update", G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(
        proxy, "Update", (GCallback)signal_handler, buf, NULL);
}


#ifdef PANEL_GNOME
static gboolean xmonad_log_applet_fill(PanelApplet *applet)
#endif
#ifdef PANEL_MATE
static gboolean xmonad_log_applet_fill(MatePanelApplet *applet)
#endif
#ifdef PANEL_XFCE4
static void xmonad_log_applet_fill(GtkContainer *container)
#endif
{
#ifdef PANEL_GNOME
    panel_applet_set_flags(
        applet,
        PANEL_APPLET_EXPAND_MAJOR |
        PANEL_APPLET_EXPAND_MINOR |
        PANEL_APPLET_HAS_HANDLE);

    panel_applet_set_background_widget(applet, GTK_WIDGET(applet));
#endif
#ifdef PANEL_MATE
    mate_panel_applet_set_flags(
        applet,
        MATE_PANEL_APPLET_EXPAND_MAJOR |
        MATE_PANEL_APPLET_EXPAND_MINOR |
        MATE_PANEL_APPLET_HAS_HANDLE);

    mate_panel_applet_set_background_widget(applet, GTK_WIDGET(applet));
#endif

    GtkWidget *box = gtk_hbox_new(FALSE, 8);
    xcb_init();
    set_up_dbus_transfer(box);

#ifndef PANEL_XFCE4
    gtk_container_add(GTK_CONTAINER(applet), box);
    gtk_widget_show_all(GTK_WIDGET(applet));

    return TRUE;
#else
    gtk_container_add(container, box);
#endif
}

#ifdef PANEL_GNOME
static gboolean xmonad_log_applet_factory(
    PanelApplet *applet, const gchar *iid, gpointer data)
{
    gboolean retval = FALSE;

    if(!strcmp(iid, "XmonadLogApplet"))
        retval = xmonad_log_applet_fill(applet);

    if(retval == FALSE) {
        printf("Wrong applet!\n");
        exit(-1);
    }

    return retval;
}
#endif
#ifdef PANEL_MATE
static gboolean xmonad_log_applet_factory(
    MatePanelApplet *applet, const gchar *iid, gpointer data)
{
    gboolean retval = FALSE;

    if(!strcmp(iid, "XmonadLogApplet"))
        retval = xmonad_log_applet_fill(applet);

    if(retval == FALSE) {
        printf("Wrong applet!\n");
        exit(-1);
    }

    return retval;
}
#endif
#ifdef PANEL_XFCE4
static void xmonad_log_applet_construct(XfcePanelPlugin *plugin)
{
    xmonad_log_applet_fill(GTK_CONTAINER(plugin));
    xfce_panel_plugin_set_expand(plugin, TRUE);
    gtk_widget_show_all(GTK_WIDGET(plugin));
}
#endif

#ifdef PANEL_GNOME
PANEL_APPLET_OUT_PROCESS_FACTORY(
    "XmonadLogAppletFactory",
    PANEL_TYPE_APPLET,
#ifdef PANEL_GNOME2
    "XmonadLogApplet",
#endif
    xmonad_log_applet_factory,
    NULL);
#endif
#ifdef PANEL_MATE
MATE_PANEL_APPLET_OUT_PROCESS_FACTORY(
    "XmonadLogAppletFactory",
    PANEL_TYPE_APPLET,
    "XmonadLogApplet",
    xmonad_log_applet_factory,
    NULL);
#endif
#ifdef PANEL_XFCE4
XFCE_PANEL_PLUGIN_REGISTER_EXTERNAL(
    xmonad_log_applet_construct);
#endif
