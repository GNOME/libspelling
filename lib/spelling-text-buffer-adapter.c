/*
 * spelling-text-buffer-adapter.c
 *
 * Copyright 2021-2023 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "cjhtextregionprivate.h"
#include "egg-action-group.h"

#include "spelling-compat-private.h"
#include "spelling-checker.h"
#include "spelling-cursor-private.h"
#include "spelling-menu-private.h"
#include "spelling-text-buffer-adapter.h"

#define RUN_UNCHECKED      GSIZE_TO_POINTER(0)
#define RUN_CHECKED        GSIZE_TO_POINTER(1)
#define UPDATE_DELAY_MSECS 100
#define UPDATE_QUANTA_USEC (G_USEC_PER_SEC/1000L*2) /* 2 msec */
#define MAX_WORD_CHARS     100
/* Keyboard repeat is 30 msec by default (see
 * org.gnome.desktop.peripherals.keyboard repeat-interval) so
 * we want something longer than that so we are likely
 * to get removed/re-added on each repeat movement.
 */
#define INVALIDATE_DELAY_MSECS 100

typedef struct
{
  gint64   deadline;
  guint    has_unchecked : 1;
} Update;

typedef struct
{
  gsize offset;
  guint found : 1;
} ScanForUnchecked;

struct _SpellingTextBufferAdapter
{
  GObject          parent_instance;

  GSignalGroup    *buffer_signals;
  GtkSourceBuffer *buffer;
  SpellingChecker *checker;
  CjhTextRegion   *region;
  GtkTextTag      *no_spell_check_tag;
  GMenuModel      *menu;
  char            *word_under_cursor;

  /* Borrowed pointers */
  GtkTextMark     *insert_mark;
  GtkTextTag      *tag;

  guint            cursor_position;
  guint            incoming_cursor_position;
  guint            queued_cursor_moved;
  guint            commit_handler;

  gsize            update_source;

  guint            enabled : 1;
};

static void spelling_add_action      (SpellingTextBufferAdapter *self,
                                      GVariant                  *param);
static void spelling_ignore_action   (SpellingTextBufferAdapter *self,
                                      GVariant                  *param);
static void spelling_enabled_action  (SpellingTextBufferAdapter *self,
                                      GVariant                  *param);
static void spelling_correct_action  (SpellingTextBufferAdapter *self,
                                      GVariant                  *param);
static void spelling_language_action (SpellingTextBufferAdapter *self,
                                      GVariant                  *param);

EGG_DEFINE_ACTION_GROUP (SpellingTextBufferAdapter, spelling_text_buffer_adapter, {
  { "add", spelling_add_action, "s" },
  { "correct", spelling_correct_action, "s" },
  { "enabled", spelling_enabled_action, NULL, "false" },
  { "ignore", spelling_ignore_action, "s" },
  { "language", spelling_language_action, "s", "''" },
})

G_DEFINE_FINAL_TYPE_WITH_CODE (SpellingTextBufferAdapter, spelling_text_buffer_adapter, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_ACTION_GROUP, spelling_text_buffer_adapter_init_action_group))

enum {
  PROP_0,
  PROP_BUFFER,
  PROP_CHECKER,
  PROP_ENABLED,
  PROP_LANGUAGE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static inline gboolean
forward_word_end (SpellingTextBufferAdapter *self,
                  GtkTextIter               *iter)
{
  return spelling_iter_forward_word_end (iter,
                                             spelling_checker_get_extra_word_chars (self->checker));
}

static inline gboolean
backward_word_start (SpellingTextBufferAdapter *self,
                     GtkTextIter               *iter)
{
  return spelling_iter_backward_word_start (iter,
                                                spelling_checker_get_extra_word_chars (self->checker));
}

static gboolean
get_current_word (SpellingTextBufferAdapter *self,
                  GtkTextIter               *begin,
                  GtkTextIter               *end)
{
  if (gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (self->buffer), begin, end))
    return FALSE;

  if (gtk_text_iter_ends_word (end))
    {
      backward_word_start (self, begin);
      return TRUE;
    }

  if (!gtk_text_iter_starts_word (begin))
    {
      if (!gtk_text_iter_inside_word (begin))
        return FALSE;

      backward_word_start (self, begin);
    }

