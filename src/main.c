/*
 * P2Browser engine probe, reconstructed from the code recorded in this chat.
 * SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "memory-policy.h"

#ifndef P2_DATA_DIR
#define P2_DATA_DIR "/usr/share/p2browser-probe"
#endif
#ifndef P2_VERSION
#define P2_VERSION "0.2.0~alpha2"
#endif
#if !WEBKIT_CHECK_VERSION(2, 26, 0)
#error "WebKitGTK >= 2.26 with the Linux sandbox is required."
#endif

typedef struct {
    GtkWidget *page, *label, *content, *failure_notice;
    WebKitWebView *view;
    gchar *uri;
    guint number, references, navigation;
    gboolean closed, suspended, text_mode, text_pending, protected_tab;
    gboolean navigation_failed, navigation_complete, process_dead;
} Tab;

typedef struct {
    GtkWidget *window, *address, *status, *memory_status, *notebook, *images_toggle;
    WebKitWebView *view;
    WebKitWebContext *context;
    GPtrArray *tabs;
    Tab *active;
    FILE *events;
    gchar *report_dir, *data_dir, *ready_title;
    gint64 started;
    guint timer, watchdog, error_count, next_tab, memory_test_phase;
    gint memory_limit_mib;
    gboolean self_test, test_finished, test_started, js_enabled, stopping;
    gboolean navigation_failed, navigation_complete;
    gboolean images_enabled, unlimited_tabs, reclaim_background, saving;
    gboolean ready_reported, pressure_reported, critical_reported;
    gboolean memory_self_test, memory_test_scheduled;
    gboolean video_compat, last_memory_known;
    P2MemorySample last_memory;
    gint64 last_memory_at;
    P2MemoryLevel memory_level;
    gint exit_code;
} App;
static App app;

static Tab *add_tab(const gchar *uri, gboolean activate);
static void ensure_view(Tab *tab);
static guint reclaim_background_tabs(void);
static void request_text(Tab *tab);
static void update_tab_label(Tab *tab);
static gboolean sample_memory(gpointer unused);
static void check_memory_test(void);
static void finish_memory_test(gboolean success);
static void clear_failure_notice(Tab *tab);
static void event(const char *category, const char *detail);

static void recovery_event(Tab *tab, const gchar *action)
{
    gchar *detail = g_strdup_printf("tab=%u;action=%s", tab->number, action);
    event("web_process_recovery", detail);
    g_free(detail);
}

static Tab *tab_ref(Tab *tab)
{
    tab->references++;
    return tab;
}

static void tab_unref(gpointer data)
{
    Tab *tab = data;
    if (--tab->references)
        return;
    g_free(tab->uri);
    g_free(tab);
}

static Tab *tab_for_view(WebKitWebView *view)
{
    guint i;
    if (!app.tabs)
        return NULL;
    for (i = 0; i < app.tabs->len; i++) {
        Tab *tab = g_ptr_array_index(app.tabs, i);
        if (tab->view == view)
            return tab;
    }
    return NULL;
}

/* Never log URLs, page-provided error messages or form values. */
static void event(const char *category, const char *detail)
{
    if (!app.events || ftell(app.events) >= 1024 * 1024)
        return;
    fprintf(app.events, "%" G_GINT64_FORMAT "\t%s\t%s\n",
            (g_get_monotonic_time() - app.started) / 1000,
            category, detail ? detail : "-");
    fflush(app.events);
}

static gboolean make_report(const gchar *requested, GError **error)
{
    gchar *path;
    if (requested) {
        app.report_dir = g_canonicalize_filename(requested, NULL);
        if (g_mkdir(app.report_dir, 0700) != 0) {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                        "Katalog raportu musi być nowy: %s", g_strerror(errno));
            return FALSE;
        }
    } else {
        app.report_dir = g_dir_make_tmp("p2browser-probe-XXXXXX", error);
        if (!app.report_dir)
            return FALSE;
    }
    path = g_build_filename(app.report_dir, "events.tsv", NULL);
    app.events = fopen(path, "wx");
    g_free(path);
    if (!app.events) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Nie można utworzyć dziennika: %s", g_strerror(errno));
        return FALSE;
    }
    fprintf(app.events, "elapsed_ms\tevent\tdetail\n");
    event("probe", P2_VERSION);
    {
        gchar *version = g_strdup_printf("%u.%u.%u",
            webkit_get_major_version(), webkit_get_minor_version(),
            webkit_get_micro_version());
        event("webkit_runtime", version);
        g_free(version);
    }
    return TRUE;
}

static void status_text(const gchar *message)
{
    if (app.status)
        gtk_label_set_text(GTK_LABEL(app.status), message);
}

static gchar *fixture_uri(void)
{
    gchar *path = g_build_filename(app.data_dir, "start.html", NULL);
    gchar *absolute = g_canonicalize_filename(path, NULL);
    gchar *uri = g_filename_to_uri(absolute, NULL, NULL);
    g_free(absolute);
    g_free(path);
    return uri;
}

static void navigate(const gchar *input)
{
    gchar *value = g_strdup(input), *uri = NULL, *scheme;
    g_strstrip(value);
    if (!*value) {
        g_free(value);
        return;
    }
    scheme = g_uri_parse_scheme(value);
    if (!scheme)
        uri = g_strconcat("https://", value, NULL);
    else if (!g_ascii_strcasecmp(scheme, "http") ||
             !g_ascii_strcasecmp(scheme, "https") ||
             !g_ascii_strcasecmp(scheme, "file") ||
             !g_ascii_strcasecmp(scheme, "about"))
        uri = g_strdup(value);
    else {
        status_text("Obsługiwane adresy: HTTP, HTTPS i pliki lokalne.");
        event("navigation_rejected", "unsupported_scheme");
    }
    if (uri && app.active) {
        ensure_view(app.active);
        app.view = app.active->view;
        webkit_web_view_load_uri(app.view, uri);
    }
    g_free(uri);
    g_free(scheme);
    g_free(value);
}

static void address_activate(GtkEntry *entry, gpointer unused)
{
    (void)unused;
    navigate(gtk_entry_get_text(entry));
}

static void back_clicked(GtkButton *button, gpointer unused)
{
    (void)button; (void)unused;
    if (app.view)
        webkit_web_view_go_back(app.view);
}
static void forward_clicked(GtkButton *button, gpointer unused)
{
    (void)button; (void)unused;
    if (app.view)
        webkit_web_view_go_forward(app.view);
}
static void reload_clicked(GtkButton *button, gpointer unused)
{
    (void)button; (void)unused;
    if (app.view) {
        if (app.active && app.active->process_dead)
            recovery_event(app.active, "reload_requested");
        webkit_web_view_reload(app.view);
    } else if (app.active && app.active->uri) {
        gchar *uri = g_strdup(app.active->uri);
        ensure_view(app.active);
        app.view = app.active->view;
        webkit_web_view_load_uri(app.view, uri);
        g_free(uri);
    }
}
static void stop_clicked(GtkButton *button, gpointer unused)
{
    (void)button; (void)unused;
    if (app.view)
        webkit_web_view_stop_loading(app.view);
    event("navigation", "stopped_by_user");
}

static void js_toggled(GtkToggleButton *button, gpointer unused)
{
    guint i;
    (void)unused;
    app.js_enabled = gtk_toggle_button_get_active(button);
    for (i = 0; i < app.tabs->len; i++) {
        Tab *tab = g_ptr_array_index(app.tabs, i);
        if (tab->view)
            webkit_settings_set_enable_javascript(
                webkit_web_view_get_settings(tab->view), app.js_enabled);
    }
    event("javascript", app.js_enabled ? "engine_enabled" : "disabled");
    status_text("Ustawienie JS zmienione. Odśwież stronę.");
}

static void images_toggled(GtkToggleButton *button, gpointer unused)
{
    guint i;
    gboolean enabled = gtk_toggle_button_get_active(button);
    (void)unused;
    app.images_enabled = enabled;
    for (i = 0; i < app.tabs->len; i++) {
        Tab *tab = g_ptr_array_index(app.tabs, i);
        if (tab->view)
            webkit_settings_set_auto_load_images(
                webkit_web_view_get_settings(tab->view), enabled);
    }
    event("images", enabled ? "enabled" : "disabled");
    status_text("Ustawienie obrazów zmienione. Odśwież stronę.");
}

