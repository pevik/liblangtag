/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * lt-tag.c
 * Copyright (C) 2011-2012 Akira TAGOH
 * 
 * Authors:
 *   Akira TAGOH  <akira@tagoh.org>
 * 
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <langinfo.h>
#include <locale.h>
#include <string.h>
#include <libxml/xpath.h>
#include "lt-database.h"
#include "lt-error.h"
#include "lt-ext-module-private.h"
#include "lt-extension-private.h"
#include "lt-localealias.h"
#include "lt-mem.h"
#include "lt-utils.h"
#include "lt-xml.h"
#include "lt-tag.h"
#include "lt-tag-private.h"


/**
 * SECTION: lt-tag
 * @Short_Description: A container class for Language tag
 * @Title: Container - Tag
 *
 * This container class provides an interface to deal with the language tag.
 */
typedef struct _lt_tag_scanner_t {
	lt_mem_t  parent;
	gchar    *string;
	gsize     length;
	gsize     position;
} lt_tag_scanner_t;

struct _lt_tag_t {
	lt_mem_t            parent;
	gint32              wildcard_map;
	lt_tag_state_t      state;
	GString            *tag_string;
	lt_lang_t          *language;
	lt_extlang_t       *extlang;
	lt_script_t        *script;
	lt_region_t        *region;
	GList              *variants;
	lt_extension_t     *extension;
	GString            *privateuse;
	lt_grandfathered_t *grandfathered;
};

/*< private >*/
static gboolean
_lt_tag_gstring_compare(const GString *v1,
			const GString *v2)
{
	gboolean retval = FALSE;
	gchar *s1, *s2;

	if (v1 == v2)
		return TRUE;

	s1 = v1 ? lt_strlower(g_strdup(v1->str)) : NULL;
	s2 = v2 ? lt_strlower(g_strdup(v2->str)) : NULL;

	if (g_strcmp0(s1, "*") == 0 ||
	    g_strcmp0(s2, "*") == 0) {
		retval = TRUE;
		goto bail;
	}

	retval = g_strcmp0(s1, s2) == 0;
  bail:
	g_free(s1);
	g_free(s2);

	return retval;
}

static void
_lt_tag_variants_list_free(GList *list)
{
	GList *l;

	for (l = list; l != NULL; l = g_list_next(l)) {
		lt_variant_unref(l->data);
	}
	g_list_free(list);
}

static lt_tag_scanner_t *
lt_tag_scanner_new(const gchar *tag)
{
	lt_tag_scanner_t *retval = lt_mem_alloc_object(sizeof (lt_tag_scanner_t));

	if (retval) {
		retval->string = g_strdup(tag);
		lt_mem_add_ref(&retval->parent, retval->string,
			       (lt_destroy_func_t)g_free);
		retval->length = strlen(tag);
	}

	return retval;
}

static void
lt_tag_scanner_unref(lt_tag_scanner_t *scanner)
{
	if (scanner)
		lt_mem_unref(&scanner->parent);
}

static gboolean
lt_tag_scanner_get_token(lt_tag_scanner_t  *scanner,
			 gchar            **retval,
			 gsize             *length,
			 GError           **error)
{
	GString *string = NULL;
	gchar c;
	GError *err = NULL;

	g_return_val_if_fail (scanner != NULL, FALSE);

	if (scanner->position >= scanner->length) {
		g_set_error(&err, LT_ERROR, LT_ERR_EOT,
			    "No more tokens in buffer");
		goto bail;
	}

	string = g_string_new(NULL);
	while (scanner->position < scanner->length) {
		c = scanner->string[scanner->position++];
		if (c == 0) {
			if (string->len == 0) {
				g_set_error(&err, LT_ERROR, LT_ERR_EOT,
					    "No more tokens in buffer");
			}
			scanner->position--;
			break;
		}
		if (c == '*') {
			if (string->len > 0) {
				g_set_error(&err, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
					    "Invalid wildcard: positon = %" G_GSIZE_FORMAT,
					    scanner->position - 1);
				break;
			}
		} else if (!g_ascii_isalnum(c) && c != '-' && c != 0) {
			g_set_error(&err, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
				    "Invalid character for tag: '%c'", c);
			break;
		}
		g_string_append_c(string, c);

		if (c == '-' ||
		    c == '*')
			break;
		if (scanner->string[scanner->position] == '-' ||
		    scanner->string[scanner->position] == 0)
			break;
	}
  bail:
	if (err) {
		if (error)
			*error = g_error_copy(err);
		else
			g_warning(err->message);
		g_error_free(err);
		if (string)
			g_string_free(string, TRUE);
		*retval = NULL;
		*length = 0;

		return FALSE;
	}

	*length = string->len;
	*retval = g_string_free(string, FALSE);

	return TRUE;
}

static gboolean
lt_tag_scanner_is_eof(lt_tag_scanner_t *scanner)
{
	g_return_val_if_fail (scanner != NULL, TRUE);
	g_return_val_if_fail (scanner->position <= scanner->length, TRUE);

	return scanner->string[scanner->position] == 0 ||
		scanner->position >= scanner->length;
}

static gint
_lt_tag_variant_compare(gconstpointer a,
			gconstpointer b)
{
	return (gulong)a - (gulong)b;
}

#define DEFUNC_TAG_FREE(__func__)					\
	G_INLINE_FUNC void						\
	lt_tag_free_ ##__func__ (lt_tag_t *tag)				\
	{								\
		if (tag->__func__) {					\
			lt_mem_remove_ref(&tag->parent, tag->__func__);	\
			tag->__func__ = NULL;				\
		}							\
	}

DEFUNC_TAG_FREE (language)
DEFUNC_TAG_FREE (extlang)
DEFUNC_TAG_FREE (script)
DEFUNC_TAG_FREE (region)
DEFUNC_TAG_FREE (variants)
DEFUNC_TAG_FREE (extension)
DEFUNC_TAG_FREE (grandfathered)
DEFUNC_TAG_FREE (tag_string)

#undef DEFUNC_TAG_FREE

#define DEFUNC_TAG_SET(__func__, __unref_func__)			\
	G_INLINE_FUNC void						\
	lt_tag_set_ ##__func__ (lt_tag_t *tag, gpointer p)		\
	{								\
		lt_tag_free_ ##__func__ (tag);				\
		if (p) {						\
			tag->__func__ = p;				\
			lt_mem_add_ref(&tag->parent, tag->__func__,	\
				       (lt_destroy_func_t)__unref_func__); \
		}							\
	}

DEFUNC_TAG_SET (language, lt_lang_unref)
DEFUNC_TAG_SET (extlang, lt_extlang_unref)
DEFUNC_TAG_SET (script, lt_script_unref)
DEFUNC_TAG_SET (region, lt_region_unref)
DEFUNC_TAG_SET (extension, lt_extension_unref)
DEFUNC_TAG_SET (grandfathered, lt_grandfathered_unref)

G_INLINE_FUNC void
lt_tag_set_variant(lt_tag_t *tag,
		   gpointer  p)
{
	gboolean no_variants = (tag->variants == NULL);

	if (p) {
		tag->variants = g_list_append(tag->variants, p);
		if (no_variants)
			lt_mem_add_ref(&tag->parent, tag->variants,
				       (lt_destroy_func_t)_lt_tag_variants_list_free);
	} else {
		g_warn_if_reached();
	}
}

#undef DEFUNC_TAG_SET

G_INLINE_FUNC void
lt_tag_add_tag_string(lt_tag_t    *tag,
		      const gchar *s)
{
	if (!tag->tag_string) {
		tag->tag_string = g_string_new(NULL);
		lt_mem_add_ref(&tag->parent, tag->tag_string,
			       (lt_destroy_func_t)lt_mem_gstring_free);
	}
	if (s) {
		if (tag->tag_string->len > 0)
			g_string_append_c(tag->tag_string, '-');
		g_string_append(tag->tag_string, s);
	} else {
		g_warn_if_reached();
	}
}

static const gchar *
lt_tag_get_locale_from_locale_alias(const gchar *alias)
{
	gint i;

	g_return_val_if_fail (alias != NULL, NULL);

	for (i = 0; __lt_localealias_tables[i].alias != NULL; i++) {
		if (g_ascii_strcasecmp(alias, __lt_localealias_tables[i].alias) == 0)
			return __lt_localealias_tables[i].locale;
	}

	return NULL;
}