  if (!gtk_text_iter_ends_word (end))
    forward_word_end (self, end);

  return TRUE;
}

static gboolean
get_word_at_position (SpellingTextBufferAdapter *self,
                      guint                      position,
                      GtkTextIter               *begin,
                      GtkTextIter               *end)
{
  gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (self->buffer), begin, position);
  *end = *begin;

  if (gtk_text_iter_ends_word (end))
    {
      backward_word_start (self, begin);
      return TRUE;
    }

  if (!gtk_text_iter_starts_word (begin))
    {
      if (!gtk_text_iter_inside_word (begin))
        return FALSE;

      backward_word_start (self, begin);
    }

  if (!gtk_text_iter_ends_word (end))
    forward_word_end (self, end);

  return TRUE;
}

SpellingTextBufferAdapter *
spelling_text_buffer_adapter_new (GtkSourceBuffer *buffer,
                                  SpellingChecker *checker)
{
  g_return_val_if_fail (GTK_SOURCE_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (!checker || SPELLING_IS_CHECKER (checker), NULL);

  return g_object_new (SPELLING_TYPE_TEXT_BUFFER_ADAPTER,
                       "buffer", buffer,
                       "checker", checker,
                       NULL);
}

static gboolean
get_unchecked_start_cb (gsize                   offset,
                        const CjhTextRegionRun *run,
                        gpointer                user_data)
{
  gsize *pos = user_data;

  if (run->data == RUN_UNCHECKED)
    {
      *pos = offset;
      return TRUE;
    }

  return FALSE;
}

static gboolean
get_unchecked_start (CjhTextRegion *region,
                     GtkTextBuffer *buffer,
                     GtkTextIter   *iter)
{
  gsize pos = G_MAXSIZE;
  _cjh_text_region_foreach (region, get_unchecked_start_cb, &pos);
  if (pos == G_MAXSIZE)
    return FALSE;
  gtk_text_buffer_get_iter_at_offset (buffer, iter, pos);
  return TRUE;
}

static gboolean
spelling_text_buffer_adapter_update_range (SpellingTextBufferAdapter *self,
                                           gint64                     deadline)
{
  g_autoptr(SpellingCursor) cursor = NULL;
  GtkTextIter word_begin, word_end, begin;
  const char *extra_word_chars;
  gboolean ret = FALSE;

  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));

#if GTK_SOURCE_CHECK_VERSION(5,9,0)
  /* Ignore while we are loading */
  if (gtk_source_buffer_get_loading (self->buffer))
    return TRUE;
#endif

  extra_word_chars = spelling_checker_get_extra_word_chars (self->checker);
  cursor = spelling_cursor_new (GTK_TEXT_BUFFER (self->buffer),
                                self->region,
                                self->no_spell_check_tag,
                                extra_word_chars);

  /* Get the first unchecked position so that we can remove the tag
   * from it up to the first word match.
   */
  if (!get_unchecked_start (self->region, GTK_TEXT_BUFFER (self->buffer), &begin))
    {
      _cjh_text_region_replace (self->region,
                                0,
                                _cjh_text_region_get_length (self->region),
                                RUN_CHECKED);
      return FALSE;
    }

  while (spelling_cursor_next (cursor, &word_begin, &word_end))
    {
      g_autofree char *word = gtk_text_iter_get_slice (&word_begin, &word_end);

      if (!spelling_checker_check_word (self->checker, word, -1))
        gtk_text_buffer_apply_tag (GTK_TEXT_BUFFER (self->buffer),
                                   self->tag,
                                   &word_begin, &word_end);

      if (deadline < g_get_monotonic_time ())
        {
          ret = TRUE;
          break;
        }
    }

  _cjh_text_region_replace (self->region,
                            gtk_text_iter_get_offset (&begin),
                            gtk_text_iter_get_offset (&word_end) - gtk_text_iter_get_offset (&begin),
                            RUN_CHECKED);

  /* Now remove any tag for the current word to be less annoying */
  if (get_current_word (self, &word_begin, &word_end))
    gtk_text_buffer_remove_tag (GTK_TEXT_BUFFER (self->buffer),
                                self->tag,
                                &word_begin, &word_end);

  return ret;
}

