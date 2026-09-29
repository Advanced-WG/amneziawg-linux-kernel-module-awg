// SPDX-License-Identifier: GPL-2.0
#include "junk.h"
#include "messages.h"
#include "peer.h"

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/random.h>
#include <linux/ktime.h>
#include <linux/ctype.h>

static int parse_b_tag(char* val, struct list_head* head) {
    int err;
    int i;
    int len;
    u8* pkt;
    struct jp_tag* tag;

    if (!val || strncmp(val, "0x", 2))
        return -EINVAL;
    val += 2;

    len = strlen(val);
    if (len == 0 || len % 2 != 0)
        return -EINVAL;
    len /= 2;

    pkt = kmalloc(len, GFP_KERNEL);
    if (!pkt)
        return -ENOMEM;

    for (i = len - 1; i >= 0; --i) {
        err = kstrtou8(val + i * 2, 16, pkt + i);
        if (err) {
            err = -EINVAL;
            goto error;
        }

        /* NUL-terminate so next kstrtou8 only sees the current 2-char hex pair */
        val[i * 2] = '\0';
    }

    tag = kzalloc(sizeof(*tag), GFP_KERNEL);
    if (!tag) {
        err = -ENOMEM;
        goto error;
    }

    tag->pkt = pkt;
    tag->pkt_size = len;

    list_add(&tag->head, head);
    return 0;

error:
    kfree(pkt);
    return err;
}

static void pkt_counter_modifier(char* buf, int len, struct wg_peer *peer) {
    int val = atomic_read(&peer->jp_packet_counter);
    val = htonl(val);
    memcpy(buf, &val, sizeof(val));
}

static int parse_c_tag(char* val, struct list_head* head) {
    struct jp_tag* tag;

    if (val)
        return -EINVAL;

    tag = kzalloc(sizeof(*tag), GFP_KERNEL);
    if (!tag)
        return -ENOMEM;

    tag->pkt_size = sizeof(u32);
    tag->func = pkt_counter_modifier;

    list_add(&tag->head, head);
    return 0;
}

static void unix_time_modifier(char* buf, int len, struct wg_peer *peer) {
    u32 time = (u32)ktime_get_real_seconds();
    time = htonl(time);
    memcpy(buf, &time, sizeof(time));
}

static int parse_t_tag(char* val, struct list_head* head) {
    struct jp_tag* tag;

    if (val)
        return -EINVAL;

    tag = kzalloc(sizeof(*tag), GFP_KERNEL);
    if (!tag)
        return -ENOMEM;

    tag->pkt_size = sizeof(u32);
    tag->func = unix_time_modifier;

    list_add(&tag->head, head);
    return 0;
}

static void random_byte_modifier(char* buf, int len, struct wg_peer *peer) {
    get_random_bytes(buf, len);
}

static int parse_r_tag(char* val, struct list_head* head) {
    struct jp_tag* tag;
    int len;

    if (!val || kstrtoint(val, 10, &len) < 0 || len <= 0)
        return -EINVAL;

    tag = kzalloc(sizeof(*tag), GFP_KERNEL);
    if (!tag)
        return -ENOMEM;

    tag->pkt_size = len;
    tag->func = random_byte_modifier;

    list_add(&tag->head, head);
    return 0;
}

#define ALPHABET_LEN 26
#define LETTER_LEN (ALPHABET_LEN * 2)

static void random_char_modifier(char* buf, int len, struct wg_peer *peer) {
    int i;
    u32 byte;

    for (i = 0; i < len; ++i) {
        byte = get_random_u32() % LETTER_LEN;
        buf[i] = (byte < ALPHABET_LEN) ? 'a' + byte : 'A' + byte - ALPHABET_LEN;
    }
}

static int parse_rc_tag(char* val, struct list_head* head) {
    struct jp_tag* tag;
    int len;

    if (!val || kstrtoint(val, 10, &len) < 0 || len <= 0)
        return -EINVAL;

    tag = kzalloc(sizeof(*tag), GFP_KERNEL);
    if (!tag)
        return -ENOMEM;

    tag->pkt_size = len;
    tag->func = random_char_modifier;

    list_add(&tag->head, head);
    return 0;
}

#define DIGIT_LEN 10

static void random_digit_modifier(char* buf, int len, struct wg_peer *peer) {
    int i;

    for (i = 0; i < len; ++i)
        buf[i] = '0' + get_random_u32() % DIGIT_LEN;
}

static int parse_rd_tag(char* val, struct list_head* head) {
    struct jp_tag* tag;
    int len;

    if (!val || kstrtoint(val, 10, &len) < 0 || len <= 0)
        return -EINVAL;

    tag = kzalloc(sizeof(*tag), GFP_KERNEL);
    if (!tag)
        return -ENOMEM;

    tag->pkt_size = len;
    tag->func = random_digit_modifier;

    list_add(&tag->head, head);
    return 0;
}

/* Only whitespace may separate tags; anything else is a typo. */
static bool jp_is_blank(const char* s) {
    for (; *s; ++s)
        if (!isspace(*s))
            return false;
    return true;
}