static void uri_changed(GObject *object, GParamSpec *parameter, gpointer unused)
{
    const gchar *uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(object));
    Tab *tab = tab_for_view(WEBKIT_WEB_VIEW(object));
    (void)parameter; (void)unused;
    if (!tab || tab->closed)
        return;
    if (uri && tab && !tab->closed && !app.stopping) {
        g_free(tab->uri);
        tab->uri = g_strdup(uri);
        if (tab == app.active)
            gtk_entry_set_text(GTK_ENTRY(app.address), uri);
    }
}

static void finish_test(gboolean success)
{
    gchar *path, *json;
    GError *error = NULL;
    if (app.test_finished)
        return;
    app.test_finished = TRUE;
    path = g_build_filename(app.report_dir, "self-test.json", NULL);
    json = g_strdup_printf(
        "{\"success\":%s,\"javascript\":%s,\"scope\":\"local_fixture_only\"}\n",
        success ? "true" : "false", app.js_enabled ? "true" : "false");
    if (!g_file_set_contents(path, json, -1, &error)) {
        g_printerr("Nie można zapisać wyniku: %s\n", error->message);
        g_clear_error(&error);
        success = FALSE;
    }
    g_free(path);
    g_free(json);
    app.exit_code = success ? 0 : 1;
    event("self_test", success ? "passed" : "failed");
    if (gtk_main_level())
        gtk_main_quit();
}

static void snapshot_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    GError *error = NULL;
    cairo_surface_t *surface = webkit_web_view_get_snapshot_finish(
        WEBKIT_WEB_VIEW(object), result, &error);
    gboolean success = GPOINTER_TO_INT(data);
    if (!surface) {
        event("snapshot", "failed");
        g_clear_error(&error);
        finish_test(FALSE);
        return;
    }
    {
        gchar *path = g_build_filename(app.report_dir, "snapshot.png", NULL);
        if (cairo_surface_write_to_png(surface, path) != CAIRO_STATUS_SUCCESS)
            success = FALSE;
        g_free(path);
    }
    cairo_surface_destroy(surface);
    finish_test(success);
}

static void snapshot_test(gboolean success)
{
    webkit_web_view_get_snapshot(app.view, WEBKIT_SNAPSHOT_REGION_VISIBLE,
        WEBKIT_SNAPSHOT_OPTIONS_NONE, NULL, snapshot_ready,
        GINT_TO_POINTER(success));
}

static void javascript_test_ready(GObject *object, GAsyncResult *result,
                                  gpointer unused)
{
    GError *error = NULL;
    gboolean success = FALSE;
    (void)unused;
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    JSCValue *value = webkit_web_view_evaluate_javascript_finish(
        WEBKIT_WEB_VIEW(object), result, &error);
    if (value) {
        success = jsc_value_is_boolean(value) && jsc_value_to_boolean(value);
        g_object_unref(value);
    }
#else
    WebKitJavascriptResult *js_result = webkit_web_view_run_javascript_finish(
        WEBKIT_WEB_VIEW(object), result, &error);
    if (js_result) {
        JSCValue *value = webkit_javascript_result_get_js_value(js_result);
        success = jsc_value_is_boolean(value) && jsc_value_to_boolean(value);
        webkit_javascript_result_unref(js_result);
    }
#endif
    if (error) {
        event("self_test_javascript", "evaluation_failed");
        g_clear_error(&error);
    }
    snapshot_test(success);
}

static void run_fixture_test(void)
{
    const gchar *title = webkit_web_view_get_title(app.view);
    const gchar *script =
        "(function(){"
        "var input=document.getElementById('test-input');"
        "input.value='Pentium II';"
        "document.getElementById('test-submit').click();"
        "return document.getElementById('test-output').textContent==='Witaj, Pentium II'"
        "&&document.title==='P2Browser — JS działa'"
        "&&getComputedStyle(document.getElementById('layout-test')).display==='flex';"
        "})()";
    if (app.test_started || app.test_finished)
        return;
    /* A new view may finish its initial about:blank while the fixture's
     * navigation is still queued. Wait for the fixture's own title in both
     * modes, including the title changed by its JavaScript. */
    if (g_strcmp0(title, app.js_enabled ? "P2Browser — JS działa" :
                                          "P2Browser — test HTML i CSS") != 0)
        return;
    if (!app.js_enabled) {
        app.test_started = TRUE;
        snapshot_test(TRUE);
        return;
    }
    app.test_started = TRUE;
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    webkit_web_view_evaluate_javascript(app.view, script, -1, NULL, NULL,
                                       NULL, javascript_test_ready, NULL);
#else
    webkit_web_view_run_javascript(app.view, script, NULL,
                                   javascript_test_ready, NULL);
#endif
}

static void title_changed(GObject *object, GParamSpec *parameter, gpointer unused)
{
    Tab *tab = tab_for_view(WEBKIT_WEB_VIEW(object));
    (void)parameter; (void)unused;
    if (tab)
        update_tab_label(tab);
    if (tab == app.active && app.ready_title && tab->navigation_complete &&
        !tab->navigation_failed && !app.ready_reported &&
        g_strcmp0(webkit_web_view_get_title(tab->view), app.ready_title) == 0) {
        app.ready_reported = TRUE;
        event("fixture_ready", "matched");
    }
    if (tab == app.active && app.self_test && app.navigation_complete && !app.navigation_failed)
        run_fixture_test();
}

static void load_changed(WebKitWebView *view, WebKitLoadEvent state,
                         gpointer unused)
{
    Tab *tab = tab_for_view(view);
    gboolean current = tab == app.active;
    (void)unused;
    if (!tab || tab->closed || app.stopping)
        return;
    switch (state) {
    case WEBKIT_LOAD_STARTED:
        clear_failure_notice(tab);
        tab->process_dead = FALSE;
        tab->navigation++;
        tab->text_pending = FALSE;
        tab->navigation_failed = FALSE;
        tab->navigation_complete = FALSE;
        if (current) {
            app.error_count = 0;
            app.navigation_failed = FALSE;
            app.navigation_complete = FALSE;
            app.ready_reported = FALSE;
            status_text("Ładowanie…");
        }
        event(current ? "load" : "tab_load", "started");
        break;
    case WEBKIT_LOAD_REDIRECTED:
        event(current ? "load" : "tab_load", "redirected");
        break;
    case WEBKIT_LOAD_COMMITTED:
        event(current ? "load" : "tab_load", "committed");
        break;
    case WEBKIT_LOAD_FINISHED:
        if (tab->process_dead)
            break;
        tab->navigation_complete = TRUE;
        event(current ? "load" : "tab_load", "finished");
        if (current) {
            app.navigation_complete = TRUE;
            app.navigation_failed = tab->navigation_failed;
            if (!app.navigation_failed)
                status_text("Załadowano. Karty i budżet pamięci: prototyp eksperymentalny.");
            title_changed(G_OBJECT(view), NULL, NULL);
        }
        if (current && app.self_test && !app.navigation_failed && !app.test_finished)
            run_fixture_test();
        if (app.memory_self_test)
            check_memory_test();
        break;
    }
}

static gboolean load_failed(WebKitWebView *view, WebKitLoadEvent state,
                            gchar *uri, GError *error, gpointer unused)
{
    Tab *tab = tab_for_view(view);
    gchar *detail = g_strdup_printf("domain=%s;code=%d",
        g_quark_to_string(error->domain), error->code);
    (void)state; (void)uri; (void)unused;
    if (tab)
        tab->navigation_failed = TRUE;
    event("load_failed", detail);
    g_free(detail);
    if (tab == app.active) {
        app.navigation_failed = TRUE;
        status_text("Nie udało się załadować strony. Kod błędu zapisano w raporcie.");
    }
    if (tab == app.active && app.self_test)
        finish_test(FALSE);
    return FALSE;
}

static void clear_failure_notice(Tab *tab)
{
    GtkWidget *notice = tab->failure_notice;
    tab->failure_notice = NULL;
    if (notice)
        gtk_widget_destroy(notice);
}