static gboolean
spelling_text_buffer_adapter_run (gint64   deadline,
                                  gpointer user_data)
{
  SpellingTextBufferAdapter *self = user_data;

  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));

  if (!spelling_text_buffer_adapter_update_range (self, deadline))
    {
      self->update_source = 0;
      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

static void
spelling_text_buffer_adapter_queue_update (SpellingTextBufferAdapter *self)
{
  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));

  if (self->checker == NULL || self->buffer == NULL || !self->enabled)
    {
      gtk_source_scheduler_clear (&self->update_source);
      return;
    }

  if (self->update_source == 0)
    self->update_source = gtk_source_scheduler_add (spelling_text_buffer_adapter_run, self);
}

void
spelling_text_buffer_adapter_invalidate_all (SpellingTextBufferAdapter *self)
{
  GtkTextIter begin, end;
  gsize length;

  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));

  if (!self->enabled)
    return;

  /* We remove using the known length from the region */
  if ((length = _cjh_text_region_get_length (self->region)) > 0)
    {
      _cjh_text_region_remove (self->region, 0, length - 1);
      spelling_text_buffer_adapter_queue_update (self);
    }

  /* We add using the length from the buffer because if we were not
   * enabled previously, the textregion would be empty.
   */
  gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (self->buffer), &begin, &end);
  if (!gtk_text_iter_equal (&begin, &end))
    {
      length = gtk_text_iter_get_offset (&end) - gtk_text_iter_get_offset (&begin);
      _cjh_text_region_insert (self->region, 0, length, RUN_UNCHECKED);
      gtk_text_buffer_remove_tag (GTK_TEXT_BUFFER (self->buffer), self->tag, &begin, &end);
    }
}

static void
on_tag_added_cb (SpellingTextBufferAdapter *self,
                 GtkTextTag                *tag,
                 GtkTextTagTable           *tag_table)
{
  g_autofree char *name = NULL;

  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));
  g_assert (GTK_IS_TEXT_TAG (tag));
  g_assert (GTK_IS_TEXT_TAG_TABLE (tag_table));

  g_object_get (tag,
                "name", &name,
                NULL);

  if (name && strcmp (name, "gtksourceview:context-classes:no-spell-check") == 0)
    {
      g_set_object (&self->no_spell_check_tag, tag);
      spelling_text_buffer_adapter_invalidate_all (self);
    }
}

static void
on_tag_removed_cb (SpellingTextBufferAdapter *self,
                   GtkTextTag                *tag,
                   GtkTextTagTable           *tag_table)
{
  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));
  g_assert (GTK_IS_TEXT_TAG (tag));
  g_assert (GTK_IS_TEXT_TAG_TABLE (tag_table));

  if (tag == self->no_spell_check_tag)
    {
      g_clear_object (&self->no_spell_check_tag);
      spelling_text_buffer_adapter_invalidate_all (self);
    }
}

static void
invalidate_tag_region_cb (SpellingTextBufferAdapter *self,
                          GtkTextTag                *tag,
                          GtkTextIter               *begin,
                          GtkTextIter               *end,
                          GtkTextBuffer             *buffer)
{
  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));
  g_assert (GTK_IS_TEXT_TAG (tag));
  g_assert (GTK_IS_TEXT_BUFFER (buffer));

  if (!self->enabled)
    return;

  if (tag == self->no_spell_check_tag)
    {
      gsize begin_offset = gtk_text_iter_get_offset (begin);
      gsize end_offset = gtk_text_iter_get_offset (end);

      _cjh_text_region_replace (self->region, begin_offset, end_offset - begin_offset, RUN_UNCHECKED);
      spelling_text_buffer_adapter_queue_update (self);
    }
}

static void
apply_error_style_cb (GtkSourceBuffer *buffer,
                      GParamSpec      *pspec,
                      GtkTextTag      *tag)
{
  GtkSourceStyleScheme *scheme;
  GtkSourceStyle *style;
  static GdkRGBA error_color;

  g_assert (GTK_SOURCE_IS_BUFFER (buffer));
  g_assert (GTK_IS_TEXT_TAG (tag));

  if G_UNLIKELY (error_color.alpha == .0)
    gdk_rgba_parse (&error_color, "#e01b24");

  g_object_set (tag,
                "underline", PANGO_UNDERLINE_ERROR_LINE,
                "underline-rgba", &error_color,
                "background-set", FALSE,
                "foreground-set", FALSE,
                "weight-set", FALSE,
                "variant-set", FALSE,
                "style-set", FALSE,
                "indent-set", FALSE,
                "size-set", FALSE,
                NULL);

  if ((scheme = gtk_source_buffer_get_style_scheme (buffer)))
    {
      if ((style = gtk_source_style_scheme_get_style (scheme, "def:misspelled-word")))
        gtk_source_style_apply (style, tag);
    }
}

