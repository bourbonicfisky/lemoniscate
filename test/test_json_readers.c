/*
 * test_json_readers.c - Regression tests for the minimal JSON readers.
 *
 * The board/news readers used to require "key":"value" (no space) while the
 * writers emit "key": "value", so the server could not read back its own
 * MessageBoard.jsonl and News/ files after a restart. These tests write data
 * through the real writers and read it back through the real loaders.
 */

#include "mobius/json_builder.h"
#include "mobius/flat_news.h"
#include "mobius/jsonl_message_board.h"
#include "mobius/dir_threaded_news.h"
#include "mobius/threaded_news_yaml.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-60s ", #name); \
        fflush(stdout); \
        name(); \
        tests_passed++; \
        printf("PASS\n"); \
    } while (0)

static char tmpdir[256];

static void make_tmpdir(void)
{
    strcpy(tmpdir, "/tmp/test_json_readers_XXXXXX");
    assert(mkdtemp(tmpdir) != NULL);
}

static void rm_rf(const char *path)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    (void)system(cmd);
}

static void write_file(const char *path, const char *data)
{
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    fwrite(data, 1, strlen(data), f);
    fclose(f);
}

/* ===== json_find_value / json_read_string ===== */

static void test_find_value_compact_and_spaced(void)
{
    const char *p;
    p = json_find_value("{\"id\":7,\"nick\":\"a\"}", "nick");
    assert(p && *p == '"');
    p = json_find_value("{\"id\": 7, \"nick\": \"a\"}", "nick");
    assert(p && *p == '"');
    p = json_find_value("{\n  \"id\" :\t7\n}", "id");
    assert(p && atoi(p) == 7);
    assert(json_find_value("{\"id\": 7}", "nick") == NULL);
}

static void test_find_value_skips_key_text_inside_values(void)
{
    /* "nick" appears inside the body string before the real key */
    const char *j = "{\"body\": \"say \\\"nick\\\": \\\"evil\\\"\", \"nick\": \"real\"}";
    char out[32];
    assert(json_read_string(j, "nick", out, sizeof(out)) == 0);
    assert(strcmp(out, "real") == 0);
}

static void test_read_string_escapes(void)
{
    char out[64];
    const char *j = "{\"s\": \"a\\\"b\\\\c\\rd\\ne\\tf\\u0001g\"}";
    assert(json_read_string(j, "s", out, sizeof(out)) == 0);
    assert(strcmp(out, "a\"b\\c\rd\ne\tf\001g") == 0);
}

static void test_read_string_keeps_unknown_escapes(void)
{
    /* Older builds wrote raw backslashes (ASCII art); keep them as-is. */
    char out[64];
    assert(json_read_string("{\"s\": \"|_| \\|/ _ \\_\\\\x\"}", "s", out, sizeof(out)) == 0);
    assert(strcmp(out, "|_| \\|/ _ \\_\\x") == 0);
}

static void test_read_string_truncates_safely(void)
{
    char out[4];
    assert(json_read_string("{\"s\": \"abcdef\"}", "s", out, sizeof(out)) == 0);
    assert(strcmp(out, "abc") == 0);
}

static void test_read_string_alloc_matches(void)
{
    char *s = json_read_string_alloc("{\"s\": \"x\\ry\"}", "s");
    assert(s && strcmp(s, "x\ry") == 0);
    free(s);
    assert(json_read_string_alloc("{\"n\": 1}", "s") == NULL);
    assert(json_read_string_alloc("{\"s\": 1}", "s") == NULL);
}

/* ===== Message board round trip ===== */

static mb_post_t *load_board(const char *path, int *count)
{
    mb_post_t *posts = NULL;
    mobius_flat_news_t *fn = mobius_jsonl_news_new(path);
    assert(fn != NULL);
    assert(mobius_jsonl_get_posts(fn, &posts, count) >= 0);
    mobius_flat_news_free(fn);
    return posts;
}

static void test_board_survives_restart(void)
{
    char path[512];
    int count = 0;
    mb_post_t *posts;
    const char *post = "Greg (Nov 3)\r\rHello \"world\"\rsecond line \\ done";

    make_tmpdir();
    snprintf(path, sizeof(path), "%s/MessageBoard.jsonl", tmpdir);

    /* Post through the server's own writer... */
    {
        mobius_flat_news_t *fn = mobius_jsonl_news_new(path);
        assert(fn != NULL);
        assert(mobius_flat_news_prepend(fn, post, strlen(post)) == 0);
        mobius_flat_news_free(fn);
    }

    /* ...then "restart" and read it back from disk. */
    posts = load_board(path, &count);
    assert(count == 1);
    assert(posts[0].id == 1);
    assert(strcmp(posts[0].nick, "Greg") == 0);
    assert(posts[0].ts[0] != '\0');
    assert(strcmp(posts[0].body, "Hello \"world\"\rsecond line \\ done") == 0);
    mobius_jsonl_free_posts(posts, count);
    rm_rf(tmpdir);
}

