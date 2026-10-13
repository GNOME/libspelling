/* test-view-adapter.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <libspelling.h>

static void
test_context_without_moving_cursor (void)
{
  g_autoptr(SpellingChecker) checker = NULL;
  g_autoptr(SpellingTextBufferAdapter) buffer_adapter = NULL;
  g_autoptr(SpellingTextViewAdapter) view_adapter = NULL;
  g_autoptr(SpellingTextViewAdapter) second_adapter = NULL;
  g_auto(GStrv) corrections = NULL;
  g_auto(GStrv) second_corrections = NULL;
  g_autofree char *text = NULL;
  GtkSourceBuffer *buffer;
  GtkWidget *view;
  GtkWidget *second_view;

  GtkTextIter iter, begin, end;
  int cursor_offset;

  checker = spelling_checker_new (NULL, "en_US");
  corrections = spelling_checker_list_corrections (checker, "helo");
  second_corrections = spelling_checker_list_corrections (checker, "wrld");
  if (corrections == NULL || corrections[0] == NULL ||
      second_corrections == NULL || second_corrections[0] == NULL)
    {
      g_test_skip ("English dictionary with suggestions is unavailable");
      return;
    }

  buffer = gtk_source_buffer_new (NULL);
  gtk_text_buffer_set_text (GTK_TEXT_BUFFER (buffer), "helo wrld", -1);
  view = gtk_source_view_new_with_buffer (buffer);
  g_object_ref_sink (view);
  second_view = gtk_source_view_new_with_buffer (buffer);
  g_object_ref_sink (second_view);
  buffer_adapter = spelling_text_buffer_adapter_new (buffer, checker);
  view_adapter = spelling_text_view_adapter_new (GTK_TEXT_VIEW (view), buffer_adapter);
  second_adapter = spelling_text_view_adapter_new (GTK_TEXT_VIEW (second_view), buffer_adapter);

  gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (buffer), &iter, 9);
  gtk_text_buffer_place_cursor (GTK_TEXT_BUFFER (buffer), &iter);
  cursor_offset = gtk_text_iter_get_offset (&iter);
  gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (buffer), &iter, 1);
  spelling_text_view_adapter_update_at_iter (view_adapter, &iter);
  gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (buffer), &iter, 6);
  spelling_text_view_adapter_update_at_iter (second_adapter, &iter);
  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (buffer),
                                    &iter,
                                    gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (buffer)));
  g_assert_cmpint (gtk_text_iter_get_offset (&iter), ==, cursor_offset);

  g_assert_true (gtk_widget_activate_action (view, "spelling.correct", "s", corrections[0]));
  gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (buffer), &begin, &end);
  text = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (buffer), &begin, &end, FALSE);
  g_assert_true (g_str_has_prefix (text, corrections[0]));
  g_assert_true (g_str_has_suffix (text, " wrld"));
  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (buffer), &iter,
                                    gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (buffer)));
  g_assert_cmpint (gtk_text_iter_get_offset (&iter), ==,
                   cursor_offset + (int)g_utf8_strlen (corrections[0], -1) - 4);

  /* An edit invalidates the other view's captured correction. */
  gtk_widget_activate_action (second_view, "spelling.correct", "s", second_corrections[0]);
  g_clear_pointer (&text, g_free);
  gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (buffer), &begin, &end);
  text = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (buffer), &begin, &end, FALSE);
  g_assert_true (g_str_has_suffix (text, " wrld"));

  g_object_unref (view);
  g_object_unref (second_view);
  g_object_unref (buffer);
}

static void
test_context_cleared_when_checker_replaced (void)
{
  g_autoptr(SpellingChecker) checker = NULL;
  g_autoptr(SpellingChecker) replacement = NULL;
  g_autoptr(SpellingTextBufferAdapter) buffer_adapter = NULL;
  g_autoptr(SpellingTextViewAdapter) view_adapter = NULL;
  g_autoptr(GMenuModel) menu = NULL;
  g_autoptr(GMenuModel) corrections_menu = NULL;
  g_auto(GStrv) corrections = NULL;
  g_autofree char *text = NULL;
  GtkSourceBuffer *buffer;
  GtkWidget *view;
  GtkTextIter iter, begin, end;

  checker = spelling_checker_new (NULL, "en_US");
  replacement = spelling_checker_new (NULL, "en_US");
  corrections = spelling_checker_list_corrections (checker, "helo");
  if (corrections == NULL || corrections[0] == NULL)
    {
      g_test_skip ("English dictionary with suggestions is unavailable");
      return;
    }

  buffer = gtk_source_buffer_new (NULL);
  gtk_text_buffer_set_text (GTK_TEXT_BUFFER (buffer), "helo", -1);
  view = gtk_source_view_new_with_buffer (buffer);
  g_object_ref_sink (view);
  buffer_adapter = spelling_text_buffer_adapter_new (buffer, checker);
  view_adapter = spelling_text_view_adapter_new (GTK_TEXT_VIEW (view), buffer_adapter);

  menu = g_menu_model_get_item_link (spelling_text_view_adapter_get_menu_model (view_adapter),
                                     0, G_MENU_LINK_SECTION);
  corrections_menu = g_menu_model_get_item_link (menu, 0, G_MENU_LINK_SECTION);
  gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (buffer), &iter, 1);
  spelling_text_view_adapter_update_at_iter (view_adapter, &iter);
  g_assert_cmpint (g_menu_model_get_n_items (corrections_menu), >, 0);

  spelling_text_buffer_adapter_set_checker (buffer_adapter, replacement);

  g_assert_cmpint (g_menu_model_get_n_items (corrections_menu), ==, 0);
  g_assert_false (spelling_checker_check_word (replacement, "helo", -1));
  gtk_widget_activate_action (view, "spelling.add", NULL);
  g_assert_false (spelling_checker_check_word (replacement, "helo", -1));
  gtk_widget_activate_action (view, "spelling.ignore", NULL);
  g_assert_false (spelling_checker_check_word (replacement, "helo", -1));
  gtk_widget_activate_action (view, "spelling.correct", "s", corrections[0]);
  gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (buffer), &begin, &end);
  text = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (buffer), &begin, &end, FALSE);
  g_assert_cmpstr (text, ==, "helo");

  g_object_unref (view);
  g_object_unref (buffer);
}

int
main (int   argc,
      char *argv[])
{
  gtk_init ();
  gtk_source_init ();
  spelling_init ();
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/spelling/view-adapter/context", test_context_without_moving_cursor);
  g_test_add_func ("/spelling/view-adapter/checker-replaced", test_context_cleared_when_checker_replaced);

  return g_test_run ();
}
