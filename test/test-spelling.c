/* test-spelling.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <unistd.h>

#include <libspelling.h>

int
main (int   argc,
      char *argv[])
{
  g_autoptr(SpellingTextBufferAdapter) adapter = NULL;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) error = NULL;
  GtkSourceLanguage *language = NULL;
  GtkSourceStyleScheme *scheme = NULL;
  SpellingChecker *checker = NULL;
  GtkScrolledWindow *scroller;
  GtkSourceBuffer *source_buffer;
  GtkSourceView *source_view;
  GMenuModel *extra_menu;
  GtkWindow *window;
  GMainLoop *main_loop;
  g_autofree char *contents = NULL;
  g_autofree char *language_id = NULL;
  g_autofree char *scheme_id = NULL;

  const GOptionEntry entries[] = {
    { "language", 'l', 0, G_OPTION_ARG_STRING, &language_id, "The GtkSourceView language ID to use", "c" },
    { "scheme", 's', 0, G_OPTION_ARG_STRING, &scheme_id, "The GtkSourceView style scheme ID to use", "Adwaita" },
    { 0 }
  };

  gtk_init ();
  gtk_source_init ();
  spelling_init ();

  context = g_option_context_new ("- test spellcheck text-adapter");
  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  if (language_id != NULL)
    {
      language = gtk_source_language_manager_get_language (gtk_source_language_manager_get_default (), language_id);

      if (language == NULL)
        g_warning ("No such GtkSourceView programming language with id \"%s\"", language_id);
    }

  if (scheme_id == NULL)
    scheme_id = g_strdup ("Adwaita");

  scheme = gtk_source_style_scheme_manager_get_scheme (gtk_source_style_scheme_manager_get_default (), scheme_id);

  if (scheme == NULL)
    g_warning ("No such GtkSourceView style scheme with id \"%s\"", scheme_id);

  if (argc == 2)
    g_file_get_contents (argv[1], &contents, NULL, NULL);

  main_loop = g_main_loop_new (NULL, FALSE);
  window = g_object_new (GTK_TYPE_WINDOW,
                         "default-width", 800,
                         "default-height", 600,
                         "title", "Test Spelling",
                         NULL);
  scroller = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
                           "hscrollbar-policy", GTK_POLICY_NEVER,
                           NULL);
  source_buffer = g_object_new (GTK_SOURCE_TYPE_BUFFER,
                                "language", language,
                                "style-scheme", scheme,
                                "enable-undo", TRUE,
                                "text", contents ? contents : "",
                                NULL);
  source_view = g_object_new (GTK_SOURCE_TYPE_VIEW,
                              "buffer", source_buffer,
                              "show-line-numbers", TRUE,
                              "monospace", TRUE,
                              "top-margin", 6,
                              "left-margin", 6,
                              "right-margin", 6,
                              "bottom-margin", 6,
                              NULL);
  gtk_window_set_child (window, GTK_WIDGET (scroller));
  gtk_scrolled_window_set_child (scroller, GTK_WIDGET (source_view));
  g_signal_connect_swapped (window,
                            "close-request",
                            G_CALLBACK (g_main_loop_quit),
                            main_loop);

  /* Setup spellchecking */
  checker = spelling_checker_get_default ();
  adapter = spelling_text_buffer_adapter_new (source_buffer, checker);
  extra_menu = spelling_text_buffer_adapter_get_menu_model (adapter);
  gtk_text_view_set_extra_menu (GTK_TEXT_VIEW (source_view), extra_menu);
  gtk_widget_insert_action_group (GTK_WIDGET (source_view), "spelling", G_ACTION_GROUP (adapter));

  spelling_text_buffer_adapter_set_enabled (adapter, TRUE);

  gtk_window_present (window);
  g_main_loop_run (main_loop);

  return 0;
}