static void recovery_response(GtkInfoBar *notice, gint response, gpointer data)
{
    Tab *tab = data;
    (void)notice;
    if (tab->closed || app.stopping)
        return;
    if (response == GTK_RESPONSE_APPLY && tab->view) {
        recovery_event(tab, "reload_requested");
        webkit_web_view_reload(tab->view);
    } else
        clear_failure_notice(tab);
}

static void process_terminated(WebKitWebView *view,
                               WebKitWebProcessTerminationReason reason,
                               gpointer unused)
{
    Tab *tab = tab_for_view(view);
    const gchar *kind, *message;
    GtkWidget *label;
    gchar *detail;
    (void)unused;
    if (!tab || tab->closed || app.stopping)
        return;
    switch (reason) {
    case WEBKIT_WEB_PROCESS_CRASHED:
        kind = "crashed";
        message = "Proces tej strony uległ awarii. Możesz wczytać ją ponownie; niezapisane dane mogą być utracone.";
        break;
    case WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT:
        kind = "engine_memory_limit";
        message = "Silnik zakończył proces tej strony po przekroczeniu swojego limitu pamięci. Możesz zwolnić karty w tle i spróbować ponownie.";
        break;
    case WEBKIT_WEB_PROCESS_TERMINATED_BY_API:
        kind = "terminated_by_api";
        message = "Proces tej strony został zatrzymany. Możesz wczytać stronę ponownie.";
        break;
    default:
        kind = "unknown";
        message = "Proces tej strony zakończył działanie. Możesz wczytać ją ponownie.";
        break;
    }
    detail = g_strdup_printf("reason=%d;kind=%s;tab=%u;active=%d",
        (int)reason, kind, tab->number, tab == app.active);
    event("web_process_terminated", detail);
    g_free(detail);
    if (app.last_memory_known) {
        const P2MemorySample *memory = &app.last_memory;
        detail = g_strdup_printf("scope=browser_tree;age_ms=%" G_GINT64_FORMAT
            ";pss_complete=%d;pss_kib=%" G_GUINT64_FORMAT
            ";swap_kib=%" G_GUINT64_FORMAT ";available_known=%d;available_kib=%" G_GUINT64_FORMAT
            ";swap_free_known=%d;swap_free_kib=%" G_GUINT64_FORMAT,
            (g_get_monotonic_time() - app.last_memory_at) / 1000,
            memory->pss_complete, (guint64)memory->pss_kib, (guint64)memory->swap_kib,
            memory->available_known, (guint64)memory->available_kib,
            memory->swap_free_known, (guint64)memory->swap_free_kib);
        event("termination_memory", detail);
        g_free(detail);
    } else
        event("termination_memory", "unavailable");
    tab->navigation++;
    tab->process_dead = TRUE;
    tab->text_pending = FALSE;
    tab->navigation_failed = TRUE;
    tab->navigation_complete = FALSE;
    clear_failure_notice(tab);
    tab->failure_notice = gtk_info_bar_new_with_buttons(
        "Wczytaj ponownie", GTK_RESPONSE_APPLY, "Ukryj komunikat", GTK_RESPONSE_CLOSE, NULL);
    gtk_info_bar_set_message_type(GTK_INFO_BAR(tab->failure_notice), GTK_MESSAGE_ERROR);
    label = gtk_label_new(message);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_container_add(GTK_CONTAINER(gtk_info_bar_get_content_area(
        GTK_INFO_BAR(tab->failure_notice))), label);
    gtk_box_pack_start(GTK_BOX(tab->page), tab->failure_notice, FALSE, FALSE, 0);
    gtk_box_reorder_child(GTK_BOX(tab->page), tab->failure_notice, 0);
    g_signal_connect(tab->failure_notice, "response", G_CALLBACK(recovery_response), tab);
    gtk_widget_show_all(tab->failure_notice);
    update_tab_label(tab);
    recovery_event(tab, "notice_shown");
    if (tab == app.active) {
        app.navigation_failed = TRUE;
        app.navigation_complete = FALSE;
        app.ready_reported = FALSE;
        status_text("Proces strony zakończył działanie. Przeglądarka pozostaje otwarta; szczegóły zapisano w raporcie.");
    }
    if (tab == app.active && app.self_test)
        finish_test(FALSE);
    if (app.memory_self_test)
        finish_memory_test(FALSE);
}

static void script_error(WebKitUserContentManager *manager,
                          WebKitJavascriptResult *result, gpointer unused)
{
    JSCValue *value = webkit_javascript_result_get_js_value(result);
    gchar *text;
    unsigned int line, column, media_code;
    (void)manager; (void)unused;
    if (!jsc_value_is_string(value) || app.error_count >= 100)
        return;
    text = jsc_value_to_string(value);
    if (strlen(text) < 64 && sscanf(text, "media:error:%u", &media_code) == 1 &&
        media_code >= 1 && media_code <= 4) {
        static const gchar *const kinds[] = {"unknown", "aborted", "network", "decode", "unsupported_source"};
        gchar *detail = g_strdup_printf("code=%u;kind=%s", media_code, kinds[media_code]);
        event("media_error", detail);
        g_free(detail);
        app.error_count++;
    } else if (!strcmp(text, "media:playing")) {
        event("media_playback", "playing");
        app.error_count++;
    } else if (strlen(text) < 64 && sscanf(text, "error:%u:%u", &line, &column) == 2) {
        gchar *detail = g_strdup_printf("line=%u;column=%u", line, column);
        event("javascript_error", detail);
        g_free(detail);
        app.error_count++;
    } else if (g_strcmp0(text, "unhandled_rejection") == 0) {
        event("javascript_error", "unhandled_rejection");
        app.error_count++;
    }
    g_free(text);
}

static void finish_memory_test(gboolean success)
{
    gchar *path, *json;
    GError *error = NULL;
    if (app.test_finished)
        return;
    app.test_finished = TRUE;
    path = g_build_filename(app.report_dir, "memory-self-test.json", NULL);
    json = g_strdup_printf(
        "{\"success\":%s,\"scope\":\"local_memory_controls\",\"tab_limit\":5,\"discarded\":3,\"resumed\":%s,\"text_mode\":%s}\n",
        success ? "true" : "false", app.memory_test_phase >= 2 ? "true" : "false",
        app.active && app.active->text_mode ? "true" : "false");
    if (!g_file_set_contents(path, json, -1, &error)) {
        g_clear_error(&error);
        success = FALSE;
    }
    g_free(path);
    g_free(json);
    app.exit_code = success ? 0 : 1;
    event("memory_self_test", success ? "passed" : "failed");
    if (gtk_main_level())
        gtk_main_quit();
}

typedef struct {
    Tab *tab;
    WebKitWebView *view;
    guint navigation;
} TextRequest;

static void text_request_free(TextRequest *request)
{
    tab_unref(request->tab);
    g_object_unref(request->view);
    g_free(request);
}

static gboolean text_request_current(TextRequest *request)
{
    return !app.stopping && !request->tab->closed &&
        request->tab->view == request->view &&
        request->tab->navigation == request->navigation;
}

/* A bounded, read-only approximation for JS-disabled documents. It is not
 * an HTML renderer or a snapshot of CSS-hidden/currently edited form content. */