static void
lt_tag_fill_wildcard(lt_tag_t       *tag,
		     lt_tag_state_t  begin,
		     lt_tag_state_t  end)
{
	lt_tag_state_t i;
	lt_lang_db_t *langdb;
	lt_extlang_db_t *extlangdb;
	lt_script_db_t *scriptdb;
	lt_region_db_t *regiondb;
	lt_variant_db_t *variantdb;
	lt_extension_t *e;

	for (i = begin; i < end; i++) {
		tag->wildcard_map |= (1 << (i - 1));
		switch (i) {
		    case STATE_LANG:
			    langdb = lt_db_get_lang();
			    lt_tag_set_language(tag, lt_lang_db_lookup(langdb, "*"));
			    lt_lang_db_unref(langdb);
			    break;
		    case STATE_EXTLANG:
			    extlangdb = lt_db_get_extlang();
			    lt_tag_set_extlang(tag, lt_extlang_db_lookup(extlangdb, "*"));
			    lt_extlang_db_unref(extlangdb);
			    break;
		    case STATE_SCRIPT:
			    scriptdb = lt_db_get_script();
			    lt_tag_set_script(tag, lt_script_db_lookup(scriptdb, "*"));
			    lt_script_db_unref(scriptdb);
			    break;
		    case STATE_REGION:
			    regiondb = lt_db_get_region();
			    lt_tag_set_region(tag, lt_region_db_lookup(regiondb, "*"));
			    lt_region_db_unref(regiondb);
			    break;
		    case STATE_VARIANT:
			    variantdb = lt_db_get_variant();
			    lt_tag_set_variant(tag, lt_variant_db_lookup(variantdb, "*"));
			    lt_variant_db_unref(variantdb);
			    break;
		    case STATE_EXTENSION:
			    e = lt_extension_create();
			    lt_extension_add_singleton(e, '*', NULL, NULL);
			    lt_tag_set_extension(tag, e);
			    break;
		    case STATE_PRIVATEUSE:
			    g_string_truncate(tag->privateuse, 0);
			    g_string_append(tag->privateuse, "*");
			    break;
		    default:
			    break;
		}
	}
}

static void
lt_tag_parser_init(lt_tag_t *tag)
{
	lt_tag_clear(tag);
	tag->state = STATE_NONE;
}

static gboolean
lt_tag_parse_prestate(lt_tag_t     *tag,
		      const gchar  *token,
		      gsize         length,
		      GError      **error)
{
	gboolean retval = TRUE;

	if (g_strcmp0(token, "-") == 0) {
		switch (tag->state) {
		    case STATE_PRE_EXTLANG:
			    tag->state = STATE_EXTLANG;
			    break;
		    case STATE_PRE_SCRIPT:
			    tag->state = STATE_SCRIPT;
			    break;
		    case STATE_PRE_REGION:
			    tag->state = STATE_REGION;
			    break;
		    case STATE_PRE_VARIANT:
			    tag->state = STATE_VARIANT;
			    break;
		    case STATE_PRE_EXTENSION:
			    tag->state = STATE_EXTENSION;
			    break;
		    case STATE_IN_EXTENSION:
			    tag->state = STATE_EXTENSIONTOKEN;
			    break;
		    case STATE_IN_EXTENSIONTOKEN:
			    tag->state = STATE_EXTENSIONTOKEN2;
			    break;
		    case STATE_PRE_PRIVATEUSE:
			    tag->state = STATE_PRIVATEUSE;
			    break;
		    case STATE_IN_PRIVATEUSE:
			    tag->state = STATE_PRIVATEUSETOKEN;
			    break;
		    case STATE_IN_PRIVATEUSETOKEN:
			    tag->state = STATE_PRIVATEUSETOKEN2;
			    break;
		    default:
			    g_set_error(error, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
					"Invalid syntax found during parsing a token: %s",
					token);
			    retval = FALSE;
			    break;
		}
	} else {
		retval = FALSE;
	}

	return retval;
}

static gboolean
lt_tag_parse_state(lt_tag_t     *tag,
		   const gchar  *token,
		   gsize         length,
		   GError      **error)
{
	gboolean retval = TRUE;
	const gchar *p;

	switch (tag->state) {
	    case STATE_LANG:
		    if (length == 1) {
			    if (g_ascii_strcasecmp(token, "x") == 0) {
				    g_string_append(tag->privateuse, token);
				    tag->state = STATE_IN_PRIVATEUSE;
				    break;
			    } else {
			      invalid_tag:
				    g_set_error(error, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
						"Invalid language subtag: %s", token);
				    break;
			    }
		    } else if (length >= 2 && length <= 3) {
			    lt_lang_db_t *langdb = lt_db_get_lang();

			    /* shortest ISO 639 code */
			    tag->language = lt_lang_db_lookup(langdb, token);
			    lt_lang_db_unref(langdb);
			    if (!tag->language) {
				    g_set_error(error, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
						"Unknown ISO 639 code: %s",
						token);
				    break;
			    }
			    /* validate if it's really shortest one */
			    p = lt_lang_get_tag(tag->language);
			    if (!p || g_ascii_strcasecmp(token, p) != 0) {
				    g_set_error(error, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
						"No such language subtag: %s",
						token);
				    lt_lang_unref(tag->language);
				    tag->language = NULL;
				    break;
			    }
			    lt_mem_add_ref(&tag->parent, tag->language,
					   (lt_destroy_func_t)lt_lang_unref);
			    tag->state = STATE_PRE_EXTLANG;
		    } else if (length == 4) {
			    /* reserved for future use */
			    g_set_error(error, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
					"Reserved for future use: %s",
					token);
		    } else if (length >= 5 && length <= 8) {
			    /* registered language subtag */
			    g_set_error(error, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
					"XXX: registered language tag: %s",
					token);
		    } else {
			    goto invalid_tag;
		    }
		    break;
	    case STATE_EXTLANG:
		    if (length == 3) {
			    lt_extlang_db_t *extlangdb = lt_db_get_extlang();

			    tag->extlang = lt_extlang_db_lookup(extlangdb, token);
			    lt_extlang_db_unref(extlangdb);
			    if (tag->extlang) {
				    const gchar *prefix = lt_extlang_get_prefix(tag->extlang);
				    const gchar *subtag = lt_extlang_get_tag(tag->extlang);
				    const gchar *lang = lt_lang_get_better_tag(tag->language);

				    if (prefix &&
					g_ascii_strcasecmp(prefix, lang) != 0) {
					    g_set_error(error, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
							"extlang '%s' is supposed to be used with %s, but %s",
							subtag, prefix, lang);
					    lt_extlang_unref(tag->extlang);
					    tag->extlang = NULL;
				    } else {
					    lt_mem_add_ref(&tag->parent, tag->extlang,
							   (lt_destroy_func_t)lt_extlang_unref);
					    tag->state = STATE_PRE_SCRIPT;
				    }
				    break;
			    }
			    /* try to check something else */
		    } else {
			    /* it may be a script */
		    }
	    case STATE_SCRIPT:
		    if (length == 4) {
			    lt_script_db_t *scriptdb = lt_db_get_script();

			    lt_tag_set_script(tag, lt_script_db_lookup(scriptdb, token));
			    lt_script_db_unref(scriptdb);
			    if (tag->script) {
				    tag->state = STATE_PRE_REGION;
				    break;
			    }
			    /* try to check something else */
		    } else {
			    /* it may be a region */
		    }
	    case STATE_REGION:
		    if (length == 2 ||
			(length == 3 &&
			 g_ascii_isdigit(token[0]) &&
			 g_ascii_isdigit(token[1]) &&
			 g_ascii_isdigit(token[2]))) {
			    lt_region_db_t *regiondb = lt_db_get_region();

			    lt_tag_set_region(tag, lt_region_db_lookup(regiondb, token));
			    lt_region_db_unref(regiondb);
			    if (tag->region) {
				    tag->state = STATE_PRE_VARIANT;
				    break;
			    }
			    /* try to check something else */
		    } else {
			    /* it may be a variant */
		    }
	    case STATE_VARIANT:
		    if ((length >=5 && length <= 8) ||
			(length == 4 && g_ascii_isdigit(token[0]))) {
			    lt_variant_db_t *variantdb = lt_db_get_variant();
			    lt_variant_t *variant;

			    variant = lt_variant_db_lookup(variantdb, token);
			    lt_variant_db_unref(variantdb);
			    if (variant) {
				    const GList *prefixes = lt_variant_get_prefix(variant), *l;
				    gchar *langtag = lt_tag_canonicalize(tag, error);
				    GString *str_prefixes = g_string_new(NULL);
				    gboolean matched = FALSE;

				    if (error && *error) {
					    /* ignore it and fallback to the original tag string */
					    g_error_free(*error);
					    *error = NULL;
					    langtag = g_strdup(tag->tag_string->str);
				    }
				    for (l = prefixes; l != NULL; l = g_list_next(l)) {
					    const gchar *s = l->data;

					    if (str_prefixes->len > 0)
						    g_string_append(str_prefixes, ",");
					    g_string_append(str_prefixes, s);

					    if (g_ascii_strncasecmp(s, langtag, strlen(s)) == 0) {
						    matched = TRUE;
						    break;
					    }
				    }
				    if (prefixes && !matched) {
					    g_set_error(error, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
							"variant '%s' is supposed to be used with %s, but %s",
							token, str_prefixes->str, langtag);
					    lt_variant_unref(variant);
				    } else {
					    if (!tag->variants) {
						    lt_tag_set_variant(tag, variant);
					    } else {
						    GList *prefixes = (GList *)lt_variant_get_prefix(variant);
						    const gchar *tstr;

						    lt_tag_free_tag_string(tag);
						    tstr = lt_tag_get_string(tag);
						    if (prefixes && !g_list_find_custom(prefixes, tstr, (GCompareFunc)g_strcmp0)) {
							    g_set_error(error, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
									"Variant isn't allowed for %s: %s",
									tstr,
									lt_variant_get_tag(variant));
							    lt_variant_unref(variant);
						    } else if (!prefixes && g_list_find_custom(tag->variants, variant, _lt_tag_variant_compare)) {
							    g_set_error(error, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
									"Duplicate variants: %s",
									lt_variant_get_tag(variant));
							    lt_variant_unref(variant);
						    } else {
							    tag->variants = g_list_append(tag->variants,
											  variant);
						    }
					    }
					    /* multiple variants are allowed. */
					    tag->state = STATE_PRE_VARIANT;
				    }
				    g_free(langtag);
				    g_string_free(str_prefixes, TRUE);
				    break;
			    }
			    /* try to check something else */
		    } else {
			    /* it may be an extension */
		    }
	    case STATE_EXTENSION:
	    extension:
		    if (length == 1 &&
			token[0] != 'x' &&
			token[0] != 'X' &&
			token[0] != '*' &&
			token[0] != '-') {
			    if (!tag->extension)
				    lt_tag_set_extension(tag, lt_extension_create());
			    if (lt_extension_has_singleton(tag->extension, token[0])) {
				    g_set_error(error, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
						"Duplicate singleton for extension: %s", token);
			    } else {
				    if (lt_extension_add_singleton(tag->extension,
								    token[0],
								    tag, error)) {
					    tag->state = STATE_IN_EXTENSION;
				    }
			    }
			    break;
		    } else {
			    /* it may be a private use */
		    }
	    case STATE_PRIVATEUSE:
		    if (length == 1 && (token[0] == 'x' || token[0] == 'X')) {
			    g_string_append(tag->privateuse, token);
			    tag->state = STATE_IN_PRIVATEUSE;
		    } else {
			    /* No state to try */
			    retval = FALSE;
		    }
		    break;
	    case STATE_EXTENSIONTOKEN:
	    case STATE_EXTENSIONTOKEN2:
		    if (length >= 2 && length <= 8) {
			    if (lt_extension_add_tag(tag->extension,
						      token, error))
				    tag->state = STATE_IN_EXTENSIONTOKEN;
		    } else {
			    if (tag->state == STATE_EXTENSIONTOKEN2 &&
				lt_extension_validate_state(tag->extension)) {
				    /* No need to destroy the previous tokens.
				     * fallback to check the extension again.
				     */
				    goto extension;
			    }
			    lt_extension_cancel_tag(tag->extension);
			    /* No state to try */
			    retval = FALSE;
		    }
		    break;
	    case STATE_PRIVATEUSETOKEN:
	    case STATE_PRIVATEUSETOKEN2:
		    if (length <= 8) {
			    g_string_append_printf(tag->privateuse, "-%s", token);
			    tag->state = STATE_IN_PRIVATEUSETOKEN;
		    } else {
			    /* 'x'/'X' is reserved singleton for the private use subtag.
			     * so nothing to fallback to anything else.
			     */
			    g_set_error(error, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
					"Invalid tag for the private use: token = '%s'",
					token);
		    }
		    break;
	    default:
		    g_set_error(error, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
				"Unable to parse tag: %s, token = '%s' state = %d",
				tag->tag_string->str, token, tag->state);
		    break;
	}
	if (*error)
		retval = FALSE;

	return retval;
}