static void test_board_reads_both_formats(void)
{
    char path[512];
    int count = 0;
    mb_post_t *posts;

    make_tmpdir();
    snprintf(path, sizeof(path), "%s/MessageBoard.jsonl", tmpdir);
    write_file(path,
        "{\"id\": 1, \"login\": \"\", \"nick\": \"spaced\", \"body\": \"one\", \"ts\": \"2026-04-16T23:00:27Z\"}\n"
        "{\"id\":2,\"login\":\"\",\"nick\":\"compact\",\"body\":\"two\",\"ts\":\"2026-04-17T00:00:00Z\"}\n");
    posts = load_board(path, &count);
    assert(count == 2);
    assert(strcmp(posts[0].nick, "spaced") == 0 && strcmp(posts[0].body, "one") == 0);
    assert(strcmp(posts[0].ts, "2026-04-16T23:00:27Z") == 0);
    assert(strcmp(posts[1].nick, "compact") == 0 && strcmp(posts[1].body, "two") == 0);
    mobius_jsonl_free_posts(posts, count);
    rm_rf(tmpdir);
}

static void test_board_long_post(void)
{
    char path[512];
    int count = 0;
    mb_post_t *posts;
    size_t n = 50000, i;
    char *line = (char *)malloc(n + 200);
    char *expect = (char *)malloc(n + 1);

    assert(line && expect);
    for (i = 0; i < n; i++) expect[i] = (char)('a' + (i % 26));
    expect[n] = '\0';
    sprintf(line, "{\"id\": 1, \"nick\": \"big\", \"body\": \"%s\", \"ts\": \"2026-01-01T00:00:00Z\"}\n", expect);

    make_tmpdir();
    snprintf(path, sizeof(path), "%s/MessageBoard.jsonl", tmpdir);
    write_file(path, line);
    posts = load_board(path, &count);
    assert(count == 1);
    assert(strlen(posts[0].body) == n);
    assert(strcmp(posts[0].body, expect) == 0);
    mobius_jsonl_free_posts(posts, count);
    free(line);
    free(expect);
    rm_rf(tmpdir);
}

static void test_board_migration_escapes_bodies(void)
{
    char txt[512], jsonl[512];
    int count = 0, i, found = 0;
    mb_post_t *posts;

    make_tmpdir();
    snprintf(txt, sizeof(txt), "%s/MessageBoard.txt", tmpdir);
    snprintf(jsonl, sizeof(jsonl), "%s/MessageBoard.jsonl", tmpdir);
    /* Classic flat board: newest first, CR line endings, a quote in the body */
    write_file(txt,
        "dmg (Apr 16)\r\rline one\rsays \"hi\"\rline three"
        "\r_____________________________________________\r"
        "guest (Apr 15)\r\rolder post");
    assert(mobius_migrate_flat_to_jsonl(txt, jsonl) == 0);

    posts = load_board(jsonl, &count);
    assert(count == 2);
    for (i = 0; i < count; i++) {
        if (strcmp(posts[i].nick, "dmg") == 0) {
            assert(strcmp(posts[i].body, "line one\rsays \"hi\"\rline three") == 0);
            found++;
        }
        if (strcmp(posts[i].nick, "guest") == 0) {
            assert(strncmp(posts[i].body, "older post", 10) == 0);
            found++;
        }
    }
    assert(found == 2);
    mobius_jsonl_free_posts(posts, count);
    rm_rf(tmpdir);
}

/* ===== Threaded news round trip ===== */

static void test_news_survives_restart(void)
{
    char cat_dir[512];
    tn_category_t *cat = (tn_category_t *)calloc(1, sizeof(*cat));
    tn_article_t *art = (tn_article_t *)calloc(1, sizeof(*art));
    mobius_threaded_news_t *tn;
    int i;

    assert(cat && art);
    make_tmpdir();
    snprintf(cat_dir, sizeof(cat_dir), "%s/General Discussion", tmpdir);
    mkdir(cat_dir, 0755);

    strcpy(cat->name, "General Discussion");
    cat->type[1] = 3;
    for (i = 0; i < 16; i++) cat->guid[i] = (uint8_t)(i + 1);
    assert(tn_dir_save_meta(tmpdir, cat->name, cat) == 0);

    art->id = 1;
    art->parent_id = 0;
    strcpy(art->title, "Success!");
    strcpy(art->poster, "dmg");
    strcpy(art->data, "Line one\rLine \"two\"");
    art->data_len = (uint16_t)strlen(art->data);
    art->active = 1;
    assert(tn_dir_save_article(tmpdir, cat->name, art) == 0);

    tn = mobius_dir_news_new(tmpdir);
    assert(tn != NULL);
    assert(tn->category_count == 1);
    assert(strcmp(tn->categories[0].name, "General Discussion") == 0);
    for (i = 0; i < 16; i++) assert(tn->categories[0].guid[i] == (uint8_t)(i + 1));
    assert(tn->categories[0].article_count >= 1);
    {
        tn_article_t *a = &tn->categories[0].articles[0];
        assert(a->id == 1);
        assert(strcmp(a->title, "Success!") == 0);
        assert(strcmp(a->poster, "dmg") == 0);
        assert(a->data_len == strlen("Line one\rLine \"two\""));
        assert(memcmp(a->data, "Line one\rLine \"two\"", a->data_len) == 0);
    }
    mobius_threaded_news_free(tn);
    free(cat);
    free(art);
    rm_rf(tmpdir);
}

int main(void)
{
    printf("JSON reader tests:\n");
    TEST(test_find_value_compact_and_spaced);
    TEST(test_find_value_skips_key_text_inside_values);
    TEST(test_read_string_escapes);
    TEST(test_read_string_keeps_unknown_escapes);
    TEST(test_read_string_truncates_safely);
    TEST(test_read_string_alloc_matches);
    TEST(test_board_survives_restart);
    TEST(test_board_reads_both_formats);
    TEST(test_board_long_post);
    TEST(test_board_migration_escapes_bodies);
    TEST(test_news_survives_restart);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