static gchar *plain_html(const guchar *bytes, gsize size)
{
    GString *text = g_string_sized_new(4096);
    gsize i = 0, limit = MIN(size, (gsize)1024 * 1024);
    const char *skip = NULL;
    while (i < limit && text->len < 128 * 1024) {
        if (skip) {
            gsize n = strlen(skip);
            if (bytes[i] == '<' && i + n + 2 < limit && bytes[i + 1] == '/' &&
                g_ascii_strncasecmp((const gchar *)bytes + i + 2, skip, n) == 0)
                skip = NULL;
            else { i++; continue; }
        }
        if (bytes[i] == '<') {
            gsize start = i + 1;
            gchar quote = 0;
            if (start + 6 < limit && g_ascii_strncasecmp((const gchar *)bytes + start, "script", 6) == 0)
                skip = "script";
            else if (start + 5 < limit && g_ascii_strncasecmp((const gchar *)bytes + start, "style", 5) == 0)
                skip = "style";
            i++;
            while (i < limit) {
                gchar c = (gchar)bytes[i++];
                if (quote) { if (c == quote) quote = 0; }
                else if (c == '\'' || c == '"') quote = c;
                else if (c == '>') break;
            }
            if (text->len && text->str[text->len - 1] != '\n')
                g_string_append_c(text, '\n');
            continue;
        }
        if (bytes[i] == '&') {
            gsize end = i + 1;
            while (end < limit && end - i < 16 && bytes[end] != ';' && bytes[end] != '<') end++;
            if (end < limit && bytes[end] == ';') {
                gchar *entity = g_strndup((const gchar *)bytes + i + 1, end - i - 1);
                gunichar c = 0;
                if (!strcmp(entity, "amp")) c = '&';
                else if (!strcmp(entity, "lt")) c = '<';
                else if (!strcmp(entity, "gt")) c = '>';
                else if (!strcmp(entity, "quot")) c = '"';
                else if (!strcmp(entity, "apos")) c = '\'';
                else if (!strcmp(entity, "nbsp")) c = ' ';
                else if (entity[0] == '#') {
                    gchar *tail;
                    guint64 number = g_ascii_strtoull(entity + (entity[1] == 'x' || entity[1] == 'X' ? 2 : 1),
                        &tail, entity[1] == 'x' || entity[1] == 'X' ? 16 : 10);
                    if (!*tail && number <= 0x10ffff) c = (gunichar)number;
                }
                g_free(entity);
                if (c && g_unichar_validate(c)) {
                    gchar encoded[6];
                    gint n = g_unichar_to_utf8(c, encoded);
                    g_string_append_len(text, encoded, n);
                    i = end + 1;
                    continue;
                }
            }
        }
        if (g_ascii_isspace(bytes[i])) {
            if (text->len && !g_ascii_isspace(text->str[text->len - 1]))
                g_string_append_c(text, ' ');
            i++;
        } else
            g_string_append_c(text, (gchar)bytes[i++]);
    }
    {
        gchar *valid = g_utf8_make_valid(text->str, (gssize)text->len);
        g_string_free(text, TRUE);
        return valid;
    }
}

static void install_text(TextRequest *request, const gchar *text)
{
    Tab *tab = request->tab;
    GtkWidget *scroll, *viewer, *old;
    gchar *bounded;
    if (!text_request_current(request))
        return;
    bounded = g_utf8_make_valid(text, (gssize)MIN(strlen(text), (gsize)128 * 1024));
    old = tab->content;
    tab->view = NULL;
    tab->content = NULL;
    tab->text_pending = FALSE;
    tab->text_mode = TRUE;
    tab->navigation++;
    if (tab == app.active)
        app.view = NULL;
    if (old)
        gtk_widget_destroy(old);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    viewer = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(viewer), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(viewer), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(viewer), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(viewer), 12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(viewer), 12);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(viewer)), bounded, -1);
    gtk_container_add(GTK_CONTAINER(scroll), viewer);
    gtk_box_pack_start(GTK_BOX(tab->page), scroll, TRUE, TRUE, 0);
    tab->content = scroll;
    gtk_widget_show_all(scroll);
    update_tab_label(tab);
    webkit_web_context_clear_cache(app.context);
    event("text_mode", "enabled_bounded_approximation");
    if (tab == app.active)
        status_text("Uproszczony tekst (do 128 KiB). Układ, interakcje i niezapisany stan są utracone. Odśwież przywraca stronę.");
    if (app.memory_self_test && app.memory_test_phase == 2) {
        app.memory_test_phase = 3;
        finish_memory_test(strstr(bounded, "P2Browser") != NULL);
    }
    g_free(bounded);
}

static void html_text_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    TextRequest *request = data;
    GError *error = NULL;
    gsize size = 0;
    guchar *bytes = webkit_web_resource_get_data_finish(WEBKIT_WEB_RESOURCE(object), result, &size, &error);
    if (bytes && text_request_current(request)) {
        gchar *text = plain_html(bytes, size);
        install_text(request, text);
        g_free(text);
    } else if (text_request_current(request)) {
        request->tab->text_pending = FALSE;
        event("text_mode", "unavailable");
        status_text("Nie udało się odczytać tekstu. Strona pozostała otwarta.");
        if (app.memory_self_test)
            finish_memory_test(FALSE);
    }
    g_free(bytes);
    g_clear_error(&error);
    text_request_free(request);
}

static void fallback_html_text(TextRequest *request)
{
    WebKitWebResource *resource;
    if (!text_request_current(request)) { text_request_free(request); return; }
    resource = webkit_web_view_get_main_resource(request->view);
    if (!resource) {
        request->tab->text_pending = FALSE;
        event("text_mode", "unavailable");
        if (app.memory_self_test)
            finish_memory_test(FALSE);
        text_request_free(request);
        return;
    }
    event("text_mode", "reading_html_resource");
    webkit_web_resource_get_data(resource, NULL, html_text_ready, request);
}

static void dom_text_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    TextRequest *request = data;
    GError *error = NULL;
    JSCValue *value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(object), result, &error);
    if (value && jsc_value_is_string(value) && text_request_current(request)) {
        gchar *text = jsc_value_to_string(value);
        install_text(request, text);
        g_free(text);
        g_object_unref(value);
        g_clear_error(&error);
        text_request_free(request);
        return;
    }
    if (value) g_object_unref(value);
    g_clear_error(&error);
    fallback_html_text(request);
}

static void request_text(Tab *tab)
{
    TextRequest *request;
    if (!tab->view || tab->closed || tab->text_pending || tab->process_dead)
        return;
    tab->text_pending = TRUE;
    request = g_new0(TextRequest, 1);
    request->tab = tab_ref(tab);
    request->view = g_object_ref(tab->view);
    request->navigation = tab->navigation;
    if (app.js_enabled) {
        webkit_web_view_evaluate_javascript(tab->view,
            "(document.body?document.body.innerText:document.documentElement.textContent).slice(0,131072)",
            -1, NULL, NULL, NULL, dom_text_ready, request);
    } else
        fallback_html_text(request);
}

static gboolean memory_test_step(gpointer unused)
{
    guint i;
    Tab *last;
    (void)unused;
    app.memory_test_scheduled = FALSE;
    if (app.stopping || app.test_finished || app.tabs->len != 5)
        return G_SOURCE_REMOVE;
    last = g_ptr_array_index(app.tabs, 4);
    if (app.memory_test_phase == 0) {
        for (i = 0; i < app.tabs->len; i++) {
            Tab *tab = g_ptr_array_index(app.tabs, i);
            if (!tab->navigation_complete) return G_SOURCE_REMOVE;
            if (tab->navigation_failed) { finish_memory_test(FALSE); return G_SOURCE_REMOVE; }
        }
        if (add_tab("about:blank", FALSE) != NULL) {
            finish_memory_test(FALSE);
            return G_SOURCE_REMOVE;
        }
        ((Tab *)g_ptr_array_index(app.tabs, 1))->protected_tab = TRUE;
        if (reclaim_background_tabs() != 3 || !last->suspended || last->view) {
            finish_memory_test(FALSE);
            return G_SOURCE_REMOVE;
        }
        app.memory_test_phase = 1;
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app.notebook), 4);
    } else if (app.memory_test_phase == 1 && app.active == last && last->navigation_complete) {
        if (!last->view || last->suspended || last->navigation_failed) {
            finish_memory_test(FALSE);
            return G_SOURCE_REMOVE;
        }
        app.memory_test_phase = 2;
        request_text(last);
    }
    return G_SOURCE_REMOVE;
}

static void check_memory_test(void)
{
    if (!app.test_finished && !app.memory_test_scheduled) {
        app.memory_test_scheduled = TRUE;
        g_idle_add(memory_test_step, NULL);
    }
}