static gboolean
_lt_tag_parse(lt_tag_t     *tag,
	      const gchar  *langtag,
	      gboolean      allow_wildcard,
	      GError      **error)
{
	lt_tag_scanner_t *scanner = NULL;
	lt_grandfathered_db_t *grandfathereddb;
	gchar *token = NULL;
	gsize len = 0;
	GError *err = NULL;
	gboolean retval = TRUE;
	lt_tag_state_t wildcard = STATE_NONE;
	gint count = 0;

	g_return_val_if_fail (tag != NULL, FALSE);
	g_return_val_if_fail (langtag != NULL, FALSE);

	if (tag->state == STATE_NONE) {
		grandfathereddb = lt_db_get_grandfathered();
		lt_tag_set_grandfathered(tag, lt_grandfathered_db_lookup(grandfathereddb, langtag));
		lt_grandfathered_db_unref(grandfathereddb);
		if (tag->grandfathered) {
			/* no need to lookup anymore. */
			goto bail;
		}
		tag->state = STATE_LANG;
	} else {
		if (tag->state == STATE_PRE_EXTLANG ||
		    tag->state == STATE_PRE_SCRIPT ||
		    tag->state == STATE_PRE_REGION ||
		    tag->state == STATE_PRE_VARIANT ||
		    tag->state == STATE_PRE_EXTENSION ||
		    tag->state == STATE_PRE_PRIVATEUSE)
			tag->state++;
	}

	scanner = lt_tag_scanner_new(langtag);
	while (!lt_tag_scanner_is_eof(scanner)) {
		if (token) {
			g_free(token);
			token = NULL;
		}
		if (!lt_tag_scanner_get_token(scanner, &token, &len, &err)) {
			if (err)
				break;
			g_set_error(&err, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
				    "Unrecoverable error");
			break;
		}
		count++;
		if (!token || len == 0) {
			g_set_error(&err, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
				    "No valid tokens found");
			break;
		}
		if (!lt_tag_parse_prestate(tag, token, len, &err)) {
			if (err)
				break;
			if (allow_wildcard && g_strcmp0(token, "*") == 0) {
				wildcard = tag->state;
				if (tag->state == STATE_LANG)
					tag->state += 1;
				else
					tag->state -= 1;
			} else {
				if (!lt_tag_parse_state(tag, token, len, &err))
					break;
				if (wildcard != STATE_NONE) {
					lt_tag_fill_wildcard(tag, wildcard, tag->state - 1);
					wildcard = STATE_NONE;
				}
			}
		}
	}
	if (wildcard != STATE_NONE) {
		lt_tag_fill_wildcard(tag, wildcard, STATE_END);
	}
	if (!err &&
	    tag->state != STATE_PRE_EXTLANG &&
	    tag->state != STATE_PRE_SCRIPT &&
	    tag->state != STATE_PRE_REGION &&
	    tag->state != STATE_PRE_VARIANT &&
	    tag->state != STATE_PRE_EXTENSION &&
	    tag->state != STATE_PRE_PRIVATEUSE &&
	    tag->state != STATE_IN_EXTENSIONTOKEN &&
	    tag->state != STATE_IN_PRIVATEUSETOKEN &&
	    tag->state != STATE_NONE) {
		g_set_error(&err, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
			    "Invalid tag: %s, last token = '%s', state = %d, parsed count = %d",
			    langtag, token, tag->state, count);
	}
  bail:
	lt_tag_add_tag_string(tag, langtag);
	lt_tag_scanner_unref(scanner);
	if (err) {
		if (error)
			*error = g_error_copy(err);
		else
			g_warning(err->message);
		g_error_free(err);
		retval = FALSE;
	}
	g_free(token);

	return retval;
}

static gboolean
_lt_tag_match(const lt_tag_t *v1,
	      lt_tag_t       *v2,
	      lt_tag_state_t  state)
{
	g_return_val_if_fail (v1 != NULL, FALSE);
	g_return_val_if_fail (v2 != NULL, FALSE);

	if (state > STATE_EXTLANG && !v2->extlang && v1->extlang) {
		lt_extlang_db_t *db = lt_db_get_extlang();

		lt_tag_set_extlang(v2, lt_extlang_db_lookup(db, ""));
		lt_extlang_db_unref(db);
	}
	if (state > STATE_SCRIPT && !v2->script && v1->script) {
		lt_script_db_t *db = lt_db_get_script();

		lt_tag_set_script(v2, lt_script_db_lookup(db, ""));
		lt_script_db_unref(db);
	}
	if (state > STATE_REGION && !v2->region && v1->region) {
		lt_region_db_t *db = lt_db_get_region();

		lt_tag_set_region(v2, lt_region_db_lookup(db, ""));
		lt_region_db_unref(db);
	}
	if (state > STATE_VARIANT && !v2->variants && v1->variants) {
		lt_variant_db_t *db = lt_db_get_variant();

		lt_tag_set_variant(v2, lt_variant_db_lookup(db, ""));
		lt_variant_db_unref(db);
	}
	if (state > STATE_EXTENSION && !v2->extension && v1->extension) {
		lt_extension_t *e = lt_extension_create();

		lt_extension_add_singleton(e, ' ', NULL, NULL);
		lt_tag_set_extension(v2, e);
	}

	return lt_tag_compare(v1, v2);
}

