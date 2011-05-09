/*
 * Mex - a media explorer
 *
 * Copyright © 2011 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses>
 */

/* This is a small webserver using libsoup to pass up control keys to dbus */

#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <libsoup/soup.h>
#include "dbus-interface.h"
#include "ssdp.h"
#include <mex/mex.h>

/* Send response after having done an HTTP post/get */
static void
send_response (SoupServer *server, SoupMessage *msg, const gchar *path)
{
  gchar *file_contents, *uri;
  gsize file_size;
  GError *error=NULL;

  SoupBuffer *buffer;

  if (g_strcmp0 (path, "/") == 0)
    path = "/index.html";

  uri = g_strconcat (mex_get_data_dir(), "/webremote/", path, NULL);
  g_debug ("uri: %s", uri);

  g_file_get_contents (uri, &file_contents, &file_size, &error);
  g_free (uri);

  if (error)
    {
      g_warning ("Could not get file contents: %s\n", error->message);
      g_error_free (error);

      if (file_contents)
        g_free (file_contents);

      soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
      return;
    }

  if (error)
    g_error ("ERROR: %s\n", error->message);

  buffer = soup_buffer_new (SOUP_MEMORY_TAKE, file_contents,
                            strlen (file_contents));

  soup_message_body_append_buffer (msg->response_body, buffer);

  soup_buffer_free (buffer);
  soup_message_set_status (msg, SOUP_STATUS_OK);
  return;
}

static void
http_post (SoupServer *server, SoupMessage *msg, const gchar *path)
{
  HTTPDBusInterface *dbus_interface;
  const gchar *post_request;

  dbus_interface = httpdbus_interface_new ();
  if (dbus_interface == NULL)
  {
    g_error ("failed to initiate dbus interface");
    return;
  }

  post_request = msg->request_body->data;

  g_debug ("post request: %s", post_request);

  if (g_str_has_prefix (post_request, "keyvalue"))
      {
        gint keyvalue;
        keyvalue = atoi ((post_request + strlen ("keyvalue=")));

        httpdbus_send_keyvalue (dbus_interface, keyvalue);
      }

  send_response (server, msg, path);
  return;
}

static void
server_cb (SoupServer *server, SoupMessage *msg,
                 const char *path, GHashTable *query,
                 SoupClientContext *context, gpointer data)
{
  g_debug ("%s %s HTTP/1.%d\n", msg->method, path,
            soup_message_get_http_version (msg));

  if (msg->method == SOUP_METHOD_POST)
    http_post (server, msg, path);
  else if (msg->method == SOUP_METHOD_GET || msg->method == SOUP_METHOD_HEAD)
    send_response (server, msg, path);
  else
    soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
}

static gboolean
auth_cb (SoupAuthDomain *domain, SoupMessage *msg, const gchar *username,
         const gchar *password, gpointer *data)
{
  if (g_strcmp0 (username, data[1]) == 0 && g_strcmp0 (password, data[2]) == 0)
    return TRUE;
  else
    return FALSE;
}


int main (int argc, char *argv[])
{
  SoupServer *server;
  GMainLoop *loop;
  SoupAuthDomain *domain;

  g_type_init ();
  server =  soup_server_new (SOUP_SERVER_PORT, 9090,
                             SOUP_SERVER_SERVER_HEADER, "remote-control",
                             NULL);
  if (!server)
    {
      g_error ("%s\n", "Unable to start server on port 9090");
      return -1;
    }

  g_message ("%s", "Starting server on port 9090");

  soup_server_run_async (server);

  /* use password */
  if (argv[1] && argv[2])
    {

      domain = soup_auth_domain_basic_new (SOUP_AUTH_DOMAIN_REALM,
                                           "Authenticate",
                                           SOUP_AUTH_DOMAIN_BASIC_AUTH_CALLBACK,
                                           auth_cb,
                                           SOUP_AUTH_DOMAIN_BASIC_AUTH_DATA,
                                           argv,
                                           SOUP_AUTH_DOMAIN_ADD_PATH, "/",
                                           NULL);

      soup_server_add_auth_domain (server, domain);
      g_object_unref (domain);
    }

  soup_server_add_handler (server, NULL, server_cb, NULL, NULL);

  ssdp_advertise_server ();

  loop = g_main_loop_new (NULL, TRUE);
  g_main_loop_run (loop);

  return EXIT_SUCCESS;
}