static void critical_response(GtkDialog *dialog, gint response, gpointer unused)
{
    (void)unused;
    if (response == GTK_RESPONSE_APPLY && !app.stopping && app.active)
        request_text(app.active);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static gboolean sample_memory(gpointer unused)
{
    P2MemorySample sample;
    P2MemoryLevel level;
    gchar *detail, *label;
    (void)unused;
    if (!p2_memory_sample("/proc", (long)getpid(), &sample)) {
        event("memory_tree", "unavailable");
        gtk_label_set_text(GTK_LABEL(app.memory_status), "RAM: brak danych");
        return G_SOURCE_CONTINUE;
    }
    app.last_memory = sample;
    app.last_memory_at = g_get_monotonic_time();
    app.last_memory_known = TRUE;
    detail = g_strdup_printf(
        "processes=%u;rss_kib=%" G_GUINT64_FORMAT ";swap_kib=%" G_GUINT64_FORMAT
        ";pss_complete=%d;pss_kib=%" G_GUINT64_FORMAT ";pss_missing=%u",
        sample.processes, (guint64)sample.rss_kib, (guint64)sample.swap_kib,
        sample.pss_complete, (guint64)sample.pss_kib, sample.pss_missing);
    event("memory_tree", detail);
    g_free(detail);
    if (sample.pss_complete)
        label = g_strdup_printf("PSS: %.1f MiB | swap: %.1f MiB | karty: %u%s",
            sample.pss_kib / 1024.0, sample.swap_kib / 1024.0,
            app.tabs->len, app.saving ? " | oszczędzanie" : "");
    else
        label = g_strdup_printf("PSS: niepełne dane (%u) | karty: %u",
            sample.pss_missing, app.tabs->len);
    gtk_label_set_text(GTK_LABEL(app.memory_status), label);
    g_free(label);
    level = p2_memory_level(&sample, (uint64_t)app.memory_limit_mib * 1024);
    if (level != app.memory_level) {
        app.memory_level = level;
        if (level == P2_MEMORY_NORMAL) {
            app.critical_reported = FALSE;
            event("memory_pressure", "normal");
        } else {
            guint i;
            event("memory_pressure", level == P2_MEMORY_CRITICAL ?
                  "critical_system_memory" : "budget_exceeded");
            if (!app.saving) {
                app.saving = TRUE;
                app.images_enabled = FALSE;
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.images_toggle), FALSE);
                for (i = 0; i < app.tabs->len; i++) {
                    Tab *tab = g_ptr_array_index(app.tabs, i);
                    if (tab->view) {
                        WebKitSettings *settings = webkit_web_view_get_settings(tab->view);
                        webkit_settings_set_auto_load_images(settings, FALSE);
                        webkit_settings_set_enable_page_cache(settings, FALSE);
                    }
                }
                webkit_web_context_clear_cache(app.context);
                event("memory_saving", "enabled");
            }
            status_text("Przekroczono miękki budżet RAM. Włączono oszczędzanie; swapem zarządza system.");
        }
    }
    if (level != P2_MEMORY_NORMAL && app.reclaim_background)
        reclaim_background_tabs();
    if (level == P2_MEMORY_CRITICAL && !app.critical_reported) {
        GtkWidget *dialog;
        app.critical_reported = TRUE;
        event("memory_notice", "critical");
        if (!app.self_test && !app.memory_self_test) {
            dialog = gtk_message_dialog_new(GTK_WINDOW(app.window),
                GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
                "Kończy się RAM i swap. Możesz przełączyć aktywną stronę na uproszczony tekst.");
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                "Podgląd tekstowy usuwa układ i niezapisany stan formularzy. To próba odzyskania pamięci, nie gwarancja uniknięcia OOM.");
            gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Kontynuuj", GTK_RESPONSE_CANCEL,
                                   "Przejdź do tekstu", GTK_RESPONSE_APPLY, NULL);
            g_signal_connect(dialog, "response", G_CALLBACK(critical_response), NULL);
            gtk_widget_show(dialog);
        }
    }
    return G_SOURCE_CONTINUE;
}

static gboolean quit_timeout(gpointer unused)
{
    (void)unused;
    app.watchdog = 0;
    event("watchdog", "time_limit");
    if (app.memory_self_test)
        finish_memory_test(FALSE);
    else if (app.self_test)
        finish_test(FALSE);
    else
        gtk_main_quit();
    return G_SOURCE_REMOVE;
}

static void window_destroy(GtkWidget *widget, gpointer unused)
{
    (void)widget; (void)unused;
    app.stopping = TRUE;
    if (app.memory_self_test && !app.test_finished)
        finish_memory_test(FALSE);
    else if (app.self_test && !app.test_finished)
        finish_test(FALSE);
    else if (gtk_main_level())
        gtk_main_quit();
}

static gboolean permission_request(WebKitWebView *view,
                                    WebKitPermissionRequest *request,
                                    gpointer unused)
{
    (void)view; (void)unused;
    webkit_permission_request_deny(request);
    event("permission", "denied_in_probe");
    status_text("Ta wersja pomiarowa nie udostępnia uprawnień urządzeń.");
    return TRUE;
}

static void download_started(WebKitWebContext *context,
                              WebKitDownload *download, gpointer unused)
{
    (void)context; (void)unused;
    webkit_download_cancel(download);
    event("download", "cancelled_not_implemented");
    status_text("Menedżer pobierania nie jest jeszcze częścią prototypu.");
}

static gboolean navigation_policy(WebKitWebView *view,
                                   WebKitPolicyDecision *decision,
                                   WebKitPolicyDecisionType type,
                                   gpointer unused)
{
    (void)view; (void)unused;
    if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        WebKitNavigationAction *action =
            webkit_navigation_policy_decision_get_navigation_action(
                WEBKIT_NAVIGATION_POLICY_DECISION(decision));
        WebKitURIRequest *request = webkit_navigation_action_get_request(action);
        add_tab(webkit_uri_request_get_uri(request), TRUE);
        webkit_policy_decision_ignore(decision);
        event("new_window", "requested_tab");
        return TRUE;
    }
    return FALSE;
}

static void add_button(GtkWidget *box, const char *label, GCallback callback)
{
    GtkWidget *item = gtk_button_new_with_label(label);
    gtk_box_pack_start(GTK_BOX(box), item, FALSE, FALSE, 0);
    g_signal_connect(item, "clicked", callback, NULL);
}

static void update_tab_label(Tab *tab)
{
    const gchar *title = tab->view ? webkit_web_view_get_title(tab->view) : NULL;
    gchar *label;
    if (tab->closed)
        return;
    if (tab->suspended)
        label = g_strdup_printf("%u: odciążona", tab->number);
    else if (tab->process_dead)
        label = g_strdup_printf("%u: błąd strony", tab->number);
    else if (tab->text_mode)
        label = g_strdup_printf("%u: tekst", tab->number);
    else {
        gchar *short_title = title ? g_utf8_substring(title, 0,
            MIN((glong)48, g_utf8_strlen(title, -1))) : g_strdup("Nowa karta");
        label = g_strdup_printf("%u: %s", tab->number, short_title);
        g_free(short_title);
    }
    gtk_label_set_text(GTK_LABEL(tab->label), label);
    g_free(label);
}