static void
mark_unchecked (SpellingTextBufferAdapter *self,
                guint                      offset,
                guint                      length)
{
  GtkTextBuffer *buffer;
  GtkTextIter begin, end;

  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));
  g_assert (GTK_IS_TEXT_BUFFER (self->buffer));
  g_assert (self->enabled);

  buffer = GTK_TEXT_BUFFER (self->buffer);

  gtk_text_buffer_get_iter_at_offset (buffer, &begin, offset);
  backward_word_start (self, &begin);

  gtk_text_buffer_get_iter_at_offset (buffer, &end, offset + length);
  forward_word_end (self, &end);

  if (!gtk_text_iter_equal (&begin, &end))
    {
      _cjh_text_region_replace (self->region,
                                gtk_text_iter_get_offset (&begin),
                                gtk_text_iter_get_offset (&end) - gtk_text_iter_get_offset (&begin),
                                RUN_UNCHECKED);
      gtk_text_buffer_remove_tag (buffer, self->tag, &begin, &end);

      spelling_text_buffer_adapter_queue_update (self);
    }
}

static void
spelling_text_buffer_adapter_commit_notify (GtkTextBuffer            *buffer,
                                            GtkTextBufferNotifyFlags  flags,
                                            guint                     position,
                                            guint                     length,
                                            gpointer                  user_data)
{
  SpellingTextBufferAdapter *self = user_data;

  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));
  g_assert (GTK_IS_TEXT_BUFFER (buffer));

  if (!self->enabled)
    return;

  if (flags == GTK_TEXT_BUFFER_NOTIFY_BEFORE_INSERT)
    _cjh_text_region_insert (self->region, position, length, RUN_UNCHECKED);
  else if (flags == GTK_TEXT_BUFFER_NOTIFY_AFTER_INSERT)
    mark_unchecked (self, position, length);
  else if (flags == GTK_TEXT_BUFFER_NOTIFY_BEFORE_DELETE)
    _cjh_text_region_remove (self->region, position, length);
  else if (flags == GTK_TEXT_BUFFER_NOTIFY_AFTER_DELETE)
    mark_unchecked (self, position, 0);
}