static void
_lt_tag_subtract(lt_tag_t       *tag,
		 const lt_tag_t *rtag)
{
	if (rtag->language) {
		lt_tag_free_language(tag);
	}
	if (rtag->extlang) {
		lt_tag_free_extlang(tag);
	}
	if (rtag->script) {
		lt_tag_free_script(tag);
	}
	if (rtag->region) {
		lt_tag_free_region(tag);
	}
	if (rtag->variants) {
		lt_tag_free_variants(tag);
	}
	if (rtag->extension) {
		/* XXX: how to deal with the multiple extensions? */
		lt_tag_free_extension(tag);
	}
	if (rtag->privateuse) {
		if (tag->privateuse)
			g_string_truncate(tag->privateuse, 0);
	}
}

static void
_lt_tag_replace(lt_tag_t       *tag,
		const lt_tag_t *rtag)
{
	if (rtag->language) {
		g_return_if_fail (!tag->language);
		lt_tag_set_language(tag, lt_lang_ref(rtag->language));
	}
	if (rtag->extlang) {
		g_return_if_fail (!tag->extlang);
		lt_tag_set_extlang(tag, lt_extlang_ref(rtag->extlang));
	}
	if (rtag->script) {
		g_return_if_fail (!tag->script);
		lt_tag_set_script(tag, lt_script_ref(rtag->script));
	}
	if (rtag->region) {
		g_return_if_fail (!tag->region);
		lt_tag_set_region(tag, lt_region_ref(rtag->region));
	}
	if (rtag->variants) {
		GList *l = rtag->variants;

		g_return_if_fail (!tag->variants);

		while (l != NULL) {
			lt_tag_set_variant(tag, lt_variant_ref(l->data));
			l = g_list_next(l);
		}
	}
	if (rtag->extension) {
		g_return_if_fail (!tag->extension);
		lt_tag_set_extension(tag, lt_extension_ref(rtag->extension));
	}
	if (rtag->privateuse) {
		g_string_truncate(tag->privateuse, 0);
		g_string_append(tag->privateuse, rtag->privateuse->str);
	}
}

/* borrowed the modifier related code from localehelper:
 * http://people.redhat.com/caolanm/BCP47/localehelper-1.0.0.tar.gz
 */
/*
 * glibc typically uses these modifiers to indicate particular
 * scripts that the language is written in
 * See ISO-15924 http://unicode.org/iso15924/iso15924-codes.html
 */
static gboolean
_lt_tag_convert_script_from_locale_modifier(const gchar  *modifier,
					    const gchar **ret)
{
	/* XXX: think about how to get rid of the hardcoded mapping table */
	static const gchar * const maps[][2] = {
		{ "Arabic", "Arab" },
		{ "Imperial_Aramaic", "Armi" },
		{ "Armenian", "Armn" },
		{ "Avestan", "Avst" },
		{ "Balinese", "Bali" },
		{ "Bamum", "Bamu" },
		{ "Bengali", "Beng" },
		{ "Bopomofo", "Bopo" },
		{ "Braille", "Brai" },
		{ "Buginese", "Bugi" },
		{ "Buhid", "Buhd" },
		{ "Canadian_Aboriginal", "Cans" },
		{ "Carian", "Cari" },
		{ "Cham", "Cham" },
		{ "Cherokee", "Cher" },
		{ "Coptic", "Copt" },
		{ "Cypriot", "Cprt" },
		{ "Cyrillic", "Cyrl" },
		{ "Devanagari", "Deva" },
		{ "Deseret", "Dsrt" },
		{ "Egyptian_Hierogyphs", "Egyp" },
		{ "Ethiopic", "Ethi" },
		{ "Georgian", "Geor" },
		{ "Glagolitic", "Glag" },
		{ "Gothic", "Goth" },
		{ "Greek", "Grek" },
		{ "Gujarati", "Gujr" },
		{ "Gurmukhi", "Guru" },
		{ "Hangul", "Hang" },
		{ "Han", "Hani" },
		{ "Hanunoo", "Hano" },
		{ "Hebrew", "Hebr" },
		{ "Hiragana", "Hira" },
		{ "Katakana_Or_Hiragana", "Hrkt" },
		{ "Old_Italic", "Ital" },
		{ "Javanese", "Java" },
		{ "Kayah_Li", "Kali" },
		{ "Katakana", "Kana" },
		{ "Kharoshthi", "Khar" },
		{ "Khmer", "Khmr" },
		{ "Kannada", "Knda" },
		{ "Kaithi", "Kthi" },
		{ "Tai_Tham", "Lana" },
		{ "Lao", "Laoo" },
		{ "Latin", "Latn" },
		{ "Lepcha", "Lepc" },
		{ "Limbu", "Limb" },
		{ "Linear_B", "Linb" },
		{ "Lisu", "Lisu" },
		{ "Lycian", "Lyci" },
		{ "Lydian", "Lydi" },
		{ "Malayalam", "Mlym" },
		{ "Mongolian", "Mong" },
		{ "Meetei_Mayek", "Mtei" },
		{ "Myanmar", "Mymr" },
		{ "Nko", "Nkoo" },
		{ "Ogham", "Ogam" },
		{ "Ol_Chiki", "Olck" },
		{ "Old_Turkic", "Orkh" },
		{ "Oriya", "Orya" },
		{ "Osmanya", "Osma" },
		{ "Phags_Pa", "Phag" },
		{ "Inscriptional_Pahlavi", "Phli" },
		{ "Phoenician", "Phnx" },
		{ "Inscriptional_Parthian", "Prti" },
		{ "Rejang", "Rjng" },
		{ "Runic", "Runr" },
		{ "Samaritan", "Samr" },
		{ "Old_South_Arabian", "Sarb" },
		{ "Saurashtra", "Saur" },
		{ "Shavian", "Shaw" },
		{ "Sinhala", "Sinh" },
		{ "Sundanese", "Sund" },
		{ "Syloti_Nagri", "Sylo" },
		{ "Syriac", "Syrc" },
		{ "Tagbanwa", "Tagb" },
		{ "Tai_Le", "Tale" },
		{ "New_Tai_Lue", "Talu" },
		{ "Tamil", "Taml" },
		{ "Tai_Viet", "Tavt" },
		{ "Telugu", "Telu" },
		{ "Tifinagh", "Tfng" },
		{ "Tagalog", "Tglg" },
		{ "Thaana", "Thaa" },
		{ "Thai", "Thai" },
		{ "Tibetan", "Tibt" },
		{ "Ugaritic", "Ugar" },
		{ "Vai", "Vaii" },
		{ "Old_Persian", "Xpeo" },
		{ "Cuneiform", "Xsux" },
		{ "Yi", "Yiii" },
		{ "Inherited", "Zinh" },
		{ "Common", "Zyyy" },
		{ "Unknown", "Zzzz" },
	};
	gsize i;

	if (modifier) {
		/*
		 * Special case this one. The script is definitely Latin
		 * and not Cyrillic. But lets bubble the transliteration scheme
		 * through another layer with return 0
		 */
		if (g_ascii_strcasecmp(modifier, "iqtelif") == 0) {
			_lt_tag_convert_script_from_locale_modifier("Latin", ret);
			return FALSE;
		}
		for (i = 0; i < sizeof (maps) / sizeof (gchar *[2]); i++) {
			if (g_ascii_strcasecmp(modifier, maps[i][0]) == 0) {
				*ret = maps[i][1];
				return TRUE;
			}
		}
	}

	return FALSE;
}

/*
 * Occasionally (ca_ES@valencia) some modifiers indicate a language variant
 * See http://www.iana.org/assignments/language-subtag-registry
 * for IANA language subtag assignments output codes
 */