static void ensure_view(Tab *tab)
{
    WebKitUserContentManager *manager;
    WebKitUserScript *script;
    WebKitSettings *settings;
    if (tab->view || tab->closed)
        return;
    clear_failure_notice(tab);
    if (tab->content) {
        gtk_widget_destroy(tab->content);
        tab->content = NULL;
    }
    tab->suspended = FALSE;
    tab->text_mode = FALSE;
    tab->navigation_complete = FALSE;
    tab->navigation_failed = FALSE;
    tab->process_dead = FALSE;
    manager = webkit_user_content_manager_new();
    g_signal_connect(manager, "script-message-received::p2ProbeError",
                      G_CALLBACK(script_error), NULL);
    webkit_user_content_manager_register_script_message_handler(manager, "p2ProbeError");
    script = webkit_user_script_new(
        "window.addEventListener('error',function(e){"
        "window.webkit.messageHandlers.p2ProbeError.postMessage("
        "'error:'+(e.lineno||0)+':'+(e.colno||0));});"
        "window.addEventListener('unhandledrejection',function(){"
        "window.webkit.messageHandlers.p2ProbeError.postMessage('unhandled_rejection');});"
        "window.addEventListener('error',function(e){var t=e.target;"
        "if(t&&(t.tagName==='VIDEO'||t.tagName==='AUDIO')&&t.error)"
        "window.webkit.messageHandlers.p2ProbeError.postMessage('media:error:'+t.error.code);},true);"
        "window.addEventListener('playing',function(e){var t=e.target;"
        "if(t&&(t.tagName==='VIDEO'||t.tagName==='AUDIO'))"
        "window.webkit.messageHandlers.p2ProbeError.postMessage('media:playing');},true);",
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, NULL, NULL);
    webkit_user_content_manager_add_script(manager, script);
    webkit_user_script_unref(script);
    tab->view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "web-context", app.context, "user-content-manager", manager, NULL));
    g_object_unref(manager);
    settings = webkit_web_view_get_settings(tab->view);
    webkit_settings_set_enable_javascript(settings, app.js_enabled);
    webkit_settings_set_auto_load_images(settings, app.images_enabled);
    webkit_settings_set_media_playback_requires_user_gesture(settings, TRUE);
    webkit_settings_set_enable_webgl(settings, FALSE);
    webkit_settings_set_enable_webaudio(settings, FALSE);
    webkit_settings_set_enable_page_cache(settings, FALSE);
    webkit_settings_set_enable_smooth_scrolling(settings, FALSE);
    webkit_settings_set_hardware_acceleration_policy(settings,
        WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
    webkit_web_view_set_is_muted(tab->view, tab != app.active);
    tab->content = GTK_WIDGET(tab->view);
    gtk_box_pack_start(GTK_BOX(tab->page), tab->content, TRUE, TRUE, 0);
    g_signal_connect(tab->view, "notify::uri", G_CALLBACK(uri_changed), NULL);
    g_signal_connect(tab->view, "notify::title", G_CALLBACK(title_changed), NULL);
    g_signal_connect(tab->view, "load-changed", G_CALLBACK(load_changed), NULL);
    g_signal_connect(tab->view, "load-failed", G_CALLBACK(load_failed), NULL);
    g_signal_connect(tab->view, "web-process-terminated", G_CALLBACK(process_terminated), NULL);
    g_signal_connect(tab->view, "permission-request", G_CALLBACK(permission_request), NULL);
    g_signal_connect(tab->view, "decide-policy", G_CALLBACK(navigation_policy), NULL);
    if (app.active == tab)
        app.view = tab->view;
    update_tab_label(tab);
    gtk_widget_show_all(tab->page);
}

static void notebook_switched(GtkNotebook *notebook, GtkWidget *page,
                              guint number, gpointer unused)
{
    Tab *tab = g_object_get_data(G_OBJECT(page), "p2-tab");
    gboolean reloaded;
    guint i;
    (void)notebook; (void)number; (void)unused;
    if (!tab || tab->closed || app.stopping)
        return;
    app.active = tab;
    reloaded = tab->suspended;
    if (reloaded)
        ensure_view(tab);
    app.view = tab->view;
    app.navigation_complete = tab->navigation_complete;
    app.navigation_failed = tab->navigation_failed;
    app.ready_reported = FALSE;
    for (i = 0; i < app.tabs->len; i++) {
        Tab *other = g_ptr_array_index(app.tabs, i);
        if (other->view)
            webkit_web_view_set_is_muted(other->view, other != tab);
    }
    gtk_entry_set_text(GTK_ENTRY(app.address), tab->uri ? tab->uri : "about:blank");
    if (reloaded) {
        gchar *uri = g_strdup(tab->uri ? tab->uri : "about:blank");
        webkit_web_view_load_uri(tab->view, uri);
        g_free(uri);
        status_text("Odciążona karta jest przeładowywana. Poprzedni stan formularzy nie został zachowany.");
        event("tab_resume", "reloaded");
    } else if (tab->text_mode)
        status_text("Uproszczony tekst. Odśwież, aby ponownie otworzyć pełną stronę.");
    else
        status_text("Karta aktywna. Ukryte karty są zarządzane przez mechanizmy WebKit.");
    event("tab", "selected");
}

static void close_tab_clicked(GtkButton *button, gpointer data)
{
    Tab *tab = data;
    gint number;
    guint i;
    (void)button;
    if (tab->closed || app.stopping)
        return;
    if (app.tabs->len == 1) {
        gtk_widget_destroy(app.window);
        return;
    }
    number = gtk_notebook_page_num(GTK_NOTEBOOK(app.notebook), tab->page);
    if (tab == app.active)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app.notebook), number ? number - 1 : 1);
    tab->closed = TRUE;
    tab->view = NULL;
    gtk_notebook_remove_page(GTK_NOTEBOOK(app.notebook), number);
    for (i = 0; i < app.tabs->len; i++) {
        if (g_ptr_array_index(app.tabs, i) == tab) {
            g_ptr_array_remove_index(app.tabs, i);
            break;
        }
    }
    event("tab", "closed");
}

static Tab *add_tab(const gchar *uri, gboolean activate)
{
    Tab *tab;
    GtkWidget *header, *close;
    gint number;
    if (!app.unlimited_tabs && app.tabs->len >= 5) {
        status_text("Osiągnięto limit pięciu kart. Możesz go zdjąć w ustawieniach pamięci.");
        event("tab_limit", "reached");
        return NULL;
    }
    tab = g_new0(Tab, 1);
    tab->references = 1;
    tab->number = ++app.next_tab;
    tab->uri = g_strdup(uri ? uri : "about:blank");
    tab->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    tab->label = gtk_label_new("Nowa karta");
    gtk_label_set_max_width_chars(GTK_LABEL(tab->label), 18);
    gtk_label_set_ellipsize(GTK_LABEL(tab->label), PANGO_ELLIPSIZE_END);
    header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    close = gtk_button_new_with_label("×");
    gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(close, "Zamknij kartę");
    gtk_box_pack_start(GTK_BOX(header), tab->label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(header), close, FALSE, FALSE, 0);
    g_signal_connect(close, "clicked", G_CALLBACK(close_tab_clicked), tab);
    g_object_set_data(G_OBJECT(tab->page), "p2-tab", tab);
    g_ptr_array_add(app.tabs, tab);
    ensure_view(tab);
    number = gtk_notebook_append_page(GTK_NOTEBOOK(app.notebook), tab->page, header);
    gtk_widget_show_all(header);
    gtk_widget_show_all(tab->page);
    if (activate)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app.notebook), number);
    if (uri) {
        gchar *load_uri = g_strdup(tab->uri);
        webkit_web_view_load_uri(tab->view, load_uri);
        g_free(load_uri);
    }
    event("tab", "created");
    return tab;
}

static guint reclaim_background_tabs(void)
{
    guint i, count = 0;
    for (i = 0; i < app.tabs->len; i++) {
        Tab *tab = g_ptr_array_index(app.tabs, i);
        GtkWidget *old;
        if (tab == app.active || tab->protected_tab || !tab->view || tab->closed)
            continue;
        old = GTK_WIDGET(tab->view);
        tab->view = NULL;
        tab->content = NULL;
        tab->suspended = TRUE;
        tab->navigation_complete = FALSE;
        clear_failure_notice(tab);
        gtk_widget_destroy(old);
        tab->content = gtk_label_new(
            "Karta została odciążona. Powrót przeładuje stronę; niezapisany stan formularzy jest utracony.");
        gtk_label_set_line_wrap(GTK_LABEL(tab->content), TRUE);
        gtk_box_pack_start(GTK_BOX(tab->page), tab->content, TRUE, TRUE, 0);
        gtk_widget_show(tab->content);
        update_tab_label(tab);
        count++;
    }
    if (count) {
        gchar *detail = g_strdup_printf("background_tabs=%u", count);
        webkit_web_context_clear_cache(app.context);
        event("memory_reclaim", detail);
        g_free(detail);
        status_text("Odciążono karty w tle. Ich ponowne otwarcie przeładuje stronę i nie przywróci formularzy.");
    }
    return count;
}

static void new_tab_clicked(GtkButton *button, gpointer unused)
{
    (void)button; (void)unused;
    add_tab("about:blank", TRUE);
}