static void
spelling_text_buffer_adapter_set_buffer (SpellingTextBufferAdapter *self,
                                         GtkSourceBuffer           *buffer)
{
  GtkTextIter begin, end;
  GtkTextTagTable *tag_table;
  guint offset;
  guint length;

  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));
  g_assert (GTK_SOURCE_IS_BUFFER (buffer));
  g_assert (self->buffer == NULL);

  g_set_weak_pointer (&self->buffer, buffer);

  self->commit_handler =
    gtk_text_buffer_add_commit_notify (GTK_TEXT_BUFFER (self->buffer),
                                       (GTK_TEXT_BUFFER_NOTIFY_BEFORE_INSERT |
                                        GTK_TEXT_BUFFER_NOTIFY_AFTER_INSERT |
                                        GTK_TEXT_BUFFER_NOTIFY_BEFORE_DELETE |
                                        GTK_TEXT_BUFFER_NOTIFY_AFTER_DELETE),
                                       spelling_text_buffer_adapter_commit_notify,
                                       self, NULL);

  self->insert_mark = gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (buffer));

  g_signal_group_set_target (self->buffer_signals, buffer);

  gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (buffer), &begin, &end);

  offset = gtk_text_iter_get_offset (&begin);
  length = gtk_text_iter_get_offset (&end) - offset;

  _cjh_text_region_insert (self->region, offset, length, RUN_UNCHECKED);

  self->tag = gtk_text_buffer_create_tag (GTK_TEXT_BUFFER (buffer), NULL,
                                          "underline", PANGO_UNDERLINE_ERROR,
                                          NULL);

  g_signal_connect_object (buffer,
                           "notify::style-scheme",
                           G_CALLBACK (apply_error_style_cb),
                           self->tag,
                           0);
  apply_error_style_cb (GTK_SOURCE_BUFFER (buffer), NULL, self->tag);

  /* Track tag changes from the tag table and extract "no-spell-check"
   * tag from GtkSourceView so that we can avoid words with that tag.
   */
  tag_table = gtk_text_buffer_get_tag_table (GTK_TEXT_BUFFER (buffer));
  g_signal_connect_object (tag_table,
                           "tag-added",
                           G_CALLBACK (on_tag_added_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (tag_table,
                           "tag-removed",
                           G_CALLBACK (on_tag_removed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (buffer,
                           "apply-tag",
                           G_CALLBACK (invalidate_tag_region_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (buffer,
                           "remove-tag",
                           G_CALLBACK (invalidate_tag_region_cb),
                           self,
                           G_CONNECT_SWAPPED);

  spelling_text_buffer_adapter_queue_update (self);
}

void
spelling_text_buffer_adapter_set_enabled (SpellingTextBufferAdapter *self,
                                          gboolean                   enabled)
{
  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));

  enabled = !!enabled;

  if (self->enabled != enabled)
    {
      GtkTextIter begin, end;

      self->enabled = enabled;

      if (self->buffer && self->tag && !self->enabled)
        {
          gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (self->buffer), &begin, &end);
          gtk_text_buffer_remove_tag (GTK_TEXT_BUFFER (self->buffer), self->tag, &begin, &end);
        }

      spelling_text_buffer_adapter_invalidate_all (self);
      spelling_text_buffer_adapter_queue_update (self);

      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);

      spelling_text_buffer_adapter_set_action_state (self,
                                                     "enabled",
                                                     g_variant_new_boolean (enabled));
    }
}

static void
remember_word_under_cursor (SpellingTextBufferAdapter *self)
{
  g_autofree char *word = NULL;
  g_auto(GStrv) corrections = NULL;
  GtkTextBuffer *buffer;
  GtkTextMark *insert;
  GtkTextIter iter, begin, end;

  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));

  g_clear_pointer (&self->word_under_cursor, g_free);

  if (self->buffer == NULL || self->checker == NULL)
    goto cleanup;

  buffer = GTK_TEXT_BUFFER (self->buffer);
  insert = gtk_text_buffer_get_insert (buffer);

  /* Get the word under the cursor */
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, insert);
  begin = iter;
  if (!gtk_text_iter_starts_word (&begin))
    gtk_text_iter_backward_word_start (&begin);
  end = begin;
  if (!gtk_text_iter_ends_word (&end))
    gtk_text_iter_forward_word_end (&end);
  if (!gtk_text_iter_equal (&begin, &end) &&
      gtk_text_iter_compare (&begin, &iter) <= 0 &&
      gtk_text_iter_compare (&iter, &end) <= 0)
    {
      /* Ignore very long words */
      if ((gtk_text_iter_get_offset (&end) - gtk_text_iter_get_offset (&begin)) < MAX_WORD_CHARS)
        {
          word = gtk_text_iter_get_slice (&begin, &end);

          if (spelling_checker_check_word (self->checker, word, -1))
            g_clear_pointer (&word, g_free);
          else
            corrections = spelling_checker_list_corrections (self->checker, word);
        }
    }

cleanup:
  g_set_str (&self->word_under_cursor, word);

  if (self->menu)
    spelling_menu_set_corrections (self->menu, word, (const char * const *)corrections);
}

static gboolean
spelling_text_buffer_adapter_cursor_moved_cb (gpointer data)
{
  SpellingTextBufferAdapter *self = data;
  GtkTextIter begin, end;

  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));

  self->queued_cursor_moved = 0;

  /* Invalidate the old position */
  if (self->enabled && get_word_at_position (self, self->cursor_position, &begin, &end))
    mark_unchecked (self,
                    gtk_text_iter_get_offset (&begin),
                    gtk_text_iter_get_offset (&end) - gtk_text_iter_get_offset (&begin));

  self->cursor_position = self->incoming_cursor_position;

  /* Invalidate word at new position */
  if (self->enabled && get_word_at_position (self, self->cursor_position, &begin, &end))
    mark_unchecked (self,
                    gtk_text_iter_get_offset (&begin),
                    gtk_text_iter_get_offset (&end) - gtk_text_iter_get_offset (&begin));

  remember_word_under_cursor (self);

  return G_SOURCE_REMOVE;
}