static gboolean
_lt_tag_convert_variant_from_locale_modifier(const gchar  *modifier,
					     const gchar **ret)
{
	/* XXx: think about how to get rid of the hardcoded mapping table */
	static const gchar * const maps[][2] = {
            { "valencia", "valencia" }
	};
	gsize i;

	if (modifier) {
		for (i = 0; i < sizeof (maps) / sizeof (gchar *[2]); i++) {
			if (g_ascii_strcasecmp(modifier, maps[i][0]) == 0) {
				*ret = maps[i][1];
				return TRUE;
			}
		}
	}

	return FALSE;
}

static const gchar * const
_lt_tag_convert_privaseuse_from_locale_modifier(const gchar *modifier)
{
	/* XXX: think about how to get rid of the hardcoded mapping table */
	static const gchar * const maps[][2] = {
		/*
		 * Old mechanism to denote that the euro currency is in use, 
		 * ignore it.
		 */
		{ "euro", NULL },
		/*
		 * http://www.mail-archive.com/cygwin@cygwin.com/msg97848.html
		 * 
		 * A modifier that indicates what width to assign to an
		 * ambiguous width char, ignore it.
		 *
		 * http://unicode.org/reports/tr11/
		 */
		{ "cjknarrow", NULL },
		/*
		 * http://www.geez.org/Collation/
		 * 
		 * Abegede Collation for Ge'ez (as opposed to Halehame, I believe)
		 *
		 * http://www.iana.org/assignments/language-subtag-registry has
		 * nothing to describe it, so using a private code
		 *
		 * http://tools.ietf.org/html/draft-davis-u-langtag-ext-01
		 * http://www.unicode.org/reports/tr35/ maybe u-co-something some day
		 */
		{ "abegede", "abegede" },
		/*
		 * http://www.alvestrand.no/pipermail/ietf-languages/2006-September/005017.html
		 * 
		 * "iqtelif" Latin orthography
		 *
		 * Bit of a mess really. Unsure if tt-Latn is sufficient, i.e. if this is
		 * the default latin orghography in practice but a private code
		 * doesn't hurt I guess
		 */
		{ "iqtelif", "iqtel" }
	};
	gsize i;

	if (modifier) {
		for (i = 0; i < sizeof (maps) / sizeof (gchar *[2]); i++) {
			if (g_ascii_strcasecmp(modifier, maps[i][0]) == 0)
				return maps[i][1];
		}

		g_warning("Unknown modifiers: %s", modifier);

		return modifier;
	}

	return NULL;
}

static lt_tag_t *
_lt_tag_convert_from_locale_string(const gchar  *locale,
				   GError      **error)
{
	gchar *s, *territory, *codeset, *modifier;
	lt_tag_t *tag;
	GError *err = NULL;

	s = g_strdup(locale);
	tag = lt_tag_new();
	g_print("%s\n", s);
	if (!s || s[0] == 0 ||
	    g_strcmp0(s, "C") == 0 ||
	    g_strcmp0(s, "POSIX") == 0) {
		if (!lt_tag_parse(tag, "en-US-u-va-posix", &err))
			goto bail;
	} else {
		GString *tag_string;
		const gchar *script = NULL, *variant = NULL, *privateuse = NULL;

		modifier = strchr(s, '@');
		if (modifier) {
			*modifier = 0;
			modifier++;
		}
		codeset = strchr(s, '.');
		if (codeset) {
			*codeset = 0;
			codeset++;
		}
		territory = strchr(s, '_');
		if (territory) {
			*territory = 0;
			territory++;
		}
		if (codeset &&
		    (g_ascii_strcasecmp(codeset, "utf-8") == 0 ||
		     g_ascii_strcasecmp(codeset, "utf8") == 0)) {
			codeset = NULL;
		}
		/* check if the language is a locale alias */
		if (strlen(s) > 3 &&
		    !territory &&
		    !codeset &&
		    !modifier) {
			const gchar *loc = lt_tag_get_locale_from_locale_alias(s);
			lt_tag_t *t;

			if (loc && (t = _lt_tag_convert_from_locale_string(loc, &err)) != NULL) {
				lt_tag_unref(tag);
				tag = t;
				goto bail;
			}
		}
		if (!_lt_tag_convert_script_from_locale_modifier(modifier, &script))
			if (!_lt_tag_convert_variant_from_locale_modifier(modifier, &variant))
				privateuse = _lt_tag_convert_privaseuse_from_locale_modifier(modifier);

		tag_string = g_string_new(s);
		if (script)
			g_string_append_printf(tag_string, "-%s", script);
		if (territory)
			g_string_append_printf(tag_string, "-%s", territory);
		if (variant)
			g_string_append_printf(tag_string, "-%s", variant);
		if (codeset || privateuse) {
			g_string_append(tag_string, "-x");
			if (codeset)
				g_string_append_printf(tag_string, "-codeset-%s", codeset);
			if (privateuse)
				g_string_append_printf(tag_string, "-%s", privateuse);
		}
		if (!lt_tag_parse(tag, tag_string->str, &err)) {
			g_string_free(tag_string, TRUE);
			goto bail;
		}
		g_string_free(tag_string, TRUE);
	}

  bail:
	g_free(s);

	if (err) {
		if (error)
			*error = g_error_copy(err);
		else
			g_warning(err->message);
		g_error_free(err);
		lt_tag_unref(tag);
		tag = NULL;
	}

	return tag;
}

/*< protected >*/
lt_tag_state_t
lt_tag_parse_wildcard(lt_tag_t     *tag,
		      const gchar  *tag_string,
		      GError      **error)
{
	GError *err = NULL;
	gboolean ret;

	lt_tag_parser_init(tag);
	ret = _lt_tag_parse(tag, tag_string, TRUE, &err);

	if (!ret && !err) {
		g_set_error(&err, LT_ERROR, LT_ERR_FAIL_ON_SCANNER,
			    "Unknown error during parsing a tag.");
	}
	if (err) {
		if (error)
			*error = g_error_copy(err);
		else
			g_warning(err->message);
		g_error_free(err);
	}

	return tag->state;
}

/*< public >*/
/**
 * lt_tag_new:
 *
 * Create a new instance of #lt_tag_t.
 *
 * Returns: (transfer full): a new instance of #lt_tag_t.
 */
lt_tag_t *
lt_tag_new(void)
{
	lt_tag_t *retval = lt_mem_alloc_object(sizeof (lt_tag_t));

	if (retval) {
		retval->state = STATE_NONE;
		retval->privateuse = g_string_new(NULL);
		lt_mem_add_ref(&retval->parent, retval->privateuse,
			       (lt_destroy_func_t)lt_mem_gstring_free);
	}

	return retval;
}

/**
 * lt_tag_ref:
 * @tag: a #lt_tag_t.
 *
 * Increases the reference count of @tag.
 *
 * Returns: (transfer none): the same @tag object.
 */
lt_tag_t *
lt_tag_ref(lt_tag_t *tag)
{
	g_return_val_if_fail (tag != NULL, NULL);

	return lt_mem_ref(&tag->parent);
}

/**
 * lt_tag_unref:
 * @tag: a #lt_tag_t.
 *
 * Decreases the reference count of @tag. when its reference count
 * drops to 0, the object is finalized (i.e. its memory is freed).
 */
void
lt_tag_unref(lt_tag_t *tag)
{
	if (tag)
		lt_mem_unref(&tag->parent);
}

/**
 * lt_tag_clear:
 * @tag: a #lt_tag_t.
 *
 * (Re-)Initialize all of the subtag information stored in @tag.
 */
void
lt_tag_clear(lt_tag_t *tag)
{
	g_return_if_fail (tag != NULL);

	lt_tag_free_tag_string(tag);
	lt_tag_free_language(tag);
	lt_tag_free_extlang(tag);
	lt_tag_free_script(tag);
	lt_tag_free_region(tag);
	lt_tag_free_variants(tag);
	lt_tag_free_extension(tag);
	if (tag->privateuse) {
		g_string_truncate(tag->privateuse, 0);
	}
	lt_tag_free_grandfathered(tag);
}

/**
 * lt_tag_parse:
 * @tag: a #lt_tag_t.
 * @tag_string: language tag to be parsed.
 * @error: (allow-none): a #GError or %NULL.
 *
 * Parse @tag_string and create appropriate instances for subtags.
 *
 * Returns: %TRUE if it's successfully completed, otherwise %FALSE.
 */
gboolean
lt_tag_parse(lt_tag_t     *tag,
	     const gchar  *tag_string,
	     GError      **error)
{
	lt_tag_parser_init(tag);

	return _lt_tag_parse(tag, tag_string, FALSE, error);
}

/**
 * lt_tag_parse_with_extra_token:
 * @tag: a #lt_tag_t.
 * @tag_string: a language tag to be parsed much more.
 * @error: (allow-none): a #GError or %NULL.
 *
 * Continue to parse a language tag with @tag_string. please use lt_tag_parse()
 * at first.
 *
 * Returns: %TRUE if it's successfully completed, otherwise %FALSE.
 */