int jp_parse_tags(char* str, struct list_head* head) {
    int err = 0;
    char* key;
    char* val;
    char* text;

    while (true)
    {
        text = strsep(&str, "<");
        if (!jp_is_blank(text))
            return -EINVAL;
        if (!str)
            break;
        val = strsep(&str, ">");
        if (!str)
            return -EINVAL; /* unclosed tag */

        key = strsep(&val, " ");

        if (!strcmp(key, "b")) {
            err = parse_b_tag(val, head);
            if (err)
                return err;
        }
        else if (!strcmp(key, "c")) {
            err = parse_c_tag(val, head);
            if (err)
                return err;
        }
        else if (!strcmp(key, "t")) {
            err = parse_t_tag(val, head);
            if (err)
                return err;
        }
        else if (!strcmp(key, "r")) {
            err = parse_r_tag(val, head);
            if (err)
                return err;
        }
        else if (!strcmp(key, "rc")) {
            err = parse_rc_tag(val, head);
            if (err)
                return err;
        }
        else if (!strcmp(key, "rd")) {
            err = parse_rd_tag(val, head);
            if (err)
                return err;
        }
        else
            return -EINVAL;
    }

    return 0;
}

void jp_tag_free(struct jp_tag* tag) {
    kfree(tag->pkt);
}

void jp_spec_free(struct jp_spec *spec) {
    mutex_lock(&spec->lock);
    kfree(spec->desc);
    kfree(spec->pkt);
    kfree(spec->mods);
    spec->desc = NULL;
    spec->pkt = NULL;
    spec->mods = NULL;
    spec->pkt_size = 0;
    spec->mods_size = 0;
    mutex_unlock(&spec->lock);
}

static void jp_tags_free(struct list_head *head) {
    struct jp_tag *tag, *tmp;

    list_for_each_entry_safe(tag, tmp, head, head) {
        jp_tag_free(tag);
        list_del(&tag->head);
        kfree(tag);
    }
}

/* Total packet size and modifier count of a parsed tag list. Each tag is
 * checked against the space still left before it is added, so the int sum
 * cannot overflow: <r 2147483647><r 2147483647><b 0x0102> used to wrap to 0,
 * kzalloc(0) returned ZERO_SIZE_PTR and the copy into it oopsed.
 */
static int jp_tags_size(struct list_head *head, int *pkt_size, int *mods_size) {
    struct jp_tag *tag;

    *pkt_size = 0;
    *mods_size = 0;

    list_for_each_entry(tag, head, head) {
        if (tag->pkt_size <= 0 || tag->pkt_size > MESSAGE_MAX_SIZE - *pkt_size)
            return -EINVAL;

        *pkt_size += tag->pkt_size;

        if (tag->func)
            ++*mods_size;
    }

    return 0;
}

void jp_built_free(struct jp_built *built) {
    kfree(built->pkt);
    kfree(built->mods);
    memset(built, 0, sizeof(*built));
}

/* Parses an I1-I5 description into a packet and its modifiers without
 * touching any spec, so a configuration can be fully prepared (and fail)
 * before anything is applied. An empty description builds an empty packet.
 */
int jp_spec_build(const char *desc, struct jp_built *out) {
    int err, pkt_size, mods_size;
    struct jp_tag *tag;
    struct jp_modifier *mod;
    char *buf;
    LIST_HEAD(head);

    memset(out, 0, sizeof(*out));

    buf = kstrdup(desc, GFP_KERNEL);
    if (!buf)
        return -ENOMEM;

    err = jp_parse_tags(buf, &head);
    if (err)
        goto out;

    err = jp_tags_size(&head, &pkt_size, &mods_size);
    if (err || !pkt_size)
        goto out;

    out->pkt = kzalloc(pkt_size, GFP_KERNEL);
    out->mods = kcalloc(max(mods_size, 1), sizeof(*out->mods), GFP_KERNEL);
    if (!out->pkt || !out->mods) {
        err = -ENOMEM;
        goto out;
    }

    list_for_each_entry_reverse(tag, &head, head) {
        if (tag->pkt)
            memcpy(out->pkt + out->pkt_size, tag->pkt, tag->pkt_size);

        if (tag->func) {
            mod = out->mods + out->mods_size++;
            mod->func = tag->func;
            mod->buf = out->pkt + out->pkt_size;
            mod->buf_len = tag->pkt_size;
        }

        out->pkt_size += tag->pkt_size;
    }

out:
    if (err)
        jp_built_free(out);
    jp_tags_free(&head);
    kfree(buf);
    return err;
}

/* Replaces the spec's description and packet. Takes ownership of desc and of
 * the built packet, and clears *built.
 */
void jp_spec_install(struct jp_spec *spec, char *desc, struct jp_built *built) {
    mutex_lock(&spec->lock);
    kfree(spec->desc);
    kfree(spec->pkt);
    kfree(spec->mods);
    spec->desc = desc;
    spec->pkt = built->pkt;
    spec->mods = built->mods;
    spec->pkt_size = built->pkt_size;
    spec->mods_size = built->mods_size;
    mutex_unlock(&spec->lock);
    memset(built, 0, sizeof(*built));
}

/* Caller must hold spec->lock */
void jp_spec_applymods(struct jp_spec* spec, struct wg_peer* peer) {
    int i;
    struct jp_modifier* mod;

    if (!spec->mods || !spec->mods_size)
        return;

    for (i = 0; i < spec->mods_size; i++) {
        mod = &spec->mods[i];
        if (mod->func && mod->buf)
            mod->func(mod->buf, mod->buf_len, peer);
    }
}