static void dialog_close(GtkDialog *dialog, gint response, gpointer unused)
{
    (void)response; (void)unused;
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void reclaim_response(GtkDialog *dialog, gint response, gpointer data)
{
    GtkToggleButton *toggle = data;
    if (response == GTK_RESPONSE_YES) {
        if (toggle) {
            app.reclaim_background = TRUE;
            gtk_toggle_button_set_active(toggle, TRUE);
            event("memory_reclaim_policy", "enabled_with_state_loss");
            sample_memory(NULL);
        } else
            reclaim_background_tabs();
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
    if (toggle)
        g_object_unref(toggle);
}

static void show_reclaim_warning(GtkToggleButton *toggle)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app.window),
        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
        "Odciążanie usuwa zawartość kart w tle z silnika.");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
        "Adres i wspólny kontekst sesji pozostają, ale niezapisane formularze, stan aplikacji, historia karty i sessionStorage mogą zostać utracone. Powrót do karty przeładuje stronę. Nie jest to bezstratne zamrożenie do swapu.");
    if (toggle)
        g_object_ref(toggle);
    g_signal_connect(dialog, "response", G_CALLBACK(reclaim_response), toggle);
    gtk_widget_show(dialog);
}

static void reclaim_clicked(GtkButton *button, gpointer unused)
{
    (void)button; (void)unused;
    show_reclaim_warning(NULL);
}

static void reclaim_toggled(GtkToggleButton *toggle, gpointer unused)
{
    gboolean enabled = gtk_toggle_button_get_active(toggle);
    (void)unused;
    if (enabled && !app.reclaim_background) {
        gtk_toggle_button_set_active(toggle, FALSE);
        show_reclaim_warning(toggle);
        return;
    }
    app.reclaim_background = enabled;
    if (!enabled)
        event("memory_reclaim_policy", "disabled");
}

static void budget_changed(GtkAdjustment *adjustment, gpointer unused)
{
    gchar *detail;
    (void)unused;
    app.memory_limit_mib = (gint)gtk_adjustment_get_value(adjustment);
    detail = g_strdup_printf("soft_limit_mib=%d", app.memory_limit_mib);
    event("memory_budget", detail);
    g_free(detail);
    sample_memory(NULL);
}

static void unlimited_toggled(GtkToggleButton *toggle, gpointer unused)
{
    (void)unused;
    app.unlimited_tabs = gtk_toggle_button_get_active(toggle);
}

static void protect_toggled(GtkToggleButton *toggle, gpointer data)
{
    Tab *tab = data;
    if (!tab->closed)
        tab->protected_tab = gtk_toggle_button_get_active(toggle);
}

static void release_tab_closure(gpointer data, GClosure *closure)
{
    (void)closure;
    tab_unref(data);
}

static void settings_clicked(GtkButton *button, gpointer unused)
{
    GtkWidget *dialog, *box, *row, *label, *scale, *spin, *reclaim, *unlimited, *protect;
    GtkAdjustment *adjustment;
    (void)button; (void)unused;
    dialog = gtk_dialog_new_with_buttons("Pamięć i karty — prototyp", GTK_WINDOW(app.window),
        GTK_DIALOG_DESTROY_WITH_PARENT, "Zamknij", GTK_RESPONSE_CLOSE, NULL);
    box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_set_spacing(GTK_BOX(box), 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    label = gtk_label_new("Miękki budżet PSS całego drzewa procesów, w MiB.\n0 = bez budżetu. To nie jest twardy limit RAM; Linux zarządza swapem.");
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    adjustment = gtk_adjustment_new(app.memory_limit_mib, 0,
        MAX(4096, app.memory_limit_mib), 1, 16, 0);
    scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_size_request(scale, 240, -1);
    spin = gtk_spin_button_new(adjustment, 1, 0);
    gtk_box_pack_start(GTK_BOX(row), scale, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    g_signal_connect(adjustment, "value-changed", G_CALLBACK(budget_changed), NULL);
    unlimited = gtk_check_button_new_with_label("Zdejmij limit pięciu kart");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(unlimited), app.unlimited_tabs);
    g_signal_connect(unlimited, "toggled", G_CALLBACK(unlimited_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(box), unlimited, FALSE, FALSE, 0);
    reclaim = gtk_check_button_new_with_label("Po przekroczeniu budżetu odrzucaj stan kart w tle (wymaga zgody)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(reclaim), app.reclaim_background);
    g_signal_connect(reclaim, "toggled", G_CALLBACK(reclaim_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(box), reclaim, FALSE, FALSE, 0);
    if (app.active) {
        protect = gtk_check_button_new_with_label("Chroń bieżącą kartę przed odciążeniem");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(protect), app.active->protected_tab);
        g_signal_connect_data(protect, "toggled", G_CALLBACK(protect_toggled),
            tab_ref(app.active), release_tab_closure, 0);
        gtk_box_pack_start(GTK_BOX(box), protect, FALSE, FALSE, 0);
    }
    label = gtk_label_new("Tryb oszczędny wyłącza automatyczne obrazy i czyści cache.\nZmiana ustawień dotyczy bieżącego uruchomienia. Podgląd tekstowy jest dostępny przyciskiem Tekst.");
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    g_signal_connect(dialog, "response", G_CALLBACK(dialog_close), NULL);
    gtk_widget_show_all(dialog);
}

static void text_clicked(GtkButton *button, gpointer unused)
{
    (void)button; (void)unused;
    if (app.active)
        request_text(app.active);
}

static void build_window(void)
{
    GtkWidget *box, *toolbar, *memorybar, *js_toggle;
    app.tabs = g_ptr_array_new_with_free_func(tab_unref);
    app.context = webkit_web_context_new_ephemeral();
    webkit_web_context_set_sandbox_enabled(app.context, TRUE);
    webkit_web_context_set_cache_model(app.context, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
    g_signal_connect(app.context, "download-started", G_CALLBACK(download_started), NULL);
    event("graphics_policy", "hardware_acceleration_never_requested");
    event("sandbox", webkit_web_context_get_sandbox_enabled(app.context) ? "enabled" : "not_enabled");
    event("javascript", app.js_enabled ? "engine_enabled" : "disabled");
    event("scope", "tabs_and_soft_memory_budget");
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "P2Browser — prototyp oszczędzania pamięci");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 1000, 740);
    g_signal_connect(app.window, "destroy", G_CALLBACK(window_destroy), NULL);
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_container_add(GTK_CONTAINER(app.window), box);
    gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);
    add_button(toolbar, "Wstecz", G_CALLBACK(back_clicked));
    add_button(toolbar, "Dalej", G_CALLBACK(forward_clicked));
    add_button(toolbar, "Odśwież", G_CALLBACK(reload_clicked));
    add_button(toolbar, "Stop", G_CALLBACK(stop_clicked));
    app.address = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.address), "Adres strony");
    gtk_box_pack_start(GTK_BOX(toolbar), app.address, TRUE, TRUE, 0);
    g_signal_connect(app.address, "activate", G_CALLBACK(address_activate), NULL);
    js_toggle = gtk_check_button_new_with_label("JS silnika");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(js_toggle), app.js_enabled);
    gtk_box_pack_start(GTK_BOX(toolbar), js_toggle, FALSE, FALSE, 0);
    g_signal_connect(js_toggle, "toggled", G_CALLBACK(js_toggled), NULL);
    app.images_toggle = gtk_check_button_new_with_label("Obrazy");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.images_toggle), app.images_enabled);
    gtk_box_pack_start(GTK_BOX(toolbar), app.images_toggle, FALSE, FALSE, 0);
    g_signal_connect(app.images_toggle, "toggled", G_CALLBACK(images_toggled), NULL);
    memorybar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(box), memorybar, FALSE, FALSE, 0);
    add_button(memorybar, "+ Karta", G_CALLBACK(new_tab_clicked));
    add_button(memorybar, "Pamięć", G_CALLBACK(settings_clicked));
    add_button(memorybar, "Odciąż tło", G_CALLBACK(reclaim_clicked));
    add_button(memorybar, "Tekst", G_CALLBACK(text_clicked));
    app.memory_status = gtk_label_new("PSS: pomiar całego drzewa procesów…");
    gtk_box_pack_start(GTK_BOX(memorybar), app.memory_status, FALSE, FALSE, 6);
    gtk_widget_set_tooltip_text(app.memory_status,
        "PSS dzieli strony współdzielone; nie obejmuje całego Xorg i kernela. Brak danych nie oznacza zera.");
    app.notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(app.notebook), TRUE);
    gtk_box_pack_start(GTK_BOX(box), app.notebook, TRUE, TRUE, 0);
    g_signal_connect(app.notebook, "switch-page", G_CALLBACK(notebook_switched), NULL);
    app.status = gtk_label_new("Prototyp. Odciążanie kart oznacza późniejsze przeładowanie, nie bezstratne usypianie.");
    gtk_label_set_xalign(GTK_LABEL(app.status), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(app.status), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(box), app.status, FALSE, FALSE, 0);
    add_tab(NULL, TRUE);
    gtk_widget_show_all(app.window);
}