gboolean
lt_tag_parse_with_extra_token(lt_tag_t     *tag,
			      const gchar  *tag_string,
			      GError      **error)
{
	g_return_val_if_fail (tag != NULL, FALSE);
	g_return_val_if_fail (tag->state != STATE_NONE, FALSE);

	return _lt_tag_parse(tag, tag_string, FALSE, error);
}

/**
 * lt_tag_copy:
 * @tag: a #lt_tag_t.
 *
 * Create a copy instance of @tag.
 *
 * Returns: (transfer full): a new instance of #lt_tag_t or %NULL if fails.
 */
lt_tag_t *
lt_tag_copy(const lt_tag_t *tag)
{
	lt_tag_t *retval;
	GList *l;

	g_return_val_if_fail (tag != NULL, NULL);

	retval = lt_tag_new();
	retval->wildcard_map = tag->wildcard_map;
	retval->state = tag->state;
	if (tag->language) {
		lt_tag_set_language(retval, lt_lang_ref(tag->language));
	}
	if (tag->extlang) {
		lt_tag_set_extlang(retval, lt_extlang_ref(tag->extlang));
	}
	if (tag->script) {
		lt_tag_set_script(retval, lt_script_ref(tag->script));
	}
	if (tag->region) {
		lt_tag_set_region(retval, lt_region_ref(tag->region));
	}
	l = tag->variants;
	while (l != NULL) {
		lt_tag_set_variant(retval, lt_variant_ref(l->data));
		l = g_list_next(l);
	}
	if (tag->extension) {
		lt_tag_set_extension(retval, lt_extension_copy(tag->extension));
	}
	if (tag->privateuse) {
		g_string_append(retval->privateuse, tag->privateuse->str);
	}
	if (tag->grandfathered) {
		lt_tag_set_grandfathered(retval, lt_grandfathered_ref(tag->grandfathered));
	}

	return retval;
}

/**
 * lt_tag_truncate:
 * @tag: a #lt_tag_t.
 * @error: (allow-none): a #GError.
 *
 * Truncate the last subtag.
 *
 * Returns: %TRUE if a subtag was truncated, otherwise %FALSE.
 */
gboolean
lt_tag_truncate(lt_tag_t  *tag,
		GError   **error)
{
	GError *err = NULL;
	gboolean retval = TRUE;

	g_return_val_if_fail (tag != NULL, FALSE);

	if (tag->grandfathered) {
		g_set_error(&err, LT_ERROR, LT_ERR_NO_TAG,
			    "Grandfathered subtag can't be truncated.");
		goto bail;
	}
	while (1) {
		if (tag->privateuse && tag->privateuse->len > 0) {
			g_string_truncate(tag->privateuse, 0);
			break;
		}
		if (tag->extension) {
			gint i;
			gchar c;
			gboolean has_tag = FALSE;

			lt_extension_truncate(tag->extension);
			for (i = 0; i < LT_MAX_EXT_MODULES; i++) {
				c = lt_ext_module_singleton_int_to_char(i);

				if (c == 'x')
					continue;
				has_tag = lt_extension_has_singleton(tag->extension, c);
				if (has_tag)
					break;
			}
			if (!has_tag) {
				lt_tag_free_extension(tag);
			}
			break;
		}
		if (tag->variants) {
			GList *l = g_list_last(tag->variants);
			lt_variant_t *v = l->data;

			if (tag->variants == l) {
				lt_mem_delete_ref(&tag->parent, tag->variants);
				tag->variants = g_list_delete_link(tag->variants, l);
				if (tag->variants)
					lt_mem_add_ref(&tag->parent, tag->variants,
						       (lt_destroy_func_t)_lt_tag_variants_list_free);
			} else {
				l = g_list_delete_link(l, l);
			}
			lt_variant_unref(v);
			break;
		}
		if (tag->region) {
			lt_tag_free_region(tag);
			break;
		}
		if (tag->script) {
			lt_tag_free_script(tag);
			break;
		}
		if (tag->extlang) {
			lt_tag_free_extlang(tag);
			break;
		}
		if (tag->language) {
			lt_tag_free_language(tag);
			break;
		}
		g_set_error(&err, LT_ERROR, LT_ERR_NO_TAG,
			    "No tags to be truncated.");
		goto bail;
	}
	lt_tag_free_tag_string(tag);
  bail:
	if (err) {
		if (error)
			*error = g_error_copy(err);
		else
			g_warning(err->message);
		g_error_free(err);
		retval = FALSE;
	}

	return retval;
}

/**
 * lt_tag_get_string:
 * @tag: a #lt_tag_t.
 *
 * Obtains a language tag in string.
 *
 * Returns: a language tag string.
 */
const gchar *
lt_tag_get_string(lt_tag_t *tag)
{
	GList *l;

	if (tag->tag_string)
		return tag->tag_string->str;

	if (tag->grandfathered)
		lt_tag_add_tag_string(tag, lt_grandfathered_get_tag(tag->grandfathered));
	else if (tag->language) {
		lt_tag_add_tag_string(tag, lt_lang_get_tag(tag->language));
		if (tag->extlang)
			lt_tag_add_tag_string(tag, lt_extlang_get_tag(tag->extlang));
		if (tag->script)
			lt_tag_add_tag_string(tag, lt_script_get_tag(tag->script));
		if (tag->region)
			lt_tag_add_tag_string(tag, lt_region_get_tag(tag->region));
		l = tag->variants;
		while (l != NULL) {
			lt_tag_add_tag_string(tag, lt_variant_get_tag(l->data));
			l = g_list_next(l);
		}
		if (tag->extension)
			lt_tag_add_tag_string(tag, lt_extension_get_tag(tag->extension));
		if (tag->privateuse && tag->privateuse->len > 0)
			lt_tag_add_tag_string(tag, tag->privateuse->str);
	} else if (tag->privateuse && tag->privateuse->len > 0) {
		lt_tag_add_tag_string(tag, tag->privateuse->str);
	} else {
		return NULL;
	}

	return tag->tag_string->str;
}

/**
 * lt_tag_canonicalize:
 * @tag: a #lt_tag_t.
 * @error: (allow-none): a #GError or %NULL.
 *
 * Canonicalize the language tag according to various information of subtags.
 *
 * Returns: a language tag string.
 */
