Title: Overview

# Overview

Spelling is a [GNOME](https://www.gnome.org/) library that provides spellchecking
for `GtkTextView` widgets.

For a source view, create a `SpellingTextBufferAdapter` for its buffer and a
`SpellingTextViewAdapter` for each view displaying that buffer. Add the view
adapter's menu model to the view's extra menu. The view adapter installs the
`spelling` action group and prepares corrections for context clicks and
keyboard menu requests without moving the insertion cursor. Keep the view
adapter alive for as long as its menu is in use.

##  pkg-config name

To build a program that uses Spelling, you can use the following command to get
the cflags and libraries necessary to compile and link.

```sh
gcc hello.c `pkg-config --cflags --libs libspelling-1` -o hello
```