static void
spelling_text_buffer_adapter_cursor_moved (SpellingTextBufferAdapter *self,
                                           GtkSourceBuffer           *buffer)
{
  GtkTextIter iter;

  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));
  g_assert (GTK_SOURCE_IS_BUFFER (buffer));

  if (!self->enabled)
    return;

  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (buffer), &iter, self->insert_mark);
  self->incoming_cursor_position = gtk_text_iter_get_offset (&iter);

  g_clear_handle_id (&self->queued_cursor_moved, g_source_remove);
  self->queued_cursor_moved = g_timeout_add_full (G_PRIORITY_LOW,
                                                  INVALIDATE_DELAY_MSECS,
                                                  spelling_text_buffer_adapter_cursor_moved_cb,
                                                  g_object_ref (self),
                                                  g_object_unref);
}

static void
spelling_text_buffer_adapter_finalize (GObject *object)
{
  SpellingTextBufferAdapter *self = (SpellingTextBufferAdapter *)object;

  self->tag = NULL;
  self->insert_mark = NULL;

  g_clear_pointer (&self->word_under_cursor, g_free);
  g_clear_object (&self->checker);
  g_clear_object (&self->no_spell_check_tag);
  g_clear_pointer (&self->region, _cjh_text_region_free);

  G_OBJECT_CLASS (spelling_text_buffer_adapter_parent_class)->finalize (object);
}

static void
spelling_text_buffer_adapter_dispose (GObject *object)
{
  SpellingTextBufferAdapter *self = (SpellingTextBufferAdapter *)object;

  if (self->buffer != NULL)
    {
      gtk_text_buffer_remove_commit_notify (GTK_TEXT_BUFFER (self->buffer), self->commit_handler);
      self->commit_handler = 0;
      g_clear_weak_pointer (&self->buffer);
    }

  gtk_source_scheduler_clear (&self->update_source);

  G_OBJECT_CLASS (spelling_text_buffer_adapter_parent_class)->dispose (object);
}