gchar *
lt_tag_canonicalize(lt_tag_t  *tag,
		    GError   **error)
{
	gchar *retval = NULL;
	GString *string = NULL;
	GError *err = NULL;
	GList *l;
	lt_redundant_db_t *rdb = NULL;
	lt_redundant_t *r = NULL;
	lt_tag_t *ctag = NULL;

	g_return_val_if_fail (tag != NULL, NULL);

	string = g_string_new(NULL);
	if (tag->grandfathered) {
		g_string_append(string, lt_grandfathered_get_better_tag(tag->grandfathered));
		goto bail1;
	}

	ctag = lt_tag_copy(tag);
	rdb = lt_db_get_redundant();
	while (1) {
		const gchar *tag_string = lt_tag_get_string(ctag);

		if (tag_string == NULL || tag_string[0] == 0)
			break;
		if (r)
			lt_redundant_unref(r);
		r = lt_redundant_db_lookup(rdb, tag_string);
		if (r) {
			const gchar *preferred = lt_redundant_get_preferred_tag(r);

			if (preferred) {
				lt_tag_t *rtag = lt_tag_new();
				lt_tag_t *ntag = lt_tag_new();

				if (!lt_tag_parse(rtag, lt_redundant_get_tag(r), &err)) {
					lt_tag_unref(rtag);
					lt_tag_unref(ntag);
					goto bail1;
				}
				if (!lt_tag_parse(ntag, preferred, &err)) {
					lt_tag_unref(rtag);
					lt_tag_unref(ntag);
					goto bail1;
				}
				_lt_tag_subtract(tag, rtag);
				_lt_tag_replace(tag, ntag);
				lt_tag_unref(rtag);
				lt_tag_unref(ntag);
			}
			break;
		} else {
			if (!lt_tag_truncate(ctag, &err))
				goto bail1;
		}
	}

	if (tag->language) {
		gsize len;
		lt_extlang_db_t *edb = lt_db_get_extlang();
		lt_extlang_t *e;

		/* If the language tag starts with a primary language subtag
		 * that is also an extlang subtag, then the language tag is
		 * prepended with the extlang's 'Prefix'.
		 */
		e = lt_extlang_db_lookup(edb, lt_lang_get_better_tag(tag->language));
		if (e) {
			const gchar *prefix = lt_extlang_get_prefix(e);

			if (prefix)
				g_string_append_printf(string, "%s-", prefix);
			lt_extlang_unref(e);
		}
		lt_extlang_db_unref(edb);

		g_string_append(string, lt_lang_get_better_tag(tag->language));
		if (tag->extlang) {
			const gchar *preferred = lt_extlang_get_preferred_tag(tag->extlang);

			if (preferred) {
				g_string_truncate(string, 0);
				g_string_append(string, preferred);
			} else {
				g_string_append_printf(string, "-%s",
						       lt_extlang_get_tag(tag->extlang));
			}
		}
		if (tag->script) {
			const gchar *script = lt_script_get_tag(tag->script);
			const gchar *suppress = lt_lang_get_suppress_script(tag->language);

			if (!suppress ||
			    g_ascii_strcasecmp(suppress, script))
				g_string_append_printf(string, "-%s", script);
		}
		if (tag->region) {
			g_string_append_printf(string, "-%s", lt_region_get_better_tag(tag->region));
		}
		l = tag->variants;
		len = string->len;
		while (l != NULL) {
			lt_variant_t *variant = l->data;
			const gchar *better = lt_variant_get_better_tag(variant);
			const gchar *s = lt_variant_get_tag(variant);

			if (better && g_ascii_strcasecmp(s, better) != 0) {
				/* ignore all of variants prior to this one */
				g_string_truncate(string, len);
			}
			g_string_append_printf(string, "-%s", better ? better : s);
			l = g_list_next(l);
		}
		if (tag->extension) {
			gchar *s = lt_extension_get_canonicalized_tag(tag->extension);

			g_string_append_printf(string, "-%s", s);
			g_free(s);
		}
	}
	if (tag->privateuse && tag->privateuse->len > 0) {
		g_string_append_printf(string, "%s%s",
				       string->len > 0 ? "-" : "",
				       tag->privateuse->str);
	}
	if (string->len == 0) {
		g_set_error(&err, LT_ERROR, LT_ERR_NO_TAG,
			    "No tag to convert.");
	}
  bail1:
	if (ctag)
		lt_tag_unref(ctag);
	if (rdb)
		lt_redundant_db_unref(rdb);
	if (r)
		lt_redundant_unref(r);
	retval = g_string_free(string, FALSE);
	if (err) {
		if (error)
			*error = g_error_copy(err);
		else
			g_warning(err->message);
		g_error_free(err);
		if (retval)
			g_free(retval);
		retval = NULL;
	}

	return retval;
}

/**
 * lt_tag_convert_from_locale:
 * @error: (allow-none): a #GError.
 *
 * Convert current locale to the language tag.
 *
 * Returns: (transfer full): a #lt_tag_t, %NULL if fails.
 */
lt_tag_t *
lt_tag_convert_from_locale(GError **error)
{
	const gchar *locale;

	locale = setlocale(LC_CTYPE, NULL);
	if (!locale)
		locale = setlocale(LC_ALL, NULL);
	return _lt_tag_convert_from_locale_string(locale, error);
}

/**
 * lt_tag_convert_to_locale:
 * @tag: a #lt_tag_t.
 * @error: (allow-none): a #GError or %NULL.
 *
 * Convert the language tag to the locale.
 *
 * Returns: a locale string or %NULL if fails
 */
gchar *
lt_tag_convert_to_locale(lt_tag_t  *tag,
			 GError   **error)
{
	gchar *retval = NULL;
	GString *string = NULL;
	GError *err = NULL;
	const gchar *mod = NULL;
	gchar *canonical_tag = NULL;
	lt_tag_t *ctag;

	g_return_val_if_fail (tag != NULL, NULL);

	canonical_tag = lt_tag_canonicalize(tag, &err);
	if (!canonical_tag)
		goto bail;
	ctag = lt_tag_new();
	if (!lt_tag_parse(ctag, canonical_tag, &err)) {
		lt_tag_unref(ctag);
		goto bail;
	}
	string = g_string_new(NULL);
	g_string_append(string, lt_lang_get_better_tag(ctag->language));
	if (ctag->region)
		g_string_append_printf(string, "_%s",
				       lt_region_get_tag(ctag->region));
	if (ctag->script) {
		mod = lt_script_convert_to_modifier(ctag->script);
		if (mod)
			g_string_append_printf(string, "@%s", mod);
	}
	lt_tag_unref(ctag);

  bail:
	g_free(canonical_tag);
	if (string)
		retval = g_string_free(string, FALSE);
	if (err) {
		if (error)
			*error = g_error_copy(err);
		else
			g_warning(err->message);
		g_error_free(err);
		if (retval)
			g_free(retval);
		retval = NULL;
	}

	return retval;
}

/**
 * lt_tag_dump:
 * @tag: a #lt_tag_t.
 *
 * Dumps the container information to the standard output.
 */
void
lt_tag_dump(const lt_tag_t *tag)
{
	GList *l;

	g_return_if_fail (tag != NULL);

	if (tag->grandfathered) {
		lt_grandfathered_dump(tag->grandfathered);
		return;
	}
	lt_lang_dump(tag->language);
	if (tag->extlang)
		lt_extlang_dump(tag->extlang);
	if (tag->script)
		lt_script_dump(tag->script);
	if (tag->region)
		lt_region_dump(tag->region);
	l = tag->variants;
	while (l != NULL) {
		lt_variant_t *variant = l->data;

		lt_variant_dump(variant);
		l = g_list_next(l);
	}
	if (tag->extension)
		lt_extension_dump(tag->extension);
	if (tag->privateuse->len > 0)
		g_print("Private Use: %s\n", tag->privateuse->str);
}

/**
 * lt_tag_compare:
 * @v1: a #lt_tag_t.
 * @v2: a #lt_tag_t.
 *
 * Compare if @v1 and @v2 is the same object or not.
 *
 * Returns: %TRUE if it's the same, otherwise %FALSE.
 */
gboolean
lt_tag_compare(const lt_tag_t *v1,
	       const lt_tag_t *v2)
{
	gboolean retval = TRUE;
	const GList *l1, *l2;

	g_return_val_if_fail (v1 != NULL, FALSE);
	g_return_val_if_fail (v2 != NULL, FALSE);
	g_return_val_if_fail (v1->grandfathered == NULL, FALSE);
	g_return_val_if_fail (v2->grandfathered == NULL, FALSE);

	retval &= lt_lang_compare(v1->language, v2->language);
	if (v2->extlang)
		retval &= lt_extlang_compare(v1->extlang, v2->extlang);
	if (v2->script)
		retval &= lt_script_compare(v1->script, v2->script);
	if (v2->region)
		retval &= lt_region_compare(v1->region, v2->region);
	l1 = v1->variants;
	l2 = v2->variants;
	while (l2 != NULL) {
		lt_variant_t *vv1, *vv2;

		vv1 = l1 ? l1->data : NULL;
		vv2 = l2->data;
		retval &= lt_variant_compare(vv1, vv2);
		l1 = l1 ? g_list_next(l1) : NULL;
		l2 = g_list_next(l2);
	}
	if (v2->extension)
		retval &= lt_extension_compare(v1->extension, v2->extension);
	if (v2->privateuse && v2->privateuse->len > 0)
		retval &= _lt_tag_gstring_compare(v1->privateuse, v2->privateuse);

	return retval;
}

/**
 * lt_tag_match:
 * @v1: a #lt_tag_t.
 * @v2: a language range string.
 * @error: (allow-none): a #GError or %NULL.
 *
 * Try matching of @v1 and @v2. any of subtags in @v2 is allowed to use
 * the wildcard according to the syntax in RFC 4647.
 *
 * Returns: %TRUE if it matches, otherwise %FALSE.
 */
gboolean
lt_tag_match(const lt_tag_t  *v1,
	     const gchar     *v2,
	     GError         **error)
{
	gboolean retval = FALSE;
	lt_tag_t *t2 = NULL;
	lt_tag_state_t state = STATE_NONE;
	GError *err = NULL;

	g_return_val_if_fail (v1 != NULL, FALSE);
	g_return_val_if_fail (v2 != NULL, FALSE);

	t2 = lt_tag_new();
	state = lt_tag_parse_wildcard(t2, v2, &err);
	if (err)
		goto bail;
	retval = _lt_tag_match(v1, t2, state);
  bail:
	if (err) {
		if (error)
			*error = g_error_copy(err);
		else
			g_warning(err->message);
		g_error_free(err);
		retval = FALSE;
	}
	if (t2)
		lt_tag_unref(t2);

	return retval;
}