int main(int argc, char **argv)
{
    gchar *report_dir = NULL, *data_dir = NULL;
    gchar **remaining = NULL, **background = NULL;
    guint i;
    gboolean version = FALSE;
    gint quit_after = 0;
    GError *error = NULL;
    GOptionContext *options;
    GOptionEntry entries[] = {
        {"version", 0, 0, G_OPTION_ARG_NONE, &version, "Wersja bez GUI", NULL},
        {"javascript", 0, 0, G_OPTION_ARG_NONE, &app.js_enabled, "Włącz JS silnika", NULL},
        {"images", 0, 0, G_OPTION_ARG_NONE, &app.images_enabled, "Włącz automatyczne obrazy", NULL},
        {"video-compat", 0, 0, G_OPTION_ARG_NONE, &app.video_compat, "Diagnostyka wideo: wyłącz wyjście GL i DMA-BUF (bez wymuszania dekodera)", NULL},
        {"memory-limit-mib", 0, 0, G_OPTION_ARG_INT, &app.memory_limit_mib, "Miękki budżet PSS; 0 = bez budżetu", "MiB"},
        {"reclaim-background", 0, 0, G_OPTION_ARG_NONE, &app.reclaim_background, "Zgoda na utratę stanu kart tła przy przekroczeniu budżetu", NULL},
        {"unlimited-tabs", 0, 0, G_OPTION_ARG_NONE, &app.unlimited_tabs, "Zdejmij limit pięciu kart", NULL},
        {"background-tab", 0, 0, G_OPTION_ARG_STRING_ARRAY, &background, "Otwórz dodatkową kartę w tle", "URL"},
        {"fixture-ready-title", 0, 0, G_OPTION_ARG_STRING, &app.ready_title, "Test: potwierdź zgodność tytułu po załadowaniu, bez logowania jego treści", "TITLE"},
        {"report-dir", 0, 0, G_OPTION_ARG_FILENAME, &report_dir, "Nowy katalog raportu", "DIR"},
        {"data-dir", 0, 0, G_OPTION_ARG_FILENAME, &data_dir, "Katalog strony testowej", "DIR"},
        {"self-test", 0, 0, G_OPTION_ARG_NONE, &app.self_test, "Test lokalnego HTML/CSS/JS", NULL},
        {"memory-self-test", 0, 0, G_OPTION_ARG_NONE, &app.memory_self_test, "Test lokalny limitu kart, odciążania, wznowienia i tekstu", NULL},
        {"quit-after", 0, 0, G_OPTION_ARG_INT, &quit_after, "Zakończ po podanej liczbie sekund", "SECONDS"},
        {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &remaining, "Adres strony", "URL"},
        {NULL, 0, 0, 0, NULL, NULL, NULL}
    };
    umask(0077);
    options = g_option_context_new("[URL] — lokalny prototyp pomiarowy");
    g_option_context_add_main_entries(options, entries, NULL);
    if (!g_option_context_parse(options, &argc, &argv, &error)) {
        g_printerr("Błąd argumentów: %s\n", error->message);
        g_error_free(error);
        g_option_context_free(options);
        return 2;
    }
    g_option_context_free(options);
    if (version) {
        g_print("p2browser-probe %s; WebKitGTK %u.%u.%u; %u-bit\n",
                P2_VERSION, webkit_get_major_version(), webkit_get_minor_version(),
                webkit_get_micro_version(), (unsigned)(sizeof(void *) * 8));
        return 0;
    }
    if (quit_after < 0 || quit_after > 86400 || app.memory_limit_mib < 0 ||
        app.memory_limit_mib > 65536 ||
        (app.ready_title && (!*app.ready_title || strlen(app.ready_title) > 256)) ||
        (!app.unlimited_tabs && background && g_strv_length(background) > 4) ||
        (remaining && remaining[0] && remaining[1]) ||
        ((app.self_test || app.memory_self_test) && ((remaining && remaining[0]) || background)) ||
        (app.memory_self_test && (app.self_test || app.unlimited_tabs ||
                                 app.reclaim_background || app.memory_limit_mib))) {
        g_printerr("Nieprawidłowy czas, budżet, liczba adresów lub połączenie opcji testu. Testy używają wyłącznie lokalnej strony.\n");
        return 2;
    }
    for (i = 0; background && background[i]; i++) {
        gchar *scheme = g_uri_parse_scheme(background[i]);
        if (scheme && g_ascii_strcasecmp(scheme, "http") &&
            g_ascii_strcasecmp(scheme, "https") && g_ascii_strcasecmp(scheme, "file") &&
            g_ascii_strcasecmp(scheme, "about")) {
            g_printerr("Nieobsługiwany schemat adresu karty w tle.\n");
            g_free(scheme);
            return 2;
        }
        if (!scheme) {
            gchar *uri = g_strconcat("https://", background[i], NULL);
            g_free(background[i]);
            background[i] = uri;
        }
        g_free(scheme);
    }
    app.started = g_get_monotonic_time();
    /* Set before GTK/GIO threads and before the engine creates subprocesses. */
    if (app.video_compat) {
        g_setenv("WEBKIT_GST_DISABLE_GL_SINK", "1", TRUE);
        g_setenv("WEBKIT_GST_DMABUF_SINK_DISABLED", "1", TRUE);
    }
    if (!make_report(report_dir, &error)) {
        g_printerr("Błąd raportu: %s\n", error->message);
        g_clear_error(&error);
        return 2;
    }
    g_print("Raport: %s\nPID interfejsu: %ld\n", app.report_dir, (long)getpid());
    event("video_output", app.video_compat ? "compat_gl_sink_disabled" : "engine_default");
    fflush(stdout);
    if (!gtk_init_check(&argc, &argv)) {
        event("startup_failed", "display_unavailable");
        g_printerr("Brak dostępnego ekranu. Raport zapisano.\n");
        fclose(app.events);
        return 3;
    }
    app.data_dir = g_strdup(data_dir ? data_dir : P2_DATA_DIR);
    if (app.self_test && !quit_after)
        quit_after = 30;
    if (app.memory_self_test && !quit_after)
        quit_after = 120;
    build_window();
    app.timer = g_timeout_add_seconds(2, sample_memory, NULL);
    if (quit_after)
        app.watchdog = g_timeout_add_seconds((guint)quit_after, quit_timeout, NULL);
    if (remaining && remaining[0]) {
        navigate(remaining[0]);
        for (i = 0; background && background[i]; i++)
            add_tab(background[i], FALSE);
        gtk_main();
    } else {
        gchar *uri = fixture_uri();
        if (!uri) {
            event("startup_failed", "fixture_path");
            app.exit_code = 2;
        } else {
            navigate(uri);
            if (app.memory_self_test) {
                for (i = 0; i < 4; i++)
                    add_tab(uri, FALSE);
            } else {
                for (i = 0; background && background[i]; i++)
                    add_tab(background[i], FALSE);
            }
            gtk_main();
            g_free(uri);
        }
    }
    if (app.timer)
        g_source_remove(app.timer);
    if (app.watchdog)
        g_source_remove(app.watchdog);
    for (i = 0; i < app.tabs->len; i++)
        ((Tab *)g_ptr_array_index(app.tabs, i))->closed = TRUE;
    if (!app.stopping)
        gtk_widget_destroy(app.window);
    g_ptr_array_unref(app.tabs);
    app.tabs = NULL;
    g_object_unref(app.context);
    event("probe", "shutdown");
    fclose(app.events);
    g_free(app.report_dir);
    g_free(app.data_dir);
    g_free(app.ready_title);
    g_free(report_dir);
    g_free(data_dir);
    g_strfreev(remaining);
    g_strfreev(background);
    return app.exit_code;
}