static void
spelling_text_buffer_adapter_get_property (GObject    *object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
  SpellingTextBufferAdapter *self = SPELLING_TEXT_BUFFER_ADAPTER (object);

  switch (prop_id)
    {
    case PROP_BUFFER:
      g_value_set_object (value, self->buffer);
      break;

    case PROP_CHECKER:
      g_value_set_object (value, spelling_text_buffer_adapter_get_checker (self));
      break;

    case PROP_ENABLED:
      g_value_set_boolean (value, self->enabled);
      break;

    case PROP_LANGUAGE:
      g_value_set_string (value, spelling_text_buffer_adapter_get_language (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
spelling_text_buffer_adapter_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
  SpellingTextBufferAdapter *self = SPELLING_TEXT_BUFFER_ADAPTER (object);

  switch (prop_id)
    {
    case PROP_BUFFER:
      spelling_text_buffer_adapter_set_buffer (self, g_value_get_object (value));
      break;

    case PROP_CHECKER:
      spelling_text_buffer_adapter_set_checker (self, g_value_get_object (value));
      break;

    case PROP_ENABLED:
      spelling_text_buffer_adapter_set_enabled (self, g_value_get_boolean (value));
      break;

    case PROP_LANGUAGE:
      spelling_text_buffer_adapter_set_language (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
spelling_text_buffer_adapter_class_init (SpellingTextBufferAdapterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = spelling_text_buffer_adapter_dispose;
  object_class->finalize = spelling_text_buffer_adapter_finalize;
  object_class->get_property = spelling_text_buffer_adapter_get_property;
  object_class->set_property = spelling_text_buffer_adapter_set_property;

  properties[PROP_BUFFER] =
    g_param_spec_object ("buffer",
                         "Buffer",
                         "Buffer",
                         GTK_SOURCE_TYPE_BUFFER,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_CHECKER] =
    g_param_spec_object ("checker",
                         "Checker",
                         "Checker",
                         SPELLING_TYPE_CHECKER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_ENABLED] =
    g_param_spec_boolean ("enabled",
                          "Enabled",
                          "If spellcheck is enabled",
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_LANGUAGE] =
    g_param_spec_string ("language",
                         "Language",
                         "The language code such as en_US",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
spelling_text_buffer_adapter_init (SpellingTextBufferAdapter *self)
{
  self->region = _cjh_text_region_new (NULL, NULL);

  self->buffer_signals = g_signal_group_new (GTK_SOURCE_TYPE_BUFFER);
  g_signal_group_connect_object (self->buffer_signals,
                                 "cursor-moved",
                                 G_CALLBACK (spelling_text_buffer_adapter_cursor_moved),
                                 self,
                                 G_CONNECT_SWAPPED);
}

/**
 * spelling_text_buffer_adapter_get_checker:
 * @self: a #SpellingTextBufferAdapter
 *
 * Gets the checker used by the adapter.
 *
 * Returns: (transfer none) (nullable): a #SpellingChecker or %NULL
 */
SpellingChecker *
spelling_text_buffer_adapter_get_checker (SpellingTextBufferAdapter *self)
{
  g_return_val_if_fail (SPELLING_IS_TEXT_BUFFER_ADAPTER (self), NULL);

  return self->checker;
}

static void
spelling_text_buffer_adapter_checker_notify_language (SpellingTextBufferAdapter *self,
                                                      GParamSpec                *pspec,
                                                      SpellingChecker           *checker)
{
  const char *code;

  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));
  g_assert (SPELLING_IS_CHECKER (checker));

  if (!(code = spelling_checker_get_language (checker)))
    code = "";

  spelling_text_buffer_adapter_set_action_state (self, "language", g_variant_new_string (code));
}

void
spelling_text_buffer_adapter_set_checker (SpellingTextBufferAdapter *self,
                                          SpellingChecker           *checker)
{
  const char *code = "";
  guint length;

  g_return_if_fail (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));
  g_return_if_fail (!checker || SPELLING_IS_CHECKER (checker));

  if (self->checker == checker)
    return;

  if (self->checker)
    g_signal_handlers_disconnect_by_func (self->checker,
                                          G_CALLBACK (spelling_text_buffer_adapter_checker_notify_language),
                                          self);

  g_set_object (&self->checker, checker);

  if (checker)
    {
      g_signal_connect_object (self->checker,
                               "notify::language",
                               G_CALLBACK (spelling_text_buffer_adapter_checker_notify_language),
                               self,
                               G_CONNECT_SWAPPED);
      code = spelling_checker_get_language (checker);
    }

  length = _cjh_text_region_get_length (self->region);

  gtk_source_scheduler_clear (&self->update_source);

  if (length > 0)
    {
      _cjh_text_region_remove (self->region, 0, length);
      _cjh_text_region_insert (self->region, 0, length, RUN_UNCHECKED);
      g_assert_cmpint (length, ==, _cjh_text_region_get_length (self->region));

    }

  spelling_text_buffer_adapter_queue_update (self);

  spelling_text_buffer_adapter_set_action_state (self, "language", g_variant_new_string (code));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CHECKER]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LANGUAGE]);
}

/**
 * spelling_text_buffer_adapter_get_buffer:
 * @self: a #SpellingTextBufferAdapter
 *
 * Gets the underlying buffer for the adapter.
 *
 * Returns: (transfer none) (nullable): a #GtkSourceBuffer
 */
GtkSourceBuffer *
spelling_text_buffer_adapter_get_buffer (SpellingTextBufferAdapter *self)
{
  g_return_val_if_fail (SPELLING_IS_TEXT_BUFFER_ADAPTER (self), NULL);

  return self->buffer;
}

const char *
spelling_text_buffer_adapter_get_language (SpellingTextBufferAdapter *self)
{
  g_return_val_if_fail (SPELLING_IS_TEXT_BUFFER_ADAPTER (self), NULL);

  return self->checker ? spelling_checker_get_language (self->checker) : NULL;
}

void
spelling_text_buffer_adapter_set_language (SpellingTextBufferAdapter *self,
                                           const char                *language)
{
  g_return_if_fail (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));

  if (self->checker == NULL && language == NULL)
    return;

  if (self->checker == NULL)
    {
      self->checker = spelling_checker_new (NULL, language);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CHECKER]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LANGUAGE]);
    }
  else if (g_strcmp0 (language, spelling_text_buffer_adapter_get_language (self)) != 0)
    {
      spelling_checker_set_language (self->checker, language);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LANGUAGE]);
    }

  spelling_text_buffer_adapter_invalidate_all (self);
}