/**
 * lt_tag_lookup:
 * @tag: a #lt_tag_t.
 * @pattern: a language range string.
 * @error: (allow-none): a #GError or %NULL.
 *
 * Lookup the language tag that @tag meets with @pattern.
 * Any of subtags in @pattern is allowed to use the wildcard according to
 * the syntax in RFC 4647.
 *
 * Returns: a language tag string if any matches, otherwise %NULL.
 */
gchar *
lt_tag_lookup(const lt_tag_t  *tag,
	      const gchar     *pattern,
	      GError         **error)
{
	lt_tag_t *t2 = NULL;
	lt_tag_state_t state = STATE_NONE;
	GError *err = NULL;
	GList *l;
	gchar *retval = NULL;

	g_return_val_if_fail (tag != NULL, NULL);
	g_return_val_if_fail (pattern != NULL, NULL);

	t2 = lt_tag_new();
	state = lt_tag_parse_wildcard(t2, pattern, &err);
	if (err)
		goto bail;
	if (_lt_tag_match(tag, t2, state)) {
		gint32 i;

		for (i = 0; i < (STATE_END - 1); i++) {
			if (t2->wildcard_map & (1 << i)) {
				switch (i + 1) {
				    case STATE_LANG:
					    lt_tag_set_language(t2, lt_lang_ref(tag->language));
					    break;
				    case STATE_EXTLANG:
					    lt_tag_free_extlang(t2);
					    if (tag->extlang) {
						    lt_tag_set_extlang(t2, lt_extlang_ref(tag->extlang));
					    }
					    break;
				    case STATE_SCRIPT:
					    lt_tag_free_script(t2);
					    if (tag->script) {
						    lt_tag_set_script(t2, lt_script_ref(tag->script));
					    }
					    break;
				    case STATE_REGION:
					    lt_tag_free_region(t2);
					    if (tag->region) {
						    lt_tag_set_region(t2, lt_region_ref(tag->region));
					    }
					    break;
				    case STATE_VARIANT:
					    lt_tag_free_variants(t2);
					    l = tag->variants;
					    while (l != NULL) {
						    lt_tag_set_variant(t2, lt_variant_ref(l->data));
						    l = g_list_next(l);
					    }
					    break;
				    case STATE_EXTENSION:
				    case STATE_EXTENSIONTOKEN:
				    case STATE_EXTENSIONTOKEN2:
					    lt_tag_free_extension(t2);
					    if (tag->extension) {
						    lt_tag_set_extension(t2, lt_extension_ref(tag->extension));
					    }
					    break;
				    case STATE_PRIVATEUSE:
				    case STATE_PRIVATEUSETOKEN:
				    case STATE_PRIVATEUSETOKEN2:
					    if (t2->privateuse) {
						    g_string_truncate(t2->privateuse, 0);
					    }
					    if (tag->privateuse) {
						    g_string_append(t2->privateuse, tag->privateuse->str);
					    }
					    break;
				    default:
					    break;
				}
			}
		}
		lt_tag_free_tag_string(t2);
		retval = g_strdup(lt_tag_get_string(t2));
	}
  bail:
	if (err) {
		if (error)
			*error = g_error_copy(err);
		else
			g_warning(err->message);
		g_error_free(err);
	}
	if (t2)
		lt_tag_unref(t2);

	return retval;
}

/**
 * lt_tag_transform:
 * @tag: a #lt_tag_t.
 * @error: (allow-none): a #GError or %NULL.
 *
 * Transform @tag according to the likelySubtags database provided by CLDR.
 *
 * Returns: a string.
 */
gchar *
lt_tag_transform(lt_tag_t  *tag,
		 GError   **error)
{
	lt_xml_t *xml = NULL;
	const gchar *tag_string;
	gchar *retval = NULL;
	GError *err = NULL;

	g_return_val_if_fail (tag != NULL, NULL);

	tag_string = lt_tag_get_string(tag);
	if (tag_string) {
		xmlDocPtr doc;
		xmlXPathContextPtr xctxt = NULL;
		xmlXPathObjectPtr xobj = NULL;
		xmlNodePtr ent;
		xmlChar *to;
		gchar *xpath_string = NULL;
		int n;
		GString *s;
		gint i;

		xml = lt_xml_new();
		doc = lt_xml_get_cldr(xml, LT_XML_CLDR_SUPPLEMENTAL_LIKELY_SUBTAGS);
		xctxt = xmlXPathNewContext(doc);
		if (!xctxt) {
			g_set_error(&err, LT_ERROR, LT_ERR_OOM,
				    "Unable to create an instance of xmlXPathContextPtr.");
			goto bail;
		}
		xpath_string = g_strdup_printf("/supplementalData/likelySubtags/likelySubtag[@from = '%s']", tag_string);
		xobj = xmlXPathEvalExpression((const xmlChar *)xpath_string, xctxt);
		if (!xobj) {
			g_set_error(&err, LT_ERROR, LT_ERR_FAIL_ON_XML,
				    "No valid elements for %s",
				    doc->name);
			goto bail;
		}
		n = xmlXPathNodeSetGetLength(xobj->nodesetval);
		if (n > 1)
			g_warning("Multiple subtag data to be transformed for %s: %d",
				  tag_string, n);

		ent = xmlXPathNodeSetItem(xobj->nodesetval, 0);
		if (!ent) {
			g_set_error(&err, LT_ERROR, LT_ERR_FAIL_ON_XML,
				    "Unable to obtain the xml node via XPath.");
			goto bail;
		}
		to = xmlGetProp(ent, (const xmlChar *)"to");
		s = g_string_new((const gchar *)to);
		xmlFree(to);
		for (i = 0; i < s->len; i++) {
			if (s->str[i] == '_')
				s->str[i] = '-';
		}
		retval = g_string_free(s, FALSE);
	  bail:
		g_free(xpath_string);
		if (xobj)
			xmlXPathFreeObject(xobj);
		if (xctxt)
			xmlXPathFreeContext(xctxt);
		lt_xml_unref(xml);
	}
	if (err) {
		if (error)
			*error = g_error_copy(err);
		else
			g_warning(err->message);
		g_error_free(err);
	}

	return retval;
}

#define DEFUNC_GET_SUBTAG(__func__,__type__)			\
	const __type__ *					\
	lt_tag_get_ ##__func__ (const lt_tag_t *tag)		\
	{							\
		g_return_val_if_fail (tag != NULL, NULL);	\
								\
		return tag->__func__;				\
	}

/**
 * lt_tag_get_language:
 * @tag: a #lt_tag_t.
 *
 * Obtain a #lt_lang_t instance in @tag.
 *
 * Returns: (transfer none): a #lt_lang_t.
 */
DEFUNC_GET_SUBTAG (language, lt_lang_t)
/**
 * lt_tag_get_extlang:
 * @tag: a #lt_tag_t.
 *
 * Obtain a #lt_extlang_t instance in @tag.
 *
 * Returns: (transfer none): a #lt_extlang_t.
 */
DEFUNC_GET_SUBTAG (extlang, lt_extlang_t)
/**
 * lt_tag_get_script:
 * @tag: a #lt_tag_t.
 *
 * Obtain a #lt_script_t instance in @tag.
 *
 * Returns: (transfer none): a #lt_script_t.
 */
DEFUNC_GET_SUBTAG (script, lt_script_t)
/**
 * lt_tag_get_region:
 * @tag: a #lt_tag_t.
 *
 * Obtain a #lt_region_t instance in @tag.
 *
 * Returns: (transfer none): a #lt_region_t.
 */
DEFUNC_GET_SUBTAG (region, lt_region_t)
/**
 * lt_tag_get_variants:
 * @tag: a #lt_tag_t.
 *
 * Obtain a list of #lt_variant_t instance in @tag.
 *
 * Returns: (element-type lt_variant_t) (transfer none): a #GList containing #lt_variant_t.
 */
DEFUNC_GET_SUBTAG (variants, GList)
/**
 * lt_tag_get_extension:
 * @tag: a #lt_tag_t.
 *
 * Obtain a #lt_extension_t instance in @tag.
 *
 * Returns: (transfer none): a #lt_extension_t.
 */
DEFUNC_GET_SUBTAG (extension, lt_extension_t)
/**
 * lt_tag_get_privateuse:
 * @tag: a #lt_tag_t.
 *
 * Obtain a #GString instance in @tag.
 *
 * Returns: (transfer none): a #GString.
 */
DEFUNC_GET_SUBTAG (privateuse, GString)
/**
 * lt_tag_get_grandfathered:
 * @tag: a #lt_tag_t.
 *
 * Obtain a #lt_grandfathered_t instance in @tag.
 *
 * Returns: (transfer none): a #lt_grandfathered_t.
 */
DEFUNC_GET_SUBTAG (grandfathered, lt_grandfathered_t)

#undef DEFUNC_GET_SUBTAG
