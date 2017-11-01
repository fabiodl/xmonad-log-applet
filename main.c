/* main.c
 *
 * Copyright (c) 2009 Adam Wick
 * Copyright (c) 2011-2012 Alexander Kojevnikov
 * Copyright (c) 2011 Dan Callaghan
 * Copyright (c) 2012 Ari Croock
 * Copyright (c) 2013 Ziga Ham
 * Copyright (c) 2017 Fabio Dalla Libera
 *
 * See LICENSE for licensing information
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <dbus/dbus-glib.h>

#ifdef PANEL_GNOME
#include <panel-applet.h>
#endif
#ifdef PANEL_MATE
#include <mate-panel-applet.h>
#endif
#ifdef PANEL_XFCE4
#include <libxfce4panel/xfce-panel-plugin.h>
#endif

static void signal_handler(DBusGProxy *obj, const char *msg, GtkWidget *widget)
{
    gtk_label_set_markup(GTK_LABEL(widget), msg);
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
	#ifdef PANEL_GNOME_IN_PROC
	return;
	#else
        exit(1);
	#endif
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

#ifndef PANEL_GNOME_IN_PROC    
    panel_applet_set_background_widget(applet, GTK_WIDGET(applet));
#endif
#endif
#ifdef PANEL_MATE
    mate_panel_applet_set_flags(
        applet,
        MATE_PANEL_APPLET_EXPAND_MAJOR |
        MATE_PANEL_APPLET_EXPAND_MINOR |
        MATE_PANEL_APPLET_HAS_HANDLE);

    mate_panel_applet_set_background_widget(applet, GTK_WIDGET(applet));
#endif

    GtkWidget *label = gtk_label_new("Waiting for Xmonad...");
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);

    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label),0.0);
    gtk_label_set_yalign(GTK_LABEL(label),0.5);
    set_up_dbus_transfer(label);

#ifndef PANEL_XFCE4
    gtk_container_add(GTK_CONTAINER(applet), label);
    gtk_widget_show_all(GTK_WIDGET(applet));

    return TRUE;
#else
    gtk_container_add(container, label);
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
	#ifdef PANEL_GNOME_IN_PROC
	return FALSE;
	#else
        exit(-1);
	#endif
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
	#ifdef PANEL_GNOME_IN_PROC
	return;
	#else
        exit(-1);
	#endif
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
#ifdef PANEL_GNOME_IN_PROC
PANEL_APPLET_IN_PROCESS_FACTORY(
#else
PANEL_APPLET_OUT_PROCESS_FACTORY(
#endif				
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