/**
 * spelling_text_buffer_adapter_get_tag:
 * @self: a #SpellingTextBufferAdapter
 *
 * Gets the tag used for potentially misspelled words.
 *
 * Returns: (transfer none) (nullable): a #GtkTextTag or %NULL
 */
GtkTextTag *
spelling_text_buffer_adapter_get_tag (SpellingTextBufferAdapter *self)
{
  g_return_val_if_fail (SPELLING_IS_TEXT_BUFFER_ADAPTER (self), NULL);

  return self->tag;
}

gboolean
spelling_text_buffer_adapter_get_enabled (SpellingTextBufferAdapter *self)
{
  return self ? self->enabled : FALSE;
}

/**
 * spelling_text_buffer_adapter_get_menu_model:
 * @self: a #SpellingTextBufferAdapter
 *
 * Gets the menu model containing corrections
 *
 * Returns: (transfer none): a #GMenuModel
 */
GMenuModel *
spelling_text_buffer_adapter_get_menu_model (SpellingTextBufferAdapter *self)
{
  g_return_val_if_fail (SPELLING_IS_TEXT_BUFFER_ADAPTER (self), NULL);

  if (self->menu == NULL)
    self->menu = spelling_menu_new ();

  return self->menu;
}

static void
spelling_add_action (SpellingTextBufferAdapter *self,
                     GVariant                  *param)
{
  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_STRING));

  if (self->checker != NULL)
    {
      spelling_checker_add_word (self->checker, g_variant_get_string (param, NULL));
      spelling_text_buffer_adapter_invalidate_all (self);
    }
}

static void
spelling_ignore_action (SpellingTextBufferAdapter *self,
                        GVariant                  *param)
{
  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_STRING));

  if (self->checker != NULL)
    {
      spelling_checker_ignore_word (self->checker, g_variant_get_string (param, NULL));
      spelling_text_buffer_adapter_invalidate_all (self);
    }
}

static void
spelling_enabled_action (SpellingTextBufferAdapter *self,
                         GVariant                  *param)
{
  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));

  spelling_text_buffer_adapter_set_enabled (self, !self->enabled);
}

static void
spelling_correct_action (SpellingTextBufferAdapter *self,
                         GVariant                  *param)
{
  g_autofree char *slice = NULL;
  GtkTextBuffer *buffer;
  const char *word;
  GtkTextIter begin, end;

  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_STRING));

  if (self->buffer == NULL)
    return;

  buffer = GTK_TEXT_BUFFER (self->buffer);
  word = g_variant_get_string (param, NULL);

  /* We don't deal with selections (yet?) */
  if (gtk_text_buffer_get_selection_bounds (buffer, &begin, &end))
    return;

  /* TODO: For some languages, we might need to use spelling_iter
   * to get the same boundaries.
   */

  if (!gtk_text_iter_starts_word (&begin))
    gtk_text_iter_backward_word_start (&begin);

  if (!gtk_text_iter_ends_word (&end))
    gtk_text_iter_forward_word_end (&end);

  slice = gtk_text_iter_get_slice (&begin, &end);

  if (g_strcmp0 (slice, self->word_under_cursor) != 0)
    {
      g_debug ("Words do not match, will not replace.");
      return;
    }

  gtk_text_buffer_begin_user_action (buffer);
  gtk_text_buffer_delete (buffer, &begin, &end);
  gtk_text_buffer_insert (buffer, &begin, word, -1);
  gtk_text_buffer_end_user_action (buffer);
}

static void
spelling_language_action (SpellingTextBufferAdapter *self,
                          GVariant                  *param)
{
  const char *code;

  g_assert (SPELLING_IS_TEXT_BUFFER_ADAPTER (self));
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_STRING));

  code = g_variant_get_string (param, NULL);

  if (self->checker)
    spelling_checker_set_language (self->checker, code);
}
